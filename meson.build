project(
  'egs',
  'cpp',
  version : '0.0.1',
  default_options : ['cpp_std=c++20', 'b_ndebug=if-release']
)

inc = include_directories('include')

absl_hash = dependency(
  'absl_hash',
  required : true
)

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

absl_dependencies = [absl_hash, absl_inlined_vector, absl_flat_hash_map, absl_flat_hash_set]

egs_lib = both_libraries(
  'egs',
  'src/egs.cpp',
  'src/internal/dsu.cpp',
  include_directories : inc,
  dependencies : absl_dependencies,
  install : true
)

egs_dep = declare_dependency(
  link_with : egs_lib,
  include_directories : inc,
  dependencies : absl_dependencies
)

subdir('test')

if get_option('build_examples')
  subdir('examples')
endif
