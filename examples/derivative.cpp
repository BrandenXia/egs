#include <functional>
#include <iostream>
#include <system_error>
#include <variant>

#include "egs/egs.hpp"
#include "egs/extract.hpp"
#include "egs/pattern.hpp"
#include "egs/utils.hpp"

struct Expr;
enum class OpCode { Const, Var, Neg, Deriv, Add, Sub, Mul, Div, Exp };

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
  if (str == "exp") return {OpCode::Exp};

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
    switch (op.code) {
    case OpCode::Const: cost += 1; break;
    case OpCode::Var: cost += 1; break;
    case OpCode::Neg: cost += 5; break;
    case OpCode::Deriv: cost += 40; break;
    case OpCode::Add:
    case OpCode::Sub: cost += 10; break;
    case OpCode::Mul:
    case OpCode::Div: cost += 15; break;
    case OpCode::Exp: cost += 20; break;
    }
    return cost;
  }
};

void pretty_print(egs::ExtractedTree<Op> tree) {
  switch (tree.op.code) {
  case OpCode::Const: std::cout << tree.op.val; break;
  case OpCode::Var: std::cout << "x" << tree.op.val; break;
  case OpCode::Neg:
    std::cout << "(- ";
    pretty_print(tree.args[0]);
    std::cout << ")";
    break;
  case OpCode::Deriv:
    std::cout << "(d ";
    pretty_print(tree.args[0]);
    std::cout << " ";
    pretty_print(tree.args[1]);
    std::cout << ")";
    break;
  case OpCode::Add:
    std::cout << "(";
    pretty_print(tree.args[0]);
    std::cout << " + ";
    pretty_print(tree.args[1]);
    std::cout << ")";
    break;
  case OpCode::Sub:
    std::cout << "(";
    pretty_print(tree.args[0]);
    std::cout << " - ";
    pretty_print(tree.args[1]);
    std::cout << ")";
    break;
  case OpCode::Mul:
    std::cout << "(";
    pretty_print(tree.args[0]);
    std::cout << " * ";
    pretty_print(tree.args[1]);
    std::cout << ")";
    break;
  case OpCode::Div:
    std::cout << "(";
    pretty_print(tree.args[0]);
    std::cout << " / ";
    pretty_print(tree.args[1]);
    std::cout << ")";
    break;
  case OpCode::Exp:
    pretty_print(tree.args[0]);
    std::cout << "^";
    pretty_print(tree.args[1]);
    break;
  }
}

auto rules = egs::make_rules<Op>(
    {
      {"(d ?x ?x)", "1"},
      {"(d (+ ?x ?y) ?z)", "(+ (d ?x ?z) (d ?y ?z))"},
      {"(d (- ?x ?y) ?z)", "(- (d ?x ?z) (d ?y ?z))"},
      {"(d (* ?x ?y) ?z)", "(+ (* (d ?x ?z) ?y) (* ?x (d ?y ?z)))"},
      {"(d (/ ?x ?y) ?z)",
       "(/ (- (* (d ?x ?z) ?y) (* ?x (d ?y ?z))) (* ?y ?y))"},
      {"(d (neg ?x) ?z)", "(neg (d ?x ?z))"},

      {"(+ ?x 0)", "?x"},
      {"(+ 0 ?x)", "?x"},
      {"(* 1 ?x)", "?x"},
      {"(* ?x 1)", "?x"},
      {"(* 0 ?x)", "0"},
      {"(* ?x 0)", "0"},
      {"(- ?x 0)", "?x"},
      {"(/ ?x 1)", "?x"},
      {"(exp ?x 1)", "?x"},

      {"(+ ?x ?y)", "(+ ?y ?x)"},
      {"(* ?x ?y)", "(* ?y ?x)"},
      {"(+ (+ ?x ?y) ?z)", "(+ ?x (+ ?y ?z))"},
      {"(* (* ?x ?y) ?z)", "(* ?x (* ?y ?z))"},
    },
    parse_op);

int main() {
  auto egraph = egs::EGraph<Op>{};
  // (2x + 3)^3
  auto diff_expr = Expr{BinaryOp{
    OpCode::Deriv,
    new Expr{BinaryOp{
      OpCode::Exp,
      new Expr{BinaryOp{
        OpCode::Add,
        new Expr{BinaryOp{OpCode::Mul, new Expr{Var{1}}, new Expr{Const{2}}}},
        new Expr{Const{3}}}},
      new Expr{Const{3}}}},
    new Expr{Var{1}}}};
  auto root = egs::add_tree(egraph, diff_expr);

  rules.add("(d ?c ?x)",
            egs::bind("?c",
                      [](egs::EGraph<Op> &eg, const egs::Match<Op> &match,
                         egs::Id c) -> egs::RwResult {
                        bool has_const = false;
                        bool has_var = false;
                        eg.for_each_node(c, [&](const Op &op) {
                          if (op.code == OpCode::Const) has_const = true;
                          if (op.code == OpCode::Var) has_var = true;
                          if (has_const && has_var)
                            return egs::ControlFlow::Break;
                          return egs::ControlFlow::Continue;
                        });

                        if (has_const && !has_var)
                          return eg.leaf(OpCode::Const, 0);

                        return egs::no_rewrite;
                      }));
  rules.add("(d (exp ?u ?n) ?x)",
            egs::bind("?u", "?n", "?x",
                      [](egs::EGraph<Op> &eg, const egs::Match<Op> &match,
                         egs::Id u, egs::Id n, egs::Id x) -> egs::RwResult {
                        auto n_val = eg.find_in_eclass(
                            n, [](Op op) -> std::optional<int> {
                              if (op.code == OpCode::Const) return op.val;
                              return std::nullopt;
                            });

                        if (!n_val) return egs::no_rewrite;

                        auto n_minus_one = eg.leaf(OpCode::Const, *n_val - 1);
                        auto exp_part = eg.make({OpCode::Exp}, u, n_minus_one);
                        auto outer_deriv = eg.make({OpCode::Mul}, n, exp_part);
                        auto inner_deriv = eg.make({OpCode::Deriv}, u, x);
                        return eg.make({OpCode::Mul}, outer_deriv, inner_deriv);
                      }));
  rules.add("(* ?a ?b)",
            egs::bind("?a", "?b",
                      [](egs::EGraph<Op> &eg, const egs::Match<Op> &match,
                         egs::Id a, egs::Id b) -> egs::RwResult {
                        auto get_const = [](Op op) -> std::optional<int> {
                          if (op.code == OpCode::Const) return op.val;
                          return std::nullopt;
                        };

                        auto a_val = eg.find_in_eclass(a, get_const);
                        auto b_val = eg.find_in_eclass(b, get_const);

                        if (a_val && b_val)
                          return eg.leaf(OpCode::Const, (*a_val) * (*b_val));
                        return egs::no_rewrite;
                      }));

  auto stop_reason = egs::run(egraph, rules);
  switch (stop_reason) {
  case egs::StopReason::Saturated: std::cout << "Saturated\n"; break;
  case egs::StopReason::IterationLimit:
    std::cout << "Iteration limit reached\n";
    break;
  case egs::StopReason::NodeLimit: std::cout << "Node limit reached\n"; break;
  }

  auto extractor = egs::Extractor{egraph, Cost{}, UINT32_MAX};
  auto best = extractor.extract(root, egraph);

  pretty_print(best);
  std::cout << std::endl;
}
