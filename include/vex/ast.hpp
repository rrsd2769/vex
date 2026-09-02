// The expression AST -- week 2 of ROADMAP.md. Statements, declarations, and
// everything else the parser will eventually produce are later weeks.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "vex/span.hpp"
#include "vex/token.hpp"

namespace vex {

struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

// Literals carry the parsed value, not the raw Lexeme -- the parser is what
// turns "3.14" into a double or `"a\"b"` into an unescaped string
// (CONTEXT.md: Token carries neither text nor a parsed value; Literal does).
struct IntLiteral {
    std::int64_t value;
};

struct FloatLiteral {
    double value;
};

struct BoolLiteral {
    bool value;
};

struct StringLiteral {
    std::string value;
};

struct Identifier {
    std::string name;
};

// op is Minus or Bang.
struct UnaryExpr {
    TokenKind op;
    ExprPtr operand;
};

struct BinaryExpr {
    TokenKind op;
    ExprPtr lhs;
    ExprPtr rhs;
};

struct CallExpr {
    ExprPtr callee;
    std::vector<ExprPtr> args;
};

struct IndexExpr {
    ExprPtr object;
    ExprPtr index;
};

using ExprNode = std::variant<IntLiteral, FloatLiteral, BoolLiteral, StringLiteral, Identifier, UnaryExpr,
                               BinaryExpr, CallExpr, IndexExpr>;

// One node in the expression tree. Grouping parens (`(expr)`) do not get
// their own node -- they only ever affect which shape the parser builds,
// the same "no ceremony beyond what's needed" call as Diagnostic/Label.
struct Expr {
    Span span;
    ExprNode node;
};

// Renders an Expr as a parenthesized S-expression, e.g. `(+ 1 (* 2 (- 3
// 4)))` for `1 + 2 * (3 - 4)`. Exists to verify parser output in tests
// without hand-deriving tree shapes by eye.
std::string dump_expr(const Expr& expr);

}  // namespace vex
