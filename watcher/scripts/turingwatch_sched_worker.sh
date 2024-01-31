#!/bin/bash
# RUN THIS VIA SCHEDULED SBATCH (--begin) OR ONESHOT SYSTEMD
source /etc/profile
module load slurm

set -eu
# Make sure path is accessible by compute nodes
# when sbatch is being used in pipeline
TOOLROOT="/home/someuser/turingwatch"
SCRIPTDIR="$TOOLROOT/scripts"
WORKINGDIR="$TOOLROOT/tmp"
RESULTDIR="/opt/turingwatch/analysis_result"
cd "$RESULTDIR"
mkdir -p "$WORKINGDIR"
TMPDIR=$(mktemp -d -p "$WORKINGDIR")
chmod 700 $TMPDIR
PERIOD=$(ls -tr *.tar.gz | tail -n 1 | cut -d. -f1)
TARBALL="$PERIOD.tar.gz"
cp "$TARBALL" "$TMPDIR"

cd "$TMPDIR"
mkdir "$PERIOD" && cd "$PERIOD"
tar -xf "../$TARBALL"

# PIPELINES
echo "$(date) $PERIOD"

# HTML Generation
"$SCRIPTDIR/gen_html.sh" || true
cp -r html/ "$RESULTDIR/$PERIOD"'_html'

# Send email
sbatch --wait "$SCRIPTDIR/sendmail.sh"
echo "sbatch:" && cat slurm-*.out
rm -rf $TMPDIR && (rmdir "$WORKINGDIR" || true)
