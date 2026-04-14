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

template <> struct std::hash<MyOp> {
  size_t operator()(const MyOp &o) const { return (size_t)o.code ^ o.val; }
};

template <> struct egs::EGraphTraits<Expr> {
  static MyOp get_op(const Expr &e) {
    if (std::holds_alternative<Add>(e.data))
      return {OpCode::Add};
    return {OpCode::Const, std::get<Const>(e.data).value};
  }

  static std::vector<const Expr *> get_args(const Expr &e) {
    if (auto *a = std::get_if<Add>(&e.data))
      return {a->left, a->right};
    return {};
  }
};

int main() {
  using namespace egs;

  EGraph<MyOp> egraph;

  Expr my_ast_root = Expr{Add{new Expr{Const{5}}, new Expr{Const{0}}}};

  add_tree(egraph, my_ast_root);

  RwRule<MyOp> fold_zero = {
      .searcher = Pattern<MyOp>::op(
          MyOp{OpCode::Add}, {Pattern<MyOp>::var(0),
                              Pattern<MyOp>::op(MyOp{OpCode::Const, 0}, {})}),
      .applier = Pattern<MyOp>::var(0)};
}
