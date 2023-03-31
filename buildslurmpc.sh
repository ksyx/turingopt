#!/bin/bash
set -eu
getstring() {
  if [[ $# -ne 3 ]]; then
    echo "Usage: getstring <slurm-module-name> <env-var-name> <prefix>"
    exit 1
  fi
  module show $1 | head -n -1 | tail -n +4 | awk '{print $2,$3}' | grep "^$2 " | cut -f 2 -d\ | awk "{printf \"-$3%s \", \$1 }"
}
if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <slurm-module-name>"
  exit 1
fi
module load $1
link=$(getstring $1 'LIBRARY_PATH' 'L')
inc=$(getstring $1 'CPATH' 'I')
version=$(sacct --version)
name=$(echo "${version}" | cut -f1 -d\ )
version=$(echo "${version}" | cut -f2 -d\ )
cat << EOF
Cflags: ${inc}
Libs: ${link} -lslurm
Description: Slurm API
Name: ${name}
Version: ${version}
EOF
