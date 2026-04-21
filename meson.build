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

absl_flat_hash_set = dependency(
  'absl_flat_hash_set',
  required : true
)

egs_lib = both_libraries(
  'egs',
  'src/egs.cpp',
  'src/internal/dsu.cpp',
  include_directories : inc,
  dependencies : [absl_inlined_vector, absl_flat_hash_map, absl_flat_hash_set],
  install : true
)

egs_dep = declare_dependency(
  link_with : egs_lib,
  include_directories : inc,
  dependencies : [absl_inlined_vector, absl_flat_hash_map, absl_flat_hash_set],
)

subdir('test')

if get_option('build_examples')
  subdir('examples')
endif
