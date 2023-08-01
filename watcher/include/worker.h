#ifndef _TURINGWATCHER_WORKER_H
#define _TURINGWATCHER_WORKER_H
#include "common.h"
#include "sqlite_helper.h"
#include "db_common.h"

#define TRES_ID(X) tres_t::from_str(X)
#define DISK_TRES TRES_ID("fs/disk")
#define GPU_TRES TRES_ID("gres/gpu")
#define MEM_TRES TRES_ID("mem")

#define FREQ(HOUR, MINUTE, SECOND) HOUR * 60 * 60 + MINUTE * 60 + SECOND
// The data is always there so just ensure new findings are alerted at a
// reasonable frequency
constexpr int ACCOUNTING_RPC_INTERVAL = FREQ(1, 0, 0);

// Scrapers use system call rather than RPC so it is much cheaper to use
constexpr int SCRAPE_INTERVAL = FREQ(0, 0, 20);
constexpr int TOTAL_SCRAPE_TIME_PER_NODE = FREQ(0, 15, 0);
constexpr int SCRAPE_CONCURRENT_NODES = 4;

// A multiplier of 1eT converts a gpu_util of [0, 1] to first T digits following
// the decimal
#define GPU_UTIL_MULTIPLIER 1e4
typedef double gpu_util_t;

#define READ_BUF_SIZE 4096

#define TASK_COMM_LEN 32
struct scrape_result_t {
  char comm[TASK_COMM_LEN + 1];
  pid_t pid;
  size_t res;

  size_t minor_pagefault;
  size_t cminor_pagefault;

  time_t utime;
  time_t cutime;
  time_t stime;
  time_t cstime;

  /* Privileged info, -1 == NULL */
  size_t rchar;
  size_t wchar;

  void print(bool report_child = 1) const {
    fprintf(stderr, "%s pid=%d res=%ld minor=%ld",
      comm, pid, res, minor_pagefault);
    if (report_child) {
      fprintf(stderr, " cminor=%ld", cminor_pagefault);
    }
    fprintf(stderr, " utime=%ld stime=%ld", utime, stime);
    if (report_child) {
      fprintf(stderr, " cutime=%ld cstime=%ld", cutime, cstime);
    }
    fprintf(stderr, " rchar=%ld wchar=%ld\n", rchar, wchar);
  }
};
typedef std::map<pid_t, std::vector<pid_t> > process_tree_t;
typedef std::map<pid_t, scrape_result_t> scraper_result_map_t;
typedef std::map<pid_t, slurm_step_id_t> stepd_step_id_map_t;
typedef std::set<std::string> step_application_set_t;

#define STAT_MERGE_DST my_stat
#define STAT_MERGE_SRC child_stat
#define ACCUMULATE_SCRAPER_STAT(NAME) \
  STAT_MERGE_DST.NAME += STAT_MERGE_SRC.NAME
#define AGGERGATE_SCRAPER_STAT_MAX(NAME) \
  STAT_MERGE_DST.NAME = std::max(STAT_MERGE_SRC.NAME, STAT_MERGE_DST.NAME);

#undef FREQ
#endif
