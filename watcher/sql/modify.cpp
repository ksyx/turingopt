#include "sql_helper.h"

#define _RENEW_SQL(FIELD, NEW_VAL) \
  " UPDATE SET(" FIELD ", prev_" FIELD ") = (" NEW_VAL ", " FIELD ")" \
  "   RETURNING prev_" FIELD " AS start, " FIELD " AS end"

#define _RENEW_SQL_UPSERT(FIELD) \
  " ON CONFLICT DO " _RENEW_SQL(FIELD, "excluded." FIELD)

#define _RENEW_SQL_BASE SQLITE_CODEBLOCK( \
  INSERT INTO watcher(pid, target_node, jobid, stepid, privileged) \
    VALUES(:pid, :target_node, :jobid, :stepid, :privileged) \
)
 #define _RENEW_WATCHER_RETURNING_TIMESTAMP_RANGE_SQL \
  _RENEW_SQL_BASE _RENEW_SQL_UPSERT("lastfetch")

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
  INSERT INTO jobinfo(
    jobid, stepid, user, name, submit_line,
    mem, node, ngpu
  ) VALUES (
    :jobid, :stepid, :user, :name, :submit_line,
    :mem, :node, :ngpu
  ) ON CONFLICT DO NOTHING
);
