// The type checker -- week 4 of ROADMAP.md, "core": symbol table with
// lexical scoping, type representation, expression typing, let/var
// inference and mutability enforcement.
//
// Deliberately deferred to week 5, per ROADMAP.md: function signature
// checking (arity, argument types, return paths), struct field access,
// array indexing, and definite-return analysis. Concretely this week:
//   - CallExpr does not resolve or validate its callee at all -- no
//     function symbol table exists yet, so `print(...)` and `fib(...)`
//     both type-check with no diagnostic either way. Each argument
//     expression is still walked, so a type error nested inside one is
//     still caught. The call itself types as Type::unknown().
//   - IndexExpr similarly walks its object and index but doesn't validate
//     them (there's no array type yet -- TypeRef only ever resolves to a
//     primitive or a struct name) and types as Type::unknown().
// Type::unknown() never itself triggers a mismatch diagnostic, so neither
// of the above cascades into spurious downstream errors.
#pragma once

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
    Type check_call(const CallExpr& node, Scope& scope);
    Type check_index(const IndexExpr& node, Scope& scope);

    void error(const Span& span, std::string message, std::string label = "here");

    const Program& program_;
    std::vector<Diagnostic> diagnostics_;
    // Struct names, tracked separately from Scope -- see scope.hpp's
    // comment on Symbol for why.
    std::unordered_map<std::string, const StructDecl*> structs_;
    const FunctionDecl* current_function_ = nullptr;
};

}  // namespace vex
