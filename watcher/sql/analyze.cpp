#include "sql_helper.h"

const char *ANALYZE_LIST_ACTIVE_USERS = SQLITE_CODEBLOCK(
  SELECT DISTINCT user FROM measurements, jobinfo
  WHERE measurements.recordid > :offset_start
        AND measurements.recordid <= :offset_end
        AND measurements.jobid == jobinfo.jobid
        AND user IS NOT NULL;
  EXCEPT SELECT user FROM analyze_user_info WHERE skip IS 1;
);

const char *PRE_ANALYZE_SQL = SQLITE_CODEBLOCK(
  ATTACH DATABASE ':memory:' AS inmem;
  BEGIN TRANSACTION;
);

const char *ANALYZE_CREATE_BASE_TABLES[] = {
  SQLITE_CODEBLOCK(
  CREATE TABLE inmem.measurements AS
    SELECT measurements.*, mem AS mem_limit,
           max(recordid) OVER win AS latest_recordid
    FROM measurements, watcher, jobinfo
    WHERE jobinfo.user IS :user
          AND watcher.target_node IS NOT NULL
          AND measurements.watcherid == watcher.id
          AND measurements.jobid == jobinfo.jobid
    WINDOW win AS (PARTITION BY measurements.jobid, measurements.stepid);
  ), SQLITE_CODEBLOCK(

  CREATE TABLE inmem.timeseries AS
    WITH grouped_measurements AS MATERIALIZED (
      WITH batch_ranges AS MATERIALIZED (
        SELECT rank() OVER win AS measurement_batch,
              lag(recordid, -1, 9e18) OVER win - 1 AS r,
              recordid AS l
        FROM (
          SELECT
            recordid, (lag(watcherid) OVER win IS NOT watcherid) AS flag
          FROM inmem.measurements
          WINDOW win AS (ORDER BY recordid)) WHERE flag=1
        WINDOW win AS (ORDER BY recordid)
      )
      SELECT *, (
        SELECT measurement_batch FROM batch_ranges WHERE
          recordid >= l AND recordid <= r
      ) AS measurement_batch 
        FROM inmem.measurements
    ), recombined_jobinfo AS MATERIALIZED (
      SELECT
        jobid, stepid,
        first_value(user) OVER win AS user,
        first_value(name) OVER win || '/' || iif(name IS NULL, 'null', name)
          AS name,
        first_value(submit_line) OVER win AS submit_line,
        first_value(ngpu) OVER win AS ngpu,
        first_value(nnodes) OVER win AS nnodes,
        started_at - first_value(started_at) OVER win AS step_start_offset,
        ended_at - first_value(started_at) OVER win AS step_end_offset,
        first_value(ended_at - started_at) OVER win AS job_length,
        ifnull(timelimit, first_value(timelimit) OVER win) AS timelimit
      FROM jobinfo
      WHERE user IS NULL OR user IS :user
    WINDOW win AS (PARTITION BY jobid ORDER BY stepid NULLS FIRST)
  ) SELECT
    *,
    user_sec * 1e6 + user_usec
      - lag(user_sec * 1e6 + user_usec, 1, user_sec * 1e6 + user_usec)
    OVER win AS delta_user_time,
    sys_sec * 1e6 + sys_usec
      - lag(sys_sec * 1e6 + sys_usec, 1, sys_sec * 1e6 + sys_usec)
    OVER win AS delta_sys_time,
    res_size - lag(res_size, 1, res_size) OVER win AS delta_res_size,
    res_size - first_value(res_size) OVER win AS delta_res_size_from_start,
    lag(res_size) OVER win IS NOT NULL AND
    res_size - (max(res_size) OVER win + min(res_size) OVER win) / 2 >= 0
    IS NOT
    lag(res_size) OVER win
      - (max(res_size) OVER win + min(res_size) OVER win) / 2
    >= 0
    AS delta_res_size_from_midline,
    minor_pagefault - lag(minor_pagefault, 1, minor_pagefault)
    OVER win AS delta_minor_pagefault
    FROM grouped_measurements, recombined_jobinfo
    WHERE
    recombined_jobinfo.jobid == grouped_measurements.jobid
    AND recombined_jobinfo.stepid == grouped_measurements.stepid
    WINDOW win AS (
      PARTITION BY
        grouped_measurements.jobid,
        grouped_measurements.stepid,
        measurement_batch
    );
  ), SQLITE_CODEBLOCK(

  DELETE FROM inmem.measurements;
  ), SQLITE_CODEBLOCK(

  CREATE TABLE inmem.sys_ratio AS
    WITH systime_ratio_base AS MATERIALIZED (
    WITH systime_ratio_base AS MATERIALIZED (
    WITH systime_ratio_base AS MATERIALIZED (
      SELECT jobid, stepid, latest_recordid,
             measurement_batch, name, submit_line, 
             delta_sys_time / delta_user_time AS sys_ratio
      FROM inmem.timeseries
      WHERE delta_sys_time >= 1e6 AND delta_user_time > 0
    )
    SELECT *,
      CASE WHEN sys_ratio IS NULL THEN NULL
          WHEN sys_ratio <= 0.1 THEN 0
          WHEN sys_ratio <= 1.0 / 3 THEN 333
          WHEN sys_ratio <= 2.0 / 3 THEN 667
          WHEN sys_ratio <= 1 THEN 1000
          ELSE sys_ratio * 1000
      END AS sys_ratio_category FROM systime_ratio_base
    ) SELECT *, count(*) AS tot_in_batch,
      count(sys_ratio_category) FILTER (WHERE sys_ratio_category == 333)
        AS sys_ratio_1_10,
      count(sys_ratio_category) FILTER (WHERE sys_ratio_category == 667)
        AS sys_ratio_1_3,
      count(sys_ratio_category) FILTER (WHERE sys_ratio_category == 1000)
        AS sys_ratio_2_3,
      total(sys_ratio_category / 1000) FILTER (WHERE sys_ratio_category > 1000)
        AS sys_ratio_gt_1
      FROM systime_ratio_base
      GROUP BY jobid, stepid
    )
    SELECT *,
      sys_ratio_1_3 + sys_ratio_2_3 * 2 + sys_ratio_gt_1 * 3
        AS major_ratio_unified_tot
      FROM systime_ratio_base;
  ), SQLITE_CODEBLOCK(
  
  CREATE TABLE inmem.gpu_usage_base AS
    WITH gpu_usage_base AS MATERIALIZED (
    WITH gpu_measurements_flagged AS MATERIALIZED (
    WITH gpu_measurements_flagged AS MATERIALIZED (
    WITH gpu_measurements_flagged AS MATERIALIZED (
      SELECT
        gpu_measurements.jobid, gpu_measurements.stepid, batch,
        latest_recordid, age, gpuid, power_usage, temperature, sm_clock, util,
        age IS NOT 0 AND age > lag(age) OVER win IS 1 AS discard,
        util == 0 AS zero_flags
      FROM gpu_measurements, inmem.timeseries
      WHERE gpu_measurement_batch == batch
      WINDOW win AS
      (PARTITION BY measurement_batch,
                    gpu_measurements.jobid, gpu_measurements.stepid, gpuid,
                    power_usage, temperature, sm_clock, util
      ORDER BY batch)
    )
      SELECT *,
            CASE WHEN zero_flags IS 0 OR lead(zero_flags) OVER win IS NULL
              THEN ifnull(-sum(zero_flags) OVER (
                            win ROWS UNBOUNDED PRECEDING EXCLUDE CURRENT ROW),
                          0)
              ELSE 0
            END AS segment_breaker
      FROM gpu_measurements_flagged
      WHERE discard IS 0
      WINDOW win AS (PARTITION BY jobid, stepid, gpuid ORDER BY batch)
    )
      SELECT *,
            CASE WHEN segment_breaker IS NOT 0
              THEN -(segment_breaker
                      - ifnull(
                          min(segment_breaker) OVER (
                            win ROWS UNBOUNDED PRECEDING EXCLUDE CURRENT ROW),
                          0)
                      ) + iif(util IS 0, 1, 0)
              ELSE 0
              END AS zero_segment_length
      FROM gpu_measurements_flagged
      WINDOW win AS (PARTITION BY jobid, stepid, gpuid ORDER BY batch)
    )
    SELECT ts.watcherid, ts.jobid, ts.stepid, ts.latest_recordid, ts.nnodes,
           ts.measurement_batch, name, submit_line,
           batch AS gpu_measurement_batch, gpuid,
           power_usage, temperature, sm_clock, util, zero_segment_length
    FROM inmem.timeseries AS ts, gpu_measurements_flagged
    WHERE ts.gpu_measurement_batch IS NOT NULL
          AND gpu_measurements_flagged.batch == ts.gpu_measurement_batch
          AND gpu_measurements_flagged.jobid == ts.jobid
          AND gpu_measurements_flagged.stepid == ts.stepid
    ) SELECT gpu_usage_base.jobid, gpu_usage_base.stepid, latest_recordid, name,
       iif(nnodes > 1, target_node || '/', '') || gpuid AS gpuid,
       submit_line, count(util) AS measurement_cnt,
       avg(util) AS avg_util, sum(zero_segment_length) AS zero_util_cnt,
       count(util) FILTER (WHERE util > 0 AND util < 13) AS low_util_cnt,
       max(zero_segment_length) AS longest_continuous_zero_util,
       avg(sm_clock) AS avg_clock, avg(power_usage) / 100 AS avg_power_usage,
       avg(temperature) AS avg_temperature
      FROM gpu_usage_base, watcher
      WHERE watcher.id == watcherid
      GROUP BY gpu_usage_base.jobid, gpu_usage_base.stepid, target_node, gpuid;
), 0};

