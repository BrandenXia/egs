#include <iostream>
#include <variant>
#include <vector>

#include "egs/egs.hpp"
#include "egs/pattern.hpp"

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

void print_tree(egs::ExtractedTree<MyOp> tree, int indent = 0) {
  std::string indent_str(indent, ' ');
  if (tree.op.code == OpCode::Const) {
    std::cout << indent_str << "Const(" << tree.op.val << ")\n";
  } else {
    std::cout << indent_str << "Add\n";
    for (const auto &arg : tree.args)
      print_tree(arg, indent + 2);
  }
}

int main() {
  using namespace egs;

  auto egraph = EGraph<MyOp>{};
  auto my_ast_root = Expr{Add{new Expr{Const{5}}, new Expr{Const{0}}}};
  auto root = add_tree(egraph, my_ast_root);

  using P = Pattern<MyOp>;
  auto fold_zero = RwRule<MyOp>(
      P::op(MyOp{OpCode::Add}, {P::var(0), P::op(MyOp{OpCode::Const, 0}, {})}),
      P::var(0));
  auto rules = std::vector{fold_zero};
  run(egraph, {rules});

  std::cout << "Total nodes in egraph: " << egraph.total_nodes() << "\n";

  auto extractor = Extractor{egraph, AstSizeCost{}, UINT32_MAX};
  auto best = extractor.extract(root, egraph);
  print_tree(best);
}
