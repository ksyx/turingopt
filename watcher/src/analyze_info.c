#include "analyze_info.h"

#define job_info_fields                                                        \
  {                                                                            \
    .sql_column_name = "jobid",                                                \
    .printed_name = "Job ID",                                                  \
    .help = "For results aggregated across history runs, the Job ID and Step"  \
            " fields shows a representative job step that has the same job"    \
            " name and submit line, which could be fetched by running sacct"   \
            " -j <JobID>.<Step> -o Name,SubmitLine",                           \
    .type = ANALYZE_RESULT_INT,                                                \
    .flags = ANALYZE_FIELD_NO_FLAG                                             \
  }, {                                                                         \
    .sql_column_name = "stepid",                                               \
    .printed_name = "Step",                                                    \
    .help = NULL,                                                              \
    .type = ANALYZE_RESULT_INT,                                                \
    .flags = ANALYZE_FIELD_STEP_ID                                             \
  }, {                                                                         \
    .sql_column_name = "name",                                                 \
    .printed_name = "Name",                                                    \
    .help = "This field is formed by joining job name and step name with"      \ 
            " slash",                                                          \
    .type = ANALYZE_RESULT_STR,                                                \
    .flags = ANALYZE_FIELD_NO_FLAG                                             \
  }

#define problem_field \
  {                                                                            \
    .sql_column_name = "problem_tag",                                          \
    .printed_name = "Problems Found",                                          \
    .help = NULL,                                                              \
    .type = ANALYZE_RESULT_STR,                                                \
    .flags = ANALYZE_FIELD_PROBLEMS                                            \
  }

#define tail_field { .sql_column_name = NULL }

#define ANALYSIS(NAME) struct analysis_info_t NAME

#define STRINGIFY_BLOCK(...) #__VA_ARGS__

const char *analyze_letter_stylesheet = STRINGIFY_BLOCK(
  <style>
  table, th, td {
    border: 1px solid black;
    border-collapse: collapse;
  }
  td {
    padding-left: 0.5rem;
    padding-right: 0.5rem;
  }
  td > ul, td > ol {
    margin-top: 0;
    margin-bottom: 0;
  }
  th > a, td > a, h1 > a, h2 > a, h3 > a, h4 > a, h5 > a, h6 > a, li > a {
    text-decoration: none;
    color: inherit;
  }
  span[class="x_field_help"] {
    text-decoration: underline;
    text-decoration-style: dotted;
  }
  </style>
);

#undef STRINGIFY_BLOCK

const struct analyze_result_field_t sys_ratio_fields[] = {
  job_info_fields, {
    .sql_column_name = "tot_in_batch",
    .printed_name = NULL,
    .help = NULL,
    .type = ANALYZE_RESULT_INT,
    .flags = ANALYZE_FIELD_TOTAL,
  }, {
    .sql_column_name = "sys_ratio_1_10",
    .printed_name = "(10%, 33%]",
    .help = NULL,
    .type = ANALYZE_RESULT_INT,
    .flags = ANALYZE_FIELD_SHOW_PERCENTAGE,
  }, {
    .sql_column_name = "sys_ratio_1_3",
    .printed_name = "(33%, 66%]",
    .help = NULL,
    .type = ANALYZE_RESULT_INT,
    .flags = ANALYZE_FIELD_SHOW_PERCENTAGE,
  }, {
    .sql_column_name = "sys_ratio_2_3",
    .printed_name = "(66, 100%]",
    .help = NULL,
    .type = ANALYZE_RESULT_INT,
    .flags = ANALYZE_FIELD_SHOW_PERCENTAGE,
  }, {
    .sql_column_name = "sys_ratio_gt_1",
    .printed_name = "> 100%",
    .help = "The value shown in this field is the sum of all ratios greater"
            " than 100%.",
    .type = ANALYZE_RESULT_FLOAT,
    .flags = ANALYZE_FIELD_SHOW_PERCENTAGE,
  }, {
    .sql_column_name = "major_ratio_unified_tot",
    .printed_name = "Unified Ratio",
    .help = "The value shown in this field shows the <b>underestimated</b>"
            " equivalent of the system-time to user-time ratio based on"
            " significant ratios shown in previous columns. A unified ratio"
            " that equals to the number of measurements as shown in denominator"
            " equivalents to a 1/3 system-time to user-time ratio and scales"
            " linearly.",
    .type = ANALYZE_RESULT_FLOAT,
    .flags = ANALYZE_FIELD_SHOW_PERCENTAGE,
  },
  problem_field,
  tail_field
};

