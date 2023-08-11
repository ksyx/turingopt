require_nvml = false

gpu_src = ['src/gpu/' + gpu_measurement_source + '.cpp']
# Update comment in root meson.build when there is new measurement source
if gpu_measurement_source == 'nvml'
  require_nvml = true
elif gpu_measurement_source == 'none'
  # so it wont abort
else
  error('Unrecognized GPU measurement source ' + gpu_measurement_source)
endif

nvml = dependency('nvml', required: require_nvml)

src = files([
  'src/main.cpp',
  'src/db_common.cpp',
  'src/worker.cpp',
  'src/messaging.cpp',

  # SQL Definitions
  'sql/ddl.cpp',
  'sql/modify.cpp',

  'src/gpu/interface.cpp'
] + gpu_src)

incdir = [
  'include',
  'sql',
]

executable('turingwatch', src, include_directories: incdir,
           dependencies: [slurm, sqlite, nvml],
           link_args: ['-flto', '-lpthread', '-ldl'])
