#include "main.h"

// Connections
sqlite3 *SQL_CONN_NAME;
void *slurm_conn;

// Watcher Metadata
worker_info_t worker;
bool is_server;
bool run_once;
bool update_jobinfo_only;
char *db_path;
// For building srun environment from nothing
std::string slurm_conf_path;

// Watcher Parameters
int watcher_id;
time_t time_range_start;
time_t time_range_end;

bool distribute_node_watcher_only;

slurm_job_state_string_func_t slurm_job_state_string;

std::map<std::string, int> tres_t::tres_from_str;
std::map<int, std::string> tres_t::tres_from_id;
bool tres_t::tres_map_initialized;

tres_t::tres_t() {
  if (!tres_map_initialized) {
    tres_map_initialized = 1;
    slurmdb_tres_cond_t condition;
    slurmdb_init_tres_cond(&condition, false);
    List tres_list = slurmdb_tres_get(slurm_conn, &condition);
    ListIterator tres_it = slurm_list_iterator_create(tres_list);
    DEBUGOUT(
    bool first = 1;
    )
    while (const auto tres = (slurmdb_tres_rec_t *) slurm_list_next(tres_it)) {
      DEBUGOUT(
        fprintf(stderr,
          "%s%s/%s=%d", first ? "" : ",", tres->type, tres->name, tres->id);
        first = 0;
      );
      std::string tres_type_str(tres->type);
      if (tres->name) {
        tres_type_str += "/" + std::string(tres->name);
      }
      tres_from_str.try_emplace(tres_type_str, tres->id);
      tres_from_id.try_emplace(tres->id, tres_type_str);
    }
    DEBUGOUT(fputs("\n", stderr););
    slurm_list_iterator_destroy(tres_it);
    slurm_list_destroy(tres_list);
  }
}

tres_t::tres_t(const char *tres_str) : tres_t() {
  if (!tres_str) {
    return;
  }
  size_t idx = 0;
  size_t *cur = &idx;
  while (*tres_str) {
    if (*tres_str >= '0' && *tres_str <= '9') {
      *cur = *cur * 10 + *tres_str - '0';
    } else if (*tres_str == ',') {
      cur = &idx;
      idx = 0;
    } else if (*tres_str == '=') {
      value[idx] = 0;
      cur = &value[idx];
    }
    tres_str++;
  }
}

void tres_t::print() {
  bool first = 1;
  for (auto &[idx, val] : value) {
    printf("%s%s=%ld", first ? "" : ",", from_id(idx), val);
    first = 0;
  }
  puts("");
}

static inline void build_sqlite_conn() {
  if (!IS_SQLITE_OK(sqlite3_open(db_path, &sqlite_conn))) {
    SQLITE3_PERROR("open");
    exit(1);
  }
  char *sqlite_err = NULL;
  if (!IS_SQLITE_OK(
        sqlite3_exec(sqlite_conn, INIT_DB_SQL, NULL, NULL, &sqlite_err))) {
    SQLITE3_PERROR("exec");
    exit(1);
  }
  sqlite3_free(sqlite_err);

  // Register watcher
  sqlite3_begin_transaction();
  if (!renew_watcher(REGISTER_WATCHER_SQL_RETURNING_TIMESTAMPS_AND_WATCHERID)) {
    exit(1);
  }
  if (update_jobinfo_only) {
    sqlite3_exec(SQL_CONN_NAME, "ROLLBACK;", NULL, NULL, NULL);
  } else {
    sqlite3_end_transaction();
  }
}

bool close_slurmdb_conn() {
  if (slurm_conn && !IS_SLURM_SUCCESS(slurmdb_connection_close(&slurm_conn))) {
    slurm_perror("slurmdb_connection_close");
    return false;
  }
  return true;
}

bool build_slurmdb_conn() {
  uint16_t connflag;
  if (!(slurm_conn = slurmdb_connection_get(&connflag))) {
    slurm_perror("slurmdb_connection_get");
    return false;
  }
  return true;
}

