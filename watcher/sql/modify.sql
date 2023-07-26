#ifndef _TURINGWATCHER_SQL_MODIFY_SQL
#define _TURINGWATCHER_SQL_MODIFY_SQL
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
#endif
