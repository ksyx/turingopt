src = files([
  'main.go',
  'server.go'
])

target = 'turingreport'

custom_target(target, input: src, output: target,
  command: ['go', 'build', '-o', '@OUTPUT@', '@INPUT@'],
  build_by_default: true)