#define _FILTER_LATEST_RECORD_SQL \
  "latest_recordid > :offset_start AND latest_recordid <= :offset_end"

#define _SUMMARIZE_GPU_PROBLEM_SQL SQLITE_CODEBLOCK(                           \
  iif(zero_util_cnt == measurement_cnt, 'completely_no_util',                  \
    rtrim(                                                                     \
      iif(longest_continuous_zero_util >= measurement_cnt * 0.25,                 \
          'try_split | ', '')                                                  \
      ||                                                                       \
      iif(low_util_cnt + zero_util_cnt >= 0.9 * measurement_cnt,               \
          'investigate_usage', ''),                                            \
      '| ')) AS problem_tag                                                    \
  )

const char *ANALYZE_LATEST_GPU_USAGE_SQL
  = "SELECT *, " _SUMMARIZE_GPU_PROBLEM_SQL
    " FROM inmem.gpu_usage_base"
    " WHERE " _FILTER_LATEST_RECORD_SQL
    " ORDER BY length(problem_tag) DESC, name, jobid, stepid";

const char *ANALYZE_GPU_USAGE_HISTORY_SQL
  = SQLITE_CODEBLOCK(
    WITH grouped_gpu_usage AS (
    SELECT
      jobid, stepid, name,
      sum(measurement_cnt) AS measurement_cnt,
      sum(avg_util * measurement_cnt) / sum(measurement_cnt) AS avg_util,
      sum(zero_util_cnt) AS zero_util_cnt,
      sum(low_util_cnt) AS low_util_cnt,
      max(longest_continuous_zero_util) AS longest_continuous_zero_util,
      sum(avg_clock * measurement_cnt) / sum(measurement_cnt)
        AS avg_clock,
      sum(avg_power_usage * measurement_cnt) / sum(measurement_cnt)
        AS avg_power_usage
    FROM inmem.gpu_usage_base
    GROUP BY name, submit_line
    ) SELECT *,
  )
  _SUMMARIZE_GPU_PROBLEM_SQL
  " FROM grouped_gpu_usage"
  " ORDER BY length(problem_tag) DESC, name";