ANALYSIS(sys_ratio_analysis) = {
  .name = "System time ratio",
  .fields = sys_ratio_fields,
  .analysis_description
    = "This analysis identifies job submissions that are likely to be less"
      " computationally effective but may not have been captured by other"
      " analysis, possibly due to problems that are not previously identified"
      " and therefore have no specific rules set up, or those that could not be"
      " determined with available metrics."
      #if WE_HAVE_SOMEONE_TO_DO_PROFILING_WORK
      " In this case, it is suggested to submit your entire submission for"
      " profiling, which is of low effort on your side and could potentially"
      " speed up your work."
      #endif
  ,
  .headers_description
    = "The columns are the number of sampled time slice having ratios in the"
      " shown range when the time spent on system requests (system time) is"
      " divided by the time spent on user computations (user time). Since the"
      " work counted toward system time is usually waiting for certain resource"
      " to be ready or certain device operation to be done, it is less helpful"
      " for progressing the actual computation and should be reduced to improve"
      " the overall efficiency. These ratios shows how significant the system"
      " time is when compared to user time."
};

const struct analyze_result_field_t gpu_usage_fields[] = {
  job_info_fields,
  {
    .sql_column_name = "measurement_cnt",
    .printed_name = NULL,
    .help = NULL,
    .type = ANALYZE_RESULT_INT,
    .flags = ANALYZE_FIELD_TOTAL,
  }, {
    .sql_column_name = "avg_util",
    .printed_name = "Average Util / %",
    .help = "Consider lowering GPU constraint or using partial GPU in case of"
            " low average utilization for easier allocation and saving energy,"
            " or specifying better GPU in case of high average utilization to"
            " get job done faster.",
    .type = ANALYZE_RESULT_FLOAT,
    .flags = ANALYZE_FIELD_NO_FLAG
  }, {
    .sql_column_name = "zero_util_cnt",
    .printed_name = "No Util",
    .help = NULL,
    .type = ANALYZE_RESULT_INT,
    .flags = ANALYZE_FIELD_SHOW_PERCENTAGE,
  }, {
    .sql_column_name = "low_util_cnt",
    .printed_name = "Low Util",
    .help = "Low is currently defined as those in range (0, 12.5%].",
    .type = ANALYZE_RESULT_INT,
    .flags = ANALYZE_FIELD_SHOW_PERCENTAGE,
  }, {
    .sql_column_name = "longest_continuous_zero_util",
    .printed_name = "Longest Countiunous No Util",
    .help = "High percentage indicates there could be part of your submission"
            " that is not using GPU resources and splitting them out to run"
            " under pure CPU submission, which could allow wasted cycles to be"
            " used on other jobs and make power better utilized, while also"
            " helps the queue to progress faster and shorten the turnaround"
            " time waiting for a GPU. Remember your task could be the one"
            " waiting for GPU next time! So let's be involved to make queue"
            " move faster.",
    .type = ANALYZE_RESULT_INT,
    .flags = ANALYZE_FIELD_SHOW_PERCENTAGE,
  }, {
    .sql_column_name = "avg_clock",
    .printed_name = "Average SM Clock (MHz)",
    .help = "Average streaming multiprocessor clock provided for reference.",
    .type = ANALYZE_RESULT_FLOAT,
    .flags = ANALYZE_FIELD_NO_FLAG,
  }, {
    .sql_column_name = "avg_power_usage",
    .printed_name = "Average Power Usage (Watt)",
    .help = "Average power usage provided for understanding environmental"
            " impact. As a reference, GPUs typically use 10 Watts when in"
            " idle. This does not take external power consumption into"
            " calculation, like those for cooling.",
    .type = ANALYZE_RESULT_FLOAT,
    .flags = ANALYZE_FIELD_NO_FLAG,
  },
  problem_field,
  tail_field
};

ANALYSIS(gpu_usage_analysis) = {
  .name = "GPU Usage",
  .fields = gpu_usage_fields,
  .analysis_description
    = "This analysis helps making submissions' GPU usage condition more"
      " observable and makes suggestions on requesting GPU resource so to"
      " shorten allocation turnaround time, make allocations better utilized,"
      " and save energy.",
  .headers_description
    = "The percentages shows the number of sampled time slices satisfying the"
      " condition for respective columns."
};

#undef job_info_fields
#undef problem_field
#undef tail_field
#undef ANALYSIS

struct analysis_info_t * const analysis_list[] = {
  &gpu_usage_analysis,
  &sys_ratio_analysis,
  NULL
};

void fill_analysis_list_sql() {
  static uint8_t initialized = 0;
  if (initialized) {
    return;
  }
  initialized = 1;

  #define FILL(NAME, PROBLEM_SQL, LATEST_SQL, HISTORY_SQL) \
    NAME.latest_analysis_sql = LATEST_SQL; \
    NAME.latest_problem_sql = PROBLEM_SQL; \
    NAME.history_analysis_sql = HISTORY_SQL;

  FILL(gpu_usage_analysis,
       NULL,
       ANALYZE_LATEST_GPU_USAGE_SQL,
       ANALYZE_GPU_USAGE_HISTORY_SQL);
  FILL(sys_ratio_analysis,
       ANALYSIS_LIST_PROBLEMATIC_LATEST_SYS_RATIO_SQL,
       ANALYSIS_LIST_LATEST_SYS_RATIO_SQL,
       ANALYZE_SYS_RATIO_HISTORY_SQL);
  #undef FILL
}
