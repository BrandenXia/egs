// search_jit.cpp – LLVM ORC JIT backend for egs relational pattern search.
//
// When EGS_ENABLE_JIT is not defined, thin stub implementations are provided
// so that the rest of the library links without LLVM.

#include "egs/internal/search_jit.hpp"

// ============================================================
//  Stub path – no LLVM
// ============================================================
#ifndef EGS_ENABLE_JIT

namespace egs::internal {
SearchJit *get_search_jit() { return nullptr; }

JitFn get_or_compile(SearchJit *, const std::vector<JitInst> &, uint32_t,
                     const std::vector<uint32_t> &) {
  return nullptr;
}
} // namespace egs::internal

#else // EGS_ENABLE_JIT

// ============================================================
//  LLVM ORC JIT path
// ============================================================

#include <llvm/Config/llvm-config.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>

#include <atomic>
#include <mutex>
#include <unordered_map>

namespace egs::internal {

// ---------------------------------------------------------------------------
//  Constants
// ---------------------------------------------------------------------------

/// Maximum number of virtual registers the JIT will allocate.
/// Patterns that require more registers fall back to the interpreter.
static constexpr uint32_t JIT_MAX_REGS = 32;

// ---------------------------------------------------------------------------
//  IR code generator
//
//  Generates a function with C signature:
//    void fn(const JitTable *tables, JitEmitFn emit_fn, void *ctx)
//
//  The function executes a nest of for-loops, one per LookUpOp instruction:
//
//    for (i0 in table[0]):
//      regs[out_eclass_reg0] = table[0][i0].eclass_id
//      node_args[out_node_reg0] = table[0][i0].args
//      ... BindArg / Compare between loop-0 and loop-1 ...
//      for (i1 in table[1]):
//        regs[out_eclass_reg1] = table[1][i1].eclass_id
//        ... Compare, BindArg ...
//        emit(ctx, regs[root], subst, num_vars)
//
// ---------------------------------------------------------------------------

static llvm::Function *
build_jit_function(llvm::Module &M, const std::vector<JitInst> &program,
                   uint32_t root_eclass_reg,
                   const std::vector<uint32_t> &var_regs,
                   const std::string &fn_name) {
  auto &ctx = M.getContext();

  // ---- LLVM types ----
  auto *i32 = llvm::Type::getInt32Ty(ctx);
  auto *i64 = llvm::Type::getInt64Ty(ctx);
  auto *void_ty = llvm::Type::getVoidTy(ctx);
  // In LLVM 16+ opaque pointers are the default; we use a single ptr type.
  auto *ptr_ty = llvm::PointerType::getUnqual(ctx);

  // struct JitTableEntry { uint32_t eclass_id; uint32_t num_args; const uint32_t *args; }
  // x86-64 layout: { i32@0, i32@4, ptr@8 }, sizeof = 16
  auto *entry_ty = llvm::StructType::get(ctx, {i32, i32, ptr_ty});

  // struct JitTable { const JitTableEntry *entries; uint32_t num_entries; uint32_t _pad; }
  // x86-64 layout: { ptr@0, i32@8, i32@12 }, sizeof = 16
  auto *table_ty = llvm::StructType::get(ctx, {ptr_ty, i32, i32});

  // JitEmitFn: void(void* ctx, i32 root, ptr subst, i32 n)
  auto *emit_fn_ty =
      llvm::FunctionType::get(void_ty, {ptr_ty, i32, ptr_ty, i32}, false);

  // Main function: void(ptr tables, ptr emit_fn, ptr ctx)
  auto *fn_ty =
      llvm::FunctionType::get(void_ty, {ptr_ty, ptr_ty, ptr_ty}, false);
  auto *fn = llvm::Function::Create(fn_ty, llvm::Function::ExternalLinkage,
                                    fn_name, M);
  fn->setCallingConv(llvm::CallingConv::C);

  auto *arg_tables = fn->getArg(0);
  arg_tables->setName("tables");
  auto *arg_emit = fn->getArg(1);
  arg_emit->setName("emit");
  auto *arg_ctx = fn->getArg(2);
  arg_ctx->setName("ctx");

  // ---- Safety guard: register bounds ----
  for (const auto &inst : program) {
    if (inst.out_eclass_reg >= JIT_MAX_REGS ||
        inst.in_eclass_reg >= JIT_MAX_REGS ||
        inst.out_node_reg >= JIT_MAX_REGS ||
        inst.in_node_reg >= JIT_MAX_REGS) {
      fn->eraseFromParent();
      return nullptr;
    }
  }

  llvm::IRBuilder<> B(ctx);

  // ---- Count LookUpOps ----
  int num_lookups = 0;
  for (const auto &inst : program)
    if (inst.type == JitInst::LookUpOp)
      ++num_lookups;

  // ---- Entry block: allocate scratch storage ----
  auto *entry_bb = llvm::BasicBlock::Create(ctx, "entry", fn);
  B.SetInsertPoint(entry_bb);

  auto *regs_alloca = B.CreateAlloca(llvm::ArrayType::get(i32, JIT_MAX_REGS),
                                     nullptr, "regs");
  auto *node_args_alloca =
      B.CreateAlloca(llvm::ArrayType::get(ptr_ty, JIT_MAX_REGS), nullptr,
                     "node_args");

  // One alloca'd counter per loop level (reset to 0 when entering the loop).
  std::vector<llvm::Value *> counter_alloca(num_lookups, nullptr);
  for (int k = 0; k < num_lookups; ++k)
    counter_alloca[k] =
        B.CreateAlloca(i32, nullptr, "cnt_" + std::to_string(k));

  // Alloca for the substitution array (emit site).
  llvm::Value *subst_alloca = nullptr;
  uint32_t nv = static_cast<uint32_t>(var_regs.size());
  if (nv > 0)
    subst_alloca =
        B.CreateAlloca(llvm::ArrayType::get(i32, nv), nullptr, "subst");

  // Pre-load table entries pointers and sizes into SSA values in entry block.
  // This avoids repeated loads inside the loop bodies.
  std::vector<llvm::Value *> tbl_entries(num_lookups);
  std::vector<llvm::Value *> tbl_num(num_lookups);
  {
    int k = 0;
    for (const auto &inst : program) {
      if (inst.type != JitInst::LookUpOp)
        continue;
      auto *tp = B.CreateInBoundsGEP(table_ty, arg_tables, B.getInt64(k),
                                     "tblp_" + std::to_string(k));
      auto *ep = B.CreateStructGEP(table_ty, tp, 0,
                                   "ep_" + std::to_string(k));
      tbl_entries[k] =
          B.CreateLoad(ptr_ty, ep, "entries_" + std::to_string(k));
      auto *np = B.CreateStructGEP(table_ty, tp, 1,
                                   "np_" + std::to_string(k));
      tbl_num[k] = B.CreateLoad(i32, np, "num_" + std::to_string(k));
      ++k;
    }
  }

  // ---- Create basic blocks for each loop level ----
  std::vector<llvm::BasicBlock *> lh(num_lookups); // header
  std::vector<llvm::BasicBlock *> lb(num_lookups); // body
  std::vector<llvm::BasicBlock *> li(num_lookups); // increment
  std::vector<llvm::BasicBlock *> le(num_lookups); // exit
  for (int k = 0; k < num_lookups; ++k) {
    lh[k] = llvm::BasicBlock::Create(ctx, "lh_" + std::to_string(k), fn);
    lb[k] = llvm::BasicBlock::Create(ctx, "lb_" + std::to_string(k), fn);
    li[k] = llvm::BasicBlock::Create(ctx, "li_" + std::to_string(k), fn);
    le[k] = llvm::BasicBlock::Create(ctx, "le_" + std::to_string(k), fn);
  }
  auto *ret_bb = llvm::BasicBlock::Create(ctx, "ret", fn);

  // ---- Helpers: register and node-args pointer access ----
  auto load_reg = [&](uint32_t idx) -> llvm::Value * {
    auto *ptr = B.CreateInBoundsGEP(llvm::ArrayType::get(i32, JIT_MAX_REGS),
                                    regs_alloca, {B.getInt64(0), B.getInt64(idx)});
    return B.CreateLoad(i32, ptr);
  };
  auto store_reg = [&](uint32_t idx, llvm::Value *v) {
    auto *ptr = B.CreateInBoundsGEP(llvm::ArrayType::get(i32, JIT_MAX_REGS),
                                    regs_alloca, {B.getInt64(0), B.getInt64(idx)});
    B.CreateStore(v, ptr);
  };
  auto load_nptr = [&](uint32_t idx) -> llvm::Value * {
    auto *ptr = B.CreateInBoundsGEP(
        llvm::ArrayType::get(ptr_ty, JIT_MAX_REGS), node_args_alloca,
        {B.getInt64(0), B.getInt64(idx)});
    return B.CreateLoad(ptr_ty, ptr);
  };
  auto store_nptr = [&](uint32_t idx, llvm::Value *v) {
    auto *ptr = B.CreateInBoundsGEP(
        llvm::ArrayType::get(ptr_ty, JIT_MAX_REGS), node_args_alloca,
        {B.getInt64(0), B.getInt64(idx)});
    B.CreateStore(v, ptr);
  };

  // ---- Single-pass code generation ----
  // cur_bb is the "current" insertion block. When we see a LookUpOp:
  //   • store 0 to the loop counter and branch to the loop header (terminates cur_bb)
  //   • fill loop header and the start of loop body, then switch cur_bb to lb[k]
  // Non-LookUpOp instructions are emitted into cur_bb.
  // After the last instruction we emit the match and branch to the innermost incr.

  int cur_loop = -1; // index of the deepest active LookUpOp
  llvm::BasicBlock *cur_bb = entry_bb;

  for (size_t pc = 0; pc < program.size(); ++pc) {
    const auto &inst = program[pc];
    B.SetInsertPoint(cur_bb);

    switch (inst.type) {
    case JitInst::LookUpOp: {
      ++cur_loop;
      int k = cur_loop;

      // Initialise counter k = 0 and jump to loop header.
      B.CreateStore(B.getInt32(0), counter_alloca[k]);
      B.CreateBr(lh[k]);

      // Fill loop header: load counter, branch body vs exit.
      B.SetInsertPoint(lh[k]);
      auto *cnt = B.CreateLoad(i32, counter_alloca[k], "i_" + std::to_string(k));
      auto *done = B.CreateICmpUGE(cnt, tbl_num[k]);
      B.CreateCondBr(done, le[k], lb[k]);

      // Fill loop body preamble: load the table entry for this iteration.
      B.SetInsertPoint(lb[k]);
      auto *ep = B.CreateInBoundsGEP(entry_ty, tbl_entries[k], cnt,
                                     "ep_" + std::to_string(k));
      // eclass_id = entry->eclass_id (field 0)
      auto *ec_ptr = B.CreateStructGEP(entry_ty, ep, 0);
      auto *ec_val = B.CreateLoad(i32, ec_ptr);
      store_reg(inst.out_eclass_reg, ec_val);
      // args = entry->args (field 2)
      auto *args_ptr = B.CreateStructGEP(entry_ty, ep, 2);
      auto *args_val = B.CreateLoad(ptr_ty, args_ptr);
      store_nptr(inst.out_node_reg, args_val);

      cur_bb = lb[k];
      break;
    }

    case JitInst::BindArg: {
      // regs[out_eclass_reg] = canonical_child_id = node_args[in_node_reg][child_idx]
      auto *nptr = load_nptr(inst.in_node_reg);
      auto *arg_ptr = B.CreateInBoundsGEP(i32, nptr, B.getInt32(inst.child_idx));
      store_reg(inst.out_eclass_reg, B.CreateLoad(i32, arg_ptr));
      break;
    }

    case JitInst::Compare: {
      // If regs[out_eclass_reg] != regs[in_eclass_reg], skip this iteration
      // (i.e. jump to the innermost loop's increment block).
      auto *r1 = load_reg(inst.out_eclass_reg);
      auto *r2 = load_reg(inst.in_eclass_reg);
      auto *eq = B.CreateICmpEQ(r1, r2);

      auto *cont = llvm::BasicBlock::Create(
          ctx, "cc_" + std::to_string(pc), fn);
      llvm::BasicBlock *fail_dst =
          (cur_loop >= 0) ? li[cur_loop] : ret_bb;
      B.CreateCondBr(eq, cont, fail_dst);
      cur_bb = cont;
      break;
    }
    }
  }

  // ---- Emit match at end of program ----
  B.SetInsertPoint(cur_bb);
  auto *root_ec = load_reg(root_eclass_reg);

  // Build subst array on the stack and call emit_fn.
  llvm::Value *subst_ptr;
  if (nv > 0) {
    for (uint32_t i = 0; i < nv; ++i) {
      auto *slot = B.CreateInBoundsGEP(llvm::ArrayType::get(i32, nv),
                                       subst_alloca,
                                       {B.getInt64(0), B.getInt64(i)});
      B.CreateStore(load_reg(var_regs[i]), slot);
    }
    subst_ptr = B.CreateBitOrPointerCast(subst_alloca, ptr_ty);
  } else {
    subst_ptr = llvm::ConstantPointerNull::get(
        llvm::cast<llvm::PointerType>(ptr_ty));
  }
  B.CreateCall(emit_fn_ty, arg_emit,
               {arg_ctx, root_ec, subst_ptr, B.getInt32(nv)});

  // After emitting, jump to the innermost loop's increment (or return).
  if (cur_loop >= 0)
    B.CreateBr(li[cur_loop]);
  else
    B.CreateBr(ret_bb);

  // ---- Fill increment blocks ----
  for (int k = 0; k < num_lookups; ++k) {
    B.SetInsertPoint(li[k]);
    auto *old_cnt = B.CreateLoad(i32, counter_alloca[k]);
    auto *new_cnt = B.CreateAdd(old_cnt, B.getInt32(1));
    B.CreateStore(new_cnt, counter_alloca[k]);
    B.CreateBr(lh[k]);
  }

  // ---- Fill exit blocks ----
  // When loop k's table is exhausted, go to the outer loop's increment
  // (or return if k == 0).
  for (int k = 0; k < num_lookups; ++k) {
    B.SetInsertPoint(le[k]);
    B.CreateBr(k == 0 ? ret_bb : li[k - 1]);
  }

  // ---- Return block ----
  B.SetInsertPoint(ret_bb);
  B.CreateRetVoid();

  // ---- Verify ----
  std::string err_str;
  llvm::raw_string_ostream err_os(err_str);
  if (llvm::verifyFunction(*fn, &err_os)) {
    fn->eraseFromParent();
    return nullptr;
  }

  return fn;
}

// ---------------------------------------------------------------------------
//  JIT engine: SearchJit
// ---------------------------------------------------------------------------

struct SearchJit {
  std::unique_ptr<llvm::orc::LLJIT> lljit;
  std::mutex mu;
  std::unordered_map<std::size_t, JitFn> cache;
  std::atomic<uint64_t> fn_counter{0};

