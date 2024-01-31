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
  " you to corresponding section or back to table of contents. When being lost"
  " in result rows, click on the values of <b>Job ID</b> and you would be sent"
  " back to section header. Besides, all of"
  " the <b>identified problems</b> and many of the <b>column headers</b> are"
  " also clickable, which could bring you to a detailed explanation of these"
  " names. <b>Please submit a feedback if anything you don't understand is not"
  " explained well, including the usage of this letter.</b>",
  "The highlighted and bolded sections in table of contents are suggested to"
  " read first for getting the most out of this letter. Following many users'"
  " convention, the measurements are also aggregated by job name across the"
  " entire measurement dataset collected so far for a comprehensive overview"
  " of job characteristic. Besides identifying problems, the raw data"
  " contributing to identifying the problems are also included for your"
  " reference.",
  NULL
};

const char *row_group_top_style = "style=\"border-top-style: ridge\"";

const char *analyze_letter_stylesheet = STRINGIFY_BLOCK(
  <style>
  table, th, td {
    border-collapse: collapse;
  }
  th, td {
    border: 1px solid black;
  }
  table {
    border: 1.5px solid black;
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
  </style>
);

#undef STRINGIFY_BLOCK

const struct analyze_result_field_t resource_usage_fields[] = {
  job_info_fields,  {
    .sql_column_name = "mem_usage",
    .printed_name = "Memory Usage",
    .help = NULL,
    .type = ANALYZE_RESULT_STR,
    .flags = ANALYZE_FIELD_NO_FLAG,
  }, {
    .sql_column_name = "timespan",
    .printed_name = "Timespan",
    .help = "The timing of result row relative to parent constraint. It"
            " compares allocated time and actual consumption for jobs, and"
            " shows the timing of steps relative to the time window of their"
            " parent jobs. For example, a result of [25%, 75%] for a step shows"
            " that the step started after a quarter of the job's actual time"
            " consumption has elapsed, while ends after running for as long as"
            " half of the job's actual time consumption.",
    .type = ANALYZE_RESULT_STR,
  }, {
    .sql_column_name = "cpu_usage",
    .printed_name = "CPU Util",
    .help = "The amount of CPU cores used is an average calculated from "
            " aggregated CPU time and actual time consumption and does not show"
            " peak usage.",
    .type = ANALYZE_RESULT_STR,
  }, {
    .sql_column_name = "ngpu",
    .printed_name = "GPU Count",
    .help = NULL,
    .type = ANALYZE_RESULT_INT,
    .flags = ANALYZE_FIELD_NO_FLAG,
  }, {
    .sql_column_name = "gpu_usage",
    .printed_name = "GPU Util",
    .help = "The GPU usage information is derived from sampled data and may not"
            " represent the full picture.",
    .type = ANALYZE_RESULT_STR,
  },
  problem_field,
  tail_field
};

const struct analyze_problem_t analyze_resource_usage_problems[] = {
  {
    .sql_name = "jupyter",
    .printed_name = "Jupyter Detected",
    .cause = "Your job submission involves starting your own Jupyter Notebook"
             " or Jupyter Hub on compute nodes.",
    .impact = "The interactive nature of Jupyter applications create"
              " <b>excessive</b> amount of idle time that could otherwise be"
              " allocated to other jobs and achieve higher utilization.",
    .solution = "Please refer to NEWS section and use the shared Jupyter Hub"
                " instance that has more fancy features than Jupyter Notebook,"
                " separates web server from compute jobs, and creates job on "
                " demand to ensure higher resource utilization.",
    .solution_type = ANALYZE_SOLUTION_TYPE_OTHER,
    .severity = ANALYZE_SEVERITY_MEDIUM,
  }, {
    .sql_name = "low_compute_power",
    .printed_name = "Low Compute Power",
    .cause = "The job submission requested no GPU and only a few CPU cores.",
    .impact = "While it is possible that only large amount of available memory"
              " is desired, i.e. your computation is memory-bounded, this"
              " combination of request parameter could make job to run in a"
              " performance that is <b>slower than on your laptop</b>.",
    .solution = "Confirm your need. Try requesting more CPU cores and setting"
                " higher concurrency parameter in your code with consulting"
                " library documentations to see if there is improvement. Ignore"
                " this message if the computation is memory-bounded and large"
                " amount of available memory is the only resource in need.",
    .solution_type = ANALYZE_SOLUTION_TYPE_CODE_CHANGE_OR_ALLOCATION_PARAM,
    .severity = ANALYZE_SEVERITY_SERIOUS,
  }, {
    .sql_name = "low_concurrency",
    .printed_name = "Low Concurrency",
    .cause = "Samples shows that no GPU and at most single CPU core is used.",
    .impact = "This combination of request parameter could make job to run in a"
              " performance that is <b>slower than on your laptop</b>.",
    .solution = "Set higher concurrency parameter or connect to GPU in your"
                " code with consulting library documentations to check for"
                " improvement.",
    .solution_type = ANALYZE_SOLUTION_TYPE_CODE_CHANGE_OR_ALLOCATION_PARAM,
    .severity = ANALYZE_SEVERITY_SERIOUS,
  }, {
    .sql_name = "oversubscribe",
    .printed_name = "Memory Oversubscribe",
    .cause = "The job submission used more memory than allocated.",
    .impact = "This puts the job at <b>risk of out of memory kills</b> that may"
              " lose results that are not saved to disk, through which wasted"
              " wasted computations are also created.",
    .solution = "Specify larger amount of memory in allocation request.",
    .solution_type = ANALYZE_SOLUTION_TYPE_CODE_CHANGE_OR_ALLOCATION_PARAM,
    .severity = ANALYZE_SEVERITY_MEDIUM,
  }, {
    .sql_name = "gpu_underusage",
    .printed_name = "GPU Underusage",
    .cause = "Resources allocated is not fully used.",
    .impact = "Generally, this <b>increases the difficulty</b> for the request"
              " to be satisfied, while also keeps unused resources unavailable"
              " to other jobs for the length of job and therefore lengthen the"
              " queue. Remember sometimes your other jobs could also be in the"
              " queue waiting for resources! <b>The combined result of zero GPU"
              " utilization and low amount of average CPU cores would bring the"
              " submission to be running at extremely poor performance.</b>",
    .solution = "Adjust allocation request with referring to the usage info"
                " provided.",
    .solution_type = ANALYZE_SOLUTION_TYPE_CODE_CHANGE_OR_ALLOCATION_PARAM,
    .severity = ANALYZE_SEVERITY_SERIOUS,
  }, {
    .sql_name = "cpu_underusage",
    .printed_name = "CPU Underusage",
    .oneliner = "Refer to GPU Underusage section above.",
    .solution_type = ANALYZE_SOLUTION_TYPE_CODE_CHANGE_OR_ALLOCATION_PARAM,
    .severity = ANALYZE_SEVERITY_SERIOUS,
  }, {
    .sql_name = "mem_underusage",
    .printed_name = "Memory Underusage",
    .oneliner = "Refer to GPU Underusage section above.",
    .solution_type = ANALYZE_SOLUTION_TYPE_CODE_CHANGE_OR_ALLOCATION_PARAM,
    .severity = ANALYZE_SEVERITY_MEDIUM,
  }, {
    .sql_name = "timelimit_underusage",
    .printed_name = "Time Limit Underusage",
    .oneliner = "Refer to GPU Underusage section above.",
    .solution_type = ANALYZE_SOLUTION_TYPE_CODE_CHANGE_OR_ALLOCATION_PARAM,
    .severity = ANALYZE_SEVERITY_INFO,
  },
  tail_problem
};

ANALYSIS(resource_usage_analysis) = {
  .name = "Resource Usage",
  .fields = resource_usage_fields,
  .problems = analyze_resource_usage_problems,
  .analysis_description
    = "This analysis identifies resource allocation misuses for you to set"
      " allocation request parameters that better fits the actual need and"
      " benefit the submission by having requests allocated faster or lowering"
      " the risk of having the jobs killed by using more resources than"
      " allocated.",
  .headers_description = NULL
};

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
    .impact = "Given the nature of system time, it is <b>less helpful for"
              " progressing the actual computation</b> and should be reduced to"
              " improve the overall computing efficiency.",
    .solution = "Since system time is highly coupled with the architecture"
                " behind, it is suggested to <b>submit your job for profiling"
                "</b>. This should be <b>low effort</b> and would be beneficial"
                " to both you and other cluster users.",
    .solution_type = ANALYZE_SOLUTION_TYPE_NEED_PROFILING,
    .severity = ANALYZE_SEVERITY_INFO,
  }, tail_problem
};

