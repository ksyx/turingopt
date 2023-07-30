#include "worker.h"

static sqlite3_stmt *measurement_insert;
static sqlite3_stmt *jobinfo_insert;

void worker_finalize() {
  sqlite3_stmt *stmt_to_finalize[] = {
    measurement_insert,
    jobinfo_insert,
    NULL
  };
  finalize_stmt_array(stmt_to_finalize);
}

static inline bool reset_stmt(sqlite3_stmt *stmt, const char *op) {
  if (!IS_SQLITE_OK(sqlite3_reset(stmt))) {
    fprintf(stderr, "Within %s: ", op);
    SQLITE3_PERROR("reset");
    return false;
  }
  if (!IS_SQLITE_OK(sqlite3_clear_bindings(stmt))) {
      fprintf(stderr, "Within %s: ", op);
    SQLITE3_PERROR("clear_bindings");
    return false;
  }
  return true;
}

static inline bool
setup_stmt(sqlite3_stmt *&stmt, const char *sql, const char *op) {
  if (!stmt) {
    if (!IS_SQLITE_OK(PREPARE_STMT(sql, &stmt, 1))) {
      fprintf(stderr, "Within %s: ", op);
      SQLITE3_PERROR("prepare");
      exit(1);
    }
  } else {
    if (!reset_stmt(stmt, op)) {
      exit(1);
    }
  }
  return true;
}

static void jobinfo_record_insert(slurmdb_job_rec_t *job) {
  #define OP "(jobinfo_insert)"
  if (!setup_stmt(jobinfo_insert, JOBINFO_INSERT_SQL, OP)) {
    return;
  }
  SQLITE3_BIND_START
  #define BIND(TY, VAR, VAL) SQLITE3_NAMED_BIND(TY, jobinfo_insert, VAR, VAL);
  #define BIND_TEXT(VAR, VAL) NAMED_BIND_TEXT(jobinfo_insert, VAR, VAL);
  BIND(int, ":jobid", job->jobid);
  tres_t tres_req(job->tres_req_str);
  BIND(int64, ":mem", tres_req[MEM_TRES]);
  BIND(int, ":ngpu", tres_req[GPU_TRES]);
  BIND_TEXT(":node", job->nodes);
  if (job->user) {
    BIND_TEXT(":user", job->user);
  } else if (auto info = getpwuid(job->uid)) {
    BIND_TEXT(":user", info->pw_name);
  }
  BIND_TEXT(":name", job->jobname);
  BIND_TEXT(":submit_line", job->submit_line);
  if (BIND_FAILED) {
    SQLITE3_PERROR("bind" OP);
    return;
  }
  SQLITE3_BIND_END
  if (sqlite3_step(jobinfo_insert) != SQLITE_DONE) {
    SQLITE3_PERROR("step" OP);
    return;
  }
  #if !SLURM_TRACK_STEPS_REMOVED
  if (job->track_steps)
  #endif
  {
    reset_stmt(jobinfo_insert, OP);
    SQLITE3_BIND_START
    BIND(int, ":jobid", job->jobid);
    if (BIND_FAILED) {
      SQLITE3_PERROR("bind" OP);
      return;
    }
    SQLITE3_BIND_END
    ListIterator step_it = slurm_list_iterator_create(job->steps);
    while (const auto step = (slurmdb_step_rec_t *) slurm_list_next(step_it)) {
      SQLITE3_BIND_START
      BIND(int, ":stepid", step->step_id.step_id);
      BIND_TEXT(":name", step->stepname);
      BIND_TEXT(":submit_line", step->submit_line);
      BIND_TEXT(":node", step->nodes);
      if (BIND_FAILED) {
        SQLITE3_PERROR("bind" OP);
        continue;
      }
      SQLITE3_BIND_END
      if (sqlite3_step(jobinfo_insert) != SQLITE_DONE) {
        SQLITE3_PERROR("step" OP);
        continue;
      }
      if (!IS_SQLITE_OK(sqlite3_reset(jobinfo_insert))) {
        SQLITE3_PERROR("reset" OP);
        continue;
      }
    }
    slurm_list_iterator_destroy(step_it);
  }
  #undef BIND_TEXT
  #undef BIND
  #undef OP
}

