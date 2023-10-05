#ifndef _TURINGWATCHER_ANALYZE_INFO_H
#define _TURINGWATCHER_ANALYZE_INFO_H

#include <stddef.h>
#include <stdint.h>
#include <sql.h>

enum analyze_result_data_type_t {
  ANALYZE_RESULT_INT,
  ANALYZE_RESULT_FLOAT,
  ANALYZE_RESULT_STR,
};

enum analyze_field_flag_t {
  ANALYZE_FIELD_NO_FLAG = 0x0,
  ANALYZE_FIELD_TOTAL = 0x1,
  ANALYZE_FIELD_PROBLEMS = 0x2,
  ANALYZE_FIELD_SHOW_PERCENTAGE = 0x4,
  ANALYZE_FIELD_STEP_ID = 0x8,
  ANALYZE_FIELD_NOT_IN_ACROSS_HISTORY = 0x10,
};

enum analyze_problem_solution_t {
  ANALYZE_SOLUTION_TYPE_CODE_CHANGE_OR_ALLOCATION_PARAM,
  ANALYZE_SOLUTION_TYPE_NEED_PROFILING,
  ANALYZE_SOLUTION_TYPE_SUGGEST_CONSULTATION,
};

typedef struct analyze_problem_t {
  const char *sql_name;
  const char *printed_name;
  const char *cause;
  const char *impact;
  const char *solution;
  enum analyze_problem_solution_t solution_type;
};

typedef struct analyze_result_field_t {
  const char *sql_column_name;
  // NULL: do not print
  const char *printed_name;
  // NULL: no help info available
  const char *help;
  const enum analyze_result_data_type_t type;
  // Currently there is nothing OR'd together but maybe in the future
  const enum analyze_field_flag_t flags;
};

typedef struct sqlite3_stmt;

typedef struct analysis_info_t {
  const char *name;
  const struct analyze_result_field_t *fields;
  const char *analysis_description;
  const char *headers_description;
  const struct analyze_problem_t *problems;
  #define PAIR(NAME) \
    const char *NAME##_sql; \
    struct sqlite3_stmt *NAME##_stmt;
  PAIR(latest_analysis);
  PAIR(latest_problem);
  PAIR(history_analysis);
  #undef PAIR
};

extern const char *analyze_letter_stylesheet;
extern const char *profiling_support_instructions;
extern const char *summary_letter_usage[];

extern struct analysis_info_t * const analysis_list[];
extern const char *analyze_news;
extern const char *analyze_letter_domain;
extern const char *analyze_mail_cc[];
extern const char *analyze_letter_subject;
extern const char *analyze_letter_header;
extern const char *analyze_letter_footer;
extern const char *analyze_letter_feedback_link_analysis_id_var;
extern const char *analyze_letter_feedback_link;

#ifdef __cplusplus
extern "C"
#endif
void fill_analysis_list_sql();

#endif
