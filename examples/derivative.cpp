#include <functional>
#include <iostream>
#include <variant>

#include "egs/egs.hpp"
#include "egs/extract.hpp"
#include "egs/pattern.hpp"

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
    if (op.code == OpCode::Const) cost += 1;
    if (op.code == OpCode::Var) cost += 1;
    if (op.code == OpCode::Neg) cost += 5;
    if (op.code == OpCode::Deriv) cost += 40;
    if (op.code == OpCode::Add || op.code == OpCode::Sub) cost += 10;
    if (op.code == OpCode::Mul || op.code == OpCode::Div) cost += 15;
    if (op.code == OpCode::Exp) cost += 20;
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
  case OpCode::Exp:
    std::cout << indent_str << "Exp\n";
    for (const auto &arg : tree.args)
      print_tree(arg, indent + 2);
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

  using R = egs::RwRule<Op>;

  auto const_deriv = R::parse(
      "(d ?c ?x)", parse_op, [](const egs::Pattern<Op>::PatternVarMap &vars) {
        auto cidx = vars.at("?c");

        return [cidx](egs::EGraph<Op> &eg, const egs::Match<Op> &match) {
          auto c_id = eg.find(match.subst[cidx]);
          bool has_const = false;
          bool has_var = false;

          for (const auto &node : eg.get_eclass(c_id).nodes) {
            if (node.op.code == OpCode::Const) has_const = true;
            if (node.op.code == OpCode::Var) has_var = true;
          }

          if (has_const && !has_var) {
            return eg.add(Op{OpCode::Const, 0});
          }
          return match.eclass;
        };
      });
  rules.push_back(const_deriv);
  auto power_deriv = R::parse(
      "(d (exp ?u ?n) ?x)", parse_op, // Notice ?u and ?x are different!
      [](const egs::Pattern<Op>::PatternVarMap &vars) {
        auto nidx = vars.at("?n");
        auto uidx = vars.at("?u");
        auto xidx = vars.at("?x"); // The variable we are differentiating wrt

        return [nidx, uidx, xidx](egs::EGraph<Op> &eg,
                                  const egs::Match<Op> &match) {
          auto n_id = eg.find(match.subst[nidx]);

          for (const auto &node : eg.get_eclass(n_id).nodes) {
            if (node.op.code == OpCode::Const) {
              auto u = eg.find(match.subst[uidx]);
              auto x = eg.find(match.subst[xidx]);

              auto n_minus_one = eg.add(Op{OpCode::Const, node.op.val - 1});
              auto exp_part = eg.add(Op{OpCode::Exp}, {u, n_minus_one});
              auto outer_deriv = eg.add(Op{OpCode::Mul}, {n_id, exp_part});
              auto inner_deriv = eg.add(Op{OpCode::Deriv}, {u, x});
              return eg.add(Op{OpCode::Mul}, {outer_deriv, inner_deriv});
            }
          }
          return match.eclass;
        };
      });
  rules.push_back(power_deriv);
  auto fold_const_mul = R::parse(
      "(* ?a ?b)", parse_op, [](const egs::Pattern<Op>::PatternVarMap &vars) {
        auto aidx = vars.at("?a");
        auto bidx = vars.at("?b");

        return [aidx, bidx](egs::EGraph<Op> &eg, const egs::Match<Op> &match) {
          auto a_id = eg.find(match.subst[aidx]);
          auto b_id = eg.find(match.subst[bidx]);

          std::optional<int> a_val;
          std::optional<int> b_val;

          for (const auto &node : eg.get_eclass(a_id).nodes)
            if (node.op.code == OpCode::Const) a_val = node.op.val;
          for (const auto &node : eg.get_eclass(b_id).nodes)
            if (node.op.code == OpCode::Const) b_val = node.op.val;

          if (a_val && b_val)
            return eg.add(Op{OpCode::Const, (*a_val) * (*b_val)});
          return match.eclass;
        };
      });
  rules.push_back(fold_const_mul);

  auto stop_reason = egs::run(egraph, {rules});
  switch (stop_reason) {
  case egs::StopReason::Saturated: std::cout << "Saturated\n"; break;
  case egs::StopReason::IterationLimit:
    std::cout << "Iteration limit reached\n";
  case egs::StopReason::NodeLimit: std::cout << "Node limit reached\n"; break;
  }

  auto extractor = egs::Extractor{egraph, Cost{}, UINT32_MAX};
  auto best = extractor.extract(root, egraph);

  print_tree(best);
}
