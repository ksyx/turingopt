#include "analyzer.h"

static sqlite3_stmt *renew_analyzer_stmt;
static sqlite3_stmt *list_active_user_stmt;
static std::vector<sqlite3_stmt *> create_base_table_stmt;
static int offset_start, offset_end;

// For compatibility with Microsoft Word, use anchor
#define ANCHORED_TAG(TAG, ANCHOR, TEXT) \
  "<" #TAG ">" "<a name=\"" ANCHOR "\"></a>" TEXT "</" #TAG ">"
#define ANCHOR_LINK(TARGET, TEXT, ...) \
  "<a href=\"#" TARGET "\"" __VA_ARGS__ ">" TEXT "</a>"
#define TOC_LINK(TEXT) ANCHOR_LINK("toc", TEXT)
#define HEADER_TEXT(ANCHOR, TEXT) ANCHORED_TAG(h3, ANCHOR, TOC_LINK(TEXT))
#define SUBHEADER_TEXT(ANCHOR, TEXT) ANCHORED_TAG(h4, ANCHOR, TOC_LINK(TEXT))
#define WRAPTAG(TAG, TEXT, ...) \
  "<" #TAG " " __VA_ARGS__ ">" TEXT "</" #TAG ">"
#define PARAGRAPH(TEXT, ...) WRAPTAG(p, TEXT, __VA_ARGS__)
#define TABLECELL(TEXT, ...) WRAPTAG(td, TEXT, __VA_ARGS__)
#define LISTITEM(TEXT, ...) WRAPTAG(li, TEXT, __VA_ARGS__)
#define CENTER(TEXT, ...) WRAPTAG(center, TEXT, __VA_ARGS__)
#define BOLD(TEXT, ...) WRAPTAG(b, TEXT, __VA_ARGS__)
#define COLSPAN(X) "colspan=\"" #X "\""

const auto mkdir_mode = S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH;
static std::map<std::string, const analyze_problem_t *> problem_info;

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

