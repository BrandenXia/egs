#ifndef EGS_EGS_HPP
#define EGS_EGS_HPP

#include <concepts>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <vector>

#include <absl/container/flat_hash_map.h>
#include <absl/container/inlined_vector.h>

#include "egs/extract.hpp"
#include "egs/internal/common.hpp"
#include "egs/internal/dsu.hpp"
#include "egs/pattern.hpp"

namespace egs {

template <Operator Op>
struct EGraph {
public:
  Id add(Op op, std::span<Id> args);
  Id add(Op op, std::initializer_list<Id> args = {});
  bool merge(Id a, Id b);
  void rebuild();
  Id find(Id id) const;
  std::size_t total_nodes() const;
  inline internal::EClass<Op> &get_eclass(Id id) { return classes[id.val]; }

private:
  internal::dsu dsu;
  absl::flat_hash_map<internal::ENode<Op>, Id> hashcons;
  std::vector<internal::EClass<Op>> classes;
  std::vector<Id> worklist;
  absl::flat_hash_map<Op, std::vector<std::pair<Id, internal::ENode<Op>>>>
      op_index;

  friend std::vector<Match<Op>>
  internal::search_relational(const EGraph<Op> &egraph,
                              const std::vector<internal::Inst<Op>> &program,
                              internal::Reg root_eclass_reg,
                              const std::vector<internal::Reg> &var_regs);
  template <Operator Op_, typename CostType>
  friend struct Extractor;
};

// User is suppose to specialize this for their AST, with the following
// methods:
// static Op get_op(const UserAST &ast);
// static std::span<const UserAST*> get_args(const UserAST &ast);
template <typename UserAST>
struct EGraphTraits {};

template <typename UserAST, typename Op>
concept EGraphCompatible = requires(const UserAST ast) {
  requires Operator<Op>;
  { EGraphTraits<UserAST>::get_op(ast) } -> std::convertible_to<Op>;
  { EGraphTraits<UserAST>::get_args(ast) } -> std::ranges::contiguous_range;
  requires std::same_as<std::ranges::range_value_t<
                            decltype(EGraphTraits<UserAST>::get_args(ast))>,
                        const UserAST *>;
};

template <Operator Op, typename UserAST>
  requires EGraphCompatible<UserAST, Op>
Id add_tree(EGraph<Op> &egraph, const UserAST &ast);

} // namespace egs

// implmentations

namespace egs {

template <Operator Op, typename UserAST>
  requires EGraphCompatible<UserAST, Op>
Id add_tree(EGraph<Op> &egraph, const UserAST &ast) {
  using Traits = EGraphTraits<UserAST>;

  absl::InlinedVector<Id, 2> arg_ids;
  for (const auto &arg : Traits::get_args(ast))
    arg_ids.push_back(add_tree(egraph, *arg));
  return egraph.add(Traits::get_op(ast), arg_ids);
}

template <Operator Op>
Id EGraph<Op>::add(Op op, std::span<Id> args) {
  for (Id arg : args)
    arg = find(arg);

  if (auto it = hashcons.find({op, {args.begin(), args.end()}});
      it != hashcons.end())
    return it->second;

  Id id = dsu.make_set();
  internal::ENode<Op> node{op, {args.begin(), args.end()}};
  classes.emplace_back(id, decltype(internal::EClass<Op>::nodes){node},
                       decltype(internal::EClass<Op>::parents){});
  hashcons.emplace(node, id);

  for (Id arg : args)
    classes[find(arg).val].parents.emplace_back(node, id);

  return id;
}

template <Operator Op>
Id EGraph<Op>::add(Op op, std::initializer_list<Id> args) {
  auto temp = std::vector<Id>{args};
  return add(op, temp);
}

template <Operator Op>
bool EGraph<Op>::merge(Id a, Id b) {
  a = find(a), b = find(b);
  if (a == b) return false;

  Id nid = dsu.merge(a, b);
  Id old_id = (nid == a) ? b : a;
  internal::EClass<Op> &new_class = classes[nid.val],
                       &old_class = classes[old_id.val];
  new_class.nodes.insert(new_class.nodes.end(),
                         std::make_move_iterator(old_class.nodes.begin()),
                         std::make_move_iterator(old_class.nodes.end()));
  new_class.parents.insert(new_class.parents.end(),
                           std::make_move_iterator(old_class.parents.begin()),
                           std::make_move_iterator(old_class.parents.end()));
  old_class.nodes.clear();
  old_class.parents.clear();

  worklist.push_back(nid);

  return true;
}

template <Operator Op>
void EGraph<Op>::rebuild() {
  while (!worklist.empty()) {
    Id id = worklist.back();
    worklist.pop_back();
    internal::EClass<Op> &eclass = classes[id.val];

    for (auto &[node, parent_id] : eclass.parents) {
      parent_id = find(parent_id);

      auto canonical = node;
      bool changed = false;
      for (Id &arg : canonical.args) {
        Id canon = find(arg);
        if (canon != arg) {
          arg = canon;
          changed = true;
        }
      }

      if (changed) {
        hashcons.erase(node); // remove stale key only when necessary
        auto [it, inserted] = hashcons.try_emplace(canonical, parent_id);
        if (!inserted && it->second != parent_id) merge(it->second, parent_id);
      } else
        hashcons[node] = parent_id;
    }
  }

  op_index.clear();
  for (const auto &eclass : classes) {
    if (eclass.nodes.empty()) continue;
    for (const auto &node : eclass.nodes)
      op_index[node.op].emplace_back(eclass.id, node);
  }
}

template <Operator Op>
Id EGraph<Op>::find(Id id) const {
  return dsu.find(id);
}

template <Operator Op>
std::size_t EGraph<Op>::total_nodes() const {
  std::size_t count = 0;
  for (const auto &eclass : classes)
    count += eclass.nodes.size();
  return count;
}

} // namespace egs

#endif
