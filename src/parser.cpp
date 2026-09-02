#include "vex/parser.hpp"

#include <charconv>
#include <memory>

namespace vex {

namespace {

// Precedence of each binary operator, low to high. -1 means "not a binary
// operator". All of these are left-associative -- there's nothing in the
// language yet that isn't (no assignment or exponent expression forms).
int binary_precedence(TokenKind kind) {
    switch (kind) {
        case TokenKind::PipePipe: return 1;
        case TokenKind::AmpAmp: return 2;
        case TokenKind::EqEq:
        case TokenKind::BangEq: return 3;
        case TokenKind::Less:
        case TokenKind::LessEq:
        case TokenKind::Greater:
        case TokenKind::GreaterEq: return 4;
        case TokenKind::Plus:
        case TokenKind::Minus: return 5;
        case TokenKind::Star:
        case TokenKind::Slash:
        case TokenKind::Percent: return 6;
        default: return -1;
    }
}

}  // namespace

Parser::Parser(std::vector<Token> tokens, const SourceManager& source)
    : tokens_(std::move(tokens)), source_(source) {}

const Token& Parser::peek() const { return tokens_[pos_]; }

const Token& Parser::advance() {
    const Token& tok = tokens_[pos_];
    if (tok.kind != TokenKind::Eof) ++pos_;
    return tok;
}

bool Parser::check(TokenKind kind) const { return peek().kind == kind; }

bool Parser::match(TokenKind kind) {
    if (!check(kind)) return false;
    advance();
    return true;
}

const Token* Parser::expect(TokenKind kind, const std::string& what) {
    if (check(kind)) return &advance();
    error(peek(), "expected " + what + ", found " + token_kind_name(peek().kind));
    return nullptr;
}

std::string_view Parser::lexeme(const Token& tok) const {
    return source_.text().substr(tok.span.start, tok.span.end - tok.span.start);
}

void Parser::error(const Token& tok, const std::string& message) {
    diagnostics_.push_back(Diagnostic{
        Severity::Error,
        message,
        Label{tok.span, "unexpected"},
        {},
        std::nullopt,
    });
}

ExprPtr Parser::parse_expression() { return parse_binary(0); }

ExprPtr Parser::parse_binary(int min_precedence) {
    ExprPtr lhs = parse_unary();
    if (!lhs) return nullptr;

    for (;;) {
        int prec = binary_precedence(peek().kind);
        if (prec < min_precedence) break;

        Token op = advance();
        // prec + 1 keeps this left-associative: a same-precedence operator
        // to the right does not get folded into this call's rhs.
        ExprPtr rhs = parse_binary(prec + 1);
        if (!rhs) return nullptr;

        Span span{lhs->span.start, rhs->span.end};
        lhs = std::make_unique<Expr>(Expr{span, BinaryExpr{op.kind, std::move(lhs), std::move(rhs)}});
    }
    return lhs;
}

ExprPtr Parser::parse_unary() {
    if (check(TokenKind::Minus) || check(TokenKind::Bang)) {
        Token op = advance();
        ExprPtr operand = parse_unary();
        if (!operand) return nullptr;

        Span span{op.span.start, operand->span.end};
        return std::make_unique<Expr>(Expr{span, UnaryExpr{op.kind, std::move(operand)}});
    }
    return parse_postfix();
}

ExprPtr Parser::parse_postfix() {
    ExprPtr expr = parse_primary();
    if (!expr) return nullptr;

    for (;;) {
        if (match(TokenKind::LParen)) {
            std::vector<ExprPtr> args;
            if (!check(TokenKind::RParen)) {
                for (;;) {
                    ExprPtr arg = parse_binary(0);
                    if (!arg) return nullptr;
                    args.push_back(std::move(arg));
                    if (!match(TokenKind::Comma)) break;
                }
            }
            const Token* rparen = expect(TokenKind::RParen, "`)` to close call arguments");
            if (!rparen) return nullptr;

            Span span{expr->span.start, rparen->span.end};
            expr = std::make_unique<Expr>(Expr{span, CallExpr{std::move(expr), std::move(args)}});
        } else if (match(TokenKind::LBracket)) {
            ExprPtr index = parse_binary(0);
            if (!index) return nullptr;
            const Token* rbracket = expect(TokenKind::RBracket, "`]` to close index expression");
            if (!rbracket) return nullptr;

            Span span{expr->span.start, rbracket->span.end};
            expr = std::make_unique<Expr>(Expr{span, IndexExpr{std::move(expr), std::move(index)}});
        } else {
            break;
        }
    }
    return expr;
}

ExprPtr Parser::parse_primary() {
    if (check(TokenKind::IntLiteral)) return make_int_literal(advance());
    if (check(TokenKind::FloatLiteral)) return make_float_literal(advance());
    if (check(TokenKind::StringLiteral)) return make_string_literal(advance());

    if (check(TokenKind::KwTrue) || check(TokenKind::KwFalse)) {
        Token tok = advance();
        return std::make_unique<Expr>(Expr{tok.span, BoolLiteral{tok.kind == TokenKind::KwTrue}});
    }

    if (check(TokenKind::Identifier)) {
        Token tok = advance();
        return std::make_unique<Expr>(Expr{tok.span, Identifier{std::string(lexeme(tok))}});
    }

    if (match(TokenKind::LParen)) {
        ExprPtr inner = parse_binary(0);
        if (!inner) return nullptr;
        if (!expect(TokenKind::RParen, "`)` to close grouped expression")) return nullptr;
        return inner;
    }

    error(peek(), "expected an expression, found " + std::string(token_kind_name(peek().kind)));
    return nullptr;
}

ExprPtr Parser::make_int_literal(const Token& tok) {
    std::string_view text = lexeme(tok);
    std::int64_t value = 0;
    std::from_chars(text.data(), text.data() + text.size(), value);
    return std::make_unique<Expr>(Expr{tok.span, IntLiteral{value}});
}

ExprPtr Parser::make_float_literal(const Token& tok) {
    std::string_view text = lexeme(tok);
    double value = 0.0;
    std::from_chars(text.data(), text.data() + text.size(), value);
    return std::make_unique<Expr>(Expr{tok.span, FloatLiteral{value}});
}

// ---------------------------------------------------------------------
// Statements, items, and error recovery (week 3)
// ---------------------------------------------------------------------

void Parser::synchronize_statement() {
    while (!check(TokenKind::Eof)) {
        switch (peek().kind) {
            case TokenKind::KwLet:
            case TokenKind::KwVar:
            case TokenKind::KwIf:
            case TokenKind::KwWhile:
            case TokenKind::KwFor:
            case TokenKind::KwReturn:
            case TokenKind::KwFn:
            case TokenKind::KwStruct:
            case TokenKind::RBrace: return;
            default: break;
        }
        // Not already at a safe boundary: consume this token. If it turns
        // out to be the `;` that ends the broken statement, that's a safe
        // boundary too -- stop right after it instead of eating the next
        // statement's tokens looking for one of the keywords above.
        if (advance().kind == TokenKind::Semicolon) return;
    }
}

void Parser::synchronize_item() {
    while (!check(TokenKind::Eof)) {
        if (check(TokenKind::KwFn) || check(TokenKind::KwStruct)) return;
        advance();
    }
}

std::optional<Block> Parser::parse_block() {
    const Token* lbrace = expect(TokenKind::LBrace, "`{` to start a block");
    if (!lbrace) return std::nullopt;

    std::vector<StmtPtr> statements;
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        if (StmtPtr stmt = parse_statement()) {
            statements.push_back(std::move(stmt));
        } else {
            synchronize_statement();
        }
    }

