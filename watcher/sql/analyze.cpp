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

#define SLURM_STYLE_TIME(VAR) \
  "ltrim((CAST(strftime('%Y', " #VAR ", 'unixepoch') AS INTEGER) - 1970)" \
  "      || '-', '0-')" \
  "|| (CAST(strftime('%j', " #VAR ", 'unixepoch') AS INTEGER) - 1)" \
  "|| strftime('-%H:%M:%S', " #VAR ", 'unixepoch') "

/*
Analysis ideas:
  - Infer how sys time is spent by checking for concurrent memory allocation
    or deallocation, as well as concurrent disk IO (needs to be process owner)
  - Memory reallocation rate -- How much memory is freed and subsequently reallocated
    (could maintain own memory pool to hold these chunks and accelearate allocation / free)
*/

const char *ANALYZE_CREATE_BASE_TABLES[] = {
  /*[0]*/SQLITE_CODEBLOCK(
  CREATE TABLE inmem.measurements AS
    SELECT measurements.*,
           max(recordid) OVER win AS latest_recordid
    FROM measurements, watcher, jobinfo
    WHERE jobinfo.user IS :user
          AND watcher.target_node IS NOT NULL
          AND measurements.watcherid == watcher.id
          AND measurements.jobid == jobinfo.jobid
    WINDOW win AS (PARTITION BY measurements.jobid, measurements.stepid);
  ), /*[1]*/SQLITE_CODEBLOCK(

    CREATE TABLE inmem.recombined_jobinfo AS SELECT * FROM (
      SELECT jobid, stepid,
      first_value(mem) OVER win AS mem_limit,
      first_value(user) OVER win AS user,
      first_value(name) OVER win || '/' || iif(name IS NULL, 'null', name)
        AS name,
      first_value(submit_line) OVER win AS submit_line,
      first_value(ngpu) OVER win AS ngpu,
      first_value(nnodes) OVER win AS nnodes,
      started_at - first_value(started_at) OVER win AS step_start_offset,
      ended_at - first_value(started_at) OVER win AS step_end_offset,
      first_value(ended_at - started_at) OVER win AS job_length,
      ifnull(timelimit, first_value(timelimit) OVER win) AS timelimit,
      peak_res_size
    FROM jobinfo
    WHERE user IS NULL OR user IS :user
    WINDOW win AS (PARTITION BY jobid ORDER BY stepid NULLS FIRST)
    ) WHERE user IS :user
  ), /*[2]*/SQLITE_CODEBLOCK(

  CREATE TABLE inmem.timeseries AS
    WITH grouped_measurements AS MATERIALIZED (
      WITH batch_ranges AS MATERIALIZED (
        SELECT recordid, sum(flag) OVER win AS measurement_batch
        FROM (
          SELECT
            recordid, (
              lag(watcherid) OVER win IS NOT watcherid
            ) AS flag, (
              max(latest_recordid) OVER win2 > :offset_start
              AND max(latest_recordid) OVER win2 <= :offset_end
            ) AS kept
          FROM inmem.measurements
               JOIN inmem.recombined_jobinfo USING (jobid, stepid)
          WINDOW win AS (ORDER BY recordid),
                 win2 AS (PARTITION BY name, submit_line)
        ) WHERE kept
        WINDOW win AS (ORDER BY recordid
          ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW)
      )
      SELECT *, measurement_batch
        FROM inmem.measurements JOIN batch_ranges USING (recordid)
    )
    SELECT
      *,
      latest_recordid > :offset_start AND latest_recordid <= :offset_end
        AS is_new_in_period,
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
    FROM grouped_measurements
           JOIN inmem.recombined_jobinfo USING (jobid, stepid)
           /*LEFT*/ JOIN job_step_cpu_available USING (jobid, stepid, watcherid)
           JOIN (SELECT *, lead(start, 1, 2e9) OVER win AS endval
                   FROM scrape_freq_log_internal WINDOW win AS (ORDER BY start))
                      ON (recordid >= start AND recordid < endval)
    WHERE
      inmem.recombined_jobinfo.jobid == grouped_measurements.jobid
      AND inmem.recombined_jobinfo.stepid == grouped_measurements.stepid
    WINDOW win AS (
      PARTITION BY
        grouped_measurements.jobid,
        grouped_measurements.stepid,
        measurement_batch
    );
  ), /*[3]*/SQLITE_CODEBLOCK(

  DELETE FROM inmem.measurements;
  ), /*[4]*/SQLITE_CODEBLOCK(

  CREATE TABLE inmem.sys_ratio AS
    WITH systime_ratio_base AS MATERIALIZED (
    WITH systime_ratio_base AS MATERIALIZED (
    WITH systime_ratio_base AS MATERIALIZED (
      SELECT jobid, stepid, is_new_in_period,
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
  ), /*[5]*/SQLITE_CODEBLOCK(
  
  CREATE TABLE inmem.gpu_usage_base AS
    WITH gpu_usage_base AS MATERIALIZED (
    WITH gpu_measurements_flagged AS MATERIALIZED (
    WITH gpu_measurements_flagged AS MATERIALIZED (
    WITH gpu_measurements_flagged AS MATERIALIZED (
      SELECT
        gpu_measurements.jobid, gpu_measurements.stepid, batch,
        is_new_in_period, age, gpuid, power_usage, temperature, sm_clock, util,
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
    SELECT ts.watcherid, ts.jobid, ts.stepid, ts.is_new_in_period, ts.nnodes,
           ts.measurement_batch, name, submit_line,
           batch AS gpu_measurement_batch, gpuid,
           power_usage, temperature, sm_clock, util, zero_segment_length
    FROM inmem.timeseries AS ts, gpu_measurements_flagged
    WHERE ts.gpu_measurement_batch IS NOT NULL
          AND gpu_measurements_flagged.batch == ts.gpu_measurement_batch
          AND gpu_measurements_flagged.jobid == ts.jobid
          AND gpu_measurements_flagged.stepid == ts.stepid
    ) SELECT gpu_usage_base.jobid, gpu_usage_base.stepid,
       is_new_in_period, name, target_node, gpuid AS gpuid_raw,
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
), /*[6]*/SQLITE_CODEBLOCK(
  CREATE TABLE inmem.gpucpu_usage AS
  SELECT DISTINCT ts.jobid, ts.stepid, target_node AS node,
        ngpu_in_use, count(*) OVER win AS cnt_gpu, ts.ngpu,
        NULL AS ncpu_in_use, NULL AS cnt_cpu, NULL AS ncpu,
        count(*) OVER win_super AS node_tot, nnodes AS alloc_nnodes
  FROM inmem.timeseries AS ts, watcher,
      (SELECT batch, count(gpuid) AS ngpu_in_use
        FROM gpu_measurements GROUP BY batch, gpuid
        UNION SELECT NULL, 0)
  WHERE batch IS gpu_measurement_batch AND watcher.id == watcherid
        AND ts.is_new_in_period
  WINDOW win AS
          (PARTITION BY ts.jobid, ts.stepid, target_node, ngpu_in_use),
        win_super AS
          (PARTITION BY ts.jobid, ts.stepid, target_node)
  UNION
  SELECT DISTINCT ts.jobid, ts.stepid, target_node AS node,
        NULL AS ngpu_in_use, NULL AS cnt_gpu, NULL AS ngpu,
        iif(the_ncpu_in_use BETWEEN 1 AND the_ncpu + 1, the_ncpu_in_use, NULL)
          AS ncpu_in_use,
        count(*) OVER win AS cnt_cpu,
        the_ncpu AS ncpu,
        count(*) FILTER (WHERE the_ncpu_in_use BETWEEN 1 AND the_ncpu + 1)
          OVER win_super AS node_tot,
        nnodes AS alloc_nnodes
  FROM inmem.timeseries AS ts, watcher, (
          SELECT recordid AS the_recordid, ncpu AS the_ncpu,
                ceil(1.0 *
                  (delta_user_time + delta_sys_time)
                    / (scrape_interval * 1e6)) AS the_ncpu_in_use
          FROM inmem.timeseries
        )
  WHERE watcher.id == watcherid AND the_recordid == recordid
        AND ts.is_new_in_period
  WINDOW win AS
          (PARTITION BY ts.jobid, ts.stepid, target_node, the_ncpu_in_use),
        win_super AS
          (PARTITION BY ts.jobid, ts.stepid, target_node)
), /*[7]*/ SQLITE_CODEBLOCK(

  CREATE TABLE inmem.resource_usage AS
  SELECT ts.jobid, ts.stepid, name,
        format('[%.2lf%%, %.2lf%%]',
                (1.0 * step_start_offset) / job_length * 100,
                (1.0 * step_end_offset) / job_length * 100) AS timespan,
        nnodes AS nnode, NULL AS ncpu, cpu_usage, ngpu, gpu_usage,
        peak_res_size, sample_cnt,
        mem_limit / iif(peak_res_size == peak_res_size_slurm, 1, nnodes)
          AS mem_limit,
        peak_res_size == peak_res_size_slurm AS is_res_size_from_slurm,
        rtrim(iif(is_jupyter, 'jupyter | ', '')
              || iif(low_concurrency, 'low_concurrency | ', '')
              || iif(peak_res_size > mem_limit, 'oversubscribe | ', '')
              || iif(gpu_flagged, 'gpu_underusage | ', '')
              || iif(cpu_flagged, 'cpu_underusage | ', '')
              , '| ') AS problem
  FROM (SELECT
    jobid, stepid, name, submit_line, job_length, ngpu,
    step_start_offset, step_end_offset, nnodes, mem_limit,
    count(recordid) AS sample_cnt,
    max(peak_res_size, max(res_size)) AS peak_res_size,
    peak_res_size AS peak_res_size_slurm
    FROM inmem.timeseries
    GROUP BY jobid, stepid
    ) AS ts, (
        SELECT jobid, stepid,
         iif(sel_ngpu == 0, '',
             group_concat(iif(gpu_flagged, '** ', '') || node
                          || x'0a' || node_tot || ' sample'
                          || iif(node_tot > 1, 's', '') || x'0a'
                          || gpu_usage, x'0a0a'))
           AS gpu_usage,
         group_concat(iif(cpu_flagged, '** ', '') || node
                          || x'0a' || node_tot || ' sample'
                          || iif(node_tot > 1, 's', '') || x'0a'
                          || cpu_usage || x'0a' || ' (' || sel_ncpu || ' core'
                          || iif(sel_ncpu > 1, 's', '')
                          || ' available)' || x'0a'
                          , x'0a0a')
           AS cpu_usage,
         count(node) AS nnode,
         max(gpu_flagged) AS gpu_flagged,
         max(cpu_flagged) AS cpu_flagged,
         max(low_concurrency) AS low_concurrency
  FROM (
    SELECT
      jobid, stepid, node,
      max(ncpu) AS sel_ncpu, max(ngpu) AS sel_ngpu, node_tot, 
      ' ncpu' || x'0a' || 'inuse percentage' || x'0a' ||  
      group_concat(iif(ncpu_in_use IS NULL, NULL, format('%5d %6.2lf%%',
                          ncpu_in_use, (1.0 * cnt_cpu) / node_tot * 100)),
                  x'0a')
        AS cpu_usage,
      ' ngpu' || x'0a' || 'inuse percentage' || x'0a' ||  
      group_concat(iif(ngpu_in_use IS NULL, NULL, format('%5d %6.2lf%%',
                          ngpu_in_use, (1.0 * cnt_gpu) / node_tot * 100)),
                  x'0a')
        AS gpu_usage,
      ngpu > 0 AND
      (first_value(ngpu_in_use) OVER win == 0
        OR first_value(cnt_gpu) OVER win != max(cnt_gpu)
        OR (alloc_nnodes == 1 AND first_value(ngpu_in_use) OVER win != ngpu))
      AS gpu_flagged,
      max(ncpu) > 1 AND max(ncpu_in_use) <= 1 AND max(ngpu_in_use) IS 0
        AS low_concurrency,
      1.0 * total(ncpu_in_use * cnt_cpu) / node_tot < 0.5 * max(ncpu)
        AS cpu_flagged
    FROM inmem.gpucpu_usage
    GROUP BY jobid, stepid, node
    WINDOW win AS
      (PARTITION BY jobid, stepid, node
      ORDER BY ngpu_in_use DESC NULLS LAST, ncpu_in_use DESC NULLS LAST)
    )
    GROUP BY jobid, stepid
    ORDER BY gpu_flagged DESC, cpu_flagged DESC
  ) AS gpucpuinfo, (
    SELECT jobid, stepid,
          max(application IN ('jupyter-noteboo', 'jupyter-lab')) AS is_jupyter
    FROM application_usage
    GROUP BY jobid, stepid
  ) AS jupyterinfo
  WHERE gpucpuinfo.jobid == ts.jobid AND gpucpuinfo.stepid == ts.stepid
        AND jupyterinfo.jobid == ts.jobid AND jupyterinfo.stepid == ts.stepid;
), /*[8]*/
    "INSERT INTO inmem.resource_usage("
    "jobid, stepid, name, peak_res_size, mem_limit, timespan, nnode,"
    "ncpu, cpu_usage, problem)"
    "SELECT jobid, NULL AS stepid, name, 0, 0,"
    "iif(elapsed > 0, "
    "format('%.2lf%% of timelimit used', 1.0 * elapsed / timelimit * 100)"
    " || x'0a' || 'actual: ' || " SLURM_STYLE_TIME(elapsed) ", 'running')"
    " || x'0a' || 'available: ' || " SLURM_STYLE_TIME(timelimit) "AS timespan,"
    "nnode, ncpu,"
    "format('allocated: %d core%s' || x'0a', ncpu, iif(ncpu > 1, 's', '')) "
    " || iif(elapsed > 0,"
    " format('average: %d cores', ceil(1.0 * actual_cpu / elapsed))"
    " || x'0a' || 'actual: ' || " SLURM_STYLE_TIME(actual_cpu)
    " || x'0a' || 'available: ' || " SLURM_STYLE_TIME(cpu_possible)
    " || x'0a' "
    " || format('percentage: %.2lf%%', 1.0 * actual_cpu / cpu_possible * 100)"
    ", '')"
    "AS cpu_usage,"
  SQLITE_CODEBLOCK(
    rtrim(iif(1.0 * actual_cpu / cpu_possible < 0.5 AND elapsed > 0,
                    'cpu_underusage | ', '')
                /*|| iif(1.0 * elapsed / timelimit < 0.75,
                      'timelimit_underusage | ', '')*/
                , '| ') AS problem
    FROM (
    WITH jobids AS MATERIALIZED
      (SELECT DISTINCT jobid FROM inmem.resource_usage)
    SELECT jobids.jobid, name, nnodes AS nnode, timelimit * 60 AS timelimit,
          ncpu, tot_time / 1e6 AS actual_cpu,
          ended_at - started_at AS elapsed,
          (ended_at - started_at) * ncpu AS cpu_possible
    FROM jobinfo, jobids,
        (SELECT t.jobid, max(tot_time) AS tot_time FROM (
            SELECT t.jobid, sum(t.tot_time) AS tot_time FROM (
              SELECT jobid, max(tot_time) AS tot_time
              FROM measurements
              GROUP BY jobid, stepid
            ) AS t GROUP BY t.jobid
            UNION SELECT DISTINCT jobid, 0 FROM jobids
          ) AS t GROUP BY t.jobid
          ) AS tot_time_info
    WHERE jobinfo.stepid IS NULL
          AND jobinfo.jobid == jobids.jobid
          AND jobinfo.jobid == tot_time_info.jobid
    )
), /*[9]*/SQLITE_CODEBLOCK(
  CREATE TABLE inmem.problem_listing(
    jobid INT,
    stepid INT,
    problem TEXT,
    PRIMARY KEY (jobid, stepid, problem)
  )
), 0};

const char *ANALYZE_INSERT_PROBLEM_LISTING_SQL = SQLITE_CODEBLOCK(
  INSERT OR IGNORE INTO inmem.problem_listing(jobid, stepid, problem)
    VALUES(:jobid, :stepid, :problem);
);

const char *ANALYZE_DUMP_DATA_TO_JSON_SQL = SQLITE_CODEBLOCK(
  SELECT
    trim(
      json_object(:user, json_object(
        'JobInfo', json(jobinfo_data), 'Problems', json(problems_data),
        'MemUsage', json(memusage_data), 'Resources', json(resources_data),
        'GPU', json(gpu_usage_data), 'AppUsage', json(application_usage_data),
        'SysTimeRatio', json(sys_time_ratio_data)
      )),
    '{}') || '}'
    AS data
  FROM (SELECT (
    SELECT json_group_array(json_object(
      'Job', ts.jobid, 'Name', jobinfo.name, 'NodeCnt', ts.nnodes,
      'JobLength', ts.job_length, 'TimeLimit', ts.timelimit
    )) AS jobinfo_data
    FROM (SELECT * FROM inmem.timeseries
            GROUP BY jobid, stepid
            HAVING is_new_in_period
          ) AS ts
          LEFT JOIN jobinfo
            ON (jobinfo.jobid == ts.jobid AND jobinfo.stepid IS NULL)
  ) AS jobinfo_data, (
    SELECT json_group_array(json_object(
      'Job', jobid, 'Step', stepid,
      'PeakRssSize', peak_res_size, 'MemLimit', mem_limit
    )) AS memusage_data FROM inmem.resource_usage
  ) AS memusage_data, (
    SELECT json_group_array(json_object(
      'Job', jobid, 'Step', stepid, 'App', application
    )) FROM application_usage
        JOIN (
          SELECT jobid, stepid FROM inmem.timeseries
          GROUP BY jobid, stepid
          HAVING is_new_in_period
        ) USING (jobid, stepid)
  ) AS application_usage_data, (
    SELECT json_group_array(json(data)) AS problems_data FROM (
    SELECT json_patch(
      json_object('Job', jobid, 'Step', stepid),
      json_group_object(problem, 1))
    AS data
    FROM inmem.problem_listing
    GROUP BY jobid, stepid)
  ) AS problems_data, (
    SELECT json_group_array(json_object(
      'Job', jobid, 'Step', stepid, 'Node', node,
      'GPUCPUEntryRatio', 1.0 * ifnull(cnt_cpu, cnt_gpu) / node_tot,
      'TotGPUCPUSamples', node_tot,
      'GPUCPUEntryType', iif(ncpu IS NULL, 'GPU', 'CPU'),
      'AllocCPU', ncpu, 'AllocGPU', ngpu,
      'CPUInUse', ncpu_in_use, 'GPUInUse', ngpu_in_use
    )) AS resources_data
      FROM inmem.gpucpu_usage
      WHERE ngpu IS NOT 0 AND ifnull(ncpu_in_use, ngpu_in_use) IS NOT NULL
  ) AS resources_data, (
    SELECT json_group_array(json_object(
      'Job', jobid, 'Step', stepid, 'Node', target_node, 'GPUID', gpuid_raw,
      'AvgGPUUtil', avg_util,
      'TotGPUSamples', measurement_cnt, 'CntZeroGPUUtil', zero_util_cnt,
      'CntLowGPUUtil', low_util_cnt,
      'LongestNoGPUUtil', longest_continuous_zero_util,
      'AvgGPUClockMHz', avg_clock, 'AvgGPUPowerUsageWatt', avg_power_usage,
      'AvgGPUTempC', avg_temperature
    )) AS gpu_usage_data FROM inmem.gpu_usage_base
    WHERE is_new_in_period
  ) AS gpu_usage_data, (
    SELECT json_group_array(json_object(
      'Job', jobid, 'Step', stepid, 'TotSysTimeRatioSamples', tot_in_batch,
      '10%~33%', sys_ratio_1_10, '33%~66%', sys_ratio_1_3,
      '66%~100%', sys_ratio_2_3, '>100%', sys_ratio_gt_1
    )) AS sys_time_ratio_data FROM inmem.sys_ratio
    WHERE is_new_in_period
  ) AS sys_time_ratio_data);
);

#define _FILTER_LATEST_RECORD_SQL "is_new_in_period"

#define _SUMMARIZE_GPU_PROBLEM_SQL SQLITE_CODEBLOCK(                           \
  iif(zero_util_cnt == measurement_cnt, 'completely_no_util',                  \
    rtrim(                                                                     \
      iif(longest_continuous_zero_util >= measurement_cnt * 0.25,              \
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
      max(jobid) AS jobid, stepid, name,
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
  = "SELECT *, 'sys_ratio' AS problem_tag FROM (" _ANALYZE_SYS_TIME_RATIO_SQL(
    "AND " _PROBLEMATIC_SYS_RATIO_CONDITION
    " AND " _FILTER_LATEST_RECORD_SQL
  ) ")";

const char *ANALYSIS_LIST_LATEST_SYS_RATIO_SQL
  = _ANALYZE_SYS_TIME_RATIO_SQL("AND " _FILTER_LATEST_RECORD_SQL);

#undef _ANALYZE_SYS_TIME_RATIO_SQL

const char *ANALYZE_SYS_RATIO_HISTORY_SQL
  = SQLITE_CODEBLOCK(
    WITH grouped_sys_ratio AS (
    SELECT
      max(jobid) AS jobid, stepid, name,
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

const char *ANALYZE_RESOURCE_USAGE_SQL = SQLITE_CODEBLOCK(
  SELECT agg_jobid AS jobid, stepid,
         name || x'0a'
         || iif(nnode IS NULL, '',
                '(' || nnode || ' node' || iif(nnode > 1, 's', '') || ')')
           AS name,
         format('%.2lf%% (%d / %d MB)',
                1.0 * agg_peak_res_size / agg_mem_limit /
                  iif(peak_res_size IS peak_res_size_slurm
                      OR stepid IS NOT NULL, 1, nnode) * 100,
                agg_peak_res_size / 1024 / 1024,
                agg_mem_limit / 1024 / 1024
                  / iif(peak_res_size IS peak_res_size_slurm
                        OR stepid IS NOT NULL, 1, nnode))
          || x'0a' || 'source: '
          || iif(agg_peak_res_size IS peak_res_size_slurm, 'SLURM', 'samples')
           AS mem_usage, timespan, cpu_usage, ngpu, gpu_usage,
         rtrim(iif(INSTR(problem, 'cpu_underusage') IS 0 AND low_compute_power,
                   'low_compute_power | ', '')
               || problem
               || iif(agg_peak_res_size < 0.75 * agg_mem_limit,
                      '| mem_underusage', ''), '| ') AS problem_tag
  FROM (
    SELECT *, iif(stepid IS NULL, jobid, NULL) AS agg_jobid,
          ifnull(sample_cnt, total(sample_cnt) OVER win) AS agg_sample_cnt,
          iif(stepid IS NULL, max(peak_res_size) OVER win, peak_res_size)
            AS agg_peak_res_size,
          iif(stepid IS NULL, max(mem_limit) OVER win, mem_limit)
            AS agg_mem_limit,
          iif(stepid IS NULL,
              max(peak_res_size)
                FILTER (WHERE is_res_size_from_slurm IS 1) OVER win,
              is_res_size_from_slurm * peak_res_size)
              AS peak_res_size_slurm,
          stepid IS NULL AND ncpu <= 8 AND max(ngpu) OVER win IS 0
            AS low_compute_power
    FROM inmem.resource_usage AS usage
    WINDOW win AS (PARTITION BY usage.jobid)
    ORDER BY usage.jobid, stepid NULLS FIRST
  );
);

const char *POST_ANALYZE_SQL = SQLITE_CODEBLOCK(
  ROLLBACK;
  DETACH DATABASE inmem;
);
