src = files([
  'src/main.cpp',
  'src/db_common.cpp'
])

incdir = [
  'include',
  'sql',
  '../include'
]

executable('turingwatch', src, include_directories: incdir,
           dependencies: [slurm, sqlite])
