#ifndef EGS_INTERNAL_COMMON_HPP
#define EGS_INTERNAL_COMMON_HPP

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <utility>

#include <absl/container/inlined_vector.h>
#include <absl/hash/hash.h>

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
  absl::InlinedVector<Id, 4> args;
  bool operator==(const ENode &) const = default;
};

template <Operator Op>
struct ENodeView {
  Op op;
  std::span<const Id> args;
};

template <Operator Op>
struct ENodeHash {
  using is_transparent = void;

  size_t operator()(const internal::ENode<Op> &n) const {
    return hash_impl(n.op, std::span<const Id>(n.args));
  }

  size_t operator()(const ENodeView<Op> &v) const {
    return hash_impl(v.op, v.args);
  }

private:
  struct HashHelper {
    Op op;
    std::span<const Id> args;

    template <typename H>
    friend H AbslHashValue(H h, const HashHelper &m) {
      h = H::combine(std::move(h), m.op);
      return H::combine_contiguous(std::move(h), m.args.data(), m.args.size());
    }
  };

  static size_t hash_impl(Op op, std::span<const Id> args) {
    return absl::Hash<HashHelper>{}(HashHelper{op, args});
  }
};

template <Operator Op>
struct ENodeEq {
  using is_transparent = void;

  bool operator()(const internal::ENode<Op> &a,
                  const internal::ENode<Op> &b) const {
    return a.op == b.op && a.args == b.args;
  }

  bool operator()(const internal::ENode<Op> &a, const ENodeView<Op> &b) const {
    if (a.op != b.op || a.args.size() != b.args.size()) return false;
    return std::equal(a.args.begin(), a.args.end(), b.args.begin());
  }
};

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