ANALYSIS(sys_ratio_analysis) = {
  .name = "System time ratio",
  .fields = sys_ratio_fields,
  .problems = analyze_sys_ratio_problems,
  .analysis_description
    = "This analysis identifies job submissions that are likely to be <b>less"
      " computationally effective</b> but may not have been captured by other"
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
    .help = "Consider lowering GPU constraint or using lower GPU specification"
            " when in low utilization for easier allocation and energy saving,"
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
    .printed_name = "Average Power Usage (Watts)",
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
    .cause = "The submission is <b>never</b> observed to be utilizing GPU.",
    .impact = "This causes unnecessary energy consumption by waking GPU from"
              " idle mode. In addition, it possibly performed heavy computing"
              " with less CPU, which would take <b>even longer</b> to complete"
              " than a pure CPU submission. It would also block other jobs from"
              " using the device for equally long time and stress the queue.",
    .solution = "Check for code and documentation to ensure the computation is"
                " using GPU.",
    .solution_type = ANALYZE_SOLUTION_TYPE_CODE_CHANGE_OR_ALLOCATION_PARAM,
    .severity = ANALYZE_SEVERITY_SERIOUS,
  }, {
    .sql_name = "try_split",
    .printed_name = "Try Splitting",
    .cause = "This submission is having a high percentage of longest zero"
            " utilization, indicating that there possibly exists segment of"
            " code that is <b>running for long time while not utilizing any GPU"
            " resource</b>.",
    .impact = "Allocations without GPU is generally faster to be allocated so"
              " that the preparation work for computing with GPU could be"
              " performed while waiting for allocation during peak time of GPU"
              " usage. This could also allow wasted GPU cycles to be used on"
              " other jobs and <b>utilize energy better</b>, while also helps"
              " the queue to progress faster and shorten the turnaround time"
              " waiting for a GPU. Remember your task could be the one waiting"
              " for GPU next time so let's be involved in this optimization!",
    .solution = "Identify code segments running long while utilizing no GPU and"
                " split execution into tasks requesting GPU and no GPU. The"
                " task depedency could be set while submitting jobs with"
                " argument <code>--dependency=afterok:(jobid)</code>. Use"
                " accurate time limit for smooth transition from one job to"
                " another.",
    .solution_type = ANALYZE_SOLUTION_TYPE_SUGGEST_CONSULTATION,
    .severity = ANALYZE_SEVERITY_INFO,
  }, {
    .sql_name = "investigate_usage",
    .printed_name = "Investigate GPU Usage",
    .cause = "This submission is observed to be utilizing GPU but in a <b>low"
             " utilization for long period</b>. This possibly indicates"
             " <b>inefficacies in GPU</b> usage, like bottlenecks in the"
             " pipeline moving data to GPU, or the computation is light enough"
             " that GPUs of lower specification or parallized pure CPU"
             " computation would fulfill the need while being easier to be "
             " allocated and uses less energy.",
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
    .solution_type = ANALYZE_SOLUTION_TYPE_SUGGEST_CONSULTATION,
    .severity = ANALYZE_SEVERITY_MEDIUM,
  }, tail_problem
};

ANALYSIS(gpu_usage_analysis) = {
  .name = "GPU Usage",
  .fields = gpu_usage_fields,
  .problems = analyze_gpu_problems,
  .analysis_description
    = "This analysis helps making submissions' <b>GPU usage condition more"
      " observable</b> and makes suggestions on requesting GPU resource so to"
      " shorten allocation turnaround time, make allocations better utilized,"
      " and save energy.",
  .headers_description = NULL
};

#undef job_info_fields
#undef problem_field
#undef tail_field
#undef ANALYSIS

struct analysis_info_t * const analysis_list[] = {
  &resource_usage_analysis,
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
  FILL(resource_usage_analysis, NULL, ANALYZE_RESOURCE_USAGE_SQL, NULL)
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
