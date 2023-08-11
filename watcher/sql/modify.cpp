#include "sql_helper.h"
#include "sql.h"
#include "common.h"

#define _RENEW_SQL(TABLE, FIELD, NEW_VAL) \
  " UPDATE " TABLE " SET(" FIELD ", prev_" FIELD ") = (" NEW_VAL ", " FIELD ")" \
  "   RETURNING prev_" FIELD " AS start, " FIELD " AS end"

#define _RENEW_SQL_UPSERT(FIELD) \
  " ON CONFLICT DO " _RENEW_SQL("", FIELD, "excluded." FIELD)

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
    gpu_measurement_batch
  ) VALUES (
    :watcherid, :jobid, :stepid,
    :dev_in, :dev_out,
    :user_sec, :user_usec,
    :sys_sec, :sys_usec,
    :res_size, :minor_pagefault,
    :gpu_measurement_batch
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

const char *APPLICATION_USAGE_INSERT_SQL = SQLITE_CODEBLOCK(
  INSERT INTO application_usage(jobid, stepid, application)
    VALUES(:jobid, :stepid, :application) ON CONFLICT DO NOTHING
);

const char *GPU_MEASUREMENT_INSERT_SQL = SQLITE_CODEBLOCK(
  INSERT INTO gpu_measurements(
    watcherid, batch, pid, jobid, stepid, gpuid,
    temperature, sm_clock, util, clock_limit_reason, source
  ) VALUES (
    :watcherid, :batch, :pid, :jobid, :stepid, :gpuid,
    :temperature, :sm_clock, :util, :clock_limit_reason, :source
  )
);

const char *GET_SCHEMA_VERSION_SQL =
  "UPDATE worker_task_info SET schema_version = CASE schema_version"
  "  WHEN 0 THEN " STRINGIFY(DB_SCHEMA_VERSION) " ELSE schema_version END"
  "  RETURNING schema_version";

const char *UPDATE_GPU_BATCH_SQL
  = _RENEW_SQL("worker_task_info", "gpu_measurement_batch_cnt",
               "gpu_measurement_batch_cnt + 1");

const char *RENEW_ANALYSIS_OFFSET_SQL
  = SQLITE_CODEBLOCK(
    INSERT INTO worker_task_info
      SELECT MAX(recordid) AS analysis_offset FROM measurements WHERE TRUE
  ) _RENEW_SQL_UPSERT("analysis_offset");
