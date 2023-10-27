#include "migrate.h"

static const char *GET_LATEST_GLOBAL_MEASUREMENT_FOR_JOBSTEPS_SQL
  = SQLITE_CODEBLOCK(
    WITH ids AS MATERIALIZED (SELECT id FROM watcher WHERE target_node IS NULL)
      SELECT
      jobid, stepid,
      nullif(max(iif(watcherid IN ids, recordid, -1)), -1) AS recordid
      FROM measurements
      GROUP BY jobid, stepid
      ORDER BY jobid, stepid;
  );
static sqlite3_stmt *get_latest_global_measurement_for_jobsteps_stmt;

// All these shall be recovered in the next run of CREATE TRIGGER IF NOT EXISTS
static const char *PREPARE_MIGRATE_SQL
  = SQLITE_CODEBLOCK(
    DROP TRIGGER measurement_quality_ensurance;
    DROP TRIGGER measurements_upd;
  );

void migrate_db(int cur_version) {
  int start, end;
  if (cur_version < 3) {
    const char *MIGRATE_COMPATABILITY_SQL
      = "ALTER TABLE worker_task_info ADD COLUMN prev_schema_version INTEGER;";
    if (!sqlite3_exec_wrap(
        MIGRATE_COMPATABILITY_SQL, "(migrate_compatibility)")) {
      exit(1);
    }
  }
  sqlite3_begin_transaction();
  #define OP "(renew_schema_version)"
  sqlite3_stmt *renew_db_schema_version_stmt = NULL;
  if (!setup_stmt(renew_db_schema_version_stmt,
                  RENEW_DB_SCHEMA_VERSION_SQL,
                  OP)) {
    exit(1);
  }
  step_renew(renew_db_schema_version_stmt, OP, start, end);
  reset_stmt(renew_db_schema_version_stmt, OP);
  if (start == DB_SCHEMA_VERSION) {
    sqlite3_end_transaction();
    return;
  } else if (start > end || end != MIGRATE_TARGET_DB_SCHEMA_VERSION) {
    fputs("error: schema version unsupported by software and is not a viable"
          " migration target.\n", stderr);
    exit(1);
  }
  if (!sqlite3_exec_wrap(PREPARE_MIGRATE_SQL, "(prepare_migrate)")) {
    exit(1);
  }
  bool success = 1;
  // Fallthrough whenever possible for continuous upgrading
  fputs("warning: Database requires migration. The application would need to be"
        " restarted after migration.\n", stderr);
  switch(start) {
    // When there is no migrate functionality at all
    #define EXEC_SQL_AND_CHECK(OP, SQL) \
      if (!sqlite3_exec_wrap(SQL, "(" OP ")")) { \
        success = 0; \
        break; \
      }
    case 0: case 1: case 2:
    {
      EXEC_SQL_AND_CHECK("migrate_alter_table_3", SQLITE_CODEBLOCK(
        ALTER TABLE jobinfo ADD COLUMN ncpu INTEGER CHECK(ncpu > 0);
        ALTER TABLE jobinfo ADD COLUMN timelimit INTEGER CHECK(timelimit > 0);
        ALTER TABLE jobinfo ADD COLUMN ended_at INTEGER;
        ALTER TABLE measurements ADD COLUMN elapsed INTEGER;
      ))
    }
    case 3:
    {
      EXEC_SQL_AND_CHECK("migrate_alter_table_4", SQLITE_CODEBLOCK(
        // For a better default column order
        ALTER TABLE jobinfo DROP COLUMN ended_at;
        ALTER TABLE jobinfo ADD COLUMN started_at INTEGER CHECK(started_at > 0);
        ALTER TABLE jobinfo ADD COLUMN ended_at INTEGER
          CHECK(ended_at == 0 OR ended_at >= started_at);
      ))
    }
    case 4:
    {
      EXEC_SQL_AND_CHECK("migrate_alter_table_5", SQLITE_CODEBLOCK(
        // For a better default column order
        ALTER TABLE measurements DROP COLUMN elapsed;
        ALTER TABLE jobinfo DROP COLUMN node;
        ALTER TABLE jobinfo ADD COLUMN nnodes INTEGER CHECK(nnodes > 0);
      ))
      if (!log_scraper_freq(SQLITE_CODEBLOCK(
          INSERT INTO scrape_freq_log(start, scrape_interval)
            VALUES(1, :scrape_interval)), "(init_scraper_freq_log)")) {
        success = 0;
        break;
      }
    }
    case 5:
    EXEC_SQL_AND_CHECK("migrate_alter_table_6", SQLITE_CODEBLOCK(
      ALTER TABLE jobinfo ADD COLUMN peak_res_size INTEGER;
    ))
    // Import from SLURM after all required schema changes are performed
    {
      const char *op = "(get_latest_global_measurement_for_jobsteps)";
      std::vector<slurm_selected_step_t> jobids;
      slurm_selected_step_t selected_step_template;
      selected_step_template.array_task_id
        = selected_step_template.het_job_offset
        = selected_step_template.step_id.step_id
        = NO_VAL;
      jobstep_recordid_map_t job_record_map;
      int last_jobid = 0;
      sqlite3_stmt *&cur_stmt = get_latest_global_measurement_for_jobsteps_stmt;
      if (!setup_stmt(cur_stmt, GET_LATEST_GLOBAL_MEASUREMENT_FOR_JOBSTEPS_SQL,
                      op)) {
        success = 0;
        break;
      }
      int sqlite_ret;
      while ((sqlite_ret = sqlite3_step(cur_stmt)) == SQLITE_ROW) {
        int cur_stepid = 0;
        SQLITE3_FETCH_COLUMNS_START("jobid", "stepid", "recordid")
        SQLITE3_FETCH_COLUMNS_LOOP_HEADER(i, cur_stmt)
          if (!IS_EXPECTED_COLUMN) {
            PRINT_COLUMN_MISMATCH_MSG(op);
            success = 0;
            break;
          }
          int val = SQLITE3_FETCH(int);
          switch (i) {
            case 0:
              if (last_jobid != val) {
                selected_step_template.step_id.job_id = val;
                jobids.push_back(selected_step_template);
                last_jobid = val;
              }
              break;
            case 1:
              cur_stepid = val;
              break;
            case 2:
              if (val) {
                job_record_map[std::make_pair(last_jobid, cur_stepid)] = val;
              }
              break;
          }
        SQLITE3_FETCH_COLUMNS_END
      }
      if (!verify_sqlite_ret(sqlite_ret, op)) {
        success = 0;
        break;
      }
      sqlite3_finalize(cur_stmt);
      auto job_cond = setup_job_cond();
      job_cond->step_list = slurm_list_create(NULL);
      job_cond->usage_start = 1;
      job_cond->usage_end = time(NULL);
      for (auto &id : jobids) {
        slurm_list_append(job_cond->step_list, &id);
      }
      measurement_record_insert(job_cond, job_record_map);
    }
    EXEC_SQL_AND_CHECK("backfill_global_watcherid", SQLITE_CODEBLOCK(
      UPDATE measurements SET watcherid = target.id
      FROM (SELECT id FROM watcher WHERE target_node IS NULL LIMIT 1) AS target
      WHERE watcherid IS 0;
    ));
    #undef EXEC_SQL_AND_CHECK
  }
  cleanup_all_stmts();
  // By reaching here it should be the same as schema for version [[end]]
  if (success) {
    fputs("Database migrated. Please rerun the software.\n", stderr);
    sqlite3_end_transaction();
  } else {
    sqlite3_exec_wrap("ROLLBACK;", "rollback");
  }
  exit(2);
  #undef OP
}
