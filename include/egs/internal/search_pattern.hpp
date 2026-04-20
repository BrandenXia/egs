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
  std::vector<MachineState<Op>> stack;

  stack.push_back(MachineState<Op>{});

  while (!stack.empty()) {
    auto state = stack.back();
    stack.pop_back();

    if (state.pc == program.size()) {
      Match<Op> match;
      match.eclass = state.id_regs[root_eclass_reg];

      for (Reg r : var_regs)
        match.subst.push_back(state.id_regs[r]);

      results.push_back(std::move(match));
      continue;
    }

    const auto &inst = program[state.pc];
    switch (inst.type) {
    case Inst<Op>::LookUpOp: {
      auto it = egraph.op_index.find(inst.op);
      if (it == egraph.op_index.end() || it->second.empty()) break;

      const auto &table = it->second;
      if (state.table_iter_idx + 1 < table.size()) {
        MachineState<Op> new_state = state;
        new_state.table_iter_idx++;
        stack.push_back(std::move(new_state));
      }

      const auto &[elass_id, node] = table[state.table_iter_idx];

      MachineState<Op> new_state = state;
      new_state.id_regs[inst.out_eclass_reg] = elass_id;
      new_state.node_regs[inst.out_node_reg] = &node;

      new_state.pc++;
      new_state.table_iter_idx = 0;
      stack.push_back(std::move(new_state));
      break;
    }
    case Inst<Op>::BindArg: {
      const internal::ENode<Op> *node = state.node_regs[inst.in_node_reg];
      Id arg_eclass = egraph.find(node->args[inst.child_idx]);

      MachineState<Op> new_state = state;
      new_state.id_regs[inst.out_eclass_reg] = arg_eclass;

      new_state.pc++;
      stack.push_back(std::move(new_state));
      break;
    }
    case Inst<Op>::Compare: {
      Id id1 = state.id_regs[inst.out_eclass_reg];
      Id id2 = state.id_regs[inst.in_eclass_reg];

      if (id1 != id2) break;

      MachineState<Op> new_state = state;
      new_state.pc++;
      stack.push_back(std::move(new_state));
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
        .in_node_reg = node_reg,
        .out_eclass_reg = child_id_reg,
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
