#ifndef EGS_PATTERN_HPP
#define EGS_PATTERN_HPP

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <variant>

#include "egs/egs.hpp"

namespace egs {

template <Operator Op> struct Pattern {
  struct Node {
    std::variant<Var, Op> payload;
    std::vector<Node> args;
  };

  Node root;

  static Pattern var(std::uint32_t var_id) { return {Node{Var{var_id}, {}}}; }

  static Pattern op(Op operation, std::initializer_list<Pattern> args) {
    Node n{operation, {}};
    for (auto &arg : args)
      n.args.push_back(arg.root);
    return {n};
  }

  static Pattern parse(std::string_view str,
                       std::function<Op(std::string_view)> parse_op);
};

template <Operator Op>
using DynamicApplier = std::function<Id(EGraph<Op> &, std::span<const Id>)>;

template <Operator Op> struct RwRule {
  std::string name;
  Pattern<Op> searcher;
  std::variant<Pattern<Op>, DynamicApplier<Op>> applier;
};

} // namespace egs

#endif