static void
measurement_record_insert(
  slurmdb_step_rec_t *step,
  const size_t *res_size = NULL,
  const size_t *minor_pagefault = NULL,
  const gpu_util_t *gpu_util = NULL) {
  #define OP "(measurement_insert)"
  if (!setup_stmt(measurement_insert, MEASUREMENTS_INSERT_SQL, OP)) {
    return;
  }
  tres_t tres_in(step->stats.tres_usage_in_tot);
  tres_t tres_out(step->stats.tres_usage_out_tot);
  tres_t tres_max(step->stats.tres_usage_in_max);
  SQLITE3_BIND_START
  #define BIND(TY, VAR, VAL) \
    SQLITE3_NAMED_BIND(TY, measurement_insert, VAR, VAL);
  BIND(int, ":watcherid", watcher_id);
  BIND(int, ":jobid", step->job_ptr->jobid);
  BIND(int, ":stepid", step->step_id.step_id);
  BIND(int64, ":dev_in", tres_in[DISK_TRES]);
  BIND(int64, ":dev_out", tres_out[DISK_TRES]);
  BIND(int64, ":res_size", res_size ? *res_size : tres_max[MEM_TRES]);
  if (minor_pagefault) {
    BIND(int64, ":minor_pagefault", *minor_pagefault);
  }
  if (gpu_util) {
    BIND(int, ":gpu_util", (int)(*gpu_util * GPU_UTIL_MULTIPLIER));
  }
  #define BINDTIMING(VAR) \
    BIND(int64, ":" #VAR "_sec", step->VAR##_cpu_sec); \
    BIND(int, ":" #VAR "_usec", step->VAR##_cpu_usec);
  BINDTIMING(sys); BINDTIMING(user);
  #undef BINDTIMING
  #undef BIND
  if (BIND_FAILED) {
    SQLITE3_PERROR("bind" OP);
    return;
  }
  SQLITE3_BIND_END
  if (sqlite3_step(measurement_insert) != SQLITE_DONE) {
    SQLITE3_PERROR("step" OP);
    return;
  }
  #undef OP
}

static inline bool wait_until(time_t timeout) {
  static const uint32_t futex_word = 1;
  timespec t;
  t.tv_sec = timeout;
  t.tv_nsec = 0;
  syscall(SYS_futex, &futex_word, FUTEX_WAIT_BITSET | FUTEX_CLOCK_REALTIME,
          futex_word, &t, 0, FUTEX_BITSET_MATCH_ANY);
  return futex_word;
}

void watcher() {
  List state_list = slurm_list_create(NULL);
  for (int i = 0; i < JOB_END; i++) {
    if (i != JOB_PENDING) {
      slurm_list_append(state_list, slurm_job_state_string(i));
    }
  }
  auto condition = (slurmdb_job_cond_t *) calloc(1, sizeof(slurmdb_job_cond_t));
  condition->flags |= JOBCOND_FLAG_NO_TRUNC;
  condition->db_flags = SLURMDB_JOB_FLAG_NOTSET;
  time_t timeout = 0;
  while (wait_until(timeout)) {
    if (timeout) {
      renew_watcher(UPSERT_WATCHER_SQL_RETURNING_TIMESTAMP_RANGE);
    }
    time_t curtime = time(NULL);
    printf("Accounting import started at %ld\n", curtime);
    timeout = time(NULL) + ACCOUNTING_RPC_INTERVAL;
    condition->usage_start = time_range_start;
    condition->usage_end = time_range_end;
    List job_list = slurmdb_jobs_get(slurm_conn, condition);
    ListIterator job_it = slurm_list_iterator_create(job_list);
    while (const auto job = (slurmdb_job_rec_t *) slurm_list_next(job_it)) {
      sqlite3_begin_transaction();
      jobinfo_record_insert(job);
      #if !SLURM_TRACK_STEPS_REMOVED
      if (!job->track_steps && !job->steps) {
        auto job_step =
          (slurmdb_step_rec_t *) calloc(1, sizeof(slurmdb_step_rec_t));
        #define COPY(FIELD) job_step->FIELD = job->FIELD;
        COPY(start); COPY(end); COPY(submit_line); COPY(state); COPY(stats);
        #define COPYTIMING(FIELD) \
          COPY(FIELD##_cpu_usec); COPY(FIELD##_cpu_sec);
        COPYTIMING(user); COPYTIMING(tot); COPYTIMING(sys);
        #undef COPYTIMING
        #undef COPY
        job_step->stepname = job->jobname;
        job_step->job_ptr = job;
        measurement_record_insert(job_step);
        free(job_step);
      } else
      #endif
      {
        ListIterator step_it = slurm_list_iterator_create(job->steps);
        while (
          const auto step = (slurmdb_step_rec_t *) slurm_list_next(step_it)) {
          #if !SLURM_TRACK_STEPS_REMOVED
          if (!job->track_steps) {
            step->stats = job->stats;
          }
          #endif
          measurement_record_insert(step);
        }
        slurm_list_iterator_destroy(step_it);
      }
      DEBUGOUT(
        fprintf(stderr, "Ending transaction for job %d\n", job->jobid)
      );
      if (!sqlite3_end_transaction()) {
        // why
        exit(1);
      }
    }
    slurm_list_iterator_destroy(job_it);
    slurm_list_destroy(job_list);
    printf("Accounting import ended at %ld, would sleep until %ld\n",
      time(NULL), timeout);
  }
  free(condition);
}

// Implemented as RPC-free
void scraper() {

}

void parent(int argc, const char *argv[]) {

}
