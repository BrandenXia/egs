#ifndef EGS_EGS_HPP
#define EGS_EGS_HPP

#include <concepts>
#include <cstddef>
#include <iterator>
#include <vector>

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <absl/container/inlined_vector.h>

#include "egs/extract.hpp"
#include "egs/internal/common.hpp"
#include "egs/internal/dsu.hpp"
#include "egs/pattern.hpp"
#include <egs/utils.hpp>

namespace egs {

template <Operator Op>
struct EGraph {
public:
  Id add(Op op, decltype(internal::ENode<Op>::args) args);
  Id add(Op op, auto... args)
    requires(std::conjunction_v<std::is_convertible<Id, decltype(args)>...>);
  bool merge(Id a, Id b);
  void rebuild();
  inline Id find(Id id) const;
  std::size_t total_nodes() const;

  inline internal::EClass<Op> &get_eclass(Id id) { return classes[id.val]; }

  // scan with side-effect, `fn` should return `egs::ControlFlow`
  inline constexpr auto for_each_node(Id id, auto &&fn);
  // extract first matching rule, `fn` should return `std::optional`
  inline constexpr auto find_in_eclass(Id id, auto &&fn);
  // fold over nodes in eclass, `fn` should return the same type as `init`
  inline constexpr auto fold_eclass(Id id, auto &&init, auto &&fn);

  using op_type = Op;

private:
  internal::dsu dsu;
  absl::flat_hash_map<internal::ENode<Op>, Id> hashcons;
  std::vector<internal::EClass<Op>> classes;
  std::vector<Id> worklist;
  absl::flat_hash_set<Id> workset;
  absl::flat_hash_map<Op, std::vector<std::pair<Id, internal::ENode<Op>>>>
      op_index;

  inline void push_work(Id id, bool skip_find = false) {
    if (!skip_find) id = find(id);
    if (workset.insert(id).second) worklist.push_back(id);
  }

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

  decltype(internal::ENode<Op>::args) arg_ids;
  for (const auto &arg : Traits::get_args(ast))
    arg_ids.push_back(add_tree(egraph, *arg));
  return egraph.add(Traits::get_op(ast), arg_ids);
}

template <Operator Op>
Id EGraph<Op>::add(Op op, decltype(internal::ENode<Op>::args) args) {
  for (Id arg : args)
    arg = find(arg);

  const auto node = internal::ENode<Op>{op, args};

  if (auto it = hashcons.find(node); it != hashcons.end()) return it->second;

  Id id = dsu.make_set();

  for (Id arg : args)
    classes[arg.val].parents.emplace_back(node, id);

  classes.emplace_back(id, decltype(internal::EClass<Op>::nodes){node},
                       decltype(internal::EClass<Op>::parents){});
  hashcons.emplace(std::move(node), id);

  return id;
}

template <Operator Op>
Id EGraph<Op>::add(Op op, auto... args)
  requires(std::conjunction_v<std::is_convertible<Id, decltype(args)>...>)
{
  return add(op, decltype(internal::ENode<Op>::args){static_cast<Id>(args)...});
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

  push_work(nid, true);

  return true;
}

template <Operator Op>
void EGraph<Op>::rebuild() {
  while (!worklist.empty()) {
    Id id = worklist.back();
    worklist.pop_back();
    workset.erase(id);

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
inline Id EGraph<Op>::find(Id id) const {
  return dsu.find(id);
}

template <Operator Op>
std::size_t EGraph<Op>::total_nodes() const {
  std::size_t count = 0;
  for (const auto &eclass : classes)
    count += eclass.nodes.size();
  return count;
}

template <Operator Op>
inline constexpr auto EGraph<Op>::for_each_node(Id id, auto &&fn) {
  return egs::for_each_node(*this, id, std::forward<decltype(fn)>(fn));
}

template <Operator Op>
inline constexpr auto EGraph<Op>::find_in_eclass(Id id, auto &&fn) {
  return egs::find_in_eclass(*this, id, std::forward<decltype(fn)>(fn));
}

template <Operator Op>
inline constexpr auto EGraph<Op>::fold_eclass(Id id, auto &&init, auto &&fn) {
  return egs::fold_eclass(*this, id, std::forward<decltype(init)>(init),
                          std::forward<decltype(fn)>(fn));
}

} // namespace egs

#endif
