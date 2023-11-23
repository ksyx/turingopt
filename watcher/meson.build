require_nvml = false
require_libcurl = false
require_json_support = false

gpu_src = ['src/gpu/' + gpu_measurement_source + '.cpp']
# Update comment in root meson.build when there is new measurement source
if gpu_measurement_source == 'nvml'
  require_nvml = true
elif gpu_measurement_source == 'bright'
  require_libcurl = true
  require_json_support = true
elif gpu_measurement_source == 'none'
  # so it wont abort
else
  error('Unrecognized GPU measurement source ' + gpu_measurement_source)
endif

nvml = dependency('nvml', required: require_nvml)
libcurl = dependency('libcurl', required: require_libcurl)
json_support = dependency('nlohmann_json', required: require_json_support)

src = files([
  'src/main.cpp',
  'src/db_common.cpp',
  'src/worker.cpp',
  'src/messaging.cpp',
  'src/analyzer.cpp',

  'src/analyze_info.c',
  'src/analyze_mail_config.c',

  # SQL Definitions
  'sql/ddl.cpp',
  'sql/modify.cpp',
  'sql/analyze.cpp',
  'sql/migrate.cpp',

  'src/gpu/interface.cpp'
] + gpu_src)

incdir = [
  'include',
  'sql',
]

executable('turingwatch', src, include_directories: incdir,
           dependencies: [slurm, sqlite, nvml,libcurl, json_support],
           link_args: ['-flto', '-lpthread', '-ldl'])

subdir('webserver')
