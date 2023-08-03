#ifndef _TURINGWATCHER_MAIN_H
#define _TURINGWATCHER_MAIN_H
#include "common.h"

#include "worker_interface.h"
#include "db_common.h"

#define DISTRIBUTE_NODE_WATCHER_ONLY \
  WATCHER_ENV("DISTRIBUTE_NODE_WATCHER_ONLY")

static void print_only();
#endif
