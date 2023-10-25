#include "analyze_info.h"

#define job_info_fields                                                        \
  {                                                                            \
    .sql_column_name = "jobid",                                                \
    .printed_name = "Job ID",                                                  \
    .help = "For results aggregated across history runs, the Job ID and Step"  \
            " fields shows a representative job step that has the same job"    \
            " name and submit line, which could be fetched by running"         \
            " <code>sacct -j <JobID>.<Step> -o Name,SubmitLine</code>",                           \
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
            " slash.",                                                         \
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
#define tail_problem { .sql_name = NULL }

#define ANALYSIS(NAME) struct analysis_info_t NAME

#define STRINGIFY_BLOCK(...) #__VA_ARGS__

const char *summary_letter_usage[] = {
  "For best experience, use browser-based email rendering engine. Certain"
  " email clients like Outlook Desktop have the functionality of"
  " <a href=\"https://support.microsoft.com/en-us/office/view-an-email-in-your"
  "-browser-87aa5c86-be18-4f70-b408-92c814bd96ec\" style=\"color: revert;\">"
  "opening the email in browser</a>.",
  "This letter is designed for you to have a better observability of the jobs"
  " and identify problems related to resource usage, including underusage or"
  " misusage, so that you could <b>use less</b> to have the same effect or spot"
  " possible <b>bottlenecks</b> of the submissions. This not only helps you to"
  " better utilize the resources and potentially speed up your jobs, but also"
  " contributes to a collective effort of taking up what really need and make"
  " queues move faster. All these could contribute to a greener computing.",
  "Internal links are heavily used in this letter and it is <b>strongly</b>"
  " advised to use them to avoid scrolling overwhelmingly. <b>All</b> text in"
  " table of contents and headers are clickable and would respectively bring"
  " you to corresponding section or back to table of contents. Besides, all of"
  " the <b>identified problems</b> and many of the <b>column headers</b> are"
  " also clickable, which could bring you to a detailed explanation of these"
  " names. <b>Please submit a feedback if anything you don't understand is not"
  " explained well, including the usage of this letter.</b>",
  "The highlighted and bolded sections in table of contents are suggested to"
  " read first for getting the most out of this letter. Following many users'"
  " convention, the measurements are also aggregated by job name across the"
  " entire measurement dataset collected so far for a comprehensive overview"
  " of job characteristic.Besides identifying problems, the raw data"
  " contributing to identifying the problems are also included for your"
  " reference.",
  NULL
};

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
  code {
    white-space: pre;
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

const struct analyze_problem_t analyze_sys_ratio_problems[] = {
  {
    .sql_name = "sys_ratio",
    .printed_name = "System Time Ratio",
    .cause = "The process is usually waiting for certain resource to be ready"
              " or certain device operation to be done while being counted"
              " toward system time. High ratio may indicate a bottleneck of the"
              " submitted job.",
    .impact = "Given the nature of system time, it is less helpful for"
              " progressing the actual computation and should be reduced to"
              " improve the overall computing efficiency.",
    .solution = "Since system time is highly coupled with the architecture"
                " behind, it is suggested to submit your job for profiling."
                " This should be low effort and would be beneficial to cluster"
                " users including you.",
    .solution_type = ANALYZE_SOLUTION_TYPE_NEED_PROFILING
  }, tail_problem
};

ANALYSIS(sys_ratio_analysis) = {
  .name = "System time ratio",
  .fields = sys_ratio_fields,
  .problems = analyze_sys_ratio_problems,
  .analysis_description
    = "This analysis identifies job submissions that are likely to be less"
      " computationally effective but may not have been captured by other"
      " analysis, possibly due to problems that are not previously identified"
      " and therefore have no specific rules set up, or those that could not be"
      " determined with available metrics.",
  .headers_description
    = "The columns are the number of sampled time slice having ratios in the"
      " shown range when the time spent on system requests (system time) is"
      " divided by the time spent on user computations (user time). These"
      " ratios shows how significant the system time is when compared to user"
      " time."
};

const struct analyze_result_field_t gpu_usage_fields[] = {
  job_info_fields, {
    .sql_column_name = "gpuid",
    .printed_name = "GPU",
    .help = "Index of GPU on machine.",
    .type = ANALYZE_RESULT_STR,
    .flags = ANALYZE_FIELD_NOT_IN_ACROSS_HISTORY
  }, {
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
    .help = "Low is currently defined as utilization values in the range of"
            " (0, 12.5%].",
    .type = ANALYZE_RESULT_INT,
    .flags = ANALYZE_FIELD_SHOW_PERCENTAGE,
  }, {
    .sql_column_name = "longest_continuous_zero_util",
    .printed_name = "Longest Contiunous No Util",
    .help = "Number of continuous measurements showing zero utilization of the"
            " GPU streaming multiprocessor.",
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

const struct analyze_problem_t analyze_gpu_problems[] = {
  {
    .sql_name = "completely_no_util",
    .printed_name = "Completely No Util",
    .cause = "The submission is never observed to be utilizing GPU resource",
    .impact = "This causes unnecessary energy consumption by waking GPU from"
              " idle mode and possibly heavy computing with low amount of CPU"
              " that would take even longer to complete than in a pure CPU"
              " submission. It would also block other jobs from utilizing the"
              " resources for equally long time and stress the queue.",
    .solution = "Check for documentation to ensure the computation is using"
                " GPU.",
    .solution_type = ANALYZE_SOLUTION_TYPE_CODE_CHANGE_OR_ALLOCATION_PARAM
  }, {
    .sql_name = "try_split",
    .printed_name = "Try Splitting",
    .cause = "This submission is having a high percentage of longest zero"
            " utilization, indicating that there possibly exists segment of"
            " code that is running for long time while not utilizing any GPU"
            " resources.",
    .impact = "Allocations without GPU is generally faster to be allocated so"
              " that the preparation work for computing with GPU could be"
              " performed during peak time of GPU usage while making SLURM"
              " aware of the GPU usage happening later."
              " This could also allow wasted GPU cycles to be used on"
              " other jobs and utilize energy better, while also helps the"
              " queue to progress faster and shorten the turnaround time"
              " waiting for a GPU. Remember your task could be the one waiting"
              " for GPU next time so let's be involved in this optimization!",
    .solution = "Identify code segments running long while utilizing no GPU and"
                " split execution into tasks requesting GPU and no GPU. The"
                " task depedency could be set while submitting jobs with"
                " argument <code>--dependency=afterok:jobid</code>. Use"
                " accurate time limit for smooth transition from one job to"
                " another.",
    .solution_type = ANALYZE_SOLUTION_TYPE_SUGGEST_CONSULTATION
  }, {
    .sql_name = "investigate_usage",
    .printed_name = "Investigate GPU Usage",
    .cause = "This submission is observed to be utilizing GPU but in a low"
             " utilization for long period. This possibly indicates"
             " inefficacies in GPU usage like bottlenecks in the pipeline"
             " moving data to GPU, or the computation is short enough that GPUs"
             " of lower specification or parallized pure CPU computation would"
             " fulfill the need while being easier to be allocated and uses"
             " less energy.",
    .impact = "Checking for bottlenecks could help the submission to complete"
              " in a faster and more efficient manner. Choosing appropriate"
              " resource combination reliefs unnecessary constraints, so that"
              " the allocation could be assigned quicker, while avoid blocking"
              " jobs with real demands of high specification hardware.",
    .solution = "Compare time consumption on each part of computation, like"
                " that of loading data from disk and preprocessing, moving data"
                " to GPU memory, and computing with GPU. Check for bottlenecks"
                " in the pipeline. Try GPU of lower specifiction if GPU type is"
                " specified in the allocation request.",
    .solution_type = ANALYZE_SOLUTION_TYPE_SUGGEST_CONSULTATION
  }, tail_problem
};

ANALYSIS(gpu_usage_analysis) = {
  .name = "GPU Usage",
  .fields = gpu_usage_fields,
  .problems = analyze_gpu_problems,
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