static void finalize(void) {
  close(sock);
  worker_finalize();
  db_common_finalize();
  close_slurmdb_conn();
  if (SQL_CONN_NAME && !IS_SQLITE_OK(sqlite3_close(SQL_CONN_NAME))) {
    SQLITE3_PERROR("close");
  }
}

// no overflow check
template<typename T>
static inline void to_integer(const char *ptr, T *val) {
  while (const auto c = *ptr) {
    if (c >= '0' && c <= '9') {
      *val = *val * 10 + c - '0';
    } else {
      throw std::invalid_argument("");
    }
    ptr++;
  }
}

static inline bool initialize(int argc, char *argv[]) {
  slurm_job_state_string
    = (slurm_job_state_string_func_t)
        dlsym(RTLD_DEFAULT, "slurm_job_state_string");
  distribute_node_watcher_only = getenv(DISTRIBUTE_NODE_WATCHER_ONLY);
  if (!slurm_job_state_string) {
    printf("dlsym: %s", dlerror());
    exit(1);
  }
  build_socket();
  auto &worker_type = worker.type;
  if (argc > 1) {
    worker_type = WORKER_PARENT;
  } else if (getenv(IS_SCRAPER_ENV)) {
    worker_type = WORKER_SCRAPER;
  } else if (distribute_node_watcher_only) {
    worker_type = WORKER_SPECIAL;
  } else {
    worker_type = WORKER_WATCHER;
  }
  run_once = getenv(RUN_ONCE_ENV);
  update_jobinfo_only = getenv(UPDATE_JOBINFO_ONLY_ENV);
  db_path = getenv(DB_FILE_ENV);
  if (!db_path) {
    db_path = (char *)DEFAULT_DB_PATH;
  }
  if (update_jobinfo_only && !run_once) {
    fputs("error: invalid combination update_jobinfo_only && !run_once",
          stderr);
    exit(1);
  }
  worker.pid = getpid();
  if (!is_scraper && !is_parent) {
    slurm_init(NULL);
    if (!build_slurmdb_conn()) {
      return false;
    }
    const uid_t uid = geteuid();
    slurm_conf_t *conf = NULL;
    if (!IS_SLURM_SUCCESS(slurm_load_ctl_conf(0, &conf))) {
      slurm_perror("slurm_load_ctl_conf");
      return false;
    }
    slurm_conf_path = std::string(conf->slurm_conf);
    const auto is_slurm_user = [conf, uid]() {
      return uid == conf->slurm_user_id;
    };
    if (uid == 0
    #if SLURM_USER_IS_PRIVILEGED
    || is_slurm_user()
    #endif
    ) {
      // scraper could continue with fetching gpu data only
      worker.is_privileged = true;
    }
    slurm_free_ctl_conf(conf);
    if (is_parent) {
      const char *jobid_env = getenv("SLURM_JOB_ID");
      const char *stepid_env = getenv("SLURM_STEP_ID");
      if (jobid_env) {
        try {
          to_integer(jobid_env, &worker.jobstep_info.job_id);
          if (stepid_env) {
            to_integer(stepid_env, &worker.jobstep_info.step_id);
          } else {
            worker.jobstep_info.step_id = -1;
          }
        } catch (std::invalid_argument &e) {
          fputs("Invalid job or step id environment variable\n", stderr);
          return false;
        }
      } else {
        fputs("Missing environment variable SLURM_JOB_ID\n", stderr);
        return false;
      }
    }
  }
  if (is_scraper || is_parent) {
    worker.hostname = (char *)malloc(HOST_NAME_MAX);
    if (gethostname(worker.hostname, HOST_NAME_MAX)) {
      perror("gethostname");
      return false;
    }
  }
  return true;
}

