#ifndef _TURINGWATCHER_MAIN_H
#define _TURINGWATCHER_MAIN_H
#include "common.h"

#include "worker_interface.h"
#include "db_common.h"

#define WATCHER_ENV_PREFIX "TURING_WATCH_"
#define WATCHER_ENV(X) WATCHER_ENV_PREFIX X
#define IS_SCRAPER_ENV WATCHER_ENV("SCRAPER")
#define DB_FILE_ENV WATCHER_ENV("DB_FILE")
#define PRINT_ONLY_ENV WATCHER_ENV("PRINT_ONLY")
#define DEFAULT_DB_PATH "./turingwatch.db"

static void print_only();
#endif
