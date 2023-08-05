#include "db_common.h"

static sqlite3_stmt *end_transaction_stmt = NULL;

void finalize_stmt_array(sqlite3_stmt *stmt_to_finalize[]) {
  auto cur = stmt_to_finalize;
  while (*cur != FINALIZE_END_ADDR) {
    if (!IS_SQLITE_OK(sqlite3_finalize(*cur))) {
      SQLITE3_PERROR("finalize");
    }
    cur++;
  }
}

void db_common_finalize() {
  sqlite3_stmt *stmt_to_finalize[] = {
    end_transaction_stmt,
    FINALIZE_END_ADDR
  };
  finalize_stmt_array(stmt_to_finalize);
}

bool renew_watcher(const char *query, worker_info_t *worker) {
  #define OP "(renew_watcher)"
  sqlite3_stmt *insert_watcher = NULL;
  if (!IS_SQLITE_OK(
      PREPARE_STMT(query, &insert_watcher, 0)
    )) {
    SQLITE3_PERROR("prepare" OP);
    return false;
  }
  SQLITE3_BIND_START;
  NAMED_BIND_INT(insert_watcher, ":pid", worker->pid);
  NAMED_BIND_TEXT(insert_watcher, ":target_node", worker->hostname);
  NAMED_BIND_INT(insert_watcher, ":jobid", worker->jobstep_info.job_id);
  NAMED_BIND_INT(insert_watcher, ":stepid", worker->jobstep_info.step_id);
  NAMED_BIND_INT(insert_watcher, ":privileged", worker->is_privileged);
  if (BIND_FAILED) {
    return false;
  }
  SQLITE3_BIND_END;
  int ret;
  if ((ret = sqlite3_step(insert_watcher)) != SQLITE_ROW) {
    if (IS_SLURM_SUCCESS(ret)) {
      fputs("sqlite3_step" OP ": no result returned\n", stderr);
    } else {
      SQLITE3_PERROR("step" OP);
    }
    return false;
  }
  SQLITE3_FETCH_COLUMNS_START("start", "end", "id");
  SQLITE3_FETCH_COLUMNS_LOOP_HEADER(i, insert_watcher) {
    if (!IS_EXPECTED_COLUMN(insert_watcher, i)) {
      fprintf(stderr, "fetch_result_column" OP ": expected %s, got %s\n",
                EXPECTED_COLUMN_NAMES_VAR[i],
                GET_COLUMN_NAME(insert_watcher, i));
      return false;
    }
    switch(i) {
      case 0: time_range_start = SQLITE3_FETCH(int, insert_watcher, i); break;
      case 1: time_range_end = SQLITE3_FETCH(int, insert_watcher, i); break;
      case 2: watcher_id = SQLITE3_FETCH(int64, insert_watcher, i); break;
      default:
        fprintf(stderr,
                "fetch_result_column" OP ": unexpected column index %d\n", i);
    }
  }
  SQLITE3_FETCH_COLUMNS_END;
  DEBUGOUT(
    fprintf(stderr,
            "watcher_id=%d, time_range_start=%ld, time_range_end=%ld\n",
            watcher_id, time_range_start, time_range_end));
  if (!IS_SQLITE_OK(sqlite3_finalize(insert_watcher))) {
    SQLITE3_PERROR("finalize" OP);
    return false;
  }
  return true;
  #undef OP
}

void sqlite3_begin_transaction() {
  sqlite3_exec(SQL_CONN_NAME, "BEGIN TRANSACTION;", NULL, NULL, NULL);
}

bool sqlite3_end_transaction() {
  #define OP "(end_transaction)"
  if (!end_transaction_stmt &&
      !IS_SQLITE_OK(PREPARE_STMT("END TRANSACTION", &end_transaction_stmt, 1))
     ) {
    SQLITE3_PERROR("prepare" OP);
    exit(1);
  }
  int ret;
  while ((ret = sqlite3_step(end_transaction_stmt)) == SQLITE_BUSY)
    ;
  if (ret != SQLITE_DONE) {
    SQLITE3_PERROR("step" OP);
    return false;
  }
  ret = sqlite3_reset(end_transaction_stmt);
  if (!IS_SQLITE_OK(ret)) {
    SQLITE3_PERROR("reset" OP);
    return false;
  }
  return true;
  #undef OP
}
