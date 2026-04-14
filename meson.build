project(
  'egs',
  'cpp',
  version : '0.0.1',
  default_options : ['cpp_std=c++20']
)

inc = include_directories('include')

absl_inlined_vector = dependency(
  'absl_inlined_vector',
  required : true
)

absl_flat_hash_map = dependency(
  'absl_flat_hash_map',
  required : true
)

egs_lib = both_libraries(
  'egs',
  'src/egs.cpp',
  'src/internal/dsu.cpp',
  include_directories : inc,
  dependencies : [absl_inlined_vector, absl_flat_hash_map],
  install : true
)

egs_dep = declare_dependency(
  link_with : egs_lib,
  include_directories : inc,
  dependencies : [absl_inlined_vector, absl_flat_hash_map]
)

subdir('test')
