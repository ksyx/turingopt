module load slurm
echo "Watcher running..."
WATCHER=/some/path/to/turingwatch
# CHANGETHIS: Change port as necessary
export TURING_WATCH_PORT=5788

if [[ "$(hostname)" == "$1" ]]; then
  echo "Watcher server on node $1"
  "$WATCHER" &
  touch "$2"
fi

while true
do
  # CHANGETHIS: Change runtime parameters as necessary
  TURING_WATCH_SCRAPER=1 TURING_WATCH_DB_HOST="$1" SLURM_CGROUP_MOUNT_POINT=$(scontrol show config | grep CgroupMountpoint | cut -d= -f2 | tr -d ' ') TURING_WATCH_BRIGHT_URL_BASE=https://somedomain:8081/or/remove/when/unneeded TURING_WATCH_NO_CHECK_SSL_CERT=1 "$WATCHER"
done