    const Token* rbrace = expect(TokenKind::RBrace, "`}` to close block");
    Offset end = rbrace ? rbrace->span.end : peek().span.end;
    return Block{std::move(statements), Span{lbrace->span.start, end}};
}

StmtPtr Parser::parse_statement() {
    switch (peek().kind) {
        case TokenKind::KwLet:
        case TokenKind::KwVar: return parse_var_decl();
        case TokenKind::KwIf: return parse_if();
        case TokenKind::KwWhile: return parse_while();
        case TokenKind::KwFor: return parse_for();
        case TokenKind::KwReturn: return parse_return();
        default: return parse_expr_or_assign_statement();
    }
}

std::optional<TypeRef> Parser::parse_type_ref(const std::string& what) {
    const Token* tok = expect(TokenKind::Identifier, what);
    if (!tok) return std::nullopt;
    return TypeRef{std::string(lexeme(*tok)), tok->span};
}

StmtPtr Parser::parse_var_decl() {
    Token kw = advance();  // `let` or `var`
    bool is_mutable = kw.kind == TokenKind::KwVar;

    const Token* name_tok = expect(TokenKind::Identifier, "a variable name");
    if (!name_tok) return nullptr;
    std::string name(lexeme(*name_tok));

    std::optional<TypeRef> type;
    if (match(TokenKind::Colon)) {
        type = parse_type_ref("a type name");
        if (!type) return nullptr;
    }

    if (!expect(TokenKind::Eq, "`=` in variable declaration")) return nullptr;

    ExprPtr init = parse_expression();
    if (!init) return nullptr;

    const Token* semi = expect(TokenKind::Semicolon, "`;` after variable declaration");
    if (!semi) return nullptr;

    Span span{kw.span.start, semi->span.end};
    return std::make_unique<Stmt>(Stmt{span, VarDecl{is_mutable, std::move(name), std::move(type), std::move(init)}});
}

