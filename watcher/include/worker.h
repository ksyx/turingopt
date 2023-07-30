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

#undef FREQ
#endif
