#ifndef _TURINGWATCHER_COMMON_H
#define _TURINGWATCHER_COMMON_H
#include <slurm/slurmdb.h>
#include <slurm/slurm.h>
// True for slurm versions starting commit 5676bdf1a5c or release 22-05-0-0-rc1
#define SLURM_TRACK_STEPS_REMOVED 0
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <map>
#include <set>

#include <stdexcept>

#include <linux/futex.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <dlfcn.h>
#include <pwd.h>
#include <errno.h>

#include <sqlite3.h>
#include "sqlite_helper.h"

#include "sql.h"

#define SLURM_USER_IS_PRIVILEGED 0
#define ENABLE_DEBUGOUT 0
#if ENABLE_DEBUGOUT
#define DEBUGOUT(X) X
#else
#define DEBUGOUT(X) ;
#endif

typedef char *(*slurm_job_state_string_func_t)(uint32_t);
extern slurm_job_state_string_func_t slurm_job_state_string;

#define _STRINGIFY(X) #X
#define STRINGIFY(X) _STRINGIFY(X)
#define EXPECT_EQUAL(EXPR, EXPECT) ((EXPR) == (EXPECT))
#define IS_SQLITE_OK(EXPR) EXPECT_EQUAL(EXPR, SQLITE_OK)
#define IS_SLURM_SUCCESS(EXPR) EXPECT_EQUAL(EXPR, SLURM_SUCCESS)

class tres_t {
public:
  std::map<size_t, size_t> value;
  // comma delimitered string of form index=value
  tres_t(const char *tres_str);
  tres_t();
  tres_t &operator +=(const tres_t &rhs) {
    for (auto &[idx, val] : rhs.value)
      value[idx] += val;
    return *this;
  }
  size_t &operator[](std::size_t idx) {
    return value[idx];
  }
  static int from_str(const char *str) {
    return tres_from_str[std::string(str)];
  }
  static const char *from_id(int idx) {
    return tres_from_id[idx].c_str();
  }
  void print();
private:
  static std::map<std::string, int> tres_from_str;
  static std::map<int, std::string> tres_from_id;
  static bool tres_map_initialized;
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
