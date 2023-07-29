#include "sql_helper.h"

#define _RENEW_WATCHER_RETURNING_TIMESTAMP_RANGE_SQL SQLITE_CODEBLOCK( \
  INSERT INTO watcher(pid, target_node, jobid, stepid, privileged) \
    VALUES(:pid, :target_node, :jobid, :stepid, :privileged) \
  ON CONFLICT DO \
    UPDATE SET (lastfetch, prev_lastfetch) = (excluded.lastfetch, lastfetch) \
  RETURNING prev_lastfetch AS start, lastfetch AS end \
)

const char *REGISTER_WATCHER_SQL_RETURNING_TIMESTAMPS_AND_WATCHERID
  = _RENEW_WATCHER_RETURNING_TIMESTAMP_RANGE_SQL ", id";

const char *UPSERT_WATCHER_SQL_RETURNING_TIMESTAMP_RANGE
  = _RENEW_WATCHER_RETURNING_TIMESTAMP_RANGE_SQL;

const char *MEASUREMENTS_INSERT_SQL = SQLITE_CODEBLOCK(
  INSERT INTO measurements(
    watcherid, jobid, stepid,
    dev_in, dev_out,
    user_sec, user_usec,
    sys_sec, sys_usec,
    res_size, minor_pagefault,
    gpu_util
  ) VALUES (
    :watcherid, :jobid, :stepid,
    :dev_in, :dev_out,
    :user_sec, :user_usec,
    :sys_sec, :sys_usec,
    :res_size, :minor_pagefault,
    :gpu_util
  )
);

const char *JOBINFO_INSERT_SQL = SQLITE_CODEBLOCK(
  INSERT OR IGNORE INTO jobinfo(
    jobid, stepid, user, name, submit_line,
    mem, node, ngpu
  ) VALUES (
    :jobid, :stepid, :user, :name, :submit_line,
    :mem, :node, :ngpu
  )
);