static inline
void run_analysis_stmt(
  sqlite3_stmt *stmt, analysis_info_t *info, const char *title,
  std::string &tldr, bool &toc_added, bool new_toc_row, bool highlight,
  FILE *fp, FILE *header_fp) {
  if (!stmt) {
    return;
  }
  bool first_run = 0;
  auto title_machine_name = get_machine_name(title);
  const char *title_machine_name_str = title_machine_name.c_str();
  std::string info_machine_name = get_machine_name(info->name);
  const char *info_machine_name_str = info_machine_name.c_str();
  int sqlite_ret;
  bool has_total = 0;
  std::map<const analyze_problem_t *, int> problem_cnt;
  DEBUGOUT_VERBOSE(fprintf(stderr, "%s\n", sqlite3_expanded_sql(stmt)));
  int tot = 0;
  bool is_history_analysis = info->history_analysis_stmt == stmt;
  while ((sqlite_ret = sqlite3_step(stmt)) == SQLITE_ROW) {
    tot++;
    if (!first_run) {
      first_run = 1;
      if (!toc_added) {
        toc_added = 1;
        fprintf(header_fp,
                "%s<td>" ANCHOR_LINK("%s", BOLD("%s")) "<ul>"
                LISTITEM(ANCHOR_LINK("%s_metrics", "Metrics"))
                LISTITEM(ANCHOR_LINK("%s_problems",
                                     "Possible problems in the category")),
                new_toc_row ? "<tr>" : "",
                info_machine_name_str, info->name,
                info_machine_name_str, info_machine_name_str);
        fprintf(fp, HEADER_TEXT("%s", "%s") "\n" PARAGRAPH("%s"),
                    info_machine_name_str, info->name,
                    info->analysis_description);
        fprintf(fp, SUBHEADER_TEXT("%s_metrics", "Metrics") "\n",
                info_machine_name_str);
        if (info->headers_description) {
          fprintf(fp, PARAGRAPH("%s")"\n", info->headers_description);
        }
        fputs("<table>", fp);
        for (auto cur_metric = info->fields;
             cur_metric->sql_column_name;
             cur_metric++) {
          if (cur_metric->flags & ANALYZE_FIELD_PROBLEMS
              && strcmp(
                  sqlite3_column_name(stmt, sqlite3_column_count(stmt) - 1),
                  cur_metric->sql_column_name)) {
            continue;
          }
          if (cur_metric->printed_name && cur_metric->help) {
            fprintf(fp, "<tr>"
                    WRAPTAG(th, CENTER("%s"))
                    TABLECELL(ANCHORED_TAG(p, "%s_%s", "%s")) "</tr>\n",
                    cur_metric->printed_name,
                    info_machine_name_str,
                    cur_metric->sql_column_name,
                    cur_metric->help);
          }
        }
        fprintf(fp,
                "</table>"
                SUBHEADER_TEXT(
                  "%s_problems", "Possible problems in the category")
                "<table>", info_machine_name_str);

        for (auto cur_problem = info->problems;
              cur_problem->sql_name;
              cur_problem++) {
          fprintf(fp, "<tr %s>" ANCHORED_TAG(th %s, "%s_%s", "%s"),
                      row_group_top_style,
                      cur_problem->oneliner ? "" : "rowspan=\"3\"",
                      info_machine_name_str,
                      cur_problem->sql_name,
                      cur_problem->printed_name,
                      "");
          if (!cur_problem->oneliner) {
            fprintf(fp, WRAPTAG(th, "Cause") TABLECELL("%s") "</tr>\n"
                        "<tr>" WRAPTAG(th, "Impact") TABLECELL("%s") "</tr>\n"
                        "<tr>" WRAPTAG(th, "Solution") "<td>" PARAGRAPH("%s"),
                    cur_problem->cause,
                    cur_problem->impact,
                    cur_problem->solution);
            {
              const char *solution_str = NULL;
              switch (cur_problem->solution_type) {
                case ANALYZE_SOLUTION_TYPE_CODE_CHANGE_OR_ALLOCATION_PARAM:
                  solution_str = "checking your code and allocation request"
                                " parameters.";
                  break;
                case ANALYZE_SOLUTION_TYPE_NEED_PROFILING:
                  if (profiling_support_instructions) {
                    solution_str = profiling_support_instructions;
                    break;
                  } else {
                    solution_str = "profiling your code. Request a consultation"
                                  " session for more information.";
                  }
                  break;
                case ANALYZE_SOLUTION_TYPE_SUGGEST_CONSULTATION:
                  solution_str = "requesting a consultation session if needed.";
                  break;
              }
              if (solution_str) {
                fprintf(fp, PARAGRAPH("This problem could be solved by %s"),
                        solution_str);
              } else {
                fputs("warning: unknown solution type.\n", stderr);
              }
              fputs("</td></tr>\n", fp);
            }
          } else {
            fprintf(fp, TABLECELL(PARAGRAPH("%s"), COLSPAN(2)) "</tr>",
                        cur_problem->oneliner);
          }
        }
        fputs("</table>", fp);
      }
      fprintf(header_fp,
              LISTITEM(ANCHOR_LINK("%s_%s", "%s", "%s")),
              info_machine_name_str, title_machine_name_str,
              highlight ? "style=\"color: revert; font-weight: bold\"" : "",
              title);
      fprintf(fp, SUBHEADER_TEXT("%s_%s", "%s"),
                  info_machine_name_str, title_machine_name_str, title);
      fputs("<table><tr>", fp);
      for (auto cur_metric = info->fields;
            cur_metric->sql_column_name;
            cur_metric++) {
        if (cur_metric->flags & ANALYZE_FIELD_PROBLEMS
            && strcmp(
                sqlite3_column_name(stmt, sqlite3_column_count(stmt) - 1),
                cur_metric->sql_column_name)) {
          continue;
        }
        if (cur_metric->flags & ANALYZE_FIELD_NOT_IN_ACROSS_HISTORY
            && is_history_analysis) {
          continue;
        }
        if (cur_metric->printed_name) {
          if (cur_metric->help) {
            fprintf(fp, WRAPTAG(th, ANCHOR_LINK("%s_%s", "%s")) "\n",
                        info_machine_name_str, cur_metric->sql_column_name,
                        cur_metric->printed_name);
          } else {
            fprintf(fp, WRAPTAG(th, "%s") "\n", cur_metric->printed_name);
          }
        }
        if (cur_metric->flags & ANALYZE_FIELD_TOTAL) {
          has_total = 1;
        }
      }
      fputs("</tr>", fp);
    }
    fprintf(fp, "<tr %s>", row_group_top_style);
    auto cur = info->fields;
    int tot = 0;
    std::queue<double> percentages;
    std::queue<int> colspans;
    const double not_an_number = std::nan("0");
    int cnt_percentage = 0;
    bool met_stepid = 0;
    SQLITE3_FETCH_COLUMNS_START(NULL)
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
      bool is_null_data = SQLITE3_IS_NULL();
      if (!cur->printed_name) {
        goto finalize_table_row_loop;
      }
      if (is_percentage) {
        cnt_percentage++;
      } else if (cnt_percentage) {
        colspans.push(cnt_percentage);
        cnt_percentage = 0;
      }
      fprintf(fp, "<td%s>",
                  has_total && !is_percentage ? " rowspan=\"3\"" : "");
      if (cur->flags & ANALYZE_FIELD_STEP_ID) {
        met_stepid = 1;
        if (!is_null_data) {
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
        }
      } else if (cur->flags & ANALYZE_FIELD_SHOW_PERCENTAGE
                 && cur->type != ANALYZE_RESULT_STR) {
        double val = SQLITE3_FETCH(double);
        fprintf(fp,
                cur->type == ANALYZE_RESULT_INT
                  ? CENTER("%'.0lf") : CENTER("%'.2lf"),
                val);
        if (tot) {
          percentages.push(val / tot * 100);
        } else {
          percentages.push(not_an_number);
          fprintf(stderr,
                  "%s/%s: no total value available or divison by zero\n",
                  title_machine_name_str, cur->sql_column_name);
        }
      } else if (cur->flags & ANALYZE_FIELD_PROBLEMS) {
        auto str = (const char *)SQLITE3_FETCH_STR();
        std::string cur_problem = "";
        bool first = 1;
        while (str) {
            const char c = *str;
            if (isalnum(c) || c == '_') {
              cur_problem.append(1, c);
            } else if (c == '|' || c == '\0') {
              if (cur_problem.length()) {
                if (problem_info.count(cur_problem)) {
                  if (first) {
                    fputs("<ul style=\"padding-left: 0.5rem\">", fp);
                    first = 0;
                  }
                  auto &info = problem_info[cur_problem];
                  problem_cnt[info]++;
                  fprintf(fp, LISTITEM(ANCHOR_LINK("%s_%s", "%s")),
                          info_machine_name_str, info->sql_name,
                          info->printed_name);
                } else {
                  fprintf(stderr, "warning: unknown problem %s\n",
                                  cur_problem.c_str());
                }
              cur_problem = "";
            }
            if (!c) {
              break;
            }
          }
          str++;
        }
        if (!first) {
          fputs("</ul>", fp);
        }
      } else if (!is_null_data) {
        switch (cur->type) {
          case ANALYZE_RESULT_INT:
          {
            int val = SQLITE3_FETCH(int);
            if (met_stepid) {
              fprintf(fp, CENTER("%'d"), val);
            } else {
              fprintf(fp, ANCHOR_LINK("%s_%s", CENTER("%d")),
                          info_machine_name_str, title_machine_name_str, val);
            }
            break;
          }
          case ANALYZE_RESULT_FLOAT:
            fprintf(fp, CENTER("%'.2lf"), SQLITE3_FETCH(double));
            break;
          case ANALYZE_RESULT_STR:
            {
              const char *str = (const char *)SQLITE3_FETCH_STR();
              bool multiline = str ? strchr(str, '\n') : 0;
              fprintf(fp, "%s" WRAPTAG(pre, WRAPTAG(code, "%s")) "%s",
                          multiline ? "" : "<center>",
                          str,
                          multiline ? "" : "</center>");
            }
            break;
          default:
            fprintf(stderr, "%s/%s: unknown data type\n",
                    title_machine_name_str, cur->sql_column_name);
        }
      }
      fputs("</td>", fp);
      finalize_table_row_loop:
      do {
        cur++;
      } while (cur->flags & ANALYZE_FIELD_NOT_IN_ACROSS_HISTORY
               && is_history_analysis);
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
        fprintf(fp, "<td " COLSPAN(%d) ">" CENTER("out of %'d records"),
                    c, tot);
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
  bool has_named_problem = problem_cnt.size();
  if (has_named_problem || (stmt == info->latest_problem_stmt && tot)) {
    auto title_str = std::string(title);
    {
    char &title_lead = title_str[0];
    if (title_lead >= 'A' && title_lead <= 'Z') {
      title_lead = title_lead - 'A' + 'a';
    }
    }
    std::stringstream out;
    out << "<li>" << (has_named_problem ? "Within" : "Found") << " " << tot
        << " " << (has_named_problem ? "record" : "")
        << (has_named_problem && tot > 1 ? "s" : "")
        << (has_named_problem ? " in " : "")
        << "<a href=\"#" << info_machine_name_str << "_" << title_machine_name
        << "\" " << (highlight ? "style=\"color: revert\"" : "") << ">"
        << "<b>" << title_str << "</b></a>"
        << (has_named_problem ? "<ul>" : "</li>\n");
    for (const auto &[problem, cnt] : problem_cnt) {
      out << "<li>" << cnt << " entr" << (cnt == 1 ? "y has" : "ies have")
          << " problem <a href=\"#"
          << info_machine_name_str << "_" << problem->sql_name << "\">"
          << "<b>" << problem->printed_name << "</b></a>\n";
    }
    if (has_named_problem) {
      out << "</ul></li>";
    }
    tldr += out.str();
  }
  if (first_run) {
    fputs("</table>\n", fp);
    if (!verify_sqlite_ret(sqlite_ret, 
      (std::string("(") + title_machine_name + std::string(")")).c_str())) {
      fputs("Internal error while producing analysis result.", fp);
    }
  }
}

static inline
bool do_analyze(
  analysis_info_t *info, std::string &tldr, FILE *fp, FILE *header_fp) {
  bool toc_added = 0;
  static bool newline = 0;
  const auto tldr_len_old = tldr.length();
  std::stringstream out;
  out << "<li>For analysis <a href=\"#" << get_machine_name(info->name)
      << "\"><b>" << info->name << "</b></a><ul>";
  tldr += out.str();
  const auto tldr_len = tldr.length();
  #define ANALYZE(STMT, TITLE, HIGHLIGHT) \
    run_analysis_stmt( \
      STMT, info, TITLE, tldr, toc_added, !newline, HIGHLIGHT, fp, header_fp)
  ANALYZE(info->latest_problem_stmt, "Latest concerning submissions", 1);
  ANALYZE(info->latest_analysis_stmt,
          "All latest submissions",
          !info->latest_problem_stmt);
  ANALYZE(info->history_analysis_stmt, "Across submission history", 0);
  #undef ANALYZE
  if (toc_added) {
    fputs("</ul></td>", header_fp);
    if (newline) {
      fputs("</tr>\n", header_fp);
    }
    newline = !newline;
  }
  if (tldr_len != tldr.length()) {
    tldr += std::string("</ul></li>\n");
  } else {
    tldr.resize(tldr_len_old);
  }
  return toc_added;
}

void do_analyze() {
  std::string path = "analysis_result";
  auto makedir = [&path](bool allow_existing) {
    if (mkdir(path.c_str(), mkdir_mode)
        && (!allow_existing || errno != EEXIST)) {
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
      cleanup_all_stmts();
      if (!sqlite3_exec_wrap(POST_ANALYZE_SQL, "(post_analyze)")) {
        exit(1);
      }
    };
    #if ENABLE_DEBUGOUT
    {
    const char *col = sqlite3_column_name(list_active_user_stmt, 0);
    if (!strcmp(col, "user")) {
      fprintf(stderr, OPACTIVEUSER ": expecting column 'user', got '%s'", col);
    }
    }
    #endif
    auto user = user_str.c_str();
    if (!sqlite3_exec_wrap(PRE_ANALYZE_SQL, "(prepare_analyze)")) {
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
      if (i <= 1 || i == 5) {
        SQLITE3_BIND_START
        if (i <= 1) {
          NAMED_BIND_TEXT(stmt, ":user", user);
        } else if (i == 5) {
          BIND_OFFSET(stmt);
        }
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
    {
    static bool first = 1;
    while (auto &info = *cur) {
      const auto set_problem_info
        = [&](const analyze_problem_t *info) {
          while (info->sql_name) {
            problem_info[std::string(info->sql_name)] = info;
            info++;
          }
        };
      #define SETUP(NAME, BIND) \
        if (info->NAME##_sql) { \
          if (first) { \
            set_problem_info(info->problems); \
          } \
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
    first = 0;
    }
    bool has_analysis = 0;
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
    bool has_usage = summary_letter_usage[0];
    std::string tldr = "";
    fprintf(fp, "<head>%s</head>", analyze_letter_stylesheet);
    fprintf(fp,
            "%s\n" HEADER_TEXT("toc", "Table of Contents") "\n<table>\n"
            WRAPTAG(tr,
              TABLECELL(ANCHOR_LINK("tldr", CENTER(BOLD("TL; DR"))),
                        COLSPAN(2)))
            WRAPTAG(tr,
            "%s"
            TABLECELL(ANCHOR_LINK("news", CENTER(BOLD("News"))), "%s"))
            "\n",
            analyze_letter_header,
            has_usage ?
              TABLECELL(
                ANCHOR_LINK("usage", CENTER(BOLD("Usage Instructions")))
              ) : "",
            has_usage ? "" : COLSPAN(2));
    auto header_fp = fp;
    std::string analysis_id =
      std::to_string(offset_start) + std::string(":") + std::string(user);
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
        has_analysis |= do_analyze(info, tldr, fp, header_fp);
        cur++;
      }
    }
    fprintf(fp, ANCHORED_TAG(p, "footer", "%s")
                WRAPTAG(sub,
                        WRAPTAG(code, "Analysis ID: %s")
                        "<br>"
                        ANCHOR_LINK("toc", "Top")),
                analyze_letter_footer, analysis_id.c_str());
    fclose(fp);
    finalize_loop:
    fp = header_fp;
    fputs(WRAPTAG(tr,
            TABLECELL(
              ANCHOR_LINK("footer", CENTER(BOLD("Ending"))), COLSPAN(2))
          )
          "</table>\n",
          fp);

    fputs(HEADER_TEXT("tldr", "TL; DR"), fp);
    if (tldr.length()) {
      fprintf(fp,
              WRAPTAG(table,
                TABLECELL(
                  PARAGRAPH("All " BOLD("Bold")
                            " texts in this section are clickable!")
                  WRAPTAG(ul, "%s")
                  PARAGRAPH("Check out the instructions below for the best way"
                            " of reading this summary letter."))),
              tldr.c_str());
    } else {
      fputs(PARAGRAPH(
                "Nice! No problem identified and please check out the data"
                " to have a better comprehension of your job characteristics."
                " Feel free to send us any problem identified by yourself and"
                " it would be truly helpful for all cluster users."),
              fp);
    }

    if (has_usage) {
      fputs(HEADER_TEXT("usage", "Usage Instructions"), fp);
      bool is_list = summary_letter_usage[1];
      fprintf(fp, "<table><td>%s", is_list ? "<ul>" : "");
      for (auto cur = summary_letter_usage; *cur; cur++) {
        fprintf(fp, LISTITEM(PARAGRAPH("%s"))"\n", *cur);
      }
      if (analyze_letter_feedback_link) {
        std::string analysis_id_param = "";
        if (analyze_letter_feedback_link_analysis_id_var) {
          analysis_id_param
            = std::string(strchr(analyze_letter_feedback_link, '?') ? "&" : "?")
              + std::string(analyze_letter_feedback_link_analysis_id_var)
              + std::string("=")
              + analysis_id;
        }
        fprintf(fp,
                LISTITEM(PARAGRAPH(
                  "Make sure to complete <a href=\"%s%s\"> the feedback"
                  " form</a> for this summary letter to be continuously"
                  " improved and bring you more valuable information!")),
                analyze_letter_feedback_link,
                analysis_id_param.c_str());
      }
      fprintf(fp, "%s</td></table>", is_list ? "</ul>" : "");
    }
    fclose(fp);
    if (!has_analysis) {
      fclose(fopen((mail_path + std::string(".empty")).c_str(), "w"));
    }
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
#undef BOLD
#undef SUBHEADER_TEXT
#undef COLSPAN
