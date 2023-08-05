#ifndef _TURINGWATCHER_DB_COMMON_H
#define _TURINGWATCHER_DB_COMMON_H
#include "common.h"

bool renew_watcher(const char *query, worker_info_t *worker = &worker);

// Setting it to NULL breaks finializations midway at unprepared statements
#define FINALIZE_END_ADDR (sqlite3_stmt *)0xbadbeef
void finalize_stmt_array(sqlite3_stmt *stmt_to_finalize[]);
void db_common_finalize();
void sqlite3_begin_transaction();
bool sqlite3_end_transaction();
#endif
