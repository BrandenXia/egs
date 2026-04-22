#ifndef EGS_UTILS_HPP
#define EGS_UTILS_HPP

#include "egs/internal/common.hpp"
#include "egs/pattern.hpp"

namespace egs {

template <Operator Op>
constexpr std::vector<RwRule<Op>>
make_rules(std::initializer_list<std::pair<std::string_view, std::string_view>>
               rule_strs,
           auto parse_op);

template <Operator Op>
constexpr std::vector<RwRule<Op>> make_rules(
    std::initializer_list<std::pair<
        std::string_view, std::function<DynamicApplier<Op>(
                              const typename Pattern<Op>::PaternVarMap &)>>>
        rule_strs,
    auto parse_op);

inline constexpr auto get_pattern_var(const PatternVarMap &vars,
                                      auto &&var_name);

template <typename... Args>
  requires(std::conjunction_v<std::is_convertible<Args, std::string>...>)
inline constexpr auto get_pattern_vars(const PatternVarMap &vars,
                                       Args &&...args);

inline constexpr auto get_match_id(const auto &eg, const auto &match,
                                   PatternVarMap::mapped_type var);

template <typename... Args>
  requires(std::conjunction_v<
           std::is_convertible<Args, PatternVarMap::mapped_type>...>)
inline constexpr auto get_match_ids(auto &eg, const auto &match,
                                    Args &&...args);

} // namespace egs

namespace egs {

template <Operator Op>
constexpr std::vector<RwRule<Op>>
make_rules(std::initializer_list<std::pair<std::string_view, std::string_view>>
               rule_strs,
           auto parse_op) {
  auto rules = std::vector<RwRule<Op>>{};
  for (const auto &[lhs, rhs] : rule_strs)
    rules.push_back(RwRule<Op>::parse(lhs, rhs, parse_op));
  return rules;
}

template <Operator Op>
constexpr std::vector<RwRule<Op>> make_rules(
    std::initializer_list<std::pair<
        std::string_view, std::function<DynamicApplier<Op>(
                              const typename Pattern<Op>::PaternVarMap &)>>>
        rule_strs,
    auto parse_op) {
  auto rules = std::vector<RwRule<Op>>{};
  for (const auto &[lhs, rhs] : rule_strs)
    rules.push_back(RwRule<Op>::parse(lhs, rhs, parse_op));
  return rules;
}

inline constexpr auto get_pattern_var(const PatternVarMap &vars,
                                      auto &&var_name) {
  return vars.at(var_name);
}

inline constexpr auto get_match_id(const auto &eg, const auto &match,
                                   PatternVarMap::mapped_type var) {
  return eg.find(match.subst[var]);
}

template <typename... Args>
  requires(std::conjunction_v<
           std::is_convertible<Args, PatternVarMap::mapped_type>...>)
inline constexpr auto get_match_ids(auto &eg, const auto &match,
                                    Args &&...args) {
  return std::to_array({get_match_id(eg, match, args)...});
}

template <typename... Args>
  requires(std::conjunction_v<std::is_convertible<Args, std::string>...>)
inline constexpr auto get_pattern_vars(const PatternVarMap &vars,
                                       Args &&...args) {
  return std::to_array({get_pattern_var(vars, args)...});
}

} // namespace egs

#endif
