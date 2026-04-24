#ifndef EGS_PATTERN_HPP
#define EGS_PATTERN_HPP

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "egs/internal/common.hpp"
#include "egs/internal/search_pattern.hpp"

namespace egs {

template <Operator Op>
struct EGraph;

using PatternVarMap = std::unordered_map<std::string, std::uint32_t>;

template <Operator Op>
struct Pattern {
  struct Node {
    std::variant<internal::Var, Op> payload;
    std::vector<Node> args;
  };

  Node root;

  static Pattern var(std::uint32_t var_id) {
    return {Node{internal::Var{var_id}, {}}};
  }

  static constexpr Pattern op(Op operation,
                              std::initializer_list<Pattern> args) {
    Node n{operation, {}};
    for (auto &arg : args)
      n.args.push_back(arg.root);
    return {n};
  }

  template <typename Fn>
    requires std::is_invocable_r_v<Op, Fn, std::string_view>
  static constexpr Pattern parse(std::string_view str, Fn parse_op,
                                 PatternVarMap &var_map);

private:
  static constexpr std::vector<std::string_view> tokenize(std::string_view str);

  template <typename Fn>
    requires std::is_invocable_r_v<Op, Fn, std::string_view>
  static constexpr Node parse_internal(std::span<const std::string_view> tokens,
                                       std::size_t &pos, Fn parse_op,
                                       PatternVarMap &var_map);
};

inline constexpr struct no_rewrite_t {
} no_rewrite;
struct RwResult {
  std::optional<Id> id;
  RwResult(Id i) : id(i) {}
  RwResult(no_rewrite_t) : id(std::nullopt) {}
};

template <Operator Op>
using DynamicApplier = std::function<RwResult(EGraph<Op> &, const Match<Op> &)>;

struct RunConfig {
  int max_iterations = 30;
  std::size_t node_limit = 10000;
};

enum class StopReason {
  Saturated,
  IterationLimit,
  NodeLimit,
};

template <Operator Op>
struct RwRule;
template <Operator Op>
StopReason run(EGraph<Op> &egraph, std::span<const RwRule<Op>> rules,
               RunConfig config = {});

template <Operator Op>
struct RwRule {
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

  template <typename Fn>
    requires std::is_invocable_r_v<Op, Fn, std::string_view>
  static RwRule parse(std::string_view search_str, std::string_view apply_str,
                      Fn parse_op);

  template <typename ParseFn, typename ApplierGenerator>
    requires std::is_invocable_r_v<Op, ParseFn, std::string_view> &&
             std::is_invocable_r_v<DynamicApplier<Op>, ApplierGenerator,
                                   const PatternVarMap &>
  static RwRule parse(std::string_view search_str, ParseFn parse_op,
                      ApplierGenerator applier_gen);

private:
  std::optional<std::string> name;
  Searcher searcher;
  Applier applier;

  internal::CompiledPattern<Op> compiled_searcher;

  friend StopReason run<Op>(EGraph<Op> &egraph,
                            std::span<const RwRule<Op>> rules,
                            RunConfig config);
};

template <Operator Op>
struct Match {
  Id eclass;
  absl::InlinedVector<Id, 4> subst;
};

template <Operator Op>
struct RuleMatches {
  const RwRule<Op> *rule;
  std::vector<Match<Op>> matches;
};

} // namespace egs

