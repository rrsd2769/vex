#include "vex/type_checker.hpp"

#include <algorithm>
#include <functional>
#include <vector>

namespace vex {

namespace {

// Levenshtein edit distance, for "did you mean `count`?" suggestions.
// Classic O(n*m) DP over two rows -- these are identifier-length strings,
// not a hot path.
std::size_t edit_distance(const std::string& a, const std::string& b) {
    std::vector<std::size_t> prev(b.size() + 1), curr(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) prev[j] = j;

    for (std::size_t i = 1; i <= a.size(); ++i) {
        curr[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            std::size_t cost = a[i - 1] == b[j - 1] ? 0 : 1;
            curr[j] = std::min({prev[j] + 1, curr[j - 1] + 1, prev[j - 1] + cost});
        }
        std::swap(prev, curr);
    }
    return prev[b.size()];
}

// Nearest candidate to `name` within a distance threshold that scales with
// its length -- a one- or two-character typo on a short identifier should
// still match, but a wildly different name shouldn't suggest something
// unrelated just because the candidate list is long.
std::optional<std::string> closest_match(const std::string& name, const std::vector<std::string>& candidates) {
    std::optional<std::string> best;
    std::size_t best_distance = 0;
    std::size_t threshold = name.size() <= 3 ? 1 : (name.size() <= 6 ? 2 : 3);

    for (const std::string& candidate : candidates) {
        if (candidate == name) continue;  // exact match belongs to a real resolution, not a typo
        std::size_t distance = edit_distance(name, candidate);
        if (distance <= threshold && (!best || distance < best_distance)) {
            best = candidate;
            best_distance = distance;
        }
    }
    return best;
}

// Definite-return analysis (week 5): does every path through `stmt`/`block`
// end in a `return`? Conservative on purpose -- a `while`/`for` loop is
// never assumed to run at least once (no loop-bound analysis), so a
// function that only returns inside one is still flagged as not returning
// on every path. An `if` counts only when it has an `else` and both
// branches always return; a bare `if` can always fall through.
bool block_always_returns(const Block& block);

bool stmt_always_returns(const Stmt& stmt) {
    if (std::get_if<ReturnStmt>(&stmt.node)) return true;
    if (const auto* n = std::get_if<IfStmt>(&stmt.node)) {
        return n->else_block.has_value() && block_always_returns(n->then_block) &&
               block_always_returns(*n->else_block);
    }
    return false;
}

bool block_always_returns(const Block& block) {
    for (const StmtPtr& stmt : block.statements) {
        if (stmt_always_returns(*stmt)) return true;
    }
    return false;
}

// The one builtin this language has -- see type_checker.hpp's header
// comment for why it's special-cased here instead of going through
// functions_.
constexpr const char* kBuiltinPrint = "print";

}  // namespace

TypeChecker::TypeChecker(const Program& program) : program_(program) {}

void TypeChecker::error(const Span& span, std::string message, std::string label) {
    error(span, std::move(message), std::move(label), {});
}

void TypeChecker::error(const Span& span, std::string message, std::string label, std::vector<Label> secondary) {
    diagnostics_.push_back(Diagnostic{
        Severity::Error,
        std::move(message),
        Label{span, std::move(label)},
        std::move(secondary),
        std::nullopt,
    });
}

void TypeChecker::error_with_suggestion(const Span& span, std::string message, std::string label,
                                         const std::string& near_name, const std::vector<std::string>& candidates,
                                         std::vector<Label> secondary) {
    std::optional<std::string> suggestion = closest_match(near_name, candidates);
    Diagnostic diagnostic{
        Severity::Error,
        std::move(message),
        Label{span, std::move(label)},
        std::move(secondary),
        std::nullopt,
    };
    if (suggestion) {
        diagnostic.suggestion = Suggestion{"did you mean `" + *suggestion + "`?", span, *suggestion};
    }
    diagnostics_.push_back(std::move(diagnostic));
}

void TypeChecker::check() {
    register_structs();
    register_functions();
    check_no_cyclic_structs();
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
            if (auto it = item_spans_.find(st->name); it != item_spans_.end()) {
                error(item.span, "redeclaration of `" + st->name + "`", "already declared",
                      {Label{it->second, "first declared here"}});
                continue;
            }
            structs_[st->name] = st;
            item_spans_[st->name] = item.span;
        }
    }
}

