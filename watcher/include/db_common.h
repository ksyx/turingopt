#ifndef _TURINGWATCHER_DB_COMMON_H
#define _TURINGWATCHER_DB_COMMON_H
#include "common.h"

bool renew_watcher(const char *query, worker_info_t *worker = &worker);

// Setting it to NULL breaks finializations midway at unprepared statements
#define FINALIZE_END_ADDR (sqlite3_stmt *)0xbadbeef
void finalize_stmt_array(sqlite3_stmt *stmt_to_finalize[]);
void db_common_finalize();
bool step_renew(sqlite3_stmt *stmt, const char *op, int &start, int &end);
bool step_and_verify(sqlite3_stmt *stmt, bool expect_rows, const char *op);
void sqlite3_begin_transaction();
bool sqlite3_end_transaction();
bool sqlite3_exec_wrap(const char *sql, const char *op);
bool setup_stmt(sqlite3_stmt *&stmt, const char *sql, const char *op);
bool reset_stmt(sqlite3_stmt *stmt, const char *op);
#endif
