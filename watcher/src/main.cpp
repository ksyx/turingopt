#include "main.h"

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

int main() {
  uint16_t connflag;
  slurm_init(NULL);
  void *slurmconn = slurmdb_connection_get(&connflag);
  
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
  if (slurmdb_connection_close(&slurmconn) != SLURM_SUCCESS) {
    puts("Connection failed to be closed");
    return 1;
  }
  return 0;
}
