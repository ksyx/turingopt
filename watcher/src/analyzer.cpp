#include "analyzer.h"

static sqlite3_stmt *list_active_user_stmt;
static std::vector<sqlite3_stmt *> create_base_table_stmt;
static int offset_start, offset_end;

// For compatibility with Microsoft Word, use anchor
#define ANCHORED_TAG(TAG, ANCHOR, TEXT) \
  "<" #TAG ">" "<a name=\"" ANCHOR "\"></a>" TEXT "</" #TAG ">"
#define ANCHOR_LINK(TARGET, TEXT, ...) \
  "<a href=\"#" TARGET "\"" __VA_ARGS__ ">" TEXT "</a>"
#define CSS(TEXT) "style=\"" TEXT "\""
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
  int stepid = -1, jobid = -1;
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
            fputs("</td></tr>\n", fp);
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
              highlight ? CSS("color: revert; font-weight: bold") : "",
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
    analyze_problem_severity_t last_problem_severity;
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
      std::string td_styling = "";
      if (!cur->printed_name) {
        goto finalize_table_row_loop;
      }
      if (is_percentage) {
        cnt_percentage++;
      } else if (cnt_percentage) {
        colspans.push(cnt_percentage);
        cnt_percentage = 0;
      }
      if (has_total && !is_percentage) {
        td_styling += std::string(" rowspan=\"3\"");
      }
      if (cur->flags & ANALYZE_FIELD_PROBLEMS) {
        td_styling += std::string(" " CSS("padding: 0; height: 0;"
                                          "vertical-align: top;"));
      }
      fprintf(fp, "<td%s>", td_styling.c_str());
      if (cur->flags & ANALYZE_FIELD_STEP_ID) {
        met_stepid = 1;
        stepid = -1;
        if (!is_null_data) {
          int id = SQLITE3_FETCH(int);
          stepid = id;
          #include "def/slurm_stepid.inc"
          auto cur_id = slurm_stepid_mapping;
          while (cur_id->name && cur_id->stepid != id) {
            cur_id++;
          }
          fputs("<center>", fp);
          if (cur_id->name) {
            fprintf(fp, "%s", cur_id->name);
          } else {
            fprintf(fp, "%d", id);
          }
          fputs("</center>", fp);
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
        sqlite3_stmt *insert_problem_listing_stmt = NULL;
        bool bind_failed = false;
        if (!is_history_analysis) {
          if (!setup_stmt(
            insert_problem_listing_stmt, ANALYZE_INSERT_PROBLEM_LISTING_SQL,
            "(insert_problem_listing)")) {
            exit(1);
          }
          SQLITE3_BIND_START
          NAMED_BIND_INT(insert_problem_listing_stmt, ":jobid", jobid);
          if (stepid != -1) {
            NAMED_BIND_INT(insert_problem_listing_stmt, ":stepid", stepid);
          } else {
            BIND_NULL(insert_problem_listing_stmt, ":stepid");
          }
          if (BIND_FAILED) {
            bind_failed = true;
          }
          SQLITE3_BIND_END
        } else {
          bind_failed = true;
        }
        bool first = 1;
        while (str) {
            const char c = *str;
            if (isalnum(c) || c == '_') {
              cur_problem.append(1, c);
            } else if (c == '|' || c == '\0') {
              if (cur_problem.length()) {
                if (problem_info.count(cur_problem)) {
                  auto &info = problem_info[cur_problem];
                  if (first) {
                    fprintf(fp, "<div " CSS("height: 100%; display: flex;"
                                " flex-direction: column;") ">"
                                WRAPTAG(div, "",
                                        CSS("width: 100%; flex-grow: 1;"
                                        " background: #%08x")) "\n",
                            ANLAYZE_COMPOSITE_COLOR_BACKGROUND(info->severity));
                    first = 0;
                  }
                  problem_cnt[info]++;
                  fprintf(fp,
                          WRAPTAG(div,
                                  ANCHOR_LINK("%s_%s", "%s",
                                              CSS("color: #%08x;"
                                                  " text-decoration: none;"
                                                  " white-space: nowrap;"
                                                  " font-weight: bold;")),
                                  CSS("background: #%08x; text-align: center;"
                                      " padding: 0.5rem 0.5rem 0.5rem 0.5rem;"))
                          "\n",
                          ANLAYZE_COMPOSITE_COLOR_BACKGROUND(info->severity),
                          info_machine_name_str, info->sql_name,
                          ANLAYZE_COMPOSITE_COLOR_FOREGROUND(info->severity),
                          info->printed_name);
                  last_problem_severity = info->severity;
                  if (!bind_failed) {
                    sqlite3_reset(insert_problem_listing_stmt);
                    int ret;
                    SQLITE3_BIND_START
                      NAMED_BIND_TEXT(insert_problem_listing_stmt,
                                      ":problem", info->printed_name);
                      if (BIND_FAILED) {
                        goto exit_bind_problem;
                      }
                    SQLITE3_BIND_END
                    ret = step_and_verify(insert_problem_listing_stmt, 0,
                                          "(insert_problem_listing)");
                    exit_bind_problem:;
                  }
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
        sqlite3_finalize(insert_problem_listing_stmt);
        if (!first) {
          fprintf(fp, WRAPTAG(div, "",
                              CSS("width: 100%; flex-grow: 1;"
                                  " background: #%08x")
                      ) "</div>\n",
                  ANLAYZE_COMPOSITE_COLOR_BACKGROUND(last_problem_severity));
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
              jobid = val;
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
        << "\" " << (highlight ? CSS("color: revert") : "") << ">"
        << "<b>" << title_str << "</b></a>"
        << (has_named_problem ? "<ul>" : "</li>\n");
    for (const auto &[problem, cnt] : problem_cnt) {
      const char *solution_str = NULL;
      switch (problem->solution_type) {
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
      out << "<li>" << cnt << " entr" << (cnt == 1 ? "y has" : "ies have")
          << " problem <a href=\"#"
          << info_machine_name_str << "_" << problem->sql_name << "\">"
          << "<b " << "style=\"color: #"
          << std::hex << std::setw(8) << std::setfill('0')
          << ANLAYZE_COMPOSITE_COLOR_FOREGROUND(problem->severity)
          << std::dec << std::setw(0) << std::setfill(' ')
          << "\">" << problem->printed_name << "</b></a>\n";
      if (solution_str) {
        out << " and could be solved by " << solution_str;
      } else if (problem->solution_type != ANALYZE_SOLUTION_TYPE_OTHER) {
        fputs("warning: unknown solution type.\n", stderr);
      }
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
  sqlite3_stmt *renew_analyzer_stmt = NULL;
  static time_t next_period_update = 0;
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
  sqlite3_begin_transaction();
  setup_stmt(renew_analyzer_stmt, RENEW_ANALYSIS_OFFSET_SQL, OP);
  step_renew(renew_analyzer_stmt, OP, offset_start, offset_end);
  sqlite3_finalize(renew_analyzer_stmt);

  bool expired = next_period_update && time(NULL) > next_period_update;
  bool update_period = !next_period_update || expired;

  if (expired) {
    sqlite3_end_transaction();
  } else {
    sqlite3_exec(SQL_CONN_NAME, "ROLLBACK;", NULL, NULL, NULL);
  }

  if (update_period) {
    next_period_update = time(NULL) + ANALYZE_PERIOD_LENGTH;
  }

  std::string out_tar_final_filename
    = path + std::string("/") + std::to_string(offset_start) + ".tar.gz";

  std::string out_tar_tmp_filename = out_tar_final_filename + ".tmp";

  path += "/working";
  makedir(1);

  std::vector<std::string> tar_command {
    "tar", "-czf", out_tar_tmp_filename, "-C", path, "--remove-files"
  };
  const auto empty_tar_command_length = tar_command.size();
  const auto analyze_fopen = [&tar_command](std::string path) {
    auto fp = fopen(path.c_str(), "w");
    if (!fp) {
      perror("open");
      exit(1);
    }
    tar_command.push_back(std::string(basename(path.c_str())));
    return fp;
  };
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
  auto json_fp = analyze_fopen(path + std::string("raw.json"));
  fprintf(json_fp, "{\"started\": %ld, \"updated\": %ld, \"data\":{",
                   program_start, time(NULL));
  bool first_json_entry = 0;
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
    if (strcmp(col, "user")) {
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
      if (i <= 2) {
        SQLITE3_BIND_START
        if (i <= 1) {
          NAMED_BIND_TEXT(stmt, ":user", user);
        } else if (i == 2) {
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
      #define SETUP(NAME) \
        if (info->NAME##_sql) { \
          if (first) { \
            set_problem_info(info->problems); \
          } \
          if (!setup_stmt(info->NAME##_stmt, info->NAME##_sql, #NAME)) { \
            exit(1); \
          } \
        }
      SETUP(latest_analysis);
      SETUP(latest_problem);
      SETUP(history_analysis);
      #undef SETUP
      cur++;
    }
    first = 0;
    }
    bool has_analysis = 0;
    std::string mail_path = path + std::string(user) + std::string(".mail");
    auto fp = analyze_fopen(mail_path + std::string(".header"));
    if (analyze_letter_reply_address) {
      fprintf(fp, "Reply-To: %s\n", analyze_letter_reply_address);
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
    fprintf(fp, "<head>%s</head><body>", analyze_letter_stylesheet);
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
    fp = analyze_fopen(mail_path);
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
                        ANCHOR_LINK("toc", "Top"))
                "</body>",
                analyze_letter_footer, analysis_id.c_str());
    fclose(fp);
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
      fclose(analyze_fopen(mail_path + std::string(".empty")));
    } else {
      sqlite3_stmt *dump_json_stmt = NULL;
      if (setup_stmt(
        dump_json_stmt, ANALYZE_DUMP_DATA_TO_JSON_SQL, "(dump_json)")) {
        SQLITE3_BIND_START
          NAMED_BIND_TEXT(dump_json_stmt, ":user", user);
          if (BIND_FAILED) {
            post_analyze();
            continue;
          }
        SQLITE3_BIND_END
        if (step_and_verify(dump_json_stmt, 1, "(dump_json)")) {
          SQLITE3_FETCH_COLUMNS_START("data")
          SQLITE3_FETCH_COLUMNS_LOOP_HEADER(i, dump_json_stmt)
            if (i > 0) {
              break;
            }
            if (first_json_entry) {
              fputc(',', json_fp);
            } else {
              first_json_entry = 1;
            }
            fputs((const char *)SQLITE3_FETCH_STR(), json_fp);
          SQLITE3_FETCH_COLUMNS_END
        }
        sqlite3_finalize(dump_json_stmt);
      }
    }
    post_analyze();
  }
  fputs("}}", json_fp);
  fclose(json_fp);

  const auto tar_command_length = tar_command.size();
  if (tar_command_length > empty_tar_command_length) {
    std::vector<char *> tar_exec_arglist(tar_command_length + 1);
    for (int i = 0; i < tar_command_length; i++) {
      tar_exec_arglist[i] = (char *) tar_command[i].c_str();
    }
    tar_exec_arglist[tar_command_length] = NULL;
    const auto cpid = fork();
    if (cpid == -1) {
      perror("fork");
      return;
    } else if (cpid > 0) {
      int wstatus;
      do {
        if (waitpid(cpid, &wstatus, WUNTRACED | WCONTINUED) == -1) {
          perror("waitpid");
          return;
        }
        if (WIFEXITED(wstatus) && !WEXITSTATUS(wstatus)) {
          if (rename(
                out_tar_tmp_filename.c_str(), out_tar_final_filename.c_str())) {
            perror("rename");
            return;
          }
          fclose(fopen((out_tar_final_filename + std::string(".renew")).c_str(),
                       "w"));
        }
      } while (!WIFEXITED(wstatus) && !WIFSIGNALED(wstatus));
    } else {
      if (execvp(tar_exec_arglist[0], tar_exec_arglist.data()) == -1) {
        perror("execvp");
        exit(1);
      }
    }
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
#undef STYLE
#undef SUBHEADER_TEXT
#undef COLSPAN
