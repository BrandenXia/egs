#ifndef EGS_INTERNAL_COMMON_HPP
#define EGS_INTERNAL_COMMON_HPP

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>

#include <absl/container/inlined_vector.h>

namespace egs {

template <typename T>
concept Operator = requires(const T a, const T b) {
  { std::hash<T>{}(a) } -> std::convertible_to<std::size_t>;
  { a == b } -> std::same_as<bool>;
};

struct Id {
  std::uint32_t val;
  auto operator<=>(const Id &) const = default;
};

namespace internal {

struct Var {
  std::uint32_t val;
  auto operator<=>(const Var &) const = default;
};

template <Operator Op>
struct ENode {
  Op op;
  absl::InlinedVector<Id, 2> args;
  bool operator==(const ENode &) const = default;
};

template <Operator Op>
struct EClass;

template <Operator Op>
struct EClass {
  Id id;
  absl::InlinedVector<ENode<Op>, 4> nodes;
  absl::InlinedVector<std::pair<ENode<Op>, Id>, 4> parents;
};

} // namespace internal

} // namespace egs

template <>
struct std::hash<egs::Id> {
  std::size_t operator()(const egs::Id &id) const {
    return std::hash<std::uint32_t>{}(id.val);
  }
};

template <egs::Operator Op>
struct std::hash<egs::internal::ENode<Op>> {
  std::size_t operator()(const egs::internal::ENode<Op> &node) const {
    std::size_t h = std::hash<Op>{}(node.op);
    auto hash = std::hash<std::uint32_t>{};
    for (egs::Id arg : node.args)
      h ^= hash(arg.val) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
  }
};

#endif
