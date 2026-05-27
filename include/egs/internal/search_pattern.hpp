#ifndef EGS_INTERNAL_SEARCH_PATTERN_HPP
#define EGS_INTERNAL_SEARCH_PATTERN_HPP

#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "egs/internal/common.hpp"

namespace egs {

// forward declarations to avoid circular dependencies
template <Operator Op>
struct EGraph;
template <Operator Op>
struct Pattern;
template <Operator Op>
struct Match;

} // namespace egs

namespace egs::internal {

template <Operator Op>
struct CompiledPattern;

template <Operator Op>
CompiledPattern<Op> compile_pattern(const Pattern<Op> &pat);

} // namespace egs::internal

namespace egs::internal {

using Reg = std::uint32_t;

template <Operator Op>
struct Inst {
  enum Type { LookUpOp, BindArg, Compare } type;
  Op op;
  Reg out_node_reg;
  Reg out_eclass_reg;
  Reg in_node_reg;
  Reg in_eclass_reg;
  int child_idx;
};

template <Operator Op>
struct PatternCompiler {
  std::vector<Inst<Op>> program;
  Reg next_id_reg = 0;
  Reg next_node_reg = 0;
  Reg root_eclass_reg = 0;
  std::unordered_map<uint32_t, Reg> var2reg;

  void compile(const typename Pattern<Op>::Node &node,
               std::optional<Reg> expected_id_reg = std::nullopt);
};

template <Operator Op>
struct CompiledPattern {
  std::vector<Inst<Op>> program;
  Reg root_eclass_reg;
  std::vector<Reg> var_regs;
};

constexpr auto MAX_REGS = 16;

template <Operator Op>
struct MachineState {
  int pc = 0;
  std::size_t table_iter_idx = 0;
  std::array<Id, MAX_REGS> id_regs = {};
  std::array<const internal::ENode<Op> *, MAX_REGS> node_regs = {};
};

template <Operator Op>
std::vector<Match<Op>> search_relational(const EGraph<Op> &egraph,
                                         const std::vector<Inst<Op>> &program,
                                         Reg root_eclass_reg,
                                         const std::vector<Reg> &var_regs) {
  std::vector<Match<Op>> results;
  results.reserve(16);

  using TableType =
      std::remove_reference_t<decltype(egraph.op_index.begin()->second)>;
  std::vector<const TableType *> resolved_tables(program.size(), nullptr);
  for (size_t i = 0; i < program.size(); ++i)
    if (program[i].type == Inst<Op>::LookUpOp) {
      auto it = egraph.op_index.find(program[i].op);
      if (it != egraph.op_index.end() && !it->second.empty())
        resolved_tables[i] = &it->second;
    }

  std::vector<MachineState<Op>> stack;
  stack.reserve(program.size() * 2);
  stack.push_back(MachineState<Op>{});

  while (!stack.empty()) {
    auto &state = stack.back();

    if (state.pc == program.size()) {
      Match<Op> match;
      match.eclass = state.id_regs[root_eclass_reg];

      match.subst.reserve(var_regs.size());
      for (Reg r : var_regs)
        match.subst.push_back(state.id_regs[r]);

      results.push_back(std::move(match));
      stack.pop_back();
      continue;
    }

    const auto &inst = program[state.pc];
    switch (inst.type) {
    case Inst<Op>::LookUpOp: {
      const auto *table_ptr = resolved_tables[state.pc];

      if (!table_ptr || state.table_iter_idx >= table_ptr->size()) {
        stack.pop_back();
        break;
      }

      const auto &[eclass_id, node] = (*table_ptr)[state.table_iter_idx];

      state.table_iter_idx++;

      MachineState<Op> child_state = state;
      child_state.id_regs[inst.out_eclass_reg] = eclass_id;
      child_state.node_regs[inst.out_node_reg] = &node;
      child_state.pc++;
      child_state.table_iter_idx = 0;

      stack.push_back(std::move(child_state));
      break;
    }
    case Inst<Op>::BindArg: {
      const internal::ENode<Op> *node = state.node_regs[inst.in_node_reg];
      Id arg_eclass = egraph.find(node->args[inst.child_idx]);

      state.id_regs[inst.out_eclass_reg] = arg_eclass;
      state.pc++;
      break;
    }
    case Inst<Op>::Compare: {
      Id id1 = state.id_regs[inst.out_eclass_reg];
      Id id2 = state.id_regs[inst.in_eclass_reg];

      if (id1 != id2)
        stack.pop_back();
      else
        state.pc++;

      break;
    }
    }
  }

  return results;
}

template <Operator Op>
void PatternCompiler<Op>::compile(const typename Pattern<Op>::Node &node,
                                  std::optional<Reg> expected_id_reg) {
  if (std::holds_alternative<Var>(node.payload)) {
    uint32_t var_id = std::get<Var>(node.payload).val;

    auto it = var2reg.find(var_id);
    if (it != var2reg.end())
      program.push_back({
        .type = Inst<Op>::Compare,
        .out_eclass_reg = expected_id_reg.value(),
        .in_eclass_reg = it->second,
      });
    else
      var2reg[var_id] = expected_id_reg.value();
  } else {
    Op op = std::get<Op>(node.payload);
    Reg node_reg = next_node_reg++;
    Reg id_reg = next_id_reg++;

    program.push_back(
        {.type = Inst<Op>::LookUpOp,
         .op = op,
         .out_node_reg = node_reg,
         .out_eclass_reg = id_reg});

    if (expected_id_reg.has_value())
      program.push_back(
          {.type = Inst<Op>::Compare,
           .out_eclass_reg = expected_id_reg.value(),
           .in_eclass_reg = id_reg});
    else
      root_eclass_reg = id_reg;

    for (size_t i = 0; i < node.args.size(); i++) {
      Reg child_id_reg = next_id_reg++;
      program.push_back({
        .type = Inst<Op>::BindArg,
        .out_eclass_reg = child_id_reg,
        .in_node_reg = node_reg,
        .child_idx = (int)i,
      });
      compile(node.args[i], child_id_reg);
    }
  }
}

template <Operator Op>
CompiledPattern<Op> compile_pattern(const Pattern<Op> &pat) {
  if (std::holds_alternative<Var>(pat.root.payload))
    throw std::invalid_argument("Pattern root cannot be a variable");

  PatternCompiler<Op> compiler;
  compiler.compile(pat.root);

  std::uint32_t max_var_id = 0;
  for (const auto &[var_id, reg] : compiler.var2reg)
    if (var_id > max_var_id) max_var_id = var_id;

  std::vector<Reg> var_regs(max_var_id + 1, 0);
  for (const auto &[var_id, reg] : compiler.var2reg)
    var_regs[var_id] = reg;

  return CompiledPattern<Op>{
    .program = std::move(compiler.program),
    .root_eclass_reg = compiler.root_eclass_reg,
    .var_regs = std::move(var_regs)};
}

} // namespace egs::internal

#endif
