#include "main.h"

// Connections
sqlite3 *SQL_CONN_NAME;
void *slurm_conn;

// Watcher Metadata
char *hostname;
pid_t pid;
bool is_privileged;
worker_type_t worker_type;
slurm_step_id_t jobstep_info;

// Watcher Parameters
int watcher_id;
time_t time_range_start;
time_t time_range_end;

tres_t::tres_t(const char *tres_str) : tres_t() {
  size_t idx = 0;
  size_t *cur = &idx;
  while (*tres_str) {
    if (*tres_str >= '0' && *tres_str <= '9') {
      *cur = *cur * 10 + *tres_str - '0';
    } else if (*tres_str == ',') {
      cur = &idx;
      idx = 0;
    } else if (*tres_str == '=') {
      value[TRES_IDX(idx)] = 0;
      cur = &value[TRES_IDX(idx)];
    }
    tres_str++;
  }
}

void tres_t::print() {
  for (int i = 0; i < TRES_SIZE; i++) {
    printf("%s=%ld%c",
      reflect_tres_name(TRES_ENUM(i)),
      value[i],
      i == TRES_SIZE - 1 ? '\n' : ',');
  }
}

static inline void build_sqlite_conn() {
  char *db_path = getenv(DB_FILE_ENV);
  if (!db_path) {
    db_path = (char *)DEFAULT_DB_PATH;
  }
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
  if (!renew_watcher(REGISTER_WATCHER_SQL_RETURNING_TIMESTAMPS_AND_WATCHERID)) {
    exit(1);
  }
}

static void finalize(void) {
  if (slurm_conn && !IS_SLURM_SUCCESS(slurmdb_connection_close(&slurm_conn))) {
    slurm_perror("slurmdb_connection_close");
  }
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
  if (argc > 1) {
    worker_type = WORKER_PARENT;
  } else if (getenv(IS_SCRAPER_ENV)) {
    worker_type = WORKER_SCRAPER;
  } else {
    worker_type = WORKER_WATCHER;
  }
  pid = getpid();
  if (!is_scraper) {
    uint16_t connflag;
    slurm_init(NULL);
    slurm_conn = slurmdb_connection_get(&connflag);
    if (!slurm_conn) {
      slurm_perror("slurmdb_connection_get");
      return false;
    }
    const uid_t uid = geteuid();
    const auto is_slurm_user = [uid]() {
      slurm_conf_t *conf = NULL;
      if (!IS_SLURM_SUCCESS(slurm_load_ctl_conf(0, &conf))) {
        slurm_perror("slurm_load_ctl_conf");
        return false;
      }
      slurm_free_ctl_conf(conf);
      return uid == conf->slurm_user_id;
    };
    if (uid == 0 || is_slurm_user()) {
      // scraper could continue with fetching gpu data only
      is_privileged = true;
    }
    if (is_parent) {
      const char *jobid_env = getenv("SLURM_JOB_ID");
      const char *stepid_env = getenv("SLURM_STEP_ID");
      if (jobid_env) {
        try {
          to_integer(jobid_env, &jobstep_info.job_id);
          if (stepid_env) {
            to_integer(stepid_env, &jobstep_info.step_id);
          } else {
            jobstep_info.step_id = -1;
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
    hostname = (char *)malloc(HOST_NAME_MAX);
    if (!gethostname(hostname, HOST_NAME_MAX)) {
      perror("gethostname");
      return false;
    }
  }
  return true;
}

int main(int argc, char *argv[]) {
  atexit(finalize);
  if (!initialize(argc, argv)) {
    fputs("Error initializing, exiting.\n", stderr);
    return 1;
  }
  if (getenv(PRINT_ONLY_ENV)) {
    print_only();
    return 0;
  }
  build_sqlite_conn();
  if (argc == 1) {
    if (is_scraper) {
      // scrap data
    } else {
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
