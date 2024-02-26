sbatch -c 1 --mem=4096 -t '7-00:00:00' -p long -G 0 ./submit.sh && sleep 1 && squeue -u $USER
