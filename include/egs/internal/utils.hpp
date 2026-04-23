#ifndef EGS_INTERNAL_UTILS_HPP
#define EGS_INTERNAL_UTILS_HPP

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

} // namespace egs::internal::utils

#endif
