#include "worker.h"

static sqlite3_stmt *measurement_insert;
static sqlite3_stmt *jobinfo_insert;

void worker_finalize() {
  sqlite3_stmt *stmt_to_finalize[] = {
    measurement_insert,
    jobinfo_insert,
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

void watcher() {
  List state_list = slurm_list_create(NULL);
  for (int i = 0; i < JOB_END; i++) {
    if (i != JOB_PENDING) {
      slurm_list_append(state_list, slurm_job_state_string(i));
    }
  }
  auto condition = (slurmdb_job_cond_t *) calloc(1, sizeof(slurmdb_job_cond_t));
  condition->flags |= JOBCOND_FLAG_NO_TRUNC;
  condition->db_flags = SLURMDB_JOB_FLAG_NOTSET;
  time_t timeout = 0;
  do {
    if (timeout) {
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
        fprintf(stderr, "Ending transaction for job %d\n", job->jobid)
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
  } while (!run_once && (wait_until(timeout)));
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
                      "following proc/stat data:\n");
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
    fprintf(stderr,
      "\n[%s] %d res=%ld minor=%ld utime=%ld stime=%ld rchar=%ld wchar=%ld\n",
      cur_result.comm, pid, cur_result.res,
      cur_result.minor_pagefault, cur_result.utime, cur_result.stime,
      cur_result.rchar, cur_result.wchar);
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
  }
  closedir(proc_dir);
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
  time_t timeout = 0;
  int scrape_cnt = run_once ? 1 : SCRAPE_CNT;
  std::vector<std::pair<slurm_step_id_t, scrape_result_t>> stats;
  while (scrape_cnt-- && wait_until(timeout)) {
    timeout = time(NULL) + SCRAPE_INTERVAL;
    process_tree_t child;
    scraper_result_map_t result;
    stepd_step_id_map_t stepd_pids;
    fetch_proc_stats(child, result, stepd_pids);
    for (const auto &[stepd_pid, stepd_step_id] : stepd_pids) {
      step_application_set_t apps;
      if (jobstep_info.job_id && stepd_step_id.job_id != jobstep_info.job_id) {
        continue;
      }
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
  }
  if (!sqlite3_end_transaction()) {
    // anyway im leaving..
    exit(1);
  }
}

void parent(int argc, const char *argv[]) {

}
