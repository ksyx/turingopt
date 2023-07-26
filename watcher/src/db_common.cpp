#include "db_common.h"

bool renew_watcher(const char *query) {
  sqlite3_stmt *insert_watcher = NULL;
  if (!IS_SQLITE_OK(
      PREPARE_STMT(query, &insert_watcher, 0)
    )) {
    perror("prepare");
    return false;
  }
  SQLITE3_BIND_START;
  NAMED_BIND_INT(insert_watcher, ":pid", pid);
  NAMED_BIND_TEXT(insert_watcher, ":target_node", hostname);
  NAMED_BIND_INT(insert_watcher, ":jobid", jobstep_info.job_id);
  NAMED_BIND_INT(insert_watcher, ":stepid", jobstep_info.step_id);
  NAMED_BIND_INT(insert_watcher, ":privileged", is_privileged);
  if (BIND_FAILED) {
    return false;
  }
  SQLITE3_BIND_END;
  int ret;
  if ((ret = sqlite3_step(insert_watcher)) != SQLITE_ROW) {
    if (IS_SLURM_SUCCESS(ret)) {
      fputs("sqlite3_step: no result returned\n", stderr);
    } else {
      SQLITE3_PERROR("step");
    }
    return false;
  }
  SQLITE3_FETCH_COLUMNS_START("start", "end", "id");
  SQLITE3_FETCH_COLUMNS_LOOP_HEADER(i, insert_watcher) {
    if (!IS_EXPECTED_COLUMN(insert_watcher, i)) {
      fprintf(stderr, "fetch_result_column: expected %s, got %s\n",
                EXPECTED_COLUMN_NAMES_VAR[i],
                GET_COLUMN_NAME(insert_watcher, i));
      return false;
    }
    switch(i) {
      case 0: time_range_start = SQLITE3_FETCH(int, insert_watcher, i); break;
      case 1: time_range_end = SQLITE3_FETCH(int, insert_watcher, i); break;
      case 2: watcher_id = SQLITE3_FETCH(int64, insert_watcher, i); break;
      default:
        fprintf(stderr, "fetch_result_column: unexpected column index %d\n", i);
    }
  }
  SQLITE3_FETCH_COLUMNS_END;
  DEBUGOUT(
    fprintf(stderr,
            "watcher_id=%d, time_range_start=%ld, time_range_end=%ld\n",
            watcher_id, time_range_start, time_range_end));
  if (!IS_SQLITE_OK(sqlite3_finalize(insert_watcher))) {
    SQLITE3_PERROR("finalize");
    return false;
  }
  return true;
}
