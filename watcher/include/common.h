#ifndef _TURINGWATCHER_COMMON_H
#define _TURINGWATCHER_COMMON_H
#include <slurm/slurmdb.h>
#include <slurm/slurm.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include <stdexcept>

#include <sys/types.h>
#include <unistd.h>

#include <sqlite3.h>
#include "sqlite_helper.h"

#define ENABLE_DEBUGOUT 1
#if ENABLE_DEBUGOUT
#define DEBUGOUT(X) X
#else
#define DEBUGOUT(X) ;
#endif

#define EXPECT_EQUAL(EXPR, EXPECT) ((EXPR) == (EXPECT))
#define IS_SQLITE_OK(EXPR) EXPECT_EQUAL(EXPR, SQLITE_OK)
#define IS_SLURM_SUCCESS(EXPR) EXPECT_EQUAL(EXPR, SLURM_SUCCESS)

enum worker_type_t {
  WORKER_SCRAPER,
  WORKER_WATCHER,
  WORKER_PARENT,
};

// Connections
extern sqlite3 *SQL_CONN_NAME;
extern void *slurm_conn;

// Watcher Metadata
extern char *hostname;
extern pid_t pid;
extern bool is_privileged;
extern worker_type_t worker_type;
#define is_scraper (worker_type == WORKER_SCRAPER)
#define is_parent (worker_type == WORKER_PARENT)
extern slurm_step_id_t jobstep_info;

// Watcher Parameters
extern int watcher_id;
extern time_t time_range_start;
extern time_t time_range_end;
#endif
