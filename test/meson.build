test_inc = include_directories('deps/doctest')

tests = ['basic', 'parse_pattern']
foreach t : tests
  t_exe = executable(
    t,
    t + '.cpp',
    dependencies : egs_dep,
    include_directories : test_inc
  )
  test(t, t_exe)
endforeach
