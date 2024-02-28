#!/usr/bin/bash
#SBATCH -N 2
#SBATCH -n 32
#SBATCH -c 1
#SBATCH --mem-per-cpu 1g
#SBATCH --mincpus 2
#SBATCH -t 5:30:00

# USAGE INSTRUCTION
# 1. Change resource requirements set out above to actual values,
#    with noting that:
#    - scrapers on each node takes one task, so the number of tasks
#      required should be node count plus number of actual tasks
#    - mincpus should be set to >=2, and correspondingly number of
#      CPU cores should also at least be twice of node count
#    - when memory requirement is not specified in all individual 
#      srun in this script, set mem-per-cpu to avoid any srun from
#      being blocked
# 2. Look for CHANGETHIS comments below to specify actual workload.
#    DO NOT change part outside these regions unless necessary.
#    Changes include:
#    - Premable loading modules
#    - Script starting workload
# 3. Modify watch.sh to ensure running parameters are properly set.

set -u
INITDIR=$(pwd)
WAIT_FILENAME="$INITDIR/$SLURM_JOB_ID.notif"

# ======================== CHANGETHIS START ========================
# Specify modules to use and set parameter variables like output
# file name in this region for easy change of running setup

module load application/version
# ======================== CHANGETHIS E N D ========================

# DEVELOPER NOTE: USER MAY HAVE CHANGED DIRECTORY ABOVE!
NODE_LIST=$(scontrol show hostnames $SLURM_JOB_NODELIST)
WATCHER_SERVER_NODE=$(head -n 1 <<< "$NODE_LIST")
(cd "$INITDIR" && srun --exclusive -N $SLURM_NNODES -n $SLURM_NNODES -c 1 bash "$INITDIR/watch.sh" "$WATCHER_SERVER_NODE" "$WAIT_FILENAME") &
((SLURM_NTASKS+=-"$SLURM_NNODES"))
while [ ! -f "$WAIT_FILENAME" ]; do sleep 1; done
[ -s "$WAIT_FILENAME" ] || rm "$WAIT_FILENAME"

# ======================== CHANGETHIS START ========================
# Start execution. The srun line serves only as an example here and
# feel free to simply invoke script as in `python main.py`. Make sure
# --exclusive is specified in srun, but note that its meaning is
# different from that of sbatch or salloc.

# The option value --gres-flags=allow-task-sharing available in
# SLURM 23-11-4-1 or later may be helpful in making GPUs visible to
# watcher easily.

srun --exclusive -N $SLURM_NNODES -n $SLURM_NTASKS -c 1 --mpi=pmi2 APPLICATION

# When background jobs are involved in this section, keep their PIDs
# in a variable using $!, or alternatively use jobspecs, and wait for
# it at the end of the script. Record multiple PIDs when necessary.
#
# DO NOT use wait WITHOUT ARGUMENT! It will not terminate the script
# until timeframe allocated for this job has ran out.

# wait $!
# ======================== CHANGETHIS E N D ========================
