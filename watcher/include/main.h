#ifndef _TURINGWATCHER_MAIN_H
#define _TURINGWATCHER_MAIN_H
#include "common.h"
#include "tresdef.h"

#include "sqlite_helper.h"
#include "ddl.sql"
#include "modify.sql"

#define WATCHER_ENV_PREFIX "TURING_WATCH_"
#define WATCHER_ENV(X) WATCHER_ENV_PREFIX X
#define IS_SCRAPER_ENV WATCHER_ENV("SCRAPER")
#define DB_FILE_ENV WATCHER_ENV("DB_FILE")
#define PRINT_ONLY_ENV WATCHER_ENV("PRINT_ONLY")
#define DEFAULT_DB_PATH "./turingwatch.db"

enum WorkerType {
  WORKER_SCRAPER,
  WORKER_WATCHER,
  WORKER_PARENT,
};

struct tres_t {
  size_t value[TRES_SIZE];
  // comma delimitered string of form index=value
  tres_t(const char *tres_str);
  tres_t() {
    memset(value, 0, sizeof(value));
  }
  tres_t &operator +=(const tres_t &rhs) {
    for (int i = 0; i < TRES_SIZE; i++)
      value[i] += rhs.value[i];
    return *this;
  }
  void print();
};

static void print_only();
#endif
