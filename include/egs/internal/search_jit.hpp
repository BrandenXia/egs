#ifndef EGS_INTERNAL_SEARCH_JIT_HPP
#define EGS_INTERNAL_SEARCH_JIT_HPP

#include <cstdint>
#include <vector>

namespace egs::internal {

// ---------------------------------------------------------------------------
// C-compatible ABI shared between C++ callers and JIT-generated code.
// The layout of every struct must be identical on both sides.
// ---------------------------------------------------------------------------

/// One row in a type-erased op-index table.
/// `args` points to pre-canonicalised child eclass IDs (owned externally).
/// Layout on x86-64: { i32 @ 0, i32 @ 4, ptr @ 8 } → sizeof = 16
struct JitTableEntry {
  uint32_t eclass_id;
  uint32_t num_args;
  const uint32_t *args;
};

/// All rows for one operator.
/// Layout on x86-64: { ptr @ 0, i32 @ 8, i32 @ 12 } → sizeof = 16
struct JitTable {
  const JitTableEntry *entries;
  uint32_t num_entries;
  uint32_t _pad; ///< explicit padding so sizeof matches LLVM struct layout
};

/// Callback invoked by the JIT function for each complete match.
using JitEmitFn = void (*)(void *ctx, uint32_t root_eclass,
                            const uint32_t *subst, uint32_t subst_len);

/// Signature of every JIT-compiled search function.
/// `tables`  – one JitTable per LookUpOp instruction in program order.
/// `emit_fn` – called once per match.
/// `ctx`     – opaque pointer forwarded unchanged to emit_fn.
using JitFn = void (*)(const JitTable *tables, JitEmitFn emit_fn, void *ctx);

// ---------------------------------------------------------------------------
// Type-erased instruction representation (mirrors Inst<Op> but Op is gone).
// ---------------------------------------------------------------------------
struct JitInst {
  enum Type { LookUpOp = 0, BindArg = 1, Compare = 2 } type;
  uint32_t table_idx;      ///< LookUpOp only: which entry in the tables[] array
  uint32_t out_node_reg;   ///< LookUpOp: node-pointer register slot
  uint32_t out_eclass_reg; ///< LookUpOp / BindArg / Compare dest
  uint32_t in_node_reg;    ///< BindArg: source node-pointer register
  uint32_t in_eclass_reg;  ///< Compare: second operand register
  uint32_t child_idx;      ///< BindArg: which child of the node
};

// ---------------------------------------------------------------------------
// JIT engine handle (opaque).
// ---------------------------------------------------------------------------
struct SearchJit;

/// Return the process-wide JIT engine, creating it on first call (thread-safe).
/// Returns nullptr when EGS_ENABLE_JIT is not defined or LLVM init fails.
SearchJit *get_search_jit();

/// Look up or compile a JIT function for the given type-erased program.
/// `root_eclass_reg` and `var_regs` are embedded as constants in the generated
/// code.  Returns nullptr on failure; the caller falls back to the interpreter.
JitFn get_or_compile(SearchJit *jit, const std::vector<JitInst> &program,
                     uint32_t root_eclass_reg,
                     const std::vector<uint32_t> &var_regs);

} // namespace egs::internal

#endif // EGS_INTERNAL_SEARCH_JIT_HPP
