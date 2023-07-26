#ifndef _TURINGWATCHER_SQL_MODIFY_SQL
#define _TURINGWATCHER_SQL_MODIFY_SQL
#include "sql_helper.h"

#define _REGISTER_WATCHER_SQL_START SQLITE_CODEBLOCK( \
  INSERT INTO watcher(pid, target_node, jobid, stepid, privileged) \
    VALUES(:pid, :target_node, :jobid, :stepid, :privileged) \
)

#define _REGISTER_WATCHER_SQL_RETURN_WATCHERID SQLITE_CODEBLOCK( \
  RETURNING id, lastfetch AS start; \
)

#define _UPSERT_WATCHER_SQL_RETURN_TIMESTAMP_RANGE SQLITE_CODEBLOCK( \
  ON CONFLICT DO \
    UPDATE SET (lastfetch, prev_lastfetch) = (excluded.lastfetch, lastfetch) \
  RETURNING prev_lastfetch AS start, lastfetch AS end \
)

const char *REGISTER_WATCHER_SQL_RETURNING_WATCHERID
  = _REGISTER_WATCHER_SQL_START " " _REGISTER_WATCHER_SQL_RETURN_WATCHERID;

const char *UPSERT_WATCHER_SQL_RETURNING_TIMESTAMP_RANGE
  = _REGISTER_WATCHER_SQL_START " " _UPSERT_WATCHER_SQL_RETURN_TIMESTAMP_RANGE;

const char *TERMINATE_WATCHER_REGISTRATION = SQLITE_CODEBLOCK(
  DELETE FROM watcher WHERE id == :watcher_id
);
#endif
