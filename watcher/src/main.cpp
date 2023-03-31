#include "common.h"
#include "tresdef.h"

int main() {
  uint16_t connflag;
  slurm_init(NULL);
  void *slurmconn = slurmdb_connection_get(&connflag);
  
  auto jobcond = (slurmdb_job_cond_t *)calloc(1, sizeof(slurmdb_job_cond_t));
  jobcond->flags |= JOBCOND_FLAG_NO_TRUNC;
  jobcond->db_flags = SLURMDB_JOB_FLAG_NOTSET;
  List joblist = slurmdb_jobs_get(slurmconn, jobcond);
  ListIterator jobit = slurm_list_iterator_create(joblist);

  while (const auto x = (slurmdb_job_rec_t *)slurm_list_next(jobit)) {
    const auto steplist = x->steps;
    printf("%s: %d steps on %s\n", x->jobname, slurm_list_count(steplist), x->nodes);
    ListIterator stepit = slurm_list_iterator_create(steplist);
    while (const auto y = (slurmdb_step_rec_t *)slurm_list_next(stepit)) {
      printf("  %d [%ld - %ld] : %s\n", y->step_id.step_id, y->start, y->end, y->submit_line);
      printf("    %s\n", y->stats.tres_usage_in_tot);
    }
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
