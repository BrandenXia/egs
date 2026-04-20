#include <functional>
#include <iostream>

#include "egs/egs.hpp"
#include "egs/extract.hpp"
#include "egs/pattern.hpp"

struct Expr;
enum class OpCode { Const, Var, Neg, Deriv, Add, Sub, Mul, Div };

// clang-format off
struct Const { int value; };
struct Var { int id; };
struct UnaryOp { OpCode op; Expr *operand; };
struct BinaryOp { OpCode op; Expr *left; Expr *right; };

struct Expr { std::variant<UnaryOp, BinaryOp, Const, Var> data; };
// clang-format on

struct Op {
  OpCode code;
  int val = 0; // Only used if Const
  bool operator==(const Op &) const = default;
};

template <>
struct std::hash<Op> {
  auto operator()(const Op &o) const { return (size_t)o.code ^ o.val; }
};

Op parse_op(std::string_view str) {
  if (str == "neg") return {OpCode::Neg};
  if (str == "d") return {OpCode::Deriv};
  if (str == "+") return {OpCode::Add};
  if (str == "-") return {OpCode::Sub};
  if (str == "*") return {OpCode::Mul};
  if (str == "/") return {OpCode::Div};

  // If it's not one of the above, assume it's a variable or constant
  int val;
  auto [ptr, ec] = std::from_chars(str.begin(), str.end(), val);
  if (ec == std::errc())
    return {OpCode::Const, val};
  else
    return {OpCode::Var, static_cast<int>(std::hash<std::string_view>{}(str))};
}

template <>
struct egs::EGraphTraits<Expr> {
  static Op get_op(const Expr &e) {
    if (auto *c = std::get_if<Const>(&e.data)) return {OpCode::Const, c->value};
    if (auto *v = std::get_if<Var>(&e.data)) return {OpCode::Var, v->id};
    if (auto *u = std::get_if<UnaryOp>(&e.data)) return {u->op};
    return {std::get<BinaryOp>(e.data).op};
  }

  static std::vector<const Expr *> get_args(const Expr &e) {
    if (std::holds_alternative<Const>(e.data)) return {};
    if (std::holds_alternative<Var>(e.data)) return {};
    if (auto *u = std::get_if<UnaryOp>(&e.data)) return {u->operand};
    auto *b = std::get_if<BinaryOp>(&e.data);
    return {b->left, b->right};
  }
};

struct Cost {
  std::uint32_t operator()(Op op,
                           const std::vector<std::uint32_t> &arg_costs) const {
    auto cost = std::uint32_t(1);
    for (auto c : arg_costs)
      cost += c;
    if (op.code == OpCode::Const) cost += 1;
    if (op.code == OpCode::Var) cost += 1;
    if (op.code == OpCode::Neg) cost += 5;
    if (op.code == OpCode::Deriv) cost += 0;
    if (op.code == OpCode::Add || op.code == OpCode::Sub) cost += 10;
    if (op.code == OpCode::Mul || op.code == OpCode::Div) cost += 15;
    return cost;
  }
};

void print_tree(egs::ExtractedTree<Op> tree, int indent = 0) {
  std::string indent_str(indent, ' ');
  switch (tree.op.code) {
  case OpCode::Const:
    std::cout << indent_str << "Const(" << tree.op.val << ")\n";
    break;
  case OpCode::Var:
    std::cout << indent_str << "Var(" << tree.op.val << ")\n";
    break;
  case OpCode::Neg:
    std::cout << indent_str << "Neg\n";
    print_tree(tree.args[0], indent + 2);
    break;
  case OpCode::Deriv:
    std::cout << indent_str << "Deriv\n";
    print_tree(tree.args[0], indent + 2);
    print_tree(tree.args[1], indent + 2);
    break;
  case OpCode::Add:
    std::cout << indent_str << "Add\n";
    for (const auto &arg : tree.args)
      print_tree(arg, indent + 2);
    break;
  case OpCode::Sub:
    std::cout << indent_str << "Sub\n";
    for (const auto &arg : tree.args)
      print_tree(arg, indent + 2);
    break;
  case OpCode::Mul:
    std::cout << indent_str << "Mul\n";
    for (const auto &arg : tree.args)
      print_tree(arg, indent + 2);
    break;
  case OpCode::Div:
    std::cout << indent_str << "Div\n";
    for (const auto &arg : tree.args)
      print_tree(arg, indent + 2);
    break;
  }
}

constexpr auto rules =
    std::to_array<std::pair<std::string_view, std::string_view>>({
      {"(d ?x ?x)", "1"},
      {"(d (+ ?x ?y) ?z)", "(+ (d ?x ?z) (d ?y ?z))"},
      {"(d (- ?x ?y) ?z)", "(- (d ?x ?z) (d ?y ?z))"},
      {"(d (* ?x ?y) ?z)", "(+ (* (d ?x ?z) ?y) (* ?x (d ?y ?z)))"},
      {"(d (/ ?x ?y) ?z)",
       "(/ (- (* (d ?x ?z) ?y) (* ?x (d ?y ?z))) (* ?y ?y))"},
      {"(+ ?x 0)", "?x"},
      {"(+ 0 ?x)", "?x"},
      {"(* 1 ?x)", "?x"},
      {"(* ?x 1)", "?x"},
      {"(* 0 ?x)", "0"},
      {"(* ?x 0)", "0"},
      {"(- ?x 0)", "?x"},
      {"(/ ?x 1)", "?x"},
    });

int main() {
  auto egraph = egs::EGraph<Op>{};
  auto diff_expr = Expr{BinaryOp{
    OpCode::Deriv,
    new Expr{BinaryOp{OpCode::Mul, new Expr{Var{1}}, new Expr{Var{1}}}},
    new Expr{Var{1}}}};
  auto root = egs::add_tree(egraph, diff_expr);

  using R = egs::RwRule<Op>;
  auto rw_rules = std::vector<R>{};
  for (const auto &[lhs, rhs] : rules)
    rw_rules.push_back(R::parse(lhs, rhs, parse_op));

  auto const_deriv = R::parse(
      "(d ?c ?x)", parse_op, [](const egs::Pattern<Op>::PatternVarMap &vars) {
        auto cidx = vars.at("?c");

        return [cidx](egs::EGraph<Op> &eg, const egs::Match<Op> &match) {
          egs::Id c_id = eg.find(match.subst[cidx]);
          for (const auto &node : eg.get_eclass(c_id).nodes)
            if (node.op.code == OpCode::Const)
              return eg.add(Op{OpCode::Const, node.op.val}, {});
          return match.eclass;
        };
      });
  rw_rules.push_back(const_deriv);

  egs::run(egraph, {rw_rules});

  auto extractor = egs::Extractor{egraph, Cost{}, UINT32_MAX};
  auto best = extractor.extract(root, egraph);

  print_tree(best);
}
