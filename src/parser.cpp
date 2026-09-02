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
