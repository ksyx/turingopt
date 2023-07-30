src = files([
  'src/main.cpp',
  'src/db_common.cpp',
  'src/worker.cpp',

  # SQL Definitions
  'sql/ddl.cpp',
  'sql/modify.cpp'
])

incdir = [
  'include',
  'sql',
]

executable('turingwatch', src, include_directories: incdir,
           dependencies: [slurm, sqlite],
           link_args: ['-flto', '-lpthread', '-ldl'])
