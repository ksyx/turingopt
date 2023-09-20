#ifndef _TURINGWATCHER_SQLITE_HELPER_H
#define _TURINGWATCHER_SQLITE_HELPER_H
#define SQL_CONN_NAME sqlite_conn // of type sqlite3 *

#define SQLITE3_PERROR(NAME) \
  fprintf(stderr, "sqlite3_" NAME ": %s\n", sqlite3_errmsg(SQL_CONN_NAME));

#define PREPARE_STMT(SQL, STMT, PERSISTENT) \
  sqlite3_prepare_v3(SQL_CONN_NAME, SQL, -1, \
                     PERSISTENT ? SQLITE_PREPARE_PERSISTENT : 0, STMT, NULL)

#define BIND_FAIL_VAR __sqlite3_bind_failure__
#define BIND_FAILED (BIND_FAIL_VAR)
#define SQLITE3_BIND_START { bool BIND_FAIL_VAR = false;
#define SQLITE3_BIND_END }
#define BIND_NAME(STMT, NAME) sqlite3_bind_parameter_index(STMT, NAME)
#define BIND_NULL(STMT, ARGNAME) \
  sqlite3_bind_null(STMT, BIND_NAME(STMT, ARGNAME));
// Follow VAL with a comma if any varg exists
#define SQLITE3_NAMED_BIND(TY, STMT, ARGNAME, VAL, ...) \
  if (sqlite3_bind_##TY(STMT, BIND_NAME(STMT, ARGNAME), VAL __VA_ARGS__) \
      != SQLITE_OK) { \
    SQLITE3_PERROR("bind_" #TY "(" ARGNAME ")"); \
    BIND_FAIL_VAR = true; \
  }
#define NAMED_BIND_INT(STMT, ARGNAME, VAL) \
  SQLITE3_NAMED_BIND(int, STMT, ARGNAME, VAL)
#define NAMED_BIND_TEXT(STMT, ARGNAME, STR) \
  SQLITE3_NAMED_BIND(text, STMT, ARGNAME, STR, , -1, SQLITE_STATIC)

#define EXPECTED_COLUMN_NAMES_VAR __expected_column_names__
#define IDX_VAR_REF_NAME __column_idx__
#define STMT_REF_NAME __stmt__
#define SQLITE3_FETCH_COLUMNS_START(...) \
  { static const char *EXPECTED_COLUMN_NAMES_VAR[] = {__VA_ARGS__};
#define SQLITE3_FETCH_COLUMNS_LOOP_HEADER(VAR, STMT) \
  for (int VAR = 0; VAR < sqlite3_column_count(STMT); ++VAR) { \
    auto &STMT_REF_NAME = STMT; \
    auto &IDX_VAR_REF_NAME = VAR;
// For matching brackets in source files
#define SQLITE3_FETCH_COLUMNS_END }}
#define SQLITE3_FETCH(TY) sqlite3_column_##TY(STMT_REF_NAME, IDX_VAR_REF_NAME)
#define SQLITE3_FETCH_STR(TY) SQLITE3_FETCH(text)
#define GET_COLUMN_NAME() SQLITE3_FETCH(name)
#define PRINT_UNEXPECTED_COLUMN_MSG(OP) \
      fprintf(stderr, \
              "fetch_result_column" OP ": unexpected column index %d\n", \
              IDX_VAR_REF_NAME);
#if ENABLE_DEBUGOUT
#define IS_EXPECTED_COLUMN \
  (strcmp(EXPECTED_COLUMN_NAMES_VAR[IDX_VAR_REF_NAME], GET_COLUMN_NAME()) == 0)
#define PRINT_COLUMN_MISMATCH_MSG(OP) \
      fprintf(stderr, "fetch_result_column%s: expecting '%s', got '%s'\n", \
                OP, EXPECTED_COLUMN_NAMES_VAR[IDX_VAR_REF_NAME], \
                GET_COLUMN_NAME());
#else
#define IS_EXPECTED_COLUMN (true)
#define PRINT_COLUMN_MISMATCH_MSG(OP) ;
#endif
#endif
