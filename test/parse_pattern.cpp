#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <charconv>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

#include "egs/egs.hpp"
#include "egs/extract.hpp"
#include "egs/pattern.hpp"

enum class OpCode { Add, Const };

struct MyOp {
  OpCode code;
  int val = 0; // Only used if Const
  bool operator==(const MyOp &) const = default;
};

template <>
struct std::hash<MyOp> {
  size_t operator()(const MyOp &o) const { return (size_t)o.code ^ o.val; }
};

MyOp parse_my_op(std::string_view str) {
  if (str == "+") return {OpCode::Add};
  // If it's not "+", assume it's a number
  int val;
  auto [ptr, ec] = std::from_chars(str.begin(), str.end(), val);
  if (ec == std::errc())
    return {OpCode::Const, val};
  else
    throw std::runtime_error("Unknown operator: " + std::string{str});
}

struct Expr;
struct Add {
  Expr *left;
  Expr *right;
};
struct Const {
  int value;
};
struct Expr {
  std::variant<Add, Const> data;
};
template <>
struct egs::EGraphTraits<Expr> {
  static MyOp get_op(const Expr &e) {
    if (std::holds_alternative<Add>(e.data)) return {OpCode::Add};
    return {OpCode::Const, std::get<Const>(e.data).value};
  }

  static std::vector<const Expr *> get_args(const Expr &e) {
    if (auto *a = std::get_if<Add>(&e.data)) return {a->left, a->right};
    return {};
  }
};

struct AstSizeCost {
  std::uint32_t operator()(MyOp op,
                           const std::vector<std::uint32_t> &arg_costs) const {
    std::uint32_t cost = 1;
    for (auto c : arg_costs)
      cost += c;
    if (op.code == OpCode::Const) cost += 1;
    if (op.code == OpCode::Add) cost += 10;
    return cost;
  }
};

TEST_CASE("Pattern parsing test") {
  using namespace egs;

  auto egraph = EGraph<MyOp>{};
  auto my_ast_root = Expr{Add{new Expr{Const{5}}, new Expr{Const{0}}}};
  auto root = add_tree(egraph, my_ast_root);

  auto fold_zero = RwRule<MyOp>::parse("(+ ?x 0)", "?x", parse_my_op);
  auto rules = std::vector{fold_zero};
  run(egraph, {rules});

  REQUIRE(egraph.total_nodes() == 3);

  auto extractor = Extractor{egraph, AstSizeCost{}, UINT32_MAX};
  auto best = extractor.extract(root, egraph);

  CHECK(best.op.code == OpCode::Const);
  CHECK(best.op.val == 5);
}