namespace egs {

template <Operator Op>
template <typename Fn>
  requires std::is_invocable_r_v<Op, Fn, std::string_view>
constexpr Pattern<Op> Pattern<Op>::parse(std::string_view str, Fn parse_op,
                                         PatternVarMap &var_map) {
  auto tokens = tokenize(str);
  auto pos = std::size_t{0};
  auto root = parse_internal(tokens, pos, parse_op, var_map);

  if (pos < tokens.size())
    throw std::runtime_error("Unexpected tokens after parsing pattern: " +
                             std::string(tokens[pos]));

  return {root};
}

template <Operator Op>
constexpr std::vector<std::string_view>
Pattern<Op>::tokenize(std::string_view str) {
  std::vector<std::string_view> tokens;
  std::string_view current;
  for (const char &c : str)
    if (std::isspace(c)) {
      if (!current.empty()) {
        tokens.push_back(current);
        current = {};
      }
    } else if (c == '(' || c == ')') {
      if (!current.empty()) {
        tokens.push_back(current);
        current = {};
      }
      tokens.push_back({&c, 1});
    } else {
      if (current.empty())
        current = {&c, 1};
      else
        current = {current.data(), current.size() + 1};
    }
  if (!current.empty()) tokens.push_back(current);
  return tokens;
}

template <Operator Op>
template <typename Fn>
  requires std::is_invocable_r_v<Op, Fn, std::string_view>
constexpr Pattern<Op>::Node
Pattern<Op>::parse_internal(std::span<const std::string_view> tokens,
                            std::size_t &pos, Fn parse_op,
                            PatternVarMap &var_map) {
  if (pos >= tokens.size())
    throw std::runtime_error("Unexpected end of pattern");

  auto token = tokens[pos++];

  if (token == "(") {
    if (pos >= tokens.size())
      throw std::runtime_error("Unexpected end of pattern after '('");

    auto op_token = tokens[pos++];
    auto node = Node{parse_op(op_token), {}};

    while (pos < tokens.size() && tokens[pos] != ")")
      node.args.push_back(parse_internal(tokens, pos, parse_op, var_map));

    if (pos >= tokens.size() || tokens[pos] != ")")
      throw std::runtime_error("Expected ')' in pattern");

    pos++;

    return node;
  } else if (token[0] == '?') {
    auto token_str = std::string{token};
    if (var_map.find(token_str) == var_map.end())
      var_map[token_str] = static_cast<std::uint32_t>(var_map.size());
    return {internal::Var{var_map[token_str]}, {}};
  } else
    return {parse_op(token), {}};
}

template <Operator Op>
template <typename Fn>
  requires std::is_invocable_r_v<Op, Fn, std::string_view>
RwRule<Op> RwRule<Op>::parse(std::string_view search_str,
                             std::string_view apply_str, Fn parse_op) {
  auto shared_vars = PatternVarMap{};
  auto searcher = Pattern<Op>::parse(search_str, parse_op, shared_vars);
  auto applier = Pattern<Op>::parse(apply_str, parse_op, shared_vars);
  return {searcher, applier};
}

template <Operator Op>
template <typename ParseFn, typename ApplierGenerator>
  requires std::is_invocable_r_v<Op, ParseFn, std::string_view> &&
           std::is_invocable_r_v<DynamicApplier<Op>, ApplierGenerator,
                                 const PatternVarMap &>
RwRule<Op> RwRule<Op>::parse(std::string_view search_str, ParseFn parse_op,
                             ApplierGenerator applier_gen) {
  auto shared_vars = PatternVarMap{};
  auto searcher = Pattern<Op>::parse(search_str, parse_op, shared_vars);
  auto applier = applier_gen(shared_vars);
  return {searcher, applier};
}

namespace internal {

template <Operator Op>
Id add_pattern(EGraph<Op> &egraph, const typename Pattern<Op>::Node &node,
               std::span<const Id> subst) {
  if (auto *var = std::get_if<internal::Var>(&node.payload))
    return egraph.find(subst[var->val]);

  Op op = std::get<Op>(node.payload);
  absl::InlinedVector<Id, 4> arg_ids;
  arg_ids.reserve(node.args.size());
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
  egraph.rebuild();

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

    if (all_matches.empty()) return StopReason::Saturated;

    bool graph_changed = false;

    for (const auto &[rule, matches] : all_matches)
      for (const auto &match : matches) {
        Id nid = std::visit(
            [&egraph, &match](const auto &applier) {
              using T = std::decay_t<decltype(applier)>;
              if constexpr (std::is_same_v<T, Pattern<Op>>)
                return internal::add_pattern(egraph, applier, match.subst);
              else
                return applier(egraph, match).id.value_or(match.eclass);
            },
            rule->applier);

        if (egraph.merge(match.eclass, nid)) graph_changed = true;
      }

    if (graph_changed)
      egraph.rebuild();
    else
      return StopReason::Saturated;

    if (egraph.total_nodes() > config.node_limit) return StopReason::NodeLimit;
  }

  return StopReason::IterationLimit;
}

} // namespace egs

#endif
