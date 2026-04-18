#ifndef EGS_EXTRACT_HPP
#define EGS_EXTRACT_HPP

#include <concepts>
#include <vector>

#include <absl/container/inlined_vector.h>

#include "egs/internal/common.hpp"

namespace egs {

template <Operator Op>
struct EGraph;

template <Operator Op>
struct ExtractedTree {
  Op op;
  std::vector<ExtractedTree> args;
};

template <typename T, typename Op, typename CostType>
concept CostFunc =
    requires(T func, Op op, const std::vector<CostType> &arg_costs) {
      requires Operator<Op>;
      { func(op, arg_costs) } -> std::same_as<CostType>;
    };

template <Operator Op, typename CostType>
struct Extractor {
private:
  struct CostNode {
    CostType cost;
    const internal::ENode<Op> *best_node = nullptr;
  };
  std::vector<CostNode> memo;

public:
  template <CostFunc<Op, CostType> Func>
  Extractor(const EGraph<Op> &egraph, Func cost_func, CostType infinity);

  ExtractedTree<Op> extract(Id root, const EGraph<Op> &egraph) const;
};

} // namespace egs

namespace egs {

template <Operator Op, typename CostType>
template <CostFunc<Op, CostType> Func>
Extractor<Op, CostType>::Extractor(const EGraph<Op> &egraph, Func cost_func,
                                   CostType infinity) {
  memo.resize(egraph.classes.size(), {infinity, nullptr});

  bool changed = true;

  while (changed) {
    changed = false;

    for (const auto &eclass : egraph.classes) {
      if (eclass.nodes.empty()) continue;
      for (const auto &node : eclass.nodes) {
        std::vector<CostType> arg_costs;
        bool missing_child = false;

        for (Id arg_id : node.args) {
          Id canon_id = egraph.find(arg_id);
          CostType arg_cost = memo[canon_id.val].cost;

          if (arg_cost == infinity) {
            missing_child = true;
            break;
          }
          arg_costs.push_back(arg_cost);
        }

        if (missing_child) continue;

        CostType node_cost = cost_func(node.op, arg_costs);

        if (node_cost < memo[eclass.id.val].cost) {
          memo[eclass.id.val].cost = node_cost;
          memo[eclass.id.val].best_node = &node;
          changed = true;
        }
      }
    }
  }
}

template <Operator Op, typename CostType>
ExtractedTree<Op>
Extractor<Op, CostType>::extract(Id root, const EGraph<Op> &egraph) const {
  Id canon_id = egraph.find(root);
  const CostNode &best = memo[canon_id.val];
  if (!best.best_node)
    throw std::runtime_error("Cannot extract from eclass with no known cost");

  ExtractedTree<Op> tree{best.best_node->op, {}};
  for (Id arg_id : best.best_node->args)
    tree.args.push_back(extract(arg_id, egraph));

  return tree;
}

} // namespace egs

#endif