void TypeChecker::register_functions() {
    for (const Item& item : program_.items) {
        if (const auto* fn = std::get_if<FunctionDecl>(&item.node)) {
            if (auto it = item_spans_.find(fn->name); it != item_spans_.end()) {
                error(item.span, "redeclaration of `" + fn->name + "`", "already declared",
                      {Label{it->second, "first declared here"}});
                continue;
            }
            functions_[fn->name] = fn;
            item_spans_[fn->name] = item.span;
        }
    }
}

Type TypeChecker::resolve_type_ref(const TypeRef& ref) {
    if (std::optional<Type> resolved = try_resolve_type_ref(ref, structs_)) {
        return *resolved;
    }
    std::vector<std::string> candidates{"int", "float", "bool", "string"};
    for (const auto& [name, decl] : structs_) candidates.push_back(name);
    error_with_suggestion(ref.span, "unknown type `" + ref.name + "`", "no such type", ref.name, candidates);
    return Type::unknown();
}

namespace {
enum class VisitState { Unvisited, InProgress, Done };
}  // namespace

void TypeChecker::check_no_cyclic_structs() {
    std::unordered_map<std::string, VisitState> state;
    for (const auto& [name, decl] : structs_) state[name] = VisitState::Unvisited;

    // DFS from every struct; a field (or an array field's element) typed as
    // a struct currently InProgress on this DFS's path is a back-edge --
    // that struct contains itself, directly or through a chain of other
    // structs, and so has no finite size.
    std::function<void(const std::string&)> visit = [&](const std::string& name) {
        if (state[name] == VisitState::Done) return;
        state[name] = VisitState::InProgress;

        const StructDecl& decl = *structs_.at(name);
        for (const Field& field : decl.fields) {
            std::optional<Type> field_type = try_resolve_type_ref(field.type, structs_);
            if (!field_type) continue;  // unknown type name -- resolve_type_ref() will report it separately
            Type element = *field_type;
            while (element.kind == TypeKind::Array) element = *element.element_type;
            if (element.kind != TypeKind::Struct) continue;

            if (state[element.struct_name] == VisitState::InProgress) {
                error(field.type.span,
                      "struct `" + name + "` cannot contain itself" +
                          (element.struct_name == name ? "" : " (via `" + element.struct_name + "`)"),
                      "`" + field.name + ": " + to_string(*field_type) + "` would make `" + name +
                          "` infinitely large");
                continue;
            }
            visit(element.struct_name);
        }

        state[name] = VisitState::Done;
    };

    for (const auto& [name, decl] : structs_) visit(name);
}

void TypeChecker::check_struct(const StructDecl& st) {
    // Resolving each field's TypeRef is enough to catch a typo'd type name.
    // Field *access* on a value of this struct type is checked in
    // check_field_access(), not here.
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

    if (fn.return_type && !block_always_returns(fn.body)) {
        error(fn.body.span, "function `" + fn.name + "` doesn't return a value on all code paths",
              "not every path through this body returns",
              {Label{fn.return_type->span, "expected because the return type is declared `" +
                                                to_string(resolve_type_ref(*fn.return_type)) + "` here"}});
    }

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
                  "this is `" + to_string(init_type) + "`",
                  {Label{decl.type->span, "declared as `" + to_string(declared_type) + "` here"}});
        }
    }

    if (!scope.declare(Symbol{decl.name, declared_type, decl.is_mutable, stmt_span})) {
        error(stmt_span, "redeclaration of `" + decl.name + "`", "already declared in this scope");
    }
}

const Symbol* TypeChecker::assignment_root(const Expr& target, Scope& scope) {
    if (const auto* ident = std::get_if<Identifier>(&target.node)) return scope.resolve(ident->name);
    if (const auto* field = std::get_if<FieldAccessExpr>(&target.node)) return assignment_root(*field->object, scope);
    if (const auto* index = std::get_if<IndexExpr>(&target.node)) return assignment_root(*index->object, scope);
    return nullptr;
}

