srcaudit = files([
  'src/auditlib.cpp',
  'src/dlmap.cpp',
])

srcpreload = files([
  'src/preloadlib.cpp',
  'src/dlmap.cpp',
])

incdir = [
  'include',
#  proc_map_parser + '/include',
]

auditlib = 'turingaudit'
preloadlib = 'turingpreload'

compiler = meson.get_compiler('cpp').cmd_array()[0]
foreach name : [auditlib, preloadlib]
  if name == auditlib
    src = srcaudit
  elif name == preloadlib
    src = srcpreload
  else
    error('Unknown target:', name)
  endif
  # Will be overwritten, just interested in the object files
  objs = shared_library(name + '_tmp',
                        src, cpp_args: ['-c'],
                        include_directories: incdir, build_by_default: false)
  objs = objs.extract_all_objects (recursive: true)
  custom_target (name, output: 'lib'+name+'.so', input: objs,
                 command: [compiler,
                           '@INPUT@',
                           '-ldl', '-lrt', '-shared',
                           '-o', '@OUTPUT@'],
                 build_by_default: true)
endforeach
