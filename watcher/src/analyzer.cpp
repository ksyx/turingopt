#include "analyzer.h"

static sqlite3_stmt *renew_analyzer_stmt;
static sqlite3_stmt *list_active_user_stmt;
static std::vector<sqlite3_stmt *> create_base_table_stmt;
static int offset_start, offset_end;

#define ANCHORED_TAG(TAG, ANCHOR, TEXT) \
  "<" #TAG " name=\"" ANCHOR "\">" TEXT "</" #TAG ">"
#define ANCHOR_LINK(TARGET, TEXT) "<a href=\"#" TARGET "\">" TEXT "</a>"
#define TOC_LINK(TEXT) ANCHOR_LINK("toc", TEXT)
#define HEADER_TEXT(ANCHOR, TEXT) ANCHORED_TAG(h3, ANCHOR, TOC_LINK(TEXT))
#define SUBHEADER_TEXT(ANCHOR, TEXT) ANCHORED_TAG(h4, ANCHOR, TOC_LINK(TEXT))
#define PARAGRAPH(TEXT) "<p>" TEXT "</p>"
#define TABLECELL(TEXT) "<td>" TEXT "</td>"
#define LISTITEM(TEXT) "<li>" TEXT "</li>"
#define CENTER(TEXT) "<center>" TEXT "</center>"

const auto mkdir_mode = S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH;

