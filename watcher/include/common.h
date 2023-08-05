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
#include <queue>

#include <stdexcept>

#include <pthread.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
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
#define ENABLE_DEBUGOUT 1
#define ENABLE_VERBOSE_DEBUGOUT 0
#if ENABLE_DEBUGOUT
#define DEBUGOUT(X) X
#if ENABLE_VERBOSE_DEBUGOUT
#define DEBUGOUT_VERBOSE(X) X
#else
#define DEBUGOUT_VERBOSE(X) ;
#endif
#else
#define DEBUGOUT_VERBOSE(X) ;
#define DEBUGOUT(X) ;
#endif
#define DISTRIBUTE_DUMMY_WATCHER 0

#define WATCHER_ENV_PREFIX "TURING_WATCH_"
#define WATCHER_ENV(X) WATCHER_ENV_PREFIX X
#define IS_SCRAPER_ENV WATCHER_ENV("SCRAPER")
#define DB_FILE_ENV WATCHER_ENV("DB_FILE")
#define PRINT_ONLY_ENV WATCHER_ENV("PRINT_ONLY")
#define RUN_ONCE_ENV WATCHER_ENV("RUN_ONCE")
#define DB_HOST_ENV WATCHER_ENV("DB_HOST")
#define PORT_ENV WATCHER_ENV("PORT")
#define UPDATE_JOBINFO_ONLY_ENV WATCHER_ENV("UPDATE_JOBINFO_ONLY")
#define DEFAULT_DB_PATH "./turingwatch.db"

#define RESTORE_ENV 0

typedef char *(*slurm_job_state_string_func_t)(uint32_t);
extern slurm_job_state_string_func_t slurm_job_state_string;
typedef uint16_t protocol_version_t;

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
  WORKER_SPECIAL,
};

struct worker_info_t {
  char *hostname;
  pid_t pid;
  bool is_privileged;
  worker_type_t type;
  slurm_step_id_t jobstep_info;
};

// Connections
extern sqlite3 *SQL_CONN_NAME;
extern void *slurm_conn;
extern int sock;
extern bool is_server;

// Watcher Metadata
extern worker_info_t worker;
extern bool run_once;
extern bool update_jobinfo_only;
#define is_watcher (worker.type == WORKER_WATCHER)
#define is_scraper (worker.type == WORKER_SCRAPER)
#define is_parent (worker.type == WORKER_PARENT)
extern char *db_path;
extern std::string slurm_conf_path;

// Watcher Parameters
extern int watcher_id;
extern time_t time_range_start;
extern time_t time_range_end;
#endif
