#include "vex/type_checker.hpp"

namespace vex {

TypeChecker::TypeChecker(const Program& program) : program_(program) {}

void TypeChecker::error(const Span& span, std::string message, std::string label) {
    diagnostics_.push_back(Diagnostic{
        Severity::Error,
        std::move(message),
        Label{span, std::move(label)},
        {},
        std::nullopt,
    });
}

void TypeChecker::check() {
    register_structs();
    for (const Item& item : program_.items) {
        if (const auto* st = std::get_if<StructDecl>(&item.node)) {
            check_struct(*st);
        }
    }
    for (const Item& item : program_.items) {
        if (const auto* fn = std::get_if<FunctionDecl>(&item.node)) {
            check_function(*fn);
        }
    }
}

void TypeChecker::register_structs() {
    for (const Item& item : program_.items) {
        if (const auto* st = std::get_if<StructDecl>(&item.node)) {
            structs_[st->name] = st;
        }
    }
}

Type TypeChecker::resolve_type_ref(const TypeRef& ref) {
    if (ref.name == "int") return Type::primitive(TypeKind::Int);
    if (ref.name == "float") return Type::primitive(TypeKind::Float);
    if (ref.name == "bool") return Type::primitive(TypeKind::Bool);
    if (ref.name == "string") return Type::primitive(TypeKind::String);

    if (structs_.contains(ref.name)) return Type::make_struct(ref.name);

    error(ref.span, "unknown type `" + ref.name + "`", "no such type");
    return Type::unknown();
}

void TypeChecker::check_struct(const StructDecl& st) {
    // Just resolving each field's TypeRef is enough to catch a typo'd type
    // name -- validating field *access* (`.field` on an expression) is
    // week 5 (ROADMAP.md).
    for (const Field& field : st.fields) {
        resolve_type_ref(field.type);
    }
}

void TypeChecker::check_function(const FunctionDecl& fn) {
    current_function_ = &fn;

    Scope fn_scope;
    for (const Param& param : fn.params) {
        Type param_type = resolve_type_ref(param.type);
        // Params are immutable -- there's no `let`/`var` on a parameter to
        // say otherwise, and it keeps `fib(n)` from silently letting `n` be
        // reassigned inside the body.
        if (!fn_scope.declare(Symbol{param.name, param_type, /*is_mutable=*/false, param.type.span})) {
            error(param.type.span, "redeclaration of parameter `" + param.name + "`", "already declared");
        }
    }

    check_block(fn.body, fn_scope);
    current_function_ = nullptr;
}

void TypeChecker::check_block(const Block& block, const Scope& parent_scope) {
    Scope local(&parent_scope);
    for (const StmtPtr& stmt : block.statements) {
        check_stmt(*stmt, local);
    }
}

void TypeChecker::check_stmt(const Stmt& stmt, Scope& scope) {
    // Sibling `if`s, not an `if`/`else if` chain -- with -Wshadow on, each
    // `else if`'s init-declaration would nest inside the previous one's
    // scope and warn about shadowing the same name (`n`). Only one ever
    // matches, since stmt.node holds exactly one alternative.
    if (const auto* n = std::get_if<VarDecl>(&stmt.node)) {
        check_var_decl(*n, stmt.span, scope);
        return;
    }
    if (const auto* n = std::get_if<AssignStmt>(&stmt.node)) {
        check_assign(*n, scope);
        return;
    }
    if (const auto* n = std::get_if<ExprStmt>(&stmt.node)) {
        check_expr(*n->expr, scope);
        return;
    }
    if (const auto* n = std::get_if<IfStmt>(&stmt.node)) {
        check_if(*n, scope);
        return;
    }
    if (const auto* n = std::get_if<WhileStmt>(&stmt.node)) {
        check_while(*n, scope);
        return;
    }
    if (const auto* n = std::get_if<ForStmt>(&stmt.node)) {
        check_for(*n, stmt.span, scope);
        return;
    }
    if (const auto* n = std::get_if<ReturnStmt>(&stmt.node)) {
        check_return(*n, stmt.span, scope);
        return;
    }
}

void TypeChecker::check_var_decl(const VarDecl& decl, const Span& stmt_span, Scope& scope) {
    Type init_type = check_expr(*decl.init, scope);
    Type declared_type = init_type;

    if (decl.type) {
        declared_type = resolve_type_ref(*decl.type);
        if (declared_type.kind != TypeKind::Unknown && init_type.kind != TypeKind::Unknown &&
            declared_type != init_type) {
            error(decl.init->span,
                  "cannot assign value of type `" + to_string(init_type) + "` to variable of type `" +
                      to_string(declared_type) + "`",
                  "this is `" + to_string(init_type) + "`");
        }
    }

    if (!scope.declare(Symbol{decl.name, declared_type, decl.is_mutable, stmt_span})) {
        error(stmt_span, "redeclaration of `" + decl.name + "`", "already declared in this scope");
    }
}

void TypeChecker::check_assign(const AssignStmt& stmt, Scope& scope) {
    Type value_type = check_expr(*stmt.value, scope);

    const auto* ident = std::get_if<Identifier>(&stmt.target->node);
    if (!ident) {
        // Index-expression targets (`arr[i] = x`) aren't checkable yet --
        // array indexing's type rules are week 5 (ROADMAP.md). Still walk
        // the target so a bad subexpression inside it is still caught.
        check_expr(*stmt.target, scope);
        return;
    }

    const Symbol* symbol = scope.resolve(ident->name);
    if (!symbol) {
        error(stmt.target->span, "undefined variable `" + ident->name + "`", "not found in this scope");
        return;
    }

    if (!symbol->is_mutable) {
        error(stmt.target->span, "cannot assign to immutable variable `" + ident->name + "`",
              "declared with `let`, not `var`");
    }

    if (symbol->type.kind != TypeKind::Unknown && value_type.kind != TypeKind::Unknown &&
        symbol->type != value_type) {
        error(stmt.value->span,
              "cannot assign value of type `" + to_string(value_type) + "` to variable of type `" +
                  to_string(symbol->type) + "`",
              "this is `" + to_string(value_type) + "`");
    }
}

void TypeChecker::check_if(const IfStmt& stmt, Scope& scope) {
    Type cond_type = check_expr(*stmt.condition, scope);
    if (cond_type.kind != TypeKind::Unknown && cond_type != Type::primitive(TypeKind::Bool)) {
        error(stmt.condition->span, "if condition must be of type `bool`, found `" + to_string(cond_type) + "`",
              "this is `" + to_string(cond_type) + "`");
    }
    check_block(stmt.then_block, scope);
    if (stmt.else_block) check_block(*stmt.else_block, scope);
}

void TypeChecker::check_while(const WhileStmt& stmt, Scope& scope) {
    Type cond_type = check_expr(*stmt.condition, scope);
    if (cond_type.kind != TypeKind::Unknown && cond_type != Type::primitive(TypeKind::Bool)) {
        error(stmt.condition->span, "while condition must be of type `bool`, found `" + to_string(cond_type) + "`",
              "this is `" + to_string(cond_type) + "`");
    }
    check_block(stmt.body, scope);
}

void TypeChecker::check_for(const ForStmt& stmt, const Span& stmt_span, Scope& scope) {
    Type start_type = check_expr(*stmt.range_start, scope);
    Type end_type = check_expr(*stmt.range_end, scope);
    Type int_type = Type::primitive(TypeKind::Int);

    if (start_type.kind != TypeKind::Unknown && start_type != int_type) {
        error(stmt.range_start->span, "range start must be of type `int`, found `" + to_string(start_type) + "`",
              "this is `" + to_string(start_type) + "`");
    }
    if (end_type.kind != TypeKind::Unknown && end_type != int_type) {
        error(stmt.range_end->span, "range end must be of type `int`, found `" + to_string(end_type) + "`",
              "this is `" + to_string(end_type) + "`");
    }

    Scope loop_scope(&scope);
    // stmt_span (the whole `for ... {}`) is the closest span available for
    // the loop variable's own declaration -- ForStmt carries no separate
    // span for just `i`, the same limitation VarDecl and Param have.
    loop_scope.declare(Symbol{stmt.var_name, int_type, /*is_mutable=*/false, stmt_span});
    check_block(stmt.body, loop_scope);
}

void TypeChecker::check_return(const ReturnStmt& stmt, const Span& stmt_span, Scope& scope) {
    Type expected =
        current_function_->return_type ? resolve_type_ref(*current_function_->return_type) : Type::primitive(TypeKind::Void);

    if (!stmt.value) {
        if (expected.kind != TypeKind::Void) {
            error(stmt_span, "expected a return value of type `" + to_string(expected) + "`, found none",
                  "missing return value");
        }
        return;
    }

    Type actual = check_expr(*stmt.value, scope);
    if (expected.kind == TypeKind::Void) {
        error(stmt.value->span, "function returns `void`; unexpected return value of type `" + to_string(actual) + "`",
              "this is `" + to_string(actual) + "`");
        return;
    }

    if (actual.kind != TypeKind::Unknown && actual != expected) {
        error(stmt.value->span,
              "cannot return value of type `" + to_string(actual) + "` from function returning `" +
                  to_string(expected) + "`",
              "this is `" + to_string(actual) + "`");
    }
}

Type TypeChecker::check_expr(const Expr& expr, Scope& scope) {
    if (std::get_if<IntLiteral>(&expr.node)) return Type::primitive(TypeKind::Int);
    if (std::get_if<FloatLiteral>(&expr.node)) return Type::primitive(TypeKind::Float);
    if (std::get_if<BoolLiteral>(&expr.node)) return Type::primitive(TypeKind::Bool);
    if (std::get_if<StringLiteral>(&expr.node)) return Type::primitive(TypeKind::String);

    if (const auto* n = std::get_if<Identifier>(&expr.node)) {
        const Symbol* symbol = scope.resolve(n->name);
        if (!symbol) {
            error(expr.span, "undefined variable `" + n->name + "`", "not found in this scope");
            return Type::unknown();
        }
        return symbol->type;
    }
    if (const auto* n = std::get_if<UnaryExpr>(&expr.node)) return check_unary(*n, scope);
    if (const auto* n = std::get_if<BinaryExpr>(&expr.node)) return check_binary(*n, scope);
    if (const auto* n = std::get_if<CallExpr>(&expr.node)) return check_call(*n, scope);
    if (const auto* n = std::get_if<IndexExpr>(&expr.node)) return check_index(*n, scope);

    return Type::unknown();  // unreachable -- every ExprNode alternative is handled above
}

Type TypeChecker::check_unary(const UnaryExpr& node, Scope& scope) {
    Type operand_type = check_expr(*node.operand, scope);
    bool unknown = operand_type.kind == TypeKind::Unknown;

    if (node.op == TokenKind::Minus) {
        if (!unknown && !operand_type.is_numeric()) {
            error(node.operand->span, "cannot negate `" + to_string(operand_type) + "`; expected `int` or `float`",
                  "this is `" + to_string(operand_type) + "`");
            return Type::unknown();
        }
        return unknown ? Type::unknown() : operand_type;
    }

    // node.op == TokenKind::Bang. `!` always yields bool, so return it even
    // on a bad operand -- the enclosing expression's own type isn't in
    // doubt just because this operand was wrong, and returning Unknown here
    // would only cascade a second, redundant diagnostic upward.
    Type bool_type = Type::primitive(TypeKind::Bool);
    if (!unknown && operand_type != bool_type) {
        error(node.operand->span, "expected `bool`, found `" + to_string(operand_type) + "`",
              "this is `" + to_string(operand_type) + "`");
    }
    return bool_type;
}

Type TypeChecker::check_binary(const BinaryExpr& node, Scope& scope) {
    Type lhs_type = check_expr(*node.lhs, scope);
    Type rhs_type = check_expr(*node.rhs, scope);
    bool lhs_unknown = lhs_type.kind == TypeKind::Unknown;
    bool rhs_unknown = rhs_type.kind == TypeKind::Unknown;

    switch (node.op) {
        case TokenKind::Plus:
        case TokenKind::Minus:
        case TokenKind::Star:
        case TokenKind::Slash:
        case TokenKind::Percent: {
            if (lhs_unknown || rhs_unknown) return Type::unknown();
            if (!lhs_type.is_numeric()) {
                error(node.lhs->span, "expected `int` or `float`, found `" + to_string(lhs_type) + "`",
                      "this is `" + to_string(lhs_type) + "`");
                return Type::unknown();
            }
            if (rhs_type != lhs_type) {
                error(node.rhs->span, "expected `" + to_string(lhs_type) + "`, found `" + to_string(rhs_type) + "`",
                      "this is `" + to_string(rhs_type) + "`");
                return Type::unknown();
            }
            return lhs_type;
        }

        case TokenKind::Less:
        case TokenKind::LessEq:
        case TokenKind::Greater:
        case TokenKind::GreaterEq: {
            // Comparisons always yield bool by definition, so that's what
            // is returned below regardless of whether the operands were
            // valid -- returning Unknown instead would just cascade a
            // second diagnostic into whatever uses this comparison's
            // result (e.g. an `if`).
            if (!lhs_unknown && !rhs_unknown) {
                if (!lhs_type.is_numeric()) {
                    error(node.lhs->span, "expected `int` or `float`, found `" + to_string(lhs_type) + "`",
                          "this is `" + to_string(lhs_type) + "`");
                } else if (rhs_type != lhs_type) {
                    error(node.rhs->span, "expected `" + to_string(lhs_type) + "`, found `" + to_string(rhs_type) + "`",
                          "this is `" + to_string(rhs_type) + "`");
                }
            }
            return Type::primitive(TypeKind::Bool);
        }

        case TokenKind::EqEq:
        case TokenKind::BangEq: {
            if (!lhs_unknown && !rhs_unknown && rhs_type != lhs_type) {
                error(node.rhs->span,
                      "cannot compare `" + to_string(lhs_type) + "` with `" + to_string(rhs_type) + "`",
                      "this is `" + to_string(rhs_type) + "`");
            }
            return Type::primitive(TypeKind::Bool);
        }

        case TokenKind::AmpAmp:
        case TokenKind::PipePipe: {
            Type bool_type = Type::primitive(TypeKind::Bool);
            if (!lhs_unknown && lhs_type != bool_type) {
                error(node.lhs->span, "expected `bool`, found `" + to_string(lhs_type) + "`",
                      "this is `" + to_string(lhs_type) + "`");
            }
            if (!rhs_unknown && rhs_type != bool_type) {
                error(node.rhs->span, "expected `bool`, found `" + to_string(rhs_type) + "`",
                      "this is `" + to_string(rhs_type) + "`");
            }
            return bool_type;
        }

        default: return Type::unknown();  // unreachable -- every BinaryExpr::op is one of the above
    }
}

Type TypeChecker::check_call(const CallExpr& node, Scope& scope) {
    // The callee is deliberately not resolved or type-checked at all: there
    // is no function symbol table yet, so treating `print` or `fib` here as
    // an undefined *variable* would be wrong. Arguments are still walked so
    // a type error nested inside one is still caught.
    for (const ExprPtr& arg : node.args) {
        check_expr(*arg, scope);
    }
    return Type::unknown();
}

Type TypeChecker::check_index(const IndexExpr& node, Scope& scope) {
    check_expr(*node.object, scope);
    check_expr(*node.index, scope);
    return Type::unknown();
}

}  // namespace vex
