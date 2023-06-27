srcshared = [
  'src/dlmap.cpp',
  'src/mm.cpp',
  'src/mrecycle.c'
]

srcaudit = [
  'src/auditlib.cpp',
] + srcshared

srcpreload = [
  'src/preloadlib.cpp',
] + srcshared

incdir = [
  'include',
#  proc_map_parser + '/include',
]

auditlib = 'turingaudit'
preloadlib = 'turingpreload'

compiler = meson.get_compiler('cpp').cmd_array()[0]
foreach name : [auditlib, preloadlib]
  extra_cpp_args = []
  if name == auditlib
    src = files(srcaudit)
    extra_cpp_args = ['-DAUDITLIB']
  elif name == preloadlib
    src = files(srcpreload)
  else
    error('Unknown target:', name)
  endif
  # Will be overwritten, just interested in the object files
  objs = shared_library(name + '_tmp',
                        src, cpp_args: ['-c'] + extra_cpp_args,
                        include_directories: incdir, build_by_default: false)
  objs = objs.extract_all_objects (recursive: true)
  custom_target (name, output: 'lib'+name+'.so', input: objs,
                 command: [compiler,
                           '@INPUT@',
                           '-ldl', '-lrt',  '-latomic',
                           '-shared', '-flto',
                           '-o', '@OUTPUT@'],
                 build_by_default: true)
endforeach
