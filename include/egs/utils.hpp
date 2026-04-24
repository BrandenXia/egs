#ifndef EGS_UTILS_HPP
#define EGS_UTILS_HPP

#include <array>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "egs/internal/common.hpp"
#include "egs/internal/utils.hpp"
#include "egs/pattern.hpp"

namespace egs {

namespace internal {

template <typename Eg, typename Getter, typename... Args>
struct invokable_with_eg_op;

template <typename Eg, typename Getter, typename... Args>
constexpr bool invokable_with_eg_op_v =
    invokable_with_eg_op<Eg, Getter, Args...>::value_type;

} // namespace internal

using internal::utils::IsEGraph;

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

enum class ControlFlow { Continue, Break };

// side-effect scan with optional early break, `fn` should return `ControlFlow`
inline constexpr auto for_each_node(IsEGraph auto &eg, Id id, auto &&fn)
  requires(internal::invokable_with_eg_op_v<decltype(eg), decltype(fn)> &&
           std::is_same_v<typename internal::invokable_with_eg_op<
                              decltype(eg), decltype(fn)>::return_type,
                          ControlFlow>);

// extract first matching rule, `getter` should return std::optional
inline constexpr auto find_in_eclass(IsEGraph auto &eg, Id id, auto &&getter)
  requires(
      internal::invokable_with_eg_op_v<decltype(eg), decltype(getter)> &&
      internal::utils::is_optional_v<typename internal::invokable_with_eg_op<
          decltype(eg), decltype(getter)>::return_type>);

// fold over all nodes in an eclass, `fn` should return the same type as `init`
inline constexpr auto fold_eclass(IsEGraph auto &eg, Id id, auto &&init,
                                  auto &&fn)
  requires(internal::invokable_with_eg_op_v<decltype(eg), decltype(fn),
                                            decltype(init)> &&
           std::is_same_v<
               typename internal::invokable_with_eg_op<
                   decltype(eg), decltype(fn), decltype(init)>::return_type,
               decltype(init)>);

inline constexpr auto bind(auto &&...args)
  requires(internal::utils::all_but_last_are_strings<decltype(args)...>());

} // namespace egs

namespace egs {

namespace internal {

template <typename Eg, typename Getter, typename... Args>
struct invokable_with_eg_op {
  using Op = typename std::remove_cvref_t<Eg>::op_type;
  using invoke_with_t =
      std::conditional_t<sizeof(Op) <= sizeof(std::size_t), Op, const Op &>;
  static constexpr bool value_type =
      std::is_invocable_v<Getter, invoke_with_t, Args...>;
  using return_type = std::invoke_result_t<Getter, invoke_with_t, Args...>;
};

} // namespace internal

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

inline constexpr auto for_each_node(IsEGraph auto &eg, Id id, auto &&fn)
  requires(internal::invokable_with_eg_op_v<decltype(eg), decltype(fn)> &&
           std::is_same_v<typename internal::invokable_with_eg_op<
                              decltype(eg), decltype(fn)>::return_type,
                          ControlFlow>)
{
  for (const auto &node : eg.get_eclass(id).nodes)
    if (fn(node.op) == ControlFlow::Break) break;
}

inline constexpr auto find_in_eclass(IsEGraph auto &eg, Id id, auto &&getter)
  requires(
      internal::invokable_with_eg_op_v<decltype(eg), decltype(getter)> &&
      internal::utils::is_optional_v<typename internal::invokable_with_eg_op<
          decltype(eg), decltype(getter)>::return_type>)
{
  for (const auto &node : eg.get_eclass(id).nodes)
    if (auto val = getter(node.op)) return val;
  return
      typename internal::invokable_with_eg_op<decltype(eg),
                                              decltype(getter)>::return_type{};
}

inline constexpr auto fold_eclass(IsEGraph auto &eg, Id id, auto &&init,
                                  auto &&fn)
  requires(internal::invokable_with_eg_op_v<decltype(eg), decltype(fn),
                                            decltype(init)> &&
           std::is_same_v<
               typename internal::invokable_with_eg_op<
                   decltype(eg), decltype(fn), decltype(init)>::return_type,
               decltype(init)>)
{
  for (const auto &node : eg.get_eclass(id).nodes)
    init = fn(node.op, std::move(init));
  return init;
}

inline constexpr auto bind(auto &&...all_args)
  requires(internal::utils::all_but_last_are_strings<decltype(all_args)...>())
{
  constexpr std::size_t N = sizeof...(all_args);
  auto tuple_args =
      std::make_tuple(std::forward<decltype(all_args)>(all_args)...);
  auto fn = std::move(std::get<N - 1>(tuple_args));
  auto arg_names =
      [&]<std::size_t... Is>(std::index_sequence<Is...>) constexpr {
        return std::array<std::string, N - 1>{
          std::move(std::get<Is>(tuple_args))...};
      }(std::make_index_sequence<N - 1>{});

  return [fn = std::move(fn),
          arg_names = std::move(arg_names)](const PatternVarMap &vars) {
    auto arg_ids = std::apply(
        [&vars](const auto &...names) constexpr {
          return get_pattern_vars(vars, names...);
        },
        arg_names);

    return [fn, arg_ids = std::move(arg_ids)](auto &eg,
                                              const auto &match) constexpr {
      auto ids = std::apply(
          [&eg, &match](auto &&...ids) constexpr {
            return get_match_ids(eg, match,
                                 std::forward<decltype(ids)>(ids)...);
          },
          arg_ids);

      return std::apply(
          [&eg, &match, &fn](auto &&...ids) constexpr {
            return fn(eg, match, std::forward<decltype(ids)>(ids)...);
          },
          ids);
    };
  };
}

} // namespace egs

#endif
