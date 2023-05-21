project('turingopt', ['c', 'cpp'], default_options: [
  'buildtype=debugoptimized',
  'bindir=bin',
  'cpp_std=c++17'])

slurm = dependency('slurm')

run_command('reflect.sh', check: true)

subdir('watcher')
subdir('intercepter')