  bool valid() const { return lljit != nullptr; }
};

static SearchJit *g_search_jit = nullptr;
static std::once_flag g_jit_init_flag;

SearchJit *get_search_jit() {
  std::call_once(g_jit_init_flag, []() {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();

    auto jit_or_err = llvm::orc::LLJITBuilder().create();
    if (!jit_or_err) {
      llvm::consumeError(jit_or_err.takeError());
      return;
    }
    auto *j = new SearchJit{};
    j->lljit = std::move(*jit_or_err);
    g_search_jit = j;
  });
  return g_search_jit;
}

// ---------------------------------------------------------------------------
//  Program hash (key for the compiled-function cache)
// ---------------------------------------------------------------------------

static std::size_t hash_program(const std::vector<JitInst> &prog,
                                uint32_t root,
                                const std::vector<uint32_t> &vr) {
  auto mix = [](std::size_t h, std::size_t v) -> std::size_t {
    return h ^ (v + 0x9e3779b9u + (h << 6) + (h >> 2));
  };
  std::size_t h = mix(0, root);
  h = mix(h, prog.size());
  for (const auto &i : prog) {
    h = mix(h, static_cast<std::size_t>(i.type));
    h = mix(h, i.table_idx);
    h = mix(h, i.out_node_reg);
    h = mix(h, i.out_eclass_reg);
    h = mix(h, i.in_node_reg);
    h = mix(h, i.in_eclass_reg);
    h = mix(h, i.child_idx);
  }
  h = mix(h, vr.size());
  for (auto r : vr)
    h = mix(h, r);
  return h;
}

// ---------------------------------------------------------------------------
//  get_or_compile
// ---------------------------------------------------------------------------

JitFn get_or_compile(SearchJit *jit, const std::vector<JitInst> &prog,
                     uint32_t root, const std::vector<uint32_t> &vr) {
  if (!jit || !jit->valid())
    return nullptr;

  std::size_t key = hash_program(prog, root, vr);

  // Use a single lock that covers both cache lookup and compilation to prevent
  // duplicate-symbol issues if the same pattern is compiled concurrently.
  std::lock_guard<std::mutex> lock(jit->mu);

  {
    auto it = jit->cache.find(key);
    if (it != jit->cache.end())
      return it->second;
  }

  // ---- Build LLVM module ----
  auto llvm_ctx = std::make_unique<llvm::LLVMContext>();
  auto &ctx_ref = *llvm_ctx;
  auto module = std::make_unique<llvm::Module>("egs_jit", ctx_ref);
  module->setDataLayout(jit->lljit->getDataLayout());
  module->setTargetTriple(jit->lljit->getTargetTriple().str());

  uint64_t fn_id = jit->fn_counter.fetch_add(1, std::memory_order_relaxed);
  std::string fn_name = "egs_pat_" + std::to_string(fn_id);

  if (!build_jit_function(*module, prog, root, vr, fn_name))
    return nullptr;

  // ---- Add to JIT ----
  auto tsm = llvm::orc::ThreadSafeModule(std::move(module), std::move(llvm_ctx));
  if (auto err = jit->lljit->addIRModule(std::move(tsm))) {
    llvm::consumeError(std::move(err));
    return nullptr;
  }

  // ---- Look up compiled function ----
  auto sym = jit->lljit->lookup(fn_name);
  if (!sym) {
    llvm::consumeError(sym.takeError());
    return nullptr;
  }

#if LLVM_VERSION_MAJOR >= 17
  JitFn fn_ptr = sym->toPtr<JitFn>();
#else
  JitFn fn_ptr =
      reinterpret_cast<JitFn>(static_cast<uintptr_t>(sym->getAddress()));
#endif

  jit->cache[key] = fn_ptr;
  return fn_ptr;
}

} // namespace egs::internal

#endif // EGS_ENABLE_JIT
