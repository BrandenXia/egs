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

# Optional LLVM ORC JIT backend
# Automatically detected unless explicitly disabled via -Denable_jit=disabled.
# Pass -Denable_jit=enabled to make a missing LLVM a hard build error.
llvm_dep = dependency('llvm', version : '>=16',
                      modules : ['orcjit', 'native'],
                      required : get_option('enable_jit'),
                      method : 'config-tool')

jit_sources = []
jit_deps    = []
jit_args    = []

if llvm_dep.found()
  jit_sources = ['src/internal/search_jit.cpp']
  jit_deps    = [llvm_dep]
  jit_args    = ['-DEGS_ENABLE_JIT']
  message('JIT backend: enabled (LLVM ' + llvm_dep.version() + ')')
else
  jit_sources = ['src/internal/search_jit.cpp']
  message('JIT backend: disabled (LLVM not found)')
endif

egs_lib = both_libraries(
  'egs',
  'src/egs.cpp',
  'src/internal/dsu.cpp',
  jit_sources,
  include_directories : inc,
  dependencies : absl_dependencies + jit_deps,
  cpp_args : jit_args,
  install : true
)

egs_dep = declare_dependency(
  link_with : egs_lib,
  include_directories : inc,
  dependencies : absl_dependencies + jit_deps,
  compile_args : jit_args
)

subdir('test')

if get_option('build_examples')
  subdir('examples')
endif
