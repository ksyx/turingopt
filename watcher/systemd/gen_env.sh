#!/bin/bash

set -eu
mkdir env
cat > env/server.env << EOF
LANG=en_US.UTF-8
EOF
cat > env/distributor.env << EOF
TURING_WATCH_DISTRIBUTE_NODE_WATCHER_ONLY=1
# COMMENT OUT this line if checking SSL cert is desired
TURING_WATCH_NO_CHECK_SSL_CERT=1
TURING_WATCH_DB_HOST='$(hostname)'
TURING_WATCH_PORT=3755
TURING_WATCH_BRIGHT_URL_BASE='https://set_this_or_remove:port'
SLURM_CGROUP_MOUNT_POINT='$(scontrol show config | grep CgroupMountpoint | cut -d= -f2 | tr -d ' ')'
EOF
