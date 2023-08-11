project('turingopt', ['c', 'cpp'], default_options: [
  'buildtype=debugoptimized',
  'bindir=bin',
  'cpp_std=c++17', 'c_std=c11'])

slurm = dependency('slurm')
sqlite = dependency('sqlite3')

# select from ['nvml', 'none']
gpu_measurement_source = 'nvml'

subdir('watcher')
subdir('intercepter')