StmtPtr Parser::parse_if() {
    Token kw = advance();  // `if`

    ExprPtr condition = parse_expression();
    if (!condition) return nullptr;

    std::optional<Block> then_block = parse_block();
    if (!then_block) return nullptr;

    std::optional<Block> else_block;
    Offset end = then_block->span.end;
    if (match(TokenKind::KwElse)) {
        if (check(TokenKind::KwIf)) {
            // `else if` desugars to a Block holding just the nested if, so
            // IfStmt only ever needs one else-branch shape.
            StmtPtr nested = parse_if();
            if (!nested) return nullptr;
            Span nested_span = nested->span;
            std::vector<StmtPtr> wrapped;
            wrapped.push_back(std::move(nested));
            else_block = Block{std::move(wrapped), nested_span};
        } else {
            else_block = parse_block();
            if (!else_block) return nullptr;
        }
        end = else_block->span.end;
    }

    Span span{kw.span.start, end};
    return std::make_unique<Stmt>(
        Stmt{span, IfStmt{std::move(condition), std::move(*then_block), std::move(else_block)}});
}

StmtPtr Parser::parse_while() {
    Token kw = advance();  // `while`

    ExprPtr condition = parse_expression();
    if (!condition) return nullptr;

    std::optional<Block> body = parse_block();
    if (!body) return nullptr;

    Span span{kw.span.start, body->span.end};
    return std::make_unique<Stmt>(Stmt{span, WhileStmt{std::move(condition), std::move(*body)}});
}

StmtPtr Parser::parse_for() {
    Token kw = advance();  // `for`

    const Token* name_tok = expect(TokenKind::Identifier, "a loop variable name");
    if (!name_tok) return nullptr;
    std::string var_name(lexeme(*name_tok));

    if (!expect(TokenKind::KwIn, "`in`")) return nullptr;

    ExprPtr start = parse_expression();
    if (!start) return nullptr;
    if (!expect(TokenKind::DotDot, "`..` in a for-range")) return nullptr;
    ExprPtr end_expr = parse_expression();
    if (!end_expr) return nullptr;

    std::optional<Block> body = parse_block();
    if (!body) return nullptr;

    Span span{kw.span.start, body->span.end};
    return std::make_unique<Stmt>(
        Stmt{span, ForStmt{std::move(var_name), std::move(start), std::move(end_expr), std::move(*body)}});
}

StmtPtr Parser::parse_return() {
    Token kw = advance();  // `return`

    ExprPtr value = nullptr;
    if (!check(TokenKind::Semicolon)) {
        value = parse_expression();
        if (!value) return nullptr;
    }

    const Token* semi = expect(TokenKind::Semicolon, "`;` after return");
    if (!semi) return nullptr;

    Span span{kw.span.start, semi->span.end};
    return std::make_unique<Stmt>(Stmt{span, ReturnStmt{std::move(value)}});
}

StmtPtr Parser::parse_expr_or_assign_statement() {
    ExprPtr expr = parse_expression();
    if (!expr) return nullptr;

    if (match(TokenKind::Eq)) {
        ExprPtr value = parse_expression();
        if (!value) return nullptr;
        const Token* semi = expect(TokenKind::Semicolon, "`;` after assignment");
        if (!semi) return nullptr;

        Span span{expr->span.start, semi->span.end};
        return std::make_unique<Stmt>(Stmt{span, AssignStmt{std::move(expr), std::move(value)}});
    }

    const Token* semi = expect(TokenKind::Semicolon, "`;` after expression statement");
    if (!semi) return nullptr;

    Span span{expr->span.start, semi->span.end};
    return std::make_unique<Stmt>(Stmt{span, ExprStmt{std::move(expr)}});
}