static inline void reset_analyze_stmts(bool finalize) {
  #define OP "(reset_analyze_stmts)"
  std::vector<sqlite3_stmt *> stmt_to_finalize;
  for (auto &stmt : create_base_table_stmt) {
    if (finalize) {
      stmt_to_finalize.push_back(stmt);
      stmt = NULL;
    } else {
      reset_stmt(stmt, OP);
    }
  }
  auto cur = analysis_list;
  while (const auto &info = *cur) {
    #define CLEANUP(NAME) \
      if (info->NAME##_stmt) { \
        if (finalize) { \
          stmt_to_finalize.push_back(info->NAME##_stmt); \
          info->NAME##_stmt = NULL; \
        } else { \
          reset_stmt(info->NAME##_stmt, OP); \
        } \
      }
    CLEANUP(latest_analysis);
    CLEANUP(latest_problem);
    CLEANUP(history_analysis);
    #undef CLEANUP
    cur++;
  }
  stmt_to_finalize.push_back(FINALIZE_END_ADDR);
  finalize_stmt_array(stmt_to_finalize.data());
  #undef OP
}

void analyzer_finalize() {
  sqlite3_stmt *stmt_to_finalize[] = {
    renew_analyzer_stmt,
    list_active_user_stmt,
    FINALIZE_END_ADDR
  };
  reset_analyze_stmts(true);
  finalize_stmt_array(stmt_to_finalize);
}

#define BIND_OFFSET(STMT) \
  NAMED_BIND_INT(STMT, ":offset_start", offset_start); \
  NAMED_BIND_INT(STMT, ":offset_end", offset_end);

static inline std::string get_machine_name(const char *in) {
  std::string str = std::string(in);
  for (auto &c : str) {
    if (!(c >= 'a' && c <= 'z')) {
      if (c >= 'A' && c <= 'Z') {
        c = c - 'A' + 'a';
      } else {
        c = '_';
      }
    }
  }
  return str;
}

static inline bool verify_sqlite_ret(int ret, const char *op) {
  if (ret != SQLITE_DONE) {
    fprintf(stderr, "sqlite3_step%s: %s\n", op, sqlite3_errstr(ret));
    return 0;
  }
  return 1;
}

static inline
void run_analysis_stmt(
  sqlite3_stmt *stmt, analysis_info_t *info, const char *title,
  bool &toc_added, FILE *fp, FILE *header_fp) {
  if (!stmt) {
    return;
  }
  bool first_run = 0;
  auto title_machine_name = get_machine_name(title);
  const char *title_machine_name_str = title_machine_name.c_str();
  if (!reset_stmt(stmt, title_machine_name_str)) {
    return;
  }
  int sqlite_ret;
  bool has_total = 0;
  while ((sqlite_ret = sqlite3_step(stmt)) == SQLITE_ROW) {
    if (!first_run) {
      first_run = 1;
      if (!toc_added) {
        toc_added = 1;
        std::string info_machine_name = get_machine_name(info->name);
        const char *info_machine_name_str = info_machine_name.c_str();
        fprintf(header_fp,
                "<td>" ANCHOR_LINK("%s", "%s") "<ul>"
                LISTITEM(ANCHOR_LINK("metrics", "Metrics")),
                info_machine_name_str, info->name);
        fprintf(fp, HEADER_TEXT("%s", "%s"), info_machine_name_str, info->name);
        fprintf(fp,
                SUBHEADER_TEXT("metrics", "Metrics") "\n" PARAGRAPH("%s") "\n"
                "<table>",
                info->headers_description);
        auto cur_metric = info->fields;
        while (cur_metric->sql_column_name) {
          if (cur_metric->printed_name && cur_metric->help) {
            fprintf(fp, "<tr>"
                    TABLECELL(CENTER("%s"))
                    TABLECELL(ANCHORED_TAG(p, "%s", "%s")) "</tr>\n",
                    cur_metric->printed_name,
                    cur_metric->sql_column_name,
                    cur_metric->help);
          }
          if (cur_metric->flags & ANALYZE_FIELD_TOTAL) {
            has_total = 1;
          }
          cur_metric++;
        }
        fputs("</table>", fp);
      }
      fprintf(header_fp,
              LISTITEM(ANCHOR_LINK("%s", "%s")),
              title_machine_name_str, title);
      fprintf(fp, SUBHEADER_TEXT("%s", "%s") PARAGRAPH("%s"),
                  title_machine_name_str, title, info->analysis_description);
      fputs("<table><tr>", fp);
      auto cur_metric = info->fields;
      while (cur_metric->sql_column_name) {
        if (cur_metric->printed_name) {
          fprintf(fp, "<th>" ANCHOR_LINK("%s", "%s") "</th>\n",
                      cur_metric->sql_column_name, cur_metric->printed_name);
        }
        cur_metric++;
      }
      fputs("</tr>", fp);
    }
    fputs("<tr>", fp);
    auto cur = info->fields;
    int tot = 0;
    std::queue<double> percentages;
    std::queue<int> colspans;
    const double not_an_number = std::nan("0");
    int cnt_percentage = 0;
    SQLITE3_FETCH_COLUMNS_START()
    SQLITE3_FETCH_COLUMNS_LOOP_HEADER(i, stmt)
      if (!cur->sql_column_name) {
        break;
      }
      if (strcmp(GET_COLUMN_NAME(), cur->sql_column_name)) {
        continue;
      }
      if (cur->flags & ANALYZE_FIELD_TOTAL) {
        tot = SQLITE3_FETCH(int);
      }
      bool is_percentage = cur->flags & ANALYZE_FIELD_SHOW_PERCENTAGE;
      if (!cur->printed_name) {
        goto finalize;
      }
      if (is_percentage) {
        cnt_percentage++;
      } else if (cnt_percentage) {
        colspans.push(cnt_percentage);
        cnt_percentage = 0;
      }
      fprintf(fp, "<td%s><span>", has_total && !is_percentage
                  ? " rowspan=\"3\"" : "");
      if (cur->flags & ANALYZE_FIELD_STEP_ID) {
        int id = SQLITE3_FETCH(int);
        #include "def/slurm_stepid.inc"
        auto cur_id = slurm_stepid_mapping;
        while (cur_id->name && cur_id->stepid != id) {
          cur_id++;
        }
        if (cur_id->name) {
          fprintf(fp, "%s", cur_id->name);
        } else {
          fprintf(fp, "%d", id);
        }
      } else if (cur->flags & ANALYZE_FIELD_SHOW_PERCENTAGE
                 && cur->type != ANALYZE_RESULT_STR) {
        double val = SQLITE3_FETCH(double);
        fprintf(fp,
                cur->type == ANALYZE_RESULT_INT
                  ? CENTER("%.0lf") : CENTER("%.2lf"),
                val);
        if (tot) {
          percentages.push(val / tot * 100);
        } else {
          percentages.push(not_an_number);
          fprintf(stderr,
                  "%s/%s: no total value available or divison by zero\n",
                  title_machine_name_str, cur->sql_column_name);
        }
      } else {
        switch (cur->type) {
          case ANALYZE_RESULT_INT:
            fprintf(fp, CENTER("%d"), SQLITE3_FETCH(int));
            break;
          case ANALYZE_RESULT_FLOAT:
            fprintf(fp, CENTER("%.2lf"), SQLITE3_FETCH(double));
            break;
          case ANALYZE_RESULT_STR:
            fprintf(fp, "<code>%s</code>", SQLITE3_FETCH_STR());
            break;
          default:
            fprintf(stderr, "%s/%s: unknown data type\n",
                    title_machine_name_str, cur->sql_column_name);
        }
      }
      fputs("</span></td>", fp);
      finalize:
      cur++;
    SQLITE3_FETCH_COLUMNS_END
    if (cur->sql_column_name && !(cur->flags & ANALYZE_FIELD_PROBLEMS)) {
      fprintf(stderr, "warning: mismatching fields -- looking for %s\n",
                      cur->sql_column_name);
    }
    fputs("</tr>\n", fp);
    if (cnt_percentage) {
      colspans.push(cnt_percentage);
    }
    if (colspans.size()) {
      fputs("<tr>", fp);
      while (!colspans.empty()) {
        int c = colspans.front();
        fprintf(fp, "<td colspan=\"%d\"> " CENTER("out of %d results"), c, tot);
        colspans.pop();
      }
      fputs("</tr>\n<tr>", fp);
      while (!percentages.empty()) {
        double p = percentages.front();
        if (p == not_an_number) {
          fputs(TABLECELL(CENTER("--")), fp);
        } else {
          fprintf(fp, TABLECELL(CENTER("%.2lf\%")), p);
        }
        percentages.pop();
      }
      fputs("</tr>\n", fp);
    }
  }
  if (first_run) {
    fputs("</table>", fp);
    if (!verify_sqlite_ret(sqlite_ret, 
      (std::string("(") + title_machine_name + std::string("(")).c_str())) {
      fputs("Internal error while producing analysis result.", fp);
    }
  }
}

static inline
void do_analyze(analysis_info_t *info, FILE *fp, FILE *header_fp) {
  bool toc_added = 0;
  #define ANALYZE(STMT, TITLE) \
    run_analysis_stmt(STMT, info, TITLE, toc_added, fp, header_fp)
  ANALYZE(info->latest_problem_stmt, "Latest concerning submissions");
  ANALYZE(info->latest_analysis_stmt, "All latest submissions");
  ANALYZE(info->history_analysis_stmt, "Across submission history");
  #undef ANALYZE
  if (toc_added) {
    fputs("</ul></td>", header_fp);
  }
}

void do_analyze() {
  std::string path = "analysis_result";
  auto makedir = [&path](bool allow_existing) {
    if (mkdir(path.c_str(), mkdir_mode)
        && (allow_existing || errno != EEXIST)) {
      perror("mkdir");
      exit(1);
    }
  };
  makedir(1);
  #define OPACTIVEUSER "(analyze_list_active_user)"
  fill_analysis_list_sql();
  setup_stmt(list_active_user_stmt, ANALYZE_LIST_ACTIVE_USERS,
             OPACTIVEUSER);
  #define OP "(renew_analyzer)"
  setup_stmt(renew_analyzer_stmt, RENEW_ANALYSIS_OFFSET_SQL, OP);
  step_renew(renew_analyzer_stmt, OP, offset_start, offset_end);
  sqlite3_reset(renew_analyzer_stmt);
  path += "/" + std::to_string(offset_start);
  makedir(1);
  SQLITE3_BIND_START
  BIND_OFFSET(list_active_user_stmt);
  if (BIND_FAILED) {
    SQLITE3_PERROR("bind" OPACTIVEUSER);
    exit(1);
  }
  SQLITE3_BIND_END
  int sqlite_ret;
  path += "/";
  std::vector<std::string> users;
  while ((sqlite_ret = sqlite3_step(list_active_user_stmt)) == SQLITE_ROW) {
    users.push_back(
      std::string((const char *)sqlite3_column_text(list_active_user_stmt, 0)));
  }
  if (!verify_sqlite_ret(sqlite_ret, OPACTIVEUSER)) {
    return;
  }
  for (const auto &user_str : users) {
    auto post_analyze = []() {
      {
        sqlite3_stmt *cur = NULL;
        while (cur = sqlite3_next_stmt(sqlite_conn, cur)) {
          sqlite3_reset(cur);
        }
      }
      if (sqlite3_exec_wrap(POST_ANALYZE_SQL, "(post_analyze)")) {
        exit(1);
      }
    };
    #if ENABLE_DEBUGOUT
    {
    const char *col = sqlite3_column_name(list_active_user_stmt, 0);
    if (!strcmp(, "user")) {
      fprintf(stderr, OPACTIVEUSER ": expecting column 'user', got '%s'", col);
    }
    }
    #endif
    auto user = user_str.c_str();
    if (sqlite3_exec_wrap(PRE_ANALYZE_SQL, "(prepare_analyze)")) {
      exit(1);
    }
    #undef OP
    #define OP "(analyze_create_base_table)"
    auto cur_stmt = ANALYZE_CREATE_BASE_TABLES;
    for (size_t i = 0; *cur_stmt; i++, cur_stmt++) {
      if (i <= create_base_table_stmt.size()) {
        create_base_table_stmt.push_back(NULL);
      }
      auto &stmt = create_base_table_stmt[i];
      setup_stmt(stmt, *cur_stmt, OP);
      if (i <= 1) {
        SQLITE3_BIND_START
        NAMED_BIND_TEXT(stmt, ":user", user);
        if (BIND_FAILED) {
          SQLITE3_PERROR("bind" OP);
          post_analyze();
          exit(1);
        }
        SQLITE3_BIND_END
      }
      if (sqlite3_step(stmt) != SQLITE_DONE) {
        SQLITE3_PERROR("step" OP);
        post_analyze();
        exit(1);
      }
    }
    auto cur = analysis_list;
    while (auto &info = *cur) {
      #define SETUP(NAME, BIND) \
        if (info->NAME##_sql) { \
          if (!setup_stmt(info->NAME##_stmt, info->NAME##_sql, #NAME)) { \
            exit(1); \
          } \
          if (BIND) { \
          SQLITE3_BIND_START \
            BIND_OFFSET(info->NAME##_stmt); \
            if (BIND_FAILED) { \
              SQLITE3_PERROR("bind("#NAME")"); \
              exit(1); \
            } \
            SQLITE3_BIND_END \
          } \
        }
      SETUP(latest_analysis, 1);
      SETUP(latest_problem, 1);
      SETUP(history_analysis, 0);
      #undef SETUP
      cur++;
    }
    std::string mail_path = path + std::string(user) + std::string(".mail");
    auto fp = fopen((mail_path + std::string(".header")).c_str(), "w");
    if (!fp) {
      perror("fopen");
      continue;
    }
    fprintf(fp, "To: %s@%s\n", user, analyze_letter_domain);
    {
      const char **cur = analyze_mail_cc;
      while (auto cc = *cur) {
        fprintf(fp, "Cc: %s\n", cc);
        cur++;
      }
    }
    fprintf(fp,
            "Subject: %s\n"
            "Content-Type: text/html; charset=UTF-8\n",
            analyze_letter_subject);
    // Separate message header and mail header
    fputs("\n", fp);
    fprintf(fp, "<head>%s</head>", analyze_letter_stylesheet);
    fprintf(fp, "%s\n" HEADER_TEXT("toc", "Table of Contents") "\n<table>\n"
                "<td>" ANCHOR_LINK("news", "News") "</td>\n",
            analyze_letter_header);
    auto header_fp = fp;
    fp = fopen(mail_path.c_str(), "w");
    if (!fp) {
      perror("fopen");
      fputs("Error while composing the summary letter", header_fp);
      goto finalize_loop;
    }
    fprintf(fp,
            HEADER_TEXT("news", "NEWS") "\n<table><td>%s</td></table>",
            analyze_news);
    {
      auto cur = analysis_list;
      while (auto info = *cur) {
        do_analyze(info, fp, header_fp);
        cur++;
      }
    }
    fputs(analyze_letter_footer, fp);
    fprintf(fp, "<br><code>Analysis batch: %d</code>", offset_start);
    fclose(fp);
    finalize_loop:
    fputs("</table>\n", header_fp);
    fclose(header_fp);
    post_analyze();
  }
  #undef OP
  #undef OPACTIVEUSER
}

#undef PARAGRAPH
#undef TABLECELL
#undef LISTITEM
#undef BIND_OFFSET
#undef HEADER_TEXT
#undef SPAN
#undef SUBHEADER_TEXT
