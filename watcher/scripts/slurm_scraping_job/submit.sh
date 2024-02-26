#!/bin/bash
WATCHER="./turingwatch"
export TURING_WATCH_PORT=3755
{ "$WATCHER" > svr.out.$SLURM_JOB_ID 2>&1 || true && scancel $SLURM_JOB_ID; } &
{ TURING_WATCH_DISTRIBUTE_NODE_WATCHER_ONLY=1 TURING_WATCH_DB_HOST=$(hostname) TURING_WATCH_BRIGHT_URL_BASE=https://somedomain:8081/or/remove/when/unneeded TURING_WATCH_NO_CHECK_SSL_CERT=1 SLURM_CGROUP_MOUNT_POINT=$(scontrol show config | grep CgroupMountpoint | cut -d= -f2 | tr -d ' ') "$WATCHER" > client.out.$SLURM_JOB_ID 2>&1 || true && scancel $SLURM_JOB_ID; } &
wait $(jobs -p)
