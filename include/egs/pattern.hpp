#ifndef EGS_PATTERN_HPP
#define EGS_PATTERN_HPP

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <variant>

#include "egs/internal/common.hpp"
#include "egs/internal/search_pattern.hpp"

namespace egs {

template <Operator Op> struct EGraph;

template <Operator Op> struct Pattern {
  struct Node {
    std::variant<Var, Op> payload;
    std::vector<Node> args;
  };

  Node root;

  static Pattern var(std::uint32_t var_id) { return {Node{Var{var_id}, {}}}; }

  static constexpr Pattern op(Op operation,
                              std::initializer_list<Pattern> args) {
    Node n{operation, {}};
    for (auto &arg : args)
      n.args.push_back(arg.root);
    return {n};
  }

  template <typename Fn>
    requires std::is_invocable_r_v<Op, Fn, std::string_view>
  static constexpr Pattern parse(std::string_view str, Fn parse_op);
};

template <Operator Op>
using DynamicApplier = std::function<Id(EGraph<Op> &, std::span<const Id>)>;

struct RunConfig {
  int max_iterations = 100;
  size_t node_limit = 100000;
};

enum class StopReason {
  Saturated,
  IterationLimit,
  NodeLimit,
};

template <Operator Op> struct RwRule;
template <Operator Op>
StopReason run(EGraph<Op> &egraph, std::span<const RwRule<Op>> rules,
               RunConfig config = {});

template <Operator Op> struct RwRule {
public:
  using Searcher = Pattern<Op>;
  using Applier = std::variant<Pattern<Op>, DynamicApplier<Op>>;

  constexpr RwRule(std::string_view n, Searcher s, Applier a)
      : RwRule(std::optional<std::string>(n), std::move(s), std::move(a)) {}
  constexpr RwRule(Searcher s, Applier a)
      : RwRule(std::nullopt, std::move(s), std::move(a)) {}
  constexpr RwRule(std::optional<std::string> n, Searcher s, Applier a)
      : name(n), searcher(std::move(s)), applier(std::move(a)),
        compiled_searcher(internal::compile_pattern(searcher)) {}

private:
  std::optional<std::string> name;
  Searcher searcher;
  Applier applier;

  internal::CompiledPattern<Op> compiled_searcher;

  friend StopReason run<Op>(EGraph<Op> &egraph,
                            std::span<const RwRule<Op>> rules,
                            RunConfig config);
};

template <Operator Op> struct Match {
  Id eclass;
  absl::InlinedVector<Id, 4> subst;
};

template <Operator Op> struct RuleMatches {
  const RwRule<Op> *rule;
  std::vector<Match<Op>> matches;
};

} // namespace egs

namespace egs {

namespace internal {

template <Operator Op>
Id add_pattern(EGraph<Op> &egraph, const typename Pattern<Op>::Node &node,
               std::span<const Id> subst) {
  if (std::holds_alternative<Var>(node.payload)) {
    Var v = std::get<Var>(node.payload);
    return egraph.find(subst[v.val]);
  }

  Op op = std::get<Op>(node.payload);
  absl::InlinedVector<Id, 4> arg_ids;
  for (const auto &arg : node.args)
    arg_ids.push_back(add_pattern(egraph, arg, subst));

  return egraph.add(op, arg_ids);
}

template <Operator Op>
Id add_pattern(EGraph<Op> &egraph, const Pattern<Op> &pat,
               std::span<const Id> subst) {
  return add_pattern(egraph, pat.root, subst);
}

} // namespace internal

template <Operator Op>
StopReason run(EGraph<Op> &egraph, std::span<const RwRule<Op>> rules,
               RunConfig config) {
  for (int iter = 0; iter < config.max_iterations; iter++) {
    std::vector<RuleMatches<Op>> all_matches;

    for (const auto &rule : rules) {
      auto matches =
          internal::search_relational(egraph, rule.compiled_searcher.program,
                                      rule.compiled_searcher.root_eclass_reg,
                                      rule.compiled_searcher.var_regs);
      if (!matches.empty())
        all_matches.push_back(RuleMatches<Op>{&rule, std::move(matches)});
    }

    if (all_matches.empty())
      return StopReason::Saturated;

    bool graph_changed = false;

    for (const auto &[rule, matches] : all_matches)
      for (const auto &match : matches) {
        Id nid = std::visit(
            [&egraph, &match](const auto &applier) {
              using T = std::decay_t<decltype(applier)>;
              if constexpr (std::is_same_v<T, Pattern<Op>>)
                return internal::add_pattern(egraph, applier, match.subst);
              else
                return applier(egraph, match.subst);
            },
            rule->applier);

        if (egraph.merge(match.eclass, nid))
          graph_changed = true;
      }

    if (graph_changed)
      egraph.rebuild();
    else
      return StopReason::Saturated;

    if (egraph.total_nodes() > config.node_limit)
      return StopReason::NodeLimit;
  }

  return StopReason::IterationLimit;
}

} // namespace egs

#endif
