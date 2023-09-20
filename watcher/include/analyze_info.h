#ifndef _TURINGWATCHER_ANALYZE_INFO_H
#define _TURINGWATCHER_ANALYZE_INFO_H

#include <stddef.h>
#include <stdint.h>
#include <sql.h>

#define WE_HAVE_SOMEONE_TO_DO_PROFILING_WORK 1

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
};

typedef struct analyze_result_field_t {
  const char *sql_column_name;
  // NULL: do not print
  const char *printed_name;
  // NULL: no help info available
  const char *help;
  enum analyze_result_data_type_t type;
  // Currently there is nothing OR'd together but maybe in the future
  enum analyze_field_flag_t flags;
};

typedef struct analyze_problem_t {
  const char *name;
  const char *cause;
  const char *solution;
};

typedef struct sqlite3_stmt;

typedef struct analysis_info_t {
  const char *name;
  const struct analyze_result_field_t *fields;
  const char *analysis_description;
  const char *headers_description;
  #define PAIR(NAME) \
    const char *NAME##_sql; \
    struct sqlite3_stmt *NAME##_stmt;
  PAIR(latest_analysis);
  PAIR(latest_problem);
  PAIR(history_analysis);
  #undef PAIR
};

extern const char *analyze_letter_stylesheet;

extern struct analysis_info_t * const analysis_list[];
extern const char *analyze_news;
extern const char *analyze_letter_domain;
extern const char *analyze_mail_cc[];
extern const char *analyze_letter_subject;
extern const char *analyze_letter_header;
extern const char *analyze_letter_footer;

#ifdef __cplusplus
extern "C"
#endif
void fill_analysis_list_sql();

#endif
