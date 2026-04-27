# egs

A C++20 non-invasive EGraph library.

## Build

```bash
meson setup build --buildtype=release
# optionally, you can also build the examples
meson setup build --buildtype=release -Dbuild_examples=true

# and then compile
meson compile -C build
```

The resulting library will be located at `build/libegs.a` and
`build/libegs.dylib` or `build/libegs.so` depending on your platform.

The header files are located at `include/egs` and should be included in your
project.

## Usage

Here's some basic usage of the library. For more examples, please refer to the
[examples](./examples) directory.

Constructing an EGraph is straightforward:

```cpp
// `Op` is your custom operation type, which must be copyable and hashable.
auto egraph = egs::EGraph<Op>{};

// Example of Op
struct Op {
  enum class Type { Add, Mul } type;
}

template <>
struct std::hash<Op> {
  size_t operator()(const Op& op) const {
    return std::hash<int>{}(static_cast<int>(op.type));
  }
};
```

There's multiple way to construct a rule:

```cpp
Op parse_op(std::string_view str) {
  if (str == "+") return Op{Op::Type::Add};
  if (str == "*") return Op{Op::Type::Mul};
  throw std::invalid_argument("Unknown operator");
}

// batch construction of rules from S-expr patterns
auto rules = egs::make_rules<Op>({
  // commutativity of addition
  {"(+ ?x ?y)", "(+ ?y ?x)"},
  // distributivity of multiplication over addition
  {"(* ?a (+ ?b ?c))", "(+ (* ?a ?b) (* ?a ?c))"},
}, parse_op)

using R = egs::RwRule<Op>;
// parsing a single rule from S-expr patterns
// commutativity of multiplication
auto rule1 = R::parse("(* ?x ?y)", "(* ?y ?x)", parse_op);
rules.add(std::move(rule1));

// or directly add to ruleset
// no need to pass parse_op again since it's already provided to the ruleset
rules.add("(* ?x ?y)", "(* ?y ?x)");

// parsing a single rule to a lambda function
auto rule2 = R::parse("(+ ?x ?y ?z)", parse_op, egs::bind("?x", "?y", "?z",
  [](egs::EGraph<Op> &eg, const egs::Match<Op> &match,
    egs::Id x, egs::Id y, egs::Id z) -> egs::RwResult {
    return eg.add(egs::Node{parse_op("+"), {x, z}}
  }));
// can also directly add to ruleset
rules.add("(+ ?x ?y ?z)", egs::bind("?x", "?y", "?z",
  [](egs::EGraph<Op> &eg, const egs::Match<Op> &match,
    egs::Id x, egs::Id y, egs::Id z) -> egs::RwResult {
    return eg.add(egs::Node{parse_op("+"), {x, z}}
  }));

// manually constructing a rule is also possible, but less convenient
using P = egs::Pattern<Op>;
auto rule3 = R{
  P::op(Op{Op::Type::Add}, {P::var(0), P::var(1)}),
  P::op(Op{Op::Type::Add}, {P::var(1), P::var(0)})
};
```

Then, we can add expressions to the EGraph and apply the rules:

```cpp
// suppose this is your custom expression type
struct Expr {
  Op op;
  std::vector<Expr> children;
};

// specialize EGraphTraits for your expression type
template <>
struct egs::EGraphTraits<Expr> {
  static Op get_op(const Expr& expr) { return expr.op; }
  static std::vector<const Expr *> get_args(const Expr& expr) {
    std::vector<const Expr *> args;
    for (const auto& child : expr.children)
      args.push_back(&child);
    return args;
  }
};

// add an expression to the EGraph
auto test_expr = Expr{Op{Op::Type::Add}, {Expr{Op{Op::Type::Mul}, {}}}};
auto root = egs::add_tree(egraph, test_expr);

// run the rules until saturation or node limit is reached
auto stop_reason = egs::run(egraph, rules);
```

Extracting the optimized expression is also easy:

```cpp
struct CostModel {
  std::uint32_t operator()(Op op,
                           const std::vector<std::uint32_t>& arg_costs)
  const {
    switch (op.type) {
      case Op::Type::Add:
        return 1 + std::accumulate(arg_costs.begin(), arg_costs.end(), 0);
      case Op::Type::Mul:
        return 2 + std::accumulate(arg_costs.begin(), arg_costs.end(), 0);
    }
    return 0; // default cost
  }
};

auto extractor = egs::Extractor{egraph, CostModel{}, UINT32_MAX};
// here's the best expression according to the cost model
auto best = extractor.extract(root, egraph);
```