void TypeChecker::check_assign(const AssignStmt& stmt, Scope& scope) {
    Type value_type = check_expr(*stmt.value, scope);
    Type target_type = check_expr(*stmt.target, scope);

    const Symbol* root = assignment_root(*stmt.target, scope);
    if (root && !root->is_mutable) {
        error(stmt.target->span, "cannot assign to immutable variable `" + root->name + "`",
              "declared with `let`, not `var`", {Label{root->decl_span, "declared here"}});
    }

    if (target_type.kind != TypeKind::Unknown && value_type.kind != TypeKind::Unknown && target_type != value_type) {
        std::vector<Label> secondary;
        if (root) secondary.push_back(Label{root->decl_span, "declared as `" + to_string(target_type) + "` here"});
        error(stmt.value->span,
              "cannot assign value of type `" + to_string(value_type) + "` to variable of type `" +
                  to_string(target_type) + "`",
              "this is `" + to_string(value_type) + "`", std::move(secondary));
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
        std::vector<Label> secondary;
        if (current_function_->return_type) {
            secondary.push_back(
                Label{current_function_->return_type->span, "return type declared as `" + to_string(expected) + "` here"});
        }
        error(stmt.value->span,
              "cannot return value of type `" + to_string(actual) + "` from function returning `" +
                  to_string(expected) + "`",
              "this is `" + to_string(actual) + "`", std::move(secondary));
    }
}

Type TypeChecker::check_expr(const Expr& expr, Scope& scope) {
    Type type = check_expr_impl(expr, scope);
    expr_types_[&expr] = type;
    return type;
}

