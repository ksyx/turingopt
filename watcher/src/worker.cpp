#include "worker.h"

static sqlite3_stmt *measurement_insert;
static sqlite3_stmt *jobinfo_insert;
static sqlite3_stmt *application_usage_insert;

void worker_finalize() {
  sqlite3_stmt *stmt_to_finalize[] = {
    measurement_insert,
    jobinfo_insert,
    application_usage_insert,
    FINALIZE_END_ADDR
  };
  finalize_stmt_array(stmt_to_finalize);
}

static inline bool reset_stmt(sqlite3_stmt *stmt, const char *op) {
  if (!IS_SQLITE_OK(sqlite3_reset(stmt))) {
    fprintf(stderr, "Within %s: ", op);
    SQLITE3_PERROR("reset");
    return false;
  }
  if (!IS_SQLITE_OK(sqlite3_clear_bindings(stmt))) {
      fprintf(stderr, "Within %s: ", op);
    SQLITE3_PERROR("clear_bindings");
    return false;
  }
  return true;
}

static inline bool
setup_stmt(sqlite3_stmt *&stmt, const char *sql, const char *op) {
  if (!stmt) {
    if (!IS_SQLITE_OK(PREPARE_STMT(sql, &stmt, 1))) {
      fprintf(stderr, "Within %s: ", op);
      SQLITE3_PERROR("prepare");
      exit(1);
    }
  } else {
    if (!reset_stmt(stmt, op)) {
      exit(1);
    }
  }
  return true;
}

static void jobinfo_record_insert(slurmdb_job_rec_t *job) {
  #define OP "(jobinfo_insert)"
  if (!setup_stmt(jobinfo_insert, JOBINFO_INSERT_SQL, OP)) {
    return;
  }
  SQLITE3_BIND_START
  #define BIND(TY, VAR, VAL) SQLITE3_NAMED_BIND(TY, jobinfo_insert, VAR, VAL);
  #define BIND_TEXT(VAR, VAL) NAMED_BIND_TEXT(jobinfo_insert, VAR, VAL);
  BIND(int, ":jobid", job->jobid);
  tres_t tres_req(job->tres_req_str);
  BIND(int64, ":mem", tres_req[MEM_TRES] * 1024 * 1024);
  BIND(int, ":ngpu", tres_req[GPU_TRES]);
  BIND_TEXT(":node", job->nodes);
  if (job->user) {
    BIND_TEXT(":user", job->user);
  } else if (auto info = getpwuid(job->uid)) {
    BIND_TEXT(":user", info->pw_name);
  }
  BIND_TEXT(":name", job->jobname);
  BIND_TEXT(":submit_line", job->submit_line);
  if (BIND_FAILED) {
    SQLITE3_PERROR("bind" OP);
    return;
  }
  SQLITE3_BIND_END
  if (sqlite3_step(jobinfo_insert) != SQLITE_DONE) {
    SQLITE3_PERROR("step" OP);
    return;
  }
  #if !SLURM_TRACK_STEPS_REMOVED
  if (job->track_steps)
  #endif
  {
    reset_stmt(jobinfo_insert, OP);
    SQLITE3_BIND_START
    BIND(int, ":jobid", job->jobid);
    if (BIND_FAILED) {
      SQLITE3_PERROR("bind" OP);
      return;
    }
    SQLITE3_BIND_END
    ListIterator step_it = slurm_list_iterator_create(job->steps);
    while (const auto step = (slurmdb_step_rec_t *) slurm_list_next(step_it)) {
      SQLITE3_BIND_START
      BIND(int, ":stepid", step->step_id.step_id);
      BIND_TEXT(":name", step->stepname);
      BIND_TEXT(":submit_line", step->submit_line);
      BIND_TEXT(":node", step->nodes);
      if (BIND_FAILED) {
        SQLITE3_PERROR("bind" OP);
        continue;
      }
      SQLITE3_BIND_END
      if (sqlite3_step(jobinfo_insert) != SQLITE_DONE) {
        SQLITE3_PERROR("step" OP);
        continue;
      }
      if (!IS_SQLITE_OK(sqlite3_reset(jobinfo_insert))) {
        SQLITE3_PERROR("reset" OP);
        continue;
      }
    }
    slurm_list_iterator_destroy(step_it);
  }
  #undef BIND_TEXT
  #undef BIND
  #undef OP
}

