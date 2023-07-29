#ifndef _TURINGWATCHER_COMMON_H
#define _TURINGWATCHER_COMMON_H
#include <slurm/slurmdb.h>
#include <slurm/slurm.h>
// True for slurm versions starting commit 5676bdf1a5c or release 22-05-0-0-rc1
#define SLURM_TRACK_STEPS_REMOVED 0
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include <stdexcept>

#include <linux/futex.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <dlfcn.h>
#include <pwd.h>

#include <sqlite3.h>
#include "sqlite_helper.h"

#include "tresdef.h"
#include "sql.h"

#define SLURM_USER_IS_PRIVILEGED 0
#define ENABLE_DEBUGOUT 1
#if ENABLE_DEBUGOUT
#define DEBUGOUT(X) X
#else
#define DEBUGOUT(X) ;
#endif

typedef char *(*slurm_job_state_string_func_t)(uint32_t);
extern slurm_job_state_string_func_t slurm_job_state_string;

#define EXPECT_EQUAL(EXPR, EXPECT) ((EXPR) == (EXPECT))
#define IS_SQLITE_OK(EXPR) EXPECT_EQUAL(EXPR, SQLITE_OK)
#define IS_SLURM_SUCCESS(EXPR) EXPECT_EQUAL(EXPR, SLURM_SUCCESS)

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
  size_t &operator[](std::size_t idx) {
    return value[TRES_IDX(idx)];
  }
  void print();
};

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
