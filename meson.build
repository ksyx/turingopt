project('turingopt', 'cpp', default_options: [
  'buildtype=debugoptimized',
  'bindir=bin'])

slurm = dependency('slurm')

run_command('reflect.sh', check: true)

subdir('watcher')
