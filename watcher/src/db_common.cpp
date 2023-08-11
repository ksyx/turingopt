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

bool step_and_verify(sqlite3_stmt *stmt, bool expect_rows, const char *op) {
  int ret = sqlite3_step(stmt);
  const char *msg = NULL;
  if (expect_rows) {
    if (ret == SQLITE_ROW) {
      return true;
    } else if (ret == SQLITE_DONE) {
      msg = "no result returned";
    }
  } else {
    if (ret == SQLITE_DONE) {
      return true;
    }
  }
  fprintf(stderr, "sqlite3_step%s: %s\n",
          op, msg ? msg : sqlite3_errmsg(SQL_CONN_NAME));
  return false;
}

bool step_renew(sqlite3_stmt *stmt, const char *op, int &start, int &end) {
  if (!step_and_verify(stmt, true, op)) {
    return false;
  }
  SQLITE3_FETCH_COLUMNS_START("start", "end");
  SQLITE3_FETCH_COLUMNS_LOOP_HEADER(i, stmt)
    if (!IS_EXPECTED_COLUMN) {
      PRINT_COLUMN_MISMATCH_MSG(OP);
      return false;
    }
    switch(i) {
      case 0: start = SQLITE3_FETCH(int); break;
      case 1: end = SQLITE3_FETCH(int); break;
      default: fprintf(stderr, "%s: ", op); PRINT_UNEXPECTED_COLUMN_MSG("");
    }
  SQLITE3_FETCH_COLUMNS_END;
  return true;
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
  if (!step_and_verify(insert_watcher, true, OP)) {
    return false;
  }
  SQLITE3_FETCH_COLUMNS_START("start", "end", "id");
  SQLITE3_FETCH_COLUMNS_LOOP_HEADER(i, insert_watcher)
    if (!IS_EXPECTED_COLUMN) {
      PRINT_COLUMN_MISMATCH_MSG(OP);
      return false;
    }
    switch(i) {
      case 0: time_range_start = SQLITE3_FETCH(int); break;
      case 1: time_range_end = SQLITE3_FETCH(int); break;
      case 2: watcher_id = SQLITE3_FETCH(int64); break;
      default: PRINT_UNEXPECTED_COLUMN_MSG(OP);
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