static void
measurement_record_insert(const measurement_rec_t &m) {
  #define OP "(measurement_insert)"
  if (!setup_stmt(measurement_insert, MEASUREMENTS_INSERT_SQL, OP)) {
    return;
  }
  SQLITE3_BIND_START
  #define BIND(TY, VAR, VAL) \
    SQLITE3_NAMED_BIND(TY, measurement_insert, VAR, VAL);
  BIND(int, ":watcherid", watcher_id);
  BIND(int, ":jobid", m.step_id->job_id);
  BIND(int, ":stepid", m.step_id->step_id);
  if (m.dev_in) {
    BIND(int64, ":dev_in", *m.dev_in);
    BIND(int64, ":dev_out", *m.dev_out);
  }
  BIND(int64, ":res_size", *m.res_size);
  if (m.minor_pagefault) {
    BIND(int64, ":minor_pagefault", *m.minor_pagefault);
  }
  if (m.gpu_util) {
    BIND(int, ":gpu_util", (int)(*m.gpu_util * GPU_UTIL_MULTIPLIER));
  }
  #define BINDTIMING(VAR) \
    BIND(int64, ":" #VAR "_sec", *m.VAR##_cpu_sec); \
    BIND(int, ":" #VAR "_usec", *m.VAR##_cpu_usec);
  BINDTIMING(sys); BINDTIMING(user);
  #undef BINDTIMING
  #undef BIND
  if (BIND_FAILED) {
    SQLITE3_PERROR("bind" OP);
    return;
  }
  SQLITE3_BIND_END
  if (sqlite3_step(measurement_insert) != SQLITE_DONE) {
    SQLITE3_PERROR("step" OP);
    return;
  }
  #undef OP
}

static inline void measurement_record_insert(slurmdb_step_rec_t *step) {
  measurement_rec_t m;
  tres_t tres_in(step->stats.tres_usage_in_tot);
  tres_t tres_out(step->stats.tres_usage_out_tot);
  tres_t tres_max(step->stats.tres_usage_in_max);
  m.step_id = &step->step_id;
  m.dev_in = &tres_in[DISK_TRES];
  m.dev_out = &tres_out[DISK_TRES];
  m.res_size = &tres_max[MEM_TRES];
  m.minor_pagefault = NULL;
  m.gpu_util = NULL;
  #define BINDTIMING(FIELD) \
    m.FIELD##_cpu_sec = &step->FIELD##_cpu_sec; \
    m.FIELD##_cpu_usec = &step->FIELD##_cpu_usec;
  BINDTIMING(sys);
  BINDTIMING(user);
  #undef BINDTIMING
  measurement_record_insert(m);
}

static inline void measurement_record_insert(
  slurm_step_id_t stepid, const scrape_result_t result) {
  static const size_t clk_tck = sysconf(_SC_CLK_TCK);
  measurement_rec_t m;
  m.step_id = &stepid;
  m.res_size = &result.res;
  m.minor_pagefault = &result.minor_pagefault;
  m.gpu_util = NULL;
  auto split_ticks = [](uint64_t *sec, uint32_t *usec, const size_t ticks) {
    // clk_tck ticks per second --> clk_tck/1e6 ticks per usec
    // [1e6/clk_tck usec per tick] * tick = usec
    *sec = ticks / clk_tck;
    *usec = (ticks % clk_tck) * (1e6 / clk_tck);
  };
  uint64_t sys_cpu_sec;
  uint64_t user_cpu_sec;
  uint32_t sys_cpu_usec;
  uint32_t user_cpu_usec;
  split_ticks(&sys_cpu_sec, &sys_cpu_usec, result.stime);
  split_ticks(&user_cpu_sec, &user_cpu_usec, result.utime);
  #define BINDTIMING(FIELD, RESULTFIELD) \
    split_ticks(&FIELD##_cpu_sec, &FIELD##_cpu_usec, result.RESULTFIELD##time);\
    m.FIELD##_cpu_sec = &FIELD##_cpu_sec; \
    m.FIELD##_cpu_usec = &FIELD##_cpu_usec;
  BINDTIMING(sys, s);
  BINDTIMING(user, u);
  #undef BINDTIMING
  if (result.rchar) {
    m.dev_in = &result.rchar;
    m.dev_out = &result.wchar;
  } else {
    m.dev_in = NULL;
    m.dev_out = NULL;
  }
  measurement_record_insert(m);
}

static inline bool wait_until(time_t timeout) {
  static const uint32_t futex_word = 1;
  timespec t;
  t.tv_sec = timeout;
  t.tv_nsec = 0;
  syscall(SYS_futex, &futex_word, FUTEX_WAIT_BITSET | FUTEX_CLOCK_REALTIME,
          futex_word, &t, 0, FUTEX_BITSET_MATCH_ANY);
  return futex_word;
}

static inline slurmdb_job_cond_t *setup_job_cond() {
  auto condition = (slurmdb_job_cond_t *) calloc(1, sizeof(slurmdb_job_cond_t));
  condition->flags |= JOBCOND_FLAG_NO_TRUNC;
  condition->db_flags = SLURMDB_JOB_FLAG_NOTSET;
  return condition;
}

void watcher() {
  List state_list = slurm_list_create(NULL);
  static_assert(JOB_END < 100 && "Insufficient buffer");
  static char num_buf[JOB_END * 3];
  {
    char *cur = num_buf;
    int len = 2;
    for (int i = 0; i < JOB_END; i++) {
      if (i != JOB_PENDING) {
        sprintf(cur, "%d", i);
        slurm_list_append(state_list, cur);
        cur += len;
        if (*(cur - 1)) {
          len++;
          cur++;
        }
      }
    }
  }
  auto condition = setup_job_cond();
  time_t timeout = 0;
  condition->state_list = state_list;
  do {
    if (timeout) {
      build_slurmdb_conn();
      renew_watcher(UPSERT_WATCHER_SQL_RETURNING_TIMESTAMP_RANGE);
    }
    time_t curtime = time(NULL);
    printf("Accounting import started at %ld\n", curtime);
    timeout = time(NULL) + ACCOUNTING_RPC_INTERVAL;
    condition->usage_start = time_range_start;
    condition->usage_end = time_range_end;
    List job_list = slurmdb_jobs_get(slurm_conn, condition);
    ListIterator job_it = slurm_list_iterator_create(job_list);
    while (const auto job = (slurmdb_job_rec_t *) slurm_list_next(job_it)) {
      sqlite3_begin_transaction();
      jobinfo_record_insert(job);
      if (update_jobinfo_only) {
        goto skip_acct;
      }
      #if !SLURM_TRACK_STEPS_REMOVED
      if (!job->track_steps && !job->steps) {
        auto job_step =
          (slurmdb_step_rec_t *) calloc(1, sizeof(slurmdb_step_rec_t));
        #define COPY(FIELD) job_step->FIELD = job->FIELD;
        COPY(start); COPY(end); COPY(submit_line); COPY(state); COPY(stats);
        #define COPYTIMING(FIELD) \
          COPY(FIELD##_cpu_usec); COPY(FIELD##_cpu_sec);
        COPYTIMING(user); COPYTIMING(tot); COPYTIMING(sys);
        #undef COPYTIMING
        #undef COPY
        job_step->stepname = job->jobname;
        job_step->job_ptr = job;
        measurement_record_insert(job_step);
        free(job_step);
      } else
      #endif
      {
        ListIterator step_it = slurm_list_iterator_create(job->steps);
        while (
          const auto step = (slurmdb_step_rec_t *) slurm_list_next(step_it)) {
          #if !SLURM_TRACK_STEPS_REMOVED
          if (!job->track_steps) {
            step->stats = job->stats;
          }
          #endif
          measurement_record_insert(step);
        }
        slurm_list_iterator_destroy(step_it);
      }
      skip_acct:;
      DEBUGOUT(
        fprintf(stderr, "Ending transaction for job %d [%s]\n",
          job->jobid, slurm_job_state_string(job->state));
      );
      if (!sqlite3_end_transaction()) {
        // why
        exit(1);
      }
    }
    slurm_list_iterator_destroy(job_it);
    slurm_list_destroy(job_list);
    printf("Accounting import ended at %ld, would sleep until %ld\n",
      time(NULL), timeout);
    close_slurmdb_conn();
  } while (!run_once && (wait_until(timeout)));
  slurm_list_destroy(state_list);
  free(condition);
}

static inline void fetch_proc_stats (
  process_tree_t &child,
  scraper_result_map_t &result,
  stepd_step_id_map_t &stepd_pids) {
  const size_t page_size = sysconf(_SC_PAGE_SIZE);
  char buf[READ_BUF_SIZE];
  scrape_result_t cur_result;
  memset(&cur_result, 0, sizeof(cur_result));
  DIR *proc_dir = opendir("/proc");
  /* Build process tree and get stats */
  while (auto child_dir = readdir(proc_dir)) {
    pid_t pid = 0;
    pid_t ppid = 0;
    bool success = 0;
    if (!sscanf(child_dir->d_name, "%d", &pid)) {
      continue;
    }
    std::string basepath = "/proc/" + std::string(child_dir->d_name) + "/";
    int fd = open((basepath + "stat").c_str(), O_RDONLY);
    if (fd) {
      if (auto cnt = read(fd, buf, READ_BUF_SIZE)) {
        if (cnt < 0) {
          continue;
        }
        const char *end = buf + cnt;
        const char *cur = buf;
        int col = 1;
        size_t val = 0;
        for (; !success && cur != end; cur++) {
          const char c = *cur;
          // printf("%c[%ld]  ", c, val); fflush(stdout);
          if (c == ' ') {
            #define ASSIGNRAW(COL, ASSIGNMENT, ...) \
              case COL: ASSIGNMENT = val; __VA_ARGS__ break;
            #define ASSIGN(COL, ASSIGNMENT, ...) \
              ASSIGNRAW(COL, cur_result.ASSIGNMENT, __VA_ARGS__)
            #define ASSIGNLAST(COL, ASSIGNMENT) \
              ASSIGN(COL, ASSIGNMENT, success = 1;)
            switch (col) {
              ASSIGN(1, pid);
              ASSIGNRAW(4, ppid);
              ASSIGN(10, minor_pagefault);
              ASSIGN(11, cminor_pagefault);
              ASSIGN(14, utime);
              ASSIGN(15, stime);
              ASSIGN(16, cutime);
              ASSIGN(17, cstime);
              ASSIGNLAST(24, res);
            }
            #undef ASSIGNLAST
            #undef ASSIGNRAW
            #undef ASSIGN
            col++;
            val = 0;
            continue;
          } else if (c >= '0' && c <= '9') {
            val = val * 10 + c - '0';
          } else if (c == '(') {
            assert(col == 2 /*(comm)*/);
            if (col != 2) {
              fprintf(stderr,
                "error: unrecognized proc/stat format having character "
                ") outside field 2\n");
              break;
            }
            const char *tail = NULL;
            const char *search_end = end - 1;
            for (auto end = search_end; end > cur; end--)
              if (*end == ')') {
                tail = end - 1;
                break;
              }
            if (!tail) {
              fprintf(stderr,
                      "Error: Could not locate end of comm field in the"
                      "following proc/stat data [%ld bytes read]:\n", cnt);
              for (auto i = buf; i != end; i++) {
                if (i == cur) {
                  fputs(">>>", stderr);
                }
                fputc(*i, stderr);
                if (i == search_end) {
                  fputs("<<<", stderr);
                }
              }
              fputs("\n", stderr);
              break;
            }
            int comm_len = std::min(tail - cur, (long)TASK_COMM_LEN);
            for (auto i = 0; i < comm_len; i++)
              cur_result.comm[i] = cur[i + 1];
            cur_result.comm[comm_len] = '\0';
            cur = tail;
          }
        }
        close(fd);
        if (!success) {
          // Did not reach last column
          continue;
        } else {
          cur_result.res *= page_size;
        }
      }
    } else {
      continue;
    }
    if (auto f = fopen((basepath + "io").c_str(), "r")) {
      fscanf(f, "rchar: %ld wchar: %ld", &cur_result.rchar, &cur_result.wchar);
      fclose(f);
    } else {
      cur_result.rchar = cur_result.wchar = 0;
    }
    DEBUGOUT_VERBOSE(
      fprintf(stderr,
        "\n[%s] %d res=%ld minor=%ld utime=%ld stime=%ld rchar=%ld wchar=%ld\n",
        cur_result.comm, pid, cur_result.res,
        cur_result.minor_pagefault, cur_result.utime, cur_result.stime,
        cur_result.rchar, cur_result.wchar);
    );
    if (auto f = fopen((basepath + "cmdline").c_str(), "r")) {
      slurm_step_id_t step;
      char c;
      #define STEP_MAX_LENGTH 32
      char step_str[STEP_MAX_LENGTH + 1];
      if (fscanf(f, "slurmstepd: [%d.%" STRINGIFY(STEP_MAX_LENGTH) "s",
                 &step.job_id, step_str) == 2
          && (!fread(&c, sizeof(char), 1, f) || !c)) {
        // From slurm protocol definition source, reordered with possibility
        const struct {
          const char *name;
          unsigned int stepid;
        } mapping[] = {
          { "batch", SLURM_BATCH_SCRIPT },
          { "interactive", SLURM_INTERACTIVE_STEP },
          { "extern", SLURM_EXTERN_CONT },
          { "TBD", SLURM_PENDING_STEP },
          { NULL, 0 }
        };
        bool found_stepid = sscanf(step_str, "%d]", &step.step_id);
        if (!found_stepid) {
          size_t len = strlen(step_str) - 1;
          if (step_str[len] == ']') {
            step_str[len] = '\0';
            for (auto *cur = mapping; cur->name; cur++) {
              if (*step_str == *(cur->name) && !strcmp(step_str, cur->name)) {
                step.step_id = cur->stepid;
                found_stepid = true;
                break;
              }
            }
          }
        }
        if (found_stepid) {
          stepd_pids[pid] = step;
        }
      }
      #undef STEP_MAX_LENGTH
      fclose(f);
    }
    child[ppid].push_back(pid);
    result[pid] = cur_result;
    for (auto &cpid : child[pid]) {
      if (auto dir = opendir(("/proc/" + std::to_string(cpid)).c_str())) {
        closedir(dir);
      } else if (errno == ENOENT) {
        // child has terminated approximately at the time of scraping parent
        // this would double count the c____ fields
        const auto &STAT_MERGE_SRC = result[cpid];
        auto &STAT_MERGE_DST = result[pid];
        ACCUMULATE_SCRAPER_STAT(rchar);
        ACCUMULATE_SCRAPER_STAT(wchar);
        AGGERGATE_SCRAPER_STAT_MAX(res);
        result.erase(cpid);
        cpid = 0;
      }
    }
  }
  closedir(proc_dir);
}

static void walk_scraped_proc_tree (
  pid_t cur,
  process_tree_t &tree,
  scraper_result_map_t &stats,
  step_application_set_t &application_set) {

  #define ACCUMULATE_SELF_STAT(NAME) \
    STAT_MERGE_DST.c##NAME += STAT_MERGE_DST.NAME; \
    STAT_MERGE_DST.NAME = 0;

  auto &STAT_MERGE_DST = stats[cur];
  const auto &cur_stat = STAT_MERGE_DST;
  application_set.emplace(std::string(cur_stat.comm));
  const auto &childs = tree[cur];
  ACCUMULATE_SELF_STAT(utime);
  ACCUMULATE_SELF_STAT(stime);
  ACCUMULATE_SELF_STAT(minor_pagefault);
  for (const auto &cpid : childs) {
    if (!cpid)
      continue;
    const auto &STAT_MERGE_SRC = stats[cpid];
    DEBUGOUT(STAT_MERGE_SRC.print();)
    walk_scraped_proc_tree(cpid, tree, stats, application_set);
    // ____ of child is already in c____ of parent, but not recursive
    ACCUMULATE_SCRAPER_STAT(rchar);
    ACCUMULATE_SCRAPER_STAT(wchar);
    ACCUMULATE_SCRAPER_STAT(cutime);
    ACCUMULATE_SCRAPER_STAT(cstime);
    ACCUMULATE_SCRAPER_STAT(cminor_pagefault);
    AGGERGATE_SCRAPER_STAT_MAX(res);
  }
  #undef ACCUMULATE_LEAF_STAT
}

// Implemented as RPC-free
void scraper(const char *argv_0) {
  #define OP "(application_usage)"
  time_t timeout = 0;
  int scrape_cnt = run_once ? 1 : SCRAPE_CNT;
  std::vector<std::pair<slurm_step_id_t, scrape_result_t>> stats;
  if (
    !setup_stmt(application_usage_insert, APPLICATION_USAGE_INSERT_SQL, OP)) {
    return;
  }
  // Theoretically this implementation could merge two different steps with same
  // pid, happening when, within one scraping session, one exits and the pids
  // somehow rolled rocket fast and *luckily* the next slurmstepd got this pid
  // again. With this luck it is suggested that you buy a lottery and share me 5
  // million if u got a jackpot :)
  std::map<pid_t /*slurmstepd_pid*/, step_application_set_t> app_map;
  while (scrape_cnt-- && wait_until(timeout)) {
    timeout = time(NULL) + SCRAPE_INTERVAL;
    process_tree_t child;
    scraper_result_map_t result;
    stepd_step_id_map_t stepd_pids;
    fetch_proc_stats(child, result, stepd_pids);
    for (const auto &[stepd_pid, stepd_step_id] : stepd_pids) {
      if (jobstep_info.job_id && stepd_step_id.job_id != jobstep_info.job_id) {
        continue;
      }
      step_application_set_t &apps = app_map[stepd_pid];
      walk_scraped_proc_tree(stepd_pid, child, result, apps);
      auto &final_result = result[stepd_pid];
      #define MERGECHILD(FIELD) \
        final_result.FIELD += final_result.c##FIELD; \
        final_result.c##FIELD = 0;
      MERGECHILD(minor_pagefault);
      MERGECHILD(utime);
      MERGECHILD(stime);
      #undef MERGECHILD
      apps.erase("slurmstepd");
      apps.erase("slurm_script");
      DEBUGOUT(
        fputs("===> ", stderr);
        bool first = 1;
        for (const auto &app : apps) {
          fprintf(stderr, "%s%s", first ? "{" : ", ", app.c_str());
          first = 0;
        }
        fprintf(stderr, "} [%d.%d] ",
          stepd_step_id.job_id, stepd_step_id.step_id);
        final_result.print(0);
        fputs("\n", stderr);
      )
      stats.push_back(std::make_pair(stepd_step_id, final_result));
    }
  }
  if (is_scraper) {
    // not being called from parent()
    #if RESTORE_ENV
    setenv(IS_SCRAPER_ENV "_BACKUP", getenv(IS_SCRAPER_ENV), true);
    #endif
    unsetenv(IS_SCRAPER_ENV);
    // No one cares what's inside, as long as they exists
    setenv(RUN_ONCE_ENV, "1", false);
    setenv(UPDATE_JOBINFO_ONLY_ENV, "1", false);
    system(argv_0);
    DEBUGOUT(fputs("Job information updated\n",stderr););
    #if RESTORE_ENV
    setenv(IS_SCRAPER_ENV, getenv(IS_SCRAPER_ENV "_BACKUP"), true);
    unsetenv(IS_SCRAPER_ENV "_BACKUP")
    if (!run_once) {
      unsetenv(RUN_ONCE_ENV);
    }
    if (!update_jobinfo_only) {
      unsetenv(UPDATE_JOBINFO_ONLY_ENV);
    }
    #endif
  }
  sqlite3_begin_transaction();
  for (auto &[id, result] : stats) {
    measurement_record_insert(id, result);
    // Keep this part the last one in the loop
    SQLITE3_BIND_START;
    NAMED_BIND_INT(application_usage_insert, ":jobid", id.job_id);
    NAMED_BIND_INT(application_usage_insert, ":stepid", id.step_id);
    if (BIND_FAILED) {
      continue;
    }
    SQLITE3_BIND_END;
    if (app_map.count(result.pid)) {
      auto &apps = app_map[result.pid];
      for (const auto &app : apps) {
        SQLITE3_BIND_START;
        NAMED_BIND_TEXT(application_usage_insert, ":application", app.c_str());
        if (BIND_FAILED) {
          continue;
        }
        SQLITE3_BIND_END;
        if (sqlite3_step(application_usage_insert) != SQLITE_DONE) {
          SQLITE3_PERROR("step" OP);
          continue;
        }
        if (!IS_SQLITE_OK(sqlite3_reset(application_usage_insert))) {
          SQLITE3_PERROR("reset" OP);
          continue;
        }
      }
      app_map.erase(result.pid);
    }
  }
  if (!sqlite3_end_transaction()) {
    // anyway im leaving..
    exit(1);
  }
  #undef OP
}

static void expand_node_group(
  const node_val_t depth,
  const node_group_t &node_group,
  val_assignment_t &assignment_vec,
  node_string_list_t &result_list
  ) {
  if (depth == assignment_vec.size()) {
    std::string t = "";
    for (node_val_t i = 0; i < depth; i++) {
      const auto &group = node_group[i];
      t += group.prefix;
      if (group.ranges.size()) {
        const auto &result = assignment_vec[i];
        const auto numstr = std::to_string(result.first);
        size_t missing_len;
        if ((missing_len = result.second - numstr.length()) > 0) {
          t += std::string(missing_len, '0');
        }
        t += numstr;
      }
    }
    result_list.push_back(t);
    return;
  }
  const auto &range = node_group[depth].ranges;
  if (!range.size()) {
    expand_node_group(depth + 1, node_group, assignment_vec, result_list);
  } else {
    for (auto &r : range) {
      auto start = r.range.first;
      auto end = r.range.second;
      if (start > end) {
        // ???
        std::swap(start, end);
      }
      for (node_val_t i = start; i <= end; i++) {
        assignment_vec[depth] = std::make_pair(i, r.length);
        expand_node_group(depth + 1, node_group, assignment_vec, result_list);
      }
    }
  }
}

static inline bool
split_node_string(node_string_list_t &list, char *str) {
  static const node_string_part empty;
  bool in_bracket = 0;
  node_string_part cur;
  const char *seg_start = str;
  bool is_start;
  node_val_t val[2];
  uint8_t start_part_length;
  std::vector<node_group_t> node_description(1);
  DEBUGOUT_VERBOSE(fprintf(stderr, "input=%s\n", str));
  // compute-0-[29-30,32-33,35-40,47-54],compute-1-[02-04],compute-2-04
  // scontrol show hostnames node[1-3,01,1-03,01-03,001-03,001-13,999-1000]
  while (str) {
    const auto c = *str;
    DEBUGOUT_VERBOSE(fprintf(stderr, "ch=%c bracket=%d start=%d 0=%d 1=%d\n",
                      c, in_bracket, is_start, val[0], val[1]));
    if (in_bracket) {
      if (c == ']' || c == ',') {
        if (is_start) {
          // still counting left, '-' not met
          val[1] = val[0];
        }
        cur.ranges.push_back(
          {std::make_pair(val[0], val[1]), start_part_length});
        if (c == ',') {
          start_part_length = val[0] = val[1] = 0;
          is_start = 1;
        } else if (c == ']') {
          in_bracket = 0;
          node_description.back().push_back(cur);
          cur = empty;
          seg_start = str + 1;
        }
      } else if (c == '-') {
        is_start = 0;
      } else if (c >= '0' && c <= '9') {
        val[!is_start] = val[!is_start] * 10 + c - '0';
        if (is_start) {
          start_part_length++;
        }
      } else {
        return false;
      }
    } else if (c == '[') {
      *str = '\0';
      cur.prefix = std::string(seg_start);
      in_bracket = 1;
      start_part_length = val[0] = val[1] = 0;
      is_start = 1;
    } else if (c == ',' || !c) {
      if (str != seg_start) {
        *str = '\0';
        cur.prefix = std::string(seg_start);
        node_description.back().push_back(cur);
        cur = empty;
      }
      if (c == ',') {
        seg_start = str + 1;
        node_description.push_back({});
      } else {
        break;
      }
    }
    str++;
  }
  for (const auto &node_group : node_description) {
    val_assignment_t vals(node_group.size());
    expand_node_group(0, node_group, vals, list);
  }
  DEBUGOUT_VERBOSE(
    fputs("output=", stderr);
    bool first = 1;
    for (const auto &node : list) {
      fprintf(stderr, "%s%s", first ? "" : ",", node.c_str());
      first = 0;
    }
    fputs("\n", stderr);
  )
  return true;
}

static inline void send_job(
  node_string_list_t &allocated_nodes, // Output
  job_desc_msg_t desc,
  const node_set_t &excl_set,
  const char **env,
  const char **submit_argv,
  const char *req_nodes = NULL // For printing message only
) {
  auto launch_job = [&](resource_allocation_response_msg_t *response) {
    // WHY WOULD SOME SOFTWARE LIST SOMETHING AS API WHEREAS THERE IS NO WAY
    // THE USER COULD USE THAT???? --- step_launch
    pid_t child = fork();
    if (child > 0) {
      int wstatus;
      do {
        if (waitpid(child, &wstatus, 0) < 0) {
          perror("waitpid");
          exit(1);
        }
        if (WIFEXITED(wstatus)) {
          return WEXITSTATUS(wstatus);
        } else if (WIFSIGNALED(wstatus)) {
          return WSTOPSIG(wstatus);
        }
      } while (!WIFEXITED(wstatus) && !WIFSIGNALED(wstatus));
    } else if (!child) {
      static const char *srun_argv[] = {
        "srun", "-c", "1", "-N", std::to_string(response->node_cnt).c_str(),
        "--jobid", std::to_string(response->job_id).c_str(),
        NULL
      };
      auto count_argv = [](const char **arr) {
        size_t cnt = 0;
        while (*arr) {
          arr++;
          cnt++;
        }
        return cnt;
      };
      const size_t srun_cnt = count_argv(srun_argv);
      const size_t submit_cnt = count_argv((const char **)submit_argv);
      const size_t tot_cnt = srun_cnt + submit_cnt;
      auto argv = (char **)malloc(sizeof(char *) * (tot_cnt + 1));
      memcpy(argv, srun_argv, sizeof(char *) * srun_cnt);
      memcpy(argv + srun_cnt, submit_argv, sizeof(char *) * submit_cnt);
      argv[tot_cnt] = NULL;
      execvpe("srun", argv, (char * const *)env);
    } else {
      perror("fork");
    }
    return 1;
  };
  std::string excl_nodes;
  for (const auto &cur : excl_set) {
    if (excl_nodes.size()) {
      excl_nodes += ",";
    }
    excl_nodes += std::string(cur);
  }
  DEBUGOUT_VERBOSE(fprintf(stderr, "Excluded: %s\n", excl_nodes.c_str()));
  desc.exc_nodes = (char *)excl_nodes.c_str();
  auto response = slurm_allocate_resources_blocking(
    &desc, ALLOCATION_TIMEOUT, NULL);
  DEBUGOUT(
    fprintf(stderr,
            "Requesting nodes %s with account %s partition %s...  ",
            req_nodes, desc.account, desc.partition);
  );
  if (response) {
    auto *strcopy = (char *)malloc(strlen(response->node_list) + 1);
    strcpy(strcopy, response->node_list);
    DEBUGOUT(fprintf(stderr, "GOT %s\n", strcopy));
    split_node_string(allocated_nodes, strcopy);
    free((void *)strcopy);
    launch_job(response);
    slurm_free_resource_allocation_response_msg(response);
  } else if (slurm_get_errno() == ETIMEDOUT) {
    DEBUGOUT(fputs("TIMEOUT\n", stderr));
  } else {
    slurm_perror("allocate_resources");
  }
}

static inline void
watcher_distributor_count_nodes(
  char *nodes, node_val_t nnodes, node_usage_map_t &map) {
  node_string_list_t node_strings;
  DEBUGOUT_VERBOSE(
    fprintf(stderr, "split %s:\n", nodes);
  );
  if (!split_node_string(node_strings, nodes)) {
    fprintf(stderr, "error: could not split string %s\n", nodes);
    return;
  }
  DEBUGOUT_VERBOSE(
    bool first = 1;
    for (const auto &str : node_strings) {
      fprintf(stderr, "%s%s", first ? "" : ",", str.c_str());
      first = 0;
    }
    fputs("\n", stderr);
  );
  for (const auto &str : node_strings) {
    map[str]++;
  }
  if (nnodes && node_strings.size() != nnodes) {
    fprintf(stderr, "error splitting %s: expecting %d, got %ld\n",
      nodes, nnodes, node_strings.size());
  }
}

void *node_watcher_distributor(void *arg) {
  // Timeout works as a frequency guard that would return quickly to prevent
  // problematic childs that complete way too fast and stress the scheduler
  time_t timeout = 0;
  auto condition = setup_job_cond();
  node_usage_map_t node_usage_map;
  std::map<std::string /*partition*/, std::string /*acct*/> partition_account;
  std::map<std::string, std::string> node_partition;
  std::map<uint32_t, std::string> qos_name;
  std::map<std::string, partition_info_t> partition_info;
  node_set_t nodeset;
  // Todo: max jobs aware
  {
    const char *cur_user = getpwuid(geteuid())->pw_name;
    auto *assoc_cond =
      (slurmdb_assoc_cond_t *)calloc(1, sizeof(slurmdb_assoc_cond_t));
    List user_list = slurm_list_create(NULL);
    assoc_cond->user_list = user_list;
    slurm_list_append(user_list, (void *)cur_user);
    List qos_list = slurmdb_qos_get(slurm_conn, NULL);
    ListIterator list_it = slurm_list_iterator_create(qos_list);
    while (
      const auto qos = (slurmdb_qos_rec_t *) slurm_list_next(list_it)) {
      qos_name[qos->id] = std::string(qos->name);
    }
    slurm_list_iterator_destroy(list_it);
    slurm_list_destroy(qos_list);
    List assoc_list = slurmdb_associations_get(slurm_conn, assoc_cond);
    list_it = slurm_list_iterator_create(assoc_list);
    while (
      const auto assoc = (slurmdb_assoc_rec_t *) slurm_list_next(list_it)) {
      ListIterator qos_list_it = slurm_list_iterator_create(assoc->qos_list);
      std::string val(assoc->acct);
      while (const auto part = (const char *) slurm_list_next(qos_list_it)) {
        std::string key = qos_name[atoi(part/*ition*/)];
        if (assoc->is_def/*ault*/) {
          partition_account[key] = val;
        } else {
          partition_account.try_emplace(key, val);
        }
      }
    }
    slurm_list_iterator_destroy(list_it);
    slurm_list_destroy(assoc_list);
    slurm_list_destroy(user_list);
    free(assoc_cond);
  }
  do {
    if (timeout) {
      if (!build_slurmdb_conn()) {
        exit(1);
      }
    }
    uint32_t concurrency = SCRAPE_CONCURRENT_NODES;
    // load partitions and assign weight
    {
      partition_info_msg_t *partition_msg;
      struct scraper_partition_info_t {
        const partition_info_t *orig;
        // the smaller the higher weight
        bool operator < (const scraper_partition_info_t &b) const {
          #define COMPARATOR(FIELD, INFINITEVAL, RET_ME_INFINITE, COMPARATOR) \
            { \
              auto &max_a = orig->FIELD; \
              auto &max_b = b.orig->FIELD; \
              if (max_a != max_b) { \
                if (max_a == INFINITEVAL) { \
                  return RET_ME_INFINITE; \
                } else if (max_b == INFINITEVAL) { \
                  return !RET_ME_INFINITE; \
                } else { \
                  return max_a COMPARATOR max_b; \
                } \
              } \
            }
          COMPARATOR(max_nodes, 0, true, >);
          COMPARATOR(max_time, INFINITE, false, <);
          // Not a big deal
          return true;
        }
      };
      if (!slurm_load_partitions(timeout, &partition_msg, 0)) {
        std::vector<scraper_partition_info_t> usable_partitions;
        scraper_partition_info_t usable_partition;
        for (size_t i = 0; i < partition_msg->record_count; i++) {
          const auto &info = partition_msg->partition_array[i];
          const std::string name(info.name);
          if (info.allow_accounts && !partition_account.count(name)) {
            continue;
          }
          usable_partition.orig = &info;
          usable_partitions.push_back(usable_partition);
          partition_info[name] = info;
        }
        std::sort(usable_partitions.begin(), usable_partitions.end());
        for (const auto &partition : usable_partitions) {
          const auto &cur = partition.orig;
          node_string_list_t nodes;
          split_node_string(nodes, cur->nodes);
          for (const auto &node : nodes) {
            nodeset.emplace(node);
            node_partition.try_emplace(node, cur->name);
          }
        }
        slurm_free_partition_info_msg(partition_msg);
      }
    }
    struct scrape_task_t {
      std::string node;
      int cnt;
      bool operator < (const scrape_task_t &b) const {
        return cnt > b.cnt;
      }
    };
    std::map<std::string, std::vector<scrape_task_t> > tasks;
    {
      List job_list = slurmdb_jobs_get(slurm_conn, condition);
      ListIterator job_it = slurm_list_iterator_create(job_list);
      timeout = time(NULL)
        + TOTAL_SCRAPE_TIME_PER_NODE
          * (slurm_list_count(job_list) / concurrency + 1);
      while (const auto job = (slurmdb_job_rec_t *) slurm_list_next(job_it)) {
        if (job->state != JOB_RUNNING) {
          // Slurm was unhappy about filtering it by cond, returning all PENDING
          // jobs
          continue;
        }
        #if !SLURM_TRACK_STEPS_REMOVED
        if (!job->track_steps && !job->steps) {
          watcher_distributor_count_nodes(job->nodes, 0, node_usage_map);
        } else
        #endif
        {
          ListIterator step_it = slurm_list_iterator_create(job->steps);
          while (
            const auto step = (slurmdb_step_rec_t *) slurm_list_next(step_it)) {
            watcher_distributor_count_nodes
              (step->nodes, step->nnodes, node_usage_map);
          }
          slurm_list_iterator_destroy(step_it);
        }
      }
      slurm_list_iterator_destroy(job_it);
      slurm_list_destroy(job_list);
      scrape_task_t task;
      bool need_restart = 0;
      for (const auto &[str, val] : node_usage_map) {
        task.node = str;
        task.cnt = val;
        if (!node_partition.count(str)) {
          need_restart = 1;
        }
        tasks[node_partition[str]].push_back(task);
        DEBUGOUT(fprintf(stderr,
          "%s[%s] %d\n", str.c_str(), node_partition[str].c_str(), val);)
      }
      if (need_restart) {
        timeout = time(NULL);
        continue;
      }
    }
    job_desc_msg_t desc;
    slurm_init_job_desc_msg(&desc);
    static const char *submit_argv[] = {
      #if DISTRIBUTE_DUMMY_WATCHER
      "./dummy.sh",
      #else
      (const char *)arg,
      #endif
      NULL
    };
    desc.argc = 1;
    desc.argv = (char **)submit_argv;
    desc.contiguous = 0;
    desc.submit_line = (char *)submit_argv[0];
    desc.time_limit = TOTAL_SCRAPE_TIME_PER_NODE / 60 + 1;
    std::string dbenv = std::string(DB_FILE_ENV "=") + std::string(db_path);
    std::string libpath =
      std::string("LD_LIBRARY_PATH=") + std::string(getenv("LD_LIBRARY_PATH"));
    std::string conf_path_env =
      std::string("SLURM_CONF=") + std::string(slurm_conf_path);
    static const char *env[] = {
      IS_SCRAPER_ENV "=1",
      dbenv.c_str(),
      conf_path_env.c_str(),
      libpath.c_str(),
      NULL
    };
    desc.environment = (char **) env;
    desc.env_size = 0;
    {
      auto cur = env;
      while (*cur) {
        desc.env_size++;
        cur++;
      }
    }
    desc.immediate = 0;
    desc.name = (char *)SCRAPER_JOB_NAME;
    desc.open_mode = OPEN_MODE_APPEND;
    desc.shared = 1;
    DEBUGOUT(
      for (auto &[partition, account] : partition_account) {
        fprintf(stderr, "%s -- %s\n", partition.c_str(), account.c_str());
      }
    );
    for (auto &[partition, task] : tasks) {
      int s1 = 0, s2 = task.size();
      DEBUGOUT(
        fprintf(stderr, "===== Partition %s =====\n", partition.c_str());
      )
      auto &info = partition_info[partition];
      concurrency = SCRAPE_CONCURRENT_NODES;
      if (info.max_nodes && info.max_nodes != INFINITE) {
        concurrency = std::min(concurrency, info.max_nodes / 2);
      }
      desc.account = (char *)partition_account[partition].c_str();
      desc.partition = (char *)partition.c_str();
      std::sort(task.begin(), task.end());
      size_t cur = 0; bool idx = 0;
      std::queue<std::string> q[2];
      std::queue<std::string> nodemix;
      size_t target = std::min(task.size(), (size_t) concurrency);
      for (size_t i = 0; i < target; i++) {
        q[idx].push(task[cur++].node);
      }
      while (cur < task.size()) {
        node_set_t excl_set = nodeset;
        size_t missing = concurrency * 2 - q[idx].size();
        while (missing && cur < task.size()) {
          q[!idx].push(task[cur++].node);
          missing--;
        }
        // Ask slurm to choose [concurrency, 2concurrency] nodes from list
        desc.max_nodes = q[0].size() + q[1].size();
        // With a hope that slurm gives as much as possible
        desc.min_nodes = 1;
        std::map<std::string, bool> unallocated;
        std::string req_nodes;
        for (int i = 0; i < 2; i++) {
          while (!q[i].empty()) {
            const auto &cur = q[i].front();
            unallocated[cur] = i;
            excl_set.erase(cur);
            if (req_nodes.size()) {
              req_nodes += ",";
            }
            req_nodes += std::string(cur);
            q[i].pop();
          }
        }
        node_string_list_t alloc_nodes;
        send_job
          (alloc_nodes, desc, excl_set, env, submit_argv, req_nodes.c_str());
        for (const auto &node : alloc_nodes) {
          unallocated.erase(node);
        }
        s1 += alloc_nodes.size();
        for (const auto &[node, val] : unallocated) {
          if (val == idx || cur == task.size()) {
            nodemix.push(node);
          } else {
            q[!idx].push(node);
          }
        }
        idx = !idx;
      }
      {
      node_set_t excl_set = nodeset;
      while (!nodemix.empty()) {
        excl_set.erase(nodemix.front());
        nodemix.pop();
      }
      desc.max_nodes = concurrency * 2;
      // With a hope that slurm gives as much as possible
      desc.min_nodes = 1;
      while (1) {
        node_string_list_t alloc_nodes;
        send_job(alloc_nodes, desc, excl_set, env, submit_argv, "(mixset)");
        if (!alloc_nodes.size()) {
          break;
        }
        for (const auto &node : alloc_nodes) {
          excl_set.insert(node);
        }
        s1 += alloc_nodes.size();
      }
      }
      DEBUGOUT(
        fprintf(stderr,
          "======== Finally done a round [%d/%d tasks allocated] ========\n",
          s1, s2);
      );
    }
    if (!close_slurmdb_conn()) {
      exit(1);
    }
  } while (!run_once && wait_until(timeout));
  free(condition);
  return NULL;
}

void parent(int argc, const char *argv[]) {

}