std::optional<Item> Parser::parse_function_decl() {
    Token kw = advance();  // `fn`

    const Token* name_tok = expect(TokenKind::Identifier, "a function name");
    if (!name_tok) return std::nullopt;
    std::string name(lexeme(*name_tok));

    if (!expect(TokenKind::LParen, "`(` after function name")) return std::nullopt;

    std::vector<Param> params;
    if (!check(TokenKind::RParen)) {
        for (;;) {
            const Token* param_name = expect(TokenKind::Identifier, "a parameter name");
            if (!param_name) return std::nullopt;
            if (!expect(TokenKind::Colon, "`:` after parameter name")) return std::nullopt;
            std::optional<TypeRef> param_type = parse_type_ref("a parameter type");
            if (!param_type) return std::nullopt;
            params.push_back(Param{std::string(lexeme(*param_name)), std::move(*param_type)});

            if (!match(TokenKind::Comma)) break;
            if (check(TokenKind::RParen)) break;  // trailing comma
        }
    }
    if (!expect(TokenKind::RParen, "`)` to close parameters")) return std::nullopt;

    std::optional<TypeRef> return_type;
    if (match(TokenKind::Arrow)) {
        return_type = parse_type_ref("a return type");
        if (!return_type) return std::nullopt;
    }

    std::optional<Block> body = parse_block();
    if (!body) return std::nullopt;

    Span span{kw.span.start, body->span.end};
    return Item{span, FunctionDecl{std::move(name), std::move(params), std::move(return_type), std::move(*body)}};
}

std::optional<Item> Parser::parse_struct_decl() {
    Token kw = advance();  // `struct`

    const Token* name_tok = expect(TokenKind::Identifier, "a struct name");
    if (!name_tok) return std::nullopt;
    std::string name(lexeme(*name_tok));

    if (!expect(TokenKind::LBrace, "`{` after struct name")) return std::nullopt;

    std::vector<Field> fields;
    if (!check(TokenKind::RBrace)) {
        for (;;) {
            const Token* field_name = expect(TokenKind::Identifier, "a field name");
            if (!field_name) return std::nullopt;
            if (!expect(TokenKind::Colon, "`:` after field name")) return std::nullopt;
            std::optional<TypeRef> field_type = parse_type_ref("a field type");
            if (!field_type) return std::nullopt;
            fields.push_back(Field{std::string(lexeme(*field_name)), std::move(*field_type)});

            if (!match(TokenKind::Comma)) break;
            if (check(TokenKind::RBrace)) break;  // trailing comma
        }
    }

    const Token* rbrace = expect(TokenKind::RBrace, "`}` to close struct");
    if (!rbrace) return std::nullopt;

    Span span{kw.span.start, rbrace->span.end};
    return Item{span, StructDecl{std::move(name), std::move(fields)}};
}

Program Parser::parse_program() {
    Program program;
    while (!check(TokenKind::Eof)) {
        std::optional<Item> item;
        if (check(TokenKind::KwFn)) {
            item = parse_function_decl();
        } else if (check(TokenKind::KwStruct)) {
            item = parse_struct_decl();
        } else {
            error(peek(), "expected `fn` or `struct`, found " + std::string(token_kind_name(peek().kind)));
        }

        if (item) {
            program.items.push_back(std::move(*item));
        } else {
            synchronize_item();
        }
    }
    return program;
}

// text is the Lexeme including the surrounding quotes (lexer_test.cpp:
// "the Span of a StringLiteral covers the surrounding quotes"). The lexer
// already validated that every `\x` is a real escape pair before producing
// this token, so this only needs to interpret it -- not re-validate it.
ExprPtr Parser::make_string_literal(const Token& tok) {
    std::string_view text = lexeme(tok);
    std::string value;
    for (std::size_t i = 1; i + 1 < text.size(); ++i) {
        char c = text[i];
        if (c != '\\') {
            value += c;
            continue;
        }
        char next = text[i + 1];
        switch (next) {
            case 'n': value += '\n'; break;
            case 't': value += '\t'; break;
            case 'r': value += '\r'; break;
            case '0': value += '\0'; break;
            case '\\': value += '\\'; break;
            case '"': value += '"'; break;
            default:
                error(tok, std::string("unknown escape sequence `\\") + next + "`");
                value += next;
                break;
        }
        ++i;
    }
    return std::make_unique<Expr>(Expr{tok.span, StringLiteral{std::move(value)}});
}

}  // namespace vex
