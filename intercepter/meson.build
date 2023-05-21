src = files([
  'src/interceptlib.cpp',
  'src/debugreg.c',
])

proc_map_parser = get_option('proc_map_parser')

incdir = [
  'include',
  '../include',
#  proc_map_parser + '/include',
]

initfunc = 'initlib'

name = 'turingintercept'
# Will be overwritten, just interested in the object files
objs = shared_library('_tmp', src, link_args: ['-ldl'], cpp_args: ['-c'],
                      include_directories: incdir, build_by_default: false)
warning(meson.get_compiler('cpp').cmd_array())
                
objs = objs.extract_all_objects (recursive: true)
compiler = meson.get_compiler('cpp').cmd_array()[0]
custom_target (name, output: 'lib'+name+'.so', input: objs,
               command: [compiler,
                         '@INPUT@',
                         '-ldl', '-lpthread', '-shared',
#                         proc_map_parser + '/lib/libprocmaps.a',
                         '-o', '@OUTPUT@'],
               build_by_default: true)