Type TypeChecker::check_expr_impl(const Expr& expr, Scope& scope) {
    if (std::get_if<IntLiteral>(&expr.node)) return Type::primitive(TypeKind::Int);
    if (std::get_if<FloatLiteral>(&expr.node)) return Type::primitive(TypeKind::Float);
    if (std::get_if<BoolLiteral>(&expr.node)) return Type::primitive(TypeKind::Bool);
    if (std::get_if<StringLiteral>(&expr.node)) return Type::primitive(TypeKind::String);

    if (const auto* n = std::get_if<Identifier>(&expr.node)) {
        const Symbol* symbol = scope.resolve(n->name);
        if (!symbol) {
            error_with_suggestion(expr.span, "undefined variable `" + n->name + "`", "not found in this scope",
                                   n->name, scope.visible_names());
            return Type::unknown();
        }
        return symbol->type;
    }
    if (const auto* n = std::get_if<UnaryExpr>(&expr.node)) return check_unary(*n, scope);
    if (const auto* n = std::get_if<BinaryExpr>(&expr.node)) return check_binary(*n, scope);
    if (const auto* n = std::get_if<CallExpr>(&expr.node)) return check_call(*n, expr.span, scope);
    if (const auto* n = std::get_if<IndexExpr>(&expr.node)) return check_index(*n, scope);
    if (const auto* n = std::get_if<FieldAccessExpr>(&expr.node)) return check_field_access(*n, scope);
    if (const auto* n = std::get_if<ArrayLiteral>(&expr.node)) return check_array_literal(*n, expr.span, scope);

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

Type TypeChecker::check_call(const CallExpr& node, const Span& call_span, Scope& scope) {
    const auto* callee = std::get_if<Identifier>(&node.callee->node);
    if (!callee) {
        // The language has no first-class functions, so a callee that isn't
        // a bare name can never be resolved. `(f)(1)` doesn't reach here --
        // grouping parens produce no node (ast.hpp) -- but chained call
        // syntax does: `f()()` parses fine (parse_postfix loops), giving a
        // CallExpr whose own callee is itself a CallExpr. This used to
        // silently return Unknown with no diagnostic, which meant such a
        // program type-checked clean and only broke later, when week 6's
        // BytecodeCompiler tried to compile a call whose callee wasn't an
        // Identifier -- caught while building that compiler and fixed here,
        // since "no diagnostics" is supposed to mean every later stage can
        // trust the program.
        error(node.callee->span, "cannot call this expression", "only a function or struct name can be called");
        check_expr(*node.callee, scope);
        for (const ExprPtr& arg : node.args) check_expr(*arg, scope);
        return Type::unknown();
    }

    const std::string& name = callee->name;
    if (name == kBuiltinPrint) return check_call_to_builtin(node, call_span, scope);
    if (auto it = functions_.find(name); it != functions_.end()) return check_call_to_function(*it->second, node, call_span, scope);
    if (auto it = structs_.find(name); it != structs_.end()) return check_call_to_struct(*it->second, node, call_span, scope);

    for (const ExprPtr& arg : node.args) check_expr(*arg, scope);

    std::vector<std::string> candidates{kBuiltinPrint};
    for (const auto& [fn_name, decl] : functions_) candidates.push_back(fn_name);
    for (const auto& [st_name, decl] : structs_) candidates.push_back(st_name);
    error_with_suggestion(node.callee->span, "undefined function `" + name + "`", "not found", name, candidates);
    return Type::unknown();
}

Type TypeChecker::check_call_to_builtin(const CallExpr& node, const Span& call_span, Scope& scope) {
    if (node.args.empty()) {
        error(call_span, "`print` expects at least one argument", "called with no arguments");
    }
    for (const ExprPtr& arg : node.args) check_expr(*arg, scope);
    return Type::primitive(TypeKind::Void);
}

Type TypeChecker::check_call_to_function(const FunctionDecl& fn, const CallExpr& node, const Span& call_span, Scope& scope) {
    if (node.args.size() != fn.params.size()) {
        error(call_span,
              "function `" + fn.name + "` expects " + std::to_string(fn.params.size()) + " argument(s), found " +
                  std::to_string(node.args.size()),
              "wrong number of arguments");
    }

    std::size_t checked = std::min(node.args.size(), fn.params.size());
    for (std::size_t i = 0; i < checked; ++i) {
        Type arg_type = check_expr(*node.args[i], scope);
        Type param_type = resolve_type_ref(fn.params[i].type);
        if (arg_type.kind != TypeKind::Unknown && param_type.kind != TypeKind::Unknown && arg_type != param_type) {
            error(node.args[i]->span,
                  "argument " + std::to_string(i + 1) + " to `" + fn.name + "` should be `" + to_string(param_type) +
                      "`, found `" + to_string(arg_type) + "`",
                  "this is `" + to_string(arg_type) + "`",
                  {Label{fn.params[i].type.span, "parameter declared as `" + to_string(param_type) + "` here"}});
        }
    }
    // Extra arguments beyond the declared arity are still walked, so a type
    // error nested inside one is still caught even though the arity
    // mismatch was already reported above.
    for (std::size_t i = checked; i < node.args.size(); ++i) check_expr(*node.args[i], scope);

    return fn.return_type ? resolve_type_ref(*fn.return_type) : Type::primitive(TypeKind::Void);
}

Type TypeChecker::check_call_to_struct(const StructDecl& st, const CallExpr& node, const Span& call_span, Scope& scope) {
    if (node.args.size() != st.fields.size()) {
        error(call_span,
              "`" + st.name + "` has " + std::to_string(st.fields.size()) + " field(s), found " +
                  std::to_string(node.args.size()) + " argument(s)",
              "wrong number of arguments");
    }

    std::size_t checked = std::min(node.args.size(), st.fields.size());
    for (std::size_t i = 0; i < checked; ++i) {
        Type arg_type = check_expr(*node.args[i], scope);
        Type field_type = resolve_type_ref(st.fields[i].type);
        if (arg_type.kind != TypeKind::Unknown && field_type.kind != TypeKind::Unknown && arg_type != field_type) {
            error(node.args[i]->span,
                  "field `" + st.fields[i].name + "` of `" + st.name + "` should be `" + to_string(field_type) +
                      "`, found `" + to_string(arg_type) + "`",
                  "this is `" + to_string(arg_type) + "`",
                  {Label{st.fields[i].type.span, "field declared as `" + to_string(field_type) + "` here"}});
        }
    }
    for (std::size_t i = checked; i < node.args.size(); ++i) check_expr(*node.args[i], scope);

    return Type::make_struct(st.name);
}

Type TypeChecker::check_index(const IndexExpr& node, Scope& scope) {
    Type object_type = check_expr(*node.object, scope);
    Type index_type = check_expr(*node.index, scope);
    Type int_type = Type::primitive(TypeKind::Int);

    if (index_type.kind != TypeKind::Unknown && index_type != int_type) {
        error(node.index->span, "array index must be of type `int`, found `" + to_string(index_type) + "`",
              "this is `" + to_string(index_type) + "`");
    }

    if (object_type.kind == TypeKind::Unknown) return Type::unknown();
    if (object_type.kind != TypeKind::Array) {
        error(node.object->span, "cannot index into type `" + to_string(object_type) + "`",
              "this is `" + to_string(object_type) + "`");
        return Type::unknown();
    }

    // A literal index is known at compile time, so its bound can be too --
    // a cheap, worthwhile check now that array size is part of the type.
    if (const auto* literal = std::get_if<IntLiteral>(&node.index->node)) {
        if (literal->value < 0 || static_cast<std::uint64_t>(literal->value) >= object_type.array_size) {
            error(node.index->span,
                  "index " + std::to_string(literal->value) + " out of bounds for array of size " +
                      std::to_string(object_type.array_size),
                  "out of bounds");
        }
    }

    return *object_type.element_type;
}

Type TypeChecker::check_field_access(const FieldAccessExpr& node, Scope& scope) {
    Type object_type = check_expr(*node.object, scope);
    if (object_type.kind == TypeKind::Unknown) return Type::unknown();

    if (object_type.kind != TypeKind::Struct) {
        error(node.field_span, "cannot access field `" + node.field + "` on type `" + to_string(object_type) + "`",
              "this is `" + to_string(object_type) + "`");
        return Type::unknown();
    }

    const StructDecl& st = *structs_.at(object_type.struct_name);
    for (const Field& field : st.fields) {
        if (field.name == node.field) return resolve_type_ref(field.type);
    }

    std::vector<std::string> candidates;
    for (const Field& field : st.fields) candidates.push_back(field.name);
    std::vector<Label> secondary;
    if (auto it = item_spans_.find(st.name); it != item_spans_.end()) {
        secondary.push_back(Label{it->second, "struct `" + st.name + "` declared here"});
    }
    error_with_suggestion(node.field_span, "no field `" + node.field + "` on struct `" + st.name + "`", "no such field",
                           node.field, candidates, std::move(secondary));
    return Type::unknown();
}

Type TypeChecker::check_array_literal(const ArrayLiteral& node, const Span& array_span, Scope& scope) {
    if (node.elements.empty()) {
        // An empty array literal has no element to infer a type from, and
        // this language has no explicit `T[]` empty-array syntax. Report it
        // once here rather than letting every downstream use see Unknown
        // with no explanation.
        error(array_span, "cannot infer the element type of an empty array literal", "empty array literal");
        return Type::unknown();
    }

    Type element_type = check_expr(*node.elements[0], scope);
    for (std::size_t i = 1; i < node.elements.size(); ++i) {
        Type this_type = check_expr(*node.elements[i], scope);
        if (element_type.kind != TypeKind::Unknown && this_type.kind != TypeKind::Unknown &&
            this_type != element_type) {
            error(node.elements[i]->span,
                  "array element should be `" + to_string(element_type) + "`, found `" + to_string(this_type) + "`",
                  "this is `" + to_string(this_type) + "`",
                  {Label{node.elements[0]->span, "element type inferred as `" + to_string(element_type) + "` here"}});
        }
    }

    if (element_type.kind == TypeKind::Unknown) return Type::unknown();
    return Type::make_array(element_type, static_cast<std::uint32_t>(node.elements.size()));
}

}  // namespace vex
