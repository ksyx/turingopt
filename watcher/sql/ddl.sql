#ifndef _TURINGWATCHER_SQL_DDL_SQL
#define _TURINGWATCHER_SQL_DDL_SQL
#include "sql_helper.h"

const char *INIT_DB_SQL = SQLITE_CODEBLOCK(
CREATE TABLE IF NOT EXISTS watcher(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  pid INTEGER NOT NULL CHECK (pid >= 0),
  target_node TEXT,
  /* jobid == 0: node/cluster monitoring */
  jobid INTEGER NOT NULL CHECK (jobid >= 0),
  stepid INTEGER,
  /* uid is that of root or slurm, or owner(jobid) is uid */
  privileged INTEGER CHECK (privileged IN (0,1)) NOT NULL,

  /* generated columns */
  source INTEGER GENERATED ALWAYS AS (
  CASE
    WHEN jobid > 0 THEN 'parent'
    WHEN privileged THEN 'privileged'
    WHEN target_node IS NULL THEN 'sacct'
    ELSE 'raw_readings'
  END
  ) VIRTUAL NOT NULL, /* NULL: Indecisive */
  accuracy INTEGER GENERATED ALWAYS AS (
  CASE source
  WHEN 'raw_readings' THEN 0 /* With progressive data but not so trustworthy */
  WHEN 'sacct' THEN 1 /* Without progressive data but sum is trustworthy */
  WHEN 'privileged' THEN 2 /* With trustworthy and progressive data */
  WHEN 'parent' THEN 3 /* Higher sampling rate by being parent */
  ELSE NULL
  END
  ) STORED NOT NULL,
  jobstep_str TEXT GENERATED ALWAYS AS (
    CAST(jobid AS TEXT)
    || "."
    || CASE WHEN stepid IS NOT NULL THEN CAST(stepid AS TEXT) ELSE "n" END)
  STORED,

  /* Statement level CAS */
  lastfetch INTEGER NOT NULL DEFAULT(unixepoch('now')),
  prev_lastfetch INTEGER NOT NULL DEFAULT(unixepoch('now', '-28 days'))
    CHECK(prev_lastfetch <= lastfetch),

  /* UPDATE NULL INDEX WHEN CHANGED */
  UNIQUE (jobstep_str, target_node, accuracy)
);

CREATE INDEX IF NOT EXISTS watcher_index ON watcher(jobid) WHERE jobid > 0;

/* MUST BE UNIQUE COMPOSITION \ NONDUPLICATING NULLABLE COLUMNS */
CREATE UNIQUE INDEX IF NOT EXISTS watcher_unique_null
  ON watcher (jobstep_str, accuracy) WHERE target_node IS NULL;

CREATE TABLE IF NOT EXISTS jobinfo(
  jobid INTEGER NOT NULL CHECK(jobid > 0),
  stepid INTEGER,
  user TEXT,
  job_name TEXT,
  submit_line TEXT,
  PRIMARY KEY (jobid, stepid)
) WITHOUT ROWID;

CREATE UNIQUE INDEX IF NOT EXISTS jobinfo_unique_null
  ON jobinfo (jobid) WHERE stepid IS NULL;

CREATE TABLE IF NOT EXISTS measurements(
  recordid INTEGER PRIMARY KEY AUTOINCREMENT,
  watcherid INTEGER REFERENCES watcher(id),
  jobid INTEGER REFERENCES jobinfo(jobid),
  stepid INTEGER,
  /* timestamp INTEGER DEFAULT(unixepoch('now')) NOT NULL, */
  /* should ORDER BY tot_time */

  /* measurements */
  dev_in INTEGER, dev_out INTEGER,
  user_sec INTEGER NOT NULL, user_usec INTEGER NOT NULL,
  sys_sec INTEGER NOT NULL, sys_usec INTEGER NOT NULL,
  tot_time INTEGER
    GENERATED ALWAYS AS (user_sec + user_usec + sys_sec + sys_usec) STORED
    CHECK (tot_time > 0),
  res_size INTEGER, /* resident set size */
  minor_pagefault INTEGER,

  /* GPU utilization data could be directly updated given the entry exists */
  gpu_util INTEGER
);

CREATE INDEX IF NOT EXISTS measurements_index ON measurements(tot_time);

CREATE TRIGGER IF NOT EXISTS measurement_quality_ensurance
  BEFORE INSERT ON measurements
  FOR EACH ROW
  WHEN NEW.tot_time == 0
    OR EXISTS (
      WITH newentry_watcher AS MATERIALIZED (
        SELECT accuracy, target_node FROM watcher
          WHERE NEW.watcherid == watcher.id
      )
      SELECT measurements.jobid FROM measurements, watcher, newentry_watcher
        WHERE measurements.jobid == NEW.jobid
              AND measurements.stepid IS NEW.stepid
              AND measurements.watcherid == watcher.id
                   /* More accurate measurements available */
              AND (watcher.accuracy > newentry_watcher.accuracy
                 OR (watcher.accuracy == newentry_watcher.accuracy
                     /* Nothing new, theoretically */
                     AND tot_time == NEW.tot_time
                     AND watcher.target_node IS newentry_watcher.target_node
                 )
              )
    )
BEGIN SELECT RAISE (IGNORE); END;

CREATE TRIGGER IF NOT EXISTS measurements_del BEFORE DELETE ON measurements
BEGIN
  SELECT RAISE(ABORT, 'Deletion of measurement record not supported');
END;

CREATE TRIGGER IF NOT EXISTS measurements_upd BEFORE UPDATE ON measurements
BEGIN
  SELECT RAISE(ABORT, 'Update of measurement record not supported');
END;
);
#endif