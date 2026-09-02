// Statements, declarations, and top-level items -- week 3 of ROADMAP.md.
// Builds on the expression AST in ast.hpp.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "vex/ast.hpp"
#include "vex/span.hpp"

namespace vex {

struct Stmt;
using StmtPtr = std::unique_ptr<Stmt>;

// A parsed type name -- ROADMAP.md's design note: the Type grammar
// production is just an identifier either way (`int`, `Point`, ...);
// telling a primitive from a struct name is the type checker's job
// (week 4), not the parser's.
struct TypeRef {
    std::string name;
    Span span;
};

struct Block {
    std::vector<StmtPtr> statements;
    Span span;
};

struct VarDecl {
    bool is_mutable;               // `var`, not `let`
    std::string name;
    std::optional<TypeRef> type;   // absent means inferred from init
    ExprPtr init;
};

struct AssignStmt {
    ExprPtr target;
    ExprPtr value;
};

struct ExprStmt {
    ExprPtr expr;
};

struct IfStmt {
    ExprPtr condition;
    Block then_block;
    // `else if` desugars to a single-statement Block wrapping the nested
    // IfStmt, so this stays one shape instead of adding a second Stmt kind.
    std::optional<Block> else_block;
};

struct WhileStmt {
    ExprPtr condition;
    Block body;
};

struct ForStmt {
    std::string var_name;
    ExprPtr range_start;
    ExprPtr range_end;
    Block body;
};

struct ReturnStmt {
    ExprPtr value;  // null means a bare `return;`
};

using StmtNode = std::variant<VarDecl, AssignStmt, ExprStmt, IfStmt, WhileStmt, ForStmt, ReturnStmt>;

struct Stmt {
    Span span;
    StmtNode node;
};

struct Param {
    std::string name;
    TypeRef type;
};

struct FunctionDecl {
    std::string name;
    std::vector<Param> params;
    std::optional<TypeRef> return_type;  // absent means void
    Block body;
};

struct Field {
    std::string name;
    TypeRef type;
};

struct StructDecl {
    std::string name;
    std::vector<Field> fields;
};

using ItemNode = std::variant<FunctionDecl, StructDecl>;

struct Item {
    Span span;
    ItemNode node;
};

struct Program {
    std::vector<Item> items;
};

// Same purpose as dump_expr: render as an S-expression so tests can verify
// tree shape without hand-deriving it by eye.
std::string dump_stmt(const Stmt& stmt);
std::string dump_item(const Item& item);
std::string dump_program(const Program& program);

}  // namespace vex