int main(int argc, char *argv[]) {
  atexit(finalize);
  if (getenv(PRINT_ONLY_ENV)) {
    if (!build_slurmdb_conn()) {
      return 1;
    }
    print_only();
    return 0;
  }
  if (!initialize(argc, argv)) {
    fputs("Error initializing, exiting.\n", stderr);
    return 1;
  }
  if (is_server) {
    build_sqlite_conn();
  }
  if (argc == 1) {
    if (is_scraper) {
      scraper();
    } else if (distribute_node_watcher_only) {
      node_watcher_distributor(argv[0]);
    } else {
      watcher();
    }
  } else {
    // spawn subprocess with args starting argv[1] and record progressive data
  }
  return 0;
}

static void print_only() {
  void *slurmconn = slurm_conn;
  
  auto jobcond = (slurmdb_job_cond_t *)calloc(1, sizeof(slurmdb_job_cond_t));
  jobcond->flags |= JOBCOND_FLAG_NO_TRUNC;
  jobcond->db_flags = SLURMDB_JOB_FLAG_NOTSET;
  List joblist = slurmdb_jobs_get(slurmconn, jobcond);
  ListIterator jobit = slurm_list_iterator_create(joblist);

  while (const auto job = (slurmdb_job_rec_t *)slurm_list_next(jobit)) {
    const auto steplist = job->steps;
    const auto stepcnt = slurm_list_count(steplist);
    printf("[%d] %s: %d step%s on %s ",
      job->jobid,
      job->jobname,
      stepcnt,
      stepcnt > 1 ? "s" : "",
      job->nodes);
    ListIterator stepit = slurm_list_iterator_create(steplist);
    tres_t tres_in;
    tres_t tres_out;
    job->tot_cpu_sec = job->tot_cpu_usec
      = job->sys_cpu_sec = job->sys_cpu_usec
      = job->user_cpu_sec = job->user_cpu_usec = 0;
    const auto print_start_end_pair = [](time_t *start, time_t *end) {
      constexpr auto time_len = strlen("Www Mmm dd hh:mm:ss yyyy");
      char *p = ctime(start);
      p[time_len] = '\0';
      printf("[ %s - ", p);
      p = ctime(end);
      p[time_len] = '\0';
      printf("%s ]", p);
    };
    print_start_end_pair(&job->start, &job->end);
    printf("\n  SubmitLine = %s\n", job->submit_line);
    while (const auto step = (slurmdb_step_rec_t *)slurm_list_next(stepit)) {
      if (step->step_id.step_id <= SLURM_MAX_NORMAL_STEP_ID) {
        printf("  %d \n", step->step_id.step_id);
        print_start_end_pair(&step->start, &step->end);
        printf(" : %s\n", step->submit_line);
      }
      tres_in += tres_t(step->stats.tres_usage_in_tot);
      tres_out += tres_t(step->stats.tres_usage_out_tot);

      job->tot_cpu_sec += step->tot_cpu_sec;
      job->tot_cpu_usec += step->tot_cpu_usec;
      job->sys_cpu_sec += step->sys_cpu_sec;
      job->sys_cpu_usec += step->sys_cpu_usec;
      job->user_cpu_sec += step->user_cpu_sec;
      job->user_cpu_usec += step->user_cpu_usec;
    }
    printf("TRES_IN => ");
    tres_in.print();
    printf("TRES_OUT => ");
    tres_out.print();
    printf("[Total] %ld.%ld [Sys/User] %.2lf%% [Sys] %ld.%ld  [User] %ld.%ld\n",
      job->tot_cpu_sec, job->tot_cpu_usec,
      (job->sys_cpu_sec * 1'000'000.0 + job->sys_cpu_usec) /
      (job->user_cpu_sec * 1'000'000.0 + job->user_cpu_usec + 1.0) * 100,
      job->sys_cpu_sec, job->sys_cpu_usec,
      job->user_cpu_sec, job->user_cpu_usec
    );
    slurm_list_iterator_destroy(stepit);
  }

  slurm_list_iterator_destroy(jobit);
  slurm_list_destroy(joblist);
}
