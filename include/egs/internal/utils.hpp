#ifndef EGS_INTERNAL_UTILS_HPP
#define EGS_INTERNAL_UTILS_HPP

#include <cstddef>
#include <optional>
#include <type_traits>

#include "egs/internal/common.hpp"

namespace egs {

template <Operator Op>
struct EGraph;

}

namespace egs::internal::utils {

template <typename T>
struct is_optional : std::false_type {};
template <typename T>
struct is_optional<std::optional<T>> : std::true_type {};
template <typename T>
inline constexpr bool is_optional_v = is_optional<T>::value;

template <typename T>
concept IsEGraph = requires { typename T::op_type; } &&
                   std::same_as<T, EGraph<typename T::op_type>>;

template <typename... Ts>
consteval bool all_but_last_are_strings() {
  if constexpr (sizeof...(Ts) == 0)
    return false;
  else if constexpr (sizeof...(Ts) == 1)
    return true;
  else
    return []<std::size_t... Is>(std::index_sequence<Is...>) {
      using Tuple = std::tuple<Ts...>;
      return (
          std::is_convertible_v<std::tuple_element_t<Is, Tuple>, std::string> &&
          ...);
    }(std::make_index_sequence<sizeof...(Ts) - 1>{});
}

} // namespace egs::internal::utils

#endif
