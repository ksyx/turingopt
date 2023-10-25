#ifndef _TURINGWATCHER_WORKER_H
#define _TURINGWATCHER_WORKER_H
#include "common.h"
#include "sqlite_helper.h"
#include "db_common.h"
#include "messaging.h"
#include "gpu/interface.h"

// ========================== FOR DEBUG AND TEST ONLY ==========================
// THIS OPTION BRINGS KNOWN PROBLEM OF POSSIBLY MISSING MATCHING JOBINFO!!!
// Set this value to be the length of sleep that waits for last-minute messages
// Recommended to be 10 secs with manual scraper and 600 secs with distributor
// 0 = Disable
#define PROCESS_ALL_MSG_BEFORE_NEXT_ROUND 0 /* secs */
// ============================================================================

#define GUARANTEED_FREEZE_WAIT 3 /* secs */

#define TRES_ID(X) tres_t::from_str(X)
#define DISK_TRES TRES_ID("fs/disk")
#define CPU_TRES TRES_ID("cpu")
#define GPU_TRES TRES_ID("gres/gpu")
#define MEM_TRES TRES_ID("mem")
#define NODE_TRES TRES_ID("node")

#define SCRAPER_JOB_NAME "turingwatch"
#define FREQ(HOUR, MINUTE, SECOND) HOUR * 60 * 60 + MINUTE * 60 + SECOND
// The data is always there so just ensure new findings are alerted at a
// reasonable frequency
#define PRODUCTION_FREQ 1
constexpr int ACCOUNTING_RPC_INTERVAL = FREQ(
#if PRODUCTION_FREQ
1, 0, 0
#else
0, 2, 30
#endif
);

// Scrapers use system call rather than RPC so it is much cheaper to use
constexpr int SCRAPE_INTERVAL = FREQ(0, 0, 20);
constexpr int TOTAL_SCRAPE_TIME_PER_NODE = FREQ(
#if PRODUCTION_FREQ
0, 15, 0
#else
0, 1, 0
#endif
);
constexpr int SCRAPE_CNT = TOTAL_SCRAPE_TIME_PER_NODE / SCRAPE_INTERVAL;
// NOTE: The implementation could use up to double of this concurrency value
constexpr int SCRAPE_CONCURRENT_NODES = 4;
constexpr int ALLOCATION_TIMEOUT = 120;

#define READ_BUF_SIZE 4096

// No check for existence of mandatory arguments
struct measurement_rec_t {
  const int *recordid; /* Optional */
  const slurm_step_id_t *step_id;
  const size_t *dev_in;
  const size_t *dev_out;
  const size_t *res_size;
  const size_t *minor_pagefault; /* Optional */
  const pid_t *gpu_measurement_batch; /* Optional */

  const uint64_t *sys_cpu_sec;
  const uint32_t *sys_cpu_usec;
  const uint64_t *user_cpu_sec;
  const uint32_t *user_cpu_usec;
};

typedef std::map<pid_t, std::vector<pid_t> > process_tree_t;
typedef std::map<pid_t, scrape_result_t> scraper_result_map_t;
typedef std::map<pid_t, slurm_step_id_t> stepd_step_id_map_t;
typedef uint32_t node_val_t;
typedef std::set<std::string> step_application_set_t;
typedef std::set<std::string> node_set_t;
typedef std::vector<std::string> node_string_list_t;

struct node_string_part {
  struct range_t {
    std::pair<node_val_t /*start*/, node_val_t /*end*/> range;
    int length;
  };
  std::string prefix;
  std::vector<range_t> ranges;
};

typedef std::vector<node_string_part> node_group_t;
typedef std::map<std::string, node_val_t> node_usage_map_t;
typedef
std::vector<std::pair<node_val_t /*value_taken*/, uint8_t /*str_len*/>>
val_assignment_t;
typedef std::map<pid_t, std::vector<gpu_measurement_t *> >
  pid_gpu_measurement_map_t;

#define STAT_MERGE_DST my_stat
#define STAT_MERGE_SRC child_stat
#define ACCUMULATE_SCRAPER_STAT(NAME) \
  STAT_MERGE_DST.NAME += STAT_MERGE_SRC.NAME
#define AGGERGATE_SCRAPER_STAT_MAX(NAME) \
  STAT_MERGE_DST.NAME = std::max(STAT_MERGE_SRC.NAME, STAT_MERGE_DST.NAME);

typedef std::map<std::pair<uint32_t /*jobid*/, uint32_t /*stepid*/>,
                 int /*recordid*/> jobstep_val_map_t;
typedef jobstep_val_map_t jobstep_recordid_map_t;
slurmdb_job_cond_t *setup_job_cond();
void measurement_record_insert(
  slurmdb_job_cond_t *job_cond, const jobstep_recordid_map_t &map);

bool build_slurmdb_conn();
bool close_slurmdb_conn();

bool log_scraper_freq(const char *sql, const char *op);

#undef FREQ
#endif
