// The type checker -- week 4 (core) and week 5 (completion + diagnostics
// polish) of ROADMAP.md.
//
// Week 4: symbol table with lexical scoping, type representation, expression
// typing, let/var inference and mutability enforcement.
//
// Week 5 adds:
//   - Function signature checking: every call to a declared function checks
//     arity and each argument's type against the function's params, and
//     types as the function's declared (or absent -> void) return type.
//     Functions are pre-registered (register_functions()), so forward and
//     mutual/recursive calls work regardless of declaration order -- the
//     same reason register_structs() exists for week 4.
//   - Builtins: the language has no way to declare one, but the flagship
//     example (examples/fib.vx) calls `print`, so it's special-cased in
//     check_call() rather than requiring an explicit builtins declaration
//     syntax. `print` accepts one or more arguments of any known type and
//     returns void.
//   - Struct construction and field access: a struct is constructed with
//     call syntax against its own name (`Point(1, 2)`, positional, in field
//     declaration order) -- reusing CallExpr rather than inventing a new
//     brace-literal expression form, which would collide with `if`/`while`
//     condition parsing the way it classically does in C-family grammars.
//     `object.field` (FieldAccessExpr, new this week) reads a field's type.
//   - Array indexing: TypeRef gained an optional fixed size (`int[5]`), and
//     ArrayLiteral (`[1, 2, 3]`) is the only way to construct one. IndexExpr
//     now validates the index is an `int` and, when the index is a literal,
//     checks it statically against the array's size.
//   - Definite-return analysis: a function with a declared (non-void) return
//     type must return a value on every path. Conservative: a while/for
//     loop is never assumed to run, so returning only inside one doesn't
//     count -- see block_always_returns() in type_checker.cpp.
//   - Diagnostics polish: secondary labels pointing at the relevant
//     declaration (Symbol::decl_span, a TypeRef's span, ...), and
//     Levenshtein-based "did you mean `count`?" suggestions for undefined
//     variables, undefined functions, and unknown struct fields.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "vex/ast.hpp"
#include "vex/diagnostic.hpp"
#include "vex/scope.hpp"
#include "vex/stmt.hpp"
#include "vex/type.hpp"

namespace vex {

class TypeChecker {
public:
    explicit TypeChecker(const Program& program);

    void check();

    const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }

private:
    void register_structs();
    void register_functions();
    Type resolve_type_ref(const TypeRef& ref);

    void check_function(const FunctionDecl& fn);
    void check_struct(const StructDecl& st);

    void check_block(const Block& block, const Scope& parent_scope);
    void check_stmt(const Stmt& stmt, Scope& scope);
    void check_var_decl(const VarDecl& decl, const Span& stmt_span, Scope& scope);
    void check_assign(const AssignStmt& stmt, Scope& scope);
    void check_if(const IfStmt& stmt, Scope& scope);
    void check_while(const WhileStmt& stmt, Scope& scope);
    void check_for(const ForStmt& stmt, const Span& stmt_span, Scope& scope);
    void check_return(const ReturnStmt& stmt, const Span& stmt_span, Scope& scope);

    Type check_expr(const Expr& expr, Scope& scope);
    Type check_unary(const UnaryExpr& node, Scope& scope);
    Type check_binary(const BinaryExpr& node, Scope& scope);
    Type check_call(const CallExpr& node, const Span& call_span, Scope& scope);
    Type check_call_to_function(const FunctionDecl& fn, const CallExpr& node, const Span& call_span, Scope& scope);
    Type check_call_to_struct(const StructDecl& st, const CallExpr& node, const Span& call_span, Scope& scope);
    Type check_call_to_builtin(const CallExpr& node, const Span& call_span, Scope& scope);
    Type check_index(const IndexExpr& node, Scope& scope);
    Type check_field_access(const FieldAccessExpr& node, Scope& scope);
    Type check_array_literal(const ArrayLiteral& node, const Span& array_span, Scope& scope);

    // The Symbol at the root of an assignment target, e.g. `p` for both
    // `p = ...` and `p.x[0] = ...` -- used to check mutability of a target
    // that isn't a bare identifier. Returns nullptr if the target isn't
    // rooted in a resolvable identifier (undefined name, or not an lvalue
    // shape at all), in which case no mutability diagnostic is reported --
    // check_expr(target) already reported whatever was actually wrong.
    const Symbol* assignment_root(const Expr& target, Scope& scope);

    void error(const Span& span, std::string message, std::string label = "here");
    void error(const Span& span, std::string message, std::string label, std::vector<Label> secondary);
    void error_with_suggestion(const Span& span, std::string message, std::string label,
                                const std::string& near_name, const std::vector<std::string>& candidates,
                                std::vector<Label> secondary = {});

    const Program& program_;
    std::vector<Diagnostic> diagnostics_;
    // Struct names, tracked separately from Scope -- see scope.hpp's
    // comment on Symbol for why.
    std::unordered_map<std::string, const StructDecl*> structs_;
    // Function names, same reasoning -- registered up front (like structs_)
    // so a call to a not-yet-seen function, including a recursive
    // self-call, resolves regardless of where in the file it appears.
    std::unordered_map<std::string, const FunctionDecl*> functions_;
    // Top-level item name -> declaration span, covering both structs_ and
    // functions_. Exists for two things week 4 didn't need: reporting a
    // struct/function name colliding with another top-level item (structs_
    // and functions_ share one namespace even though they're stored
    // separately), and "did you mean" candidate lists that want a span to
    // point a secondary label at.
    std::unordered_map<std::string, Span> item_spans_;
    const FunctionDecl* current_function_ = nullptr;
};

}  // namespace vex