#undef _SUMMARIZE_GPU_PROBLEM_SQL

#define _PROBLEMATIC_SYS_RATIO_CONDITION \
  "major_ratio_unified_tot >= tot_in_batch"

#define _ANALYZE_SYS_TIME_RATIO_SQL(EXTRA_CONDITION) \
  "SELECT * FROM inmem.sys_ratio" \
  "  WHERE " _FILTER_LATEST_RECORD_SQL " " EXTRA_CONDITION \
  "  ORDER BY name, jobid, stepid, measurement_batch"

const char *ANALYSIS_LIST_PROBLEMATIC_LATEST_SYS_RATIO_SQL
  = _ANALYZE_SYS_TIME_RATIO_SQL(
    "AND " _PROBLEMATIC_SYS_RATIO_CONDITION
    " AND " _FILTER_LATEST_RECORD_SQL
  );

const char *ANALYSIS_LIST_LATEST_SYS_RATIO_SQL
  = _ANALYZE_SYS_TIME_RATIO_SQL("AND " _FILTER_LATEST_RECORD_SQL);

#undef _ANALYZE_SYS_TIME_RATIO_SQL

const char *ANALYZE_SYS_RATIO_HISTORY_SQL
  = SQLITE_CODEBLOCK(
    WITH grouped_sys_ratio AS (
    SELECT jobid, stepid, name,
      sum(tot_in_batch) AS tot_in_batch,
      sum(sys_ratio_1_10) AS sys_ratio_1_10,
      sum(sys_ratio_1_3) AS sys_ratio_1_3,
      sum(sys_ratio_2_3) AS sys_ratio_2_3,
      sum(sys_ratio_gt_1) AS sys_ratio_gt_1,
      sum(major_ratio_unified_tot) AS major_ratio_unified_tot
      FROM inmem.sys_ratio
      GROUP BY name, submit_line
      ORDER BY name, submit_line, measurement_batch
    ) SELECT *,
  ) "iif(" _PROBLEMATIC_SYS_RATIO_CONDITION ", 'sys_ratio', '') AS problem_tag"
  "  FROM grouped_sys_ratio"
  "  ORDER BY problem_tag DESC, major_ratio_unified_tot / tot_in_batch, name;";

#undef _FILTER_LATEST_RECORD_SQL

#undef _PROBLEMATIC_SYS_RATIO_CONDITION

const char *POST_ANALYZE_SQL = SQLITE_CODEBLOCK(
  ROLLBACK;
  DETACH DATABASE inmem;
);
