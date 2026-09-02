#include "vex/lexer.hpp"

#include <string>
#include <unordered_map>

namespace vex {

namespace {

bool is_digit(char c) { return c >= '0' && c <= '9'; }
bool is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool is_alnum(char c) { return is_alpha(c) || is_digit(c); }

const std::unordered_map<std::string_view, TokenKind>& keywords() {
    static const std::unordered_map<std::string_view, TokenKind> table = {
        {"struct", TokenKind::KwStruct}, {"fn", TokenKind::KwFn},
        {"let", TokenKind::KwLet},       {"var", TokenKind::KwVar},
        {"if", TokenKind::KwIf},         {"else", TokenKind::KwElse},
        {"while", TokenKind::KwWhile},   {"for", TokenKind::KwFor},
        {"in", TokenKind::KwIn},         {"return", TokenKind::KwReturn},
        {"true", TokenKind::KwTrue},     {"false", TokenKind::KwFalse},
    };
    return table;
}

std::string to_hex_byte(unsigned char byte) {
    static const char* digits = "0123456789abcdef";
    return std::string{digits[byte >> 4], digits[byte & 0xF]};
}

}  // namespace

Lexer::Lexer(const SourceManager& source) : text_(source.text()) {}

char Lexer::peek(std::size_t ahead) const {
    std::size_t i = pos_ + ahead;
    return i < text_.size() ? text_[i] : '\0';
}

char Lexer::advance() { return text_[pos_++]; }

bool Lexer::match(char expected) {
    if (peek() != expected) return false;
    ++pos_;
    return true;
}

Token Lexer::make(TokenKind kind, Offset start) const { return Token{kind, Span{start, pos_}}; }

void Lexer::skip_whitespace_and_comments() {
    for (;;) {
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            ++pos_;
        } else if (c == '/' && peek(1) == '/') {
            while (pos_ < text_.size() && peek() != '\n') ++pos_;
        } else {
            return;
        }
    }
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    for (;;) {
        skip_whitespace_and_comments();
        if (pos_ >= text_.size()) {
            tokens.push_back(Token{TokenKind::Eof, Span{pos_, pos_}});
            return tokens;
        }
        if (std::optional<Token> tok = scan_token()) {
            tokens.push_back(*tok);
        }
    }
}

std::optional<Token> Lexer::scan_token() {
    Offset start = pos_;
    char c = advance();

    if (is_alpha(c)) return scan_identifier_or_keyword(start);
    if (is_digit(c)) return scan_number(start);
    if (c == '"') return scan_string(start);

    switch (c) {
        case '(': return make(TokenKind::LParen, start);
        case ')': return make(TokenKind::RParen, start);
        case '{': return make(TokenKind::LBrace, start);
        case '}': return make(TokenKind::RBrace, start);
        case '[': return make(TokenKind::LBracket, start);
        case ']': return make(TokenKind::RBracket, start);
        case ',': return make(TokenKind::Comma, start);
        case ';': return make(TokenKind::Semicolon, start);
        case ':': return make(TokenKind::Colon, start);
        case '.': return make(match('.') ? TokenKind::DotDot : TokenKind::Dot, start);
        case '+': return make(TokenKind::Plus, start);
        case '-': return make(match('>') ? TokenKind::Arrow : TokenKind::Minus, start);
        case '*': return make(TokenKind::Star, start);
        case '/': return make(TokenKind::Slash, start);
        case '%': return make(TokenKind::Percent, start);
        case '=': return make(match('=') ? TokenKind::EqEq : TokenKind::Eq, start);
        case '!': return make(match('=') ? TokenKind::BangEq : TokenKind::Bang, start);
        case '<': return make(match('=') ? TokenKind::LessEq : TokenKind::Less, start);
        case '>': return make(match('=') ? TokenKind::GreaterEq : TokenKind::Greater, start);
        case '&':
            if (match('&')) return make(TokenKind::AmpAmp, start);
            return illegal_byte(start, c);
        case '|':
            if (match('|')) return make(TokenKind::PipePipe, start);
            return illegal_byte(start, c);
        default: return illegal_byte(start, c);
    }
}

Token Lexer::scan_identifier_or_keyword(Offset start) {
    while (is_alnum(peek())) ++pos_;

    std::string_view lexeme = text_.substr(start, pos_ - start);
    auto it = keywords().find(lexeme);
    TokenKind kind = it != keywords().end() ? it->second : TokenKind::Identifier;
    return make(kind, start);
}

Token Lexer::scan_number(Offset start) {
    while (is_digit(peek())) ++pos_;

    // A '.' is the decimal point only when a digit follows it -- "0..10"
    // must lex as IntLiteral, DotDot, IntLiteral, not IntLiteral, Dot,
    // Dot, IntLiteral.
    if (peek() == '.' && is_digit(peek(1))) {
        ++pos_;
        while (is_digit(peek())) ++pos_;
        return make(TokenKind::FloatLiteral, start);
    }
    return make(TokenKind::IntLiteral, start);
}

std::optional<Token> Lexer::scan_string(Offset start) {
    while (pos_ < text_.size()) {
        char c = peek();
        if (c == '"') {
            ++pos_;
            return make(TokenKind::StringLiteral, start);
        }
        if (c == '\n') break;  // unterminated -- don't swallow the newline
        if (c == '\\' && pos_ + 1 < text_.size()) {
            pos_ += 2;  // an escape pair; don't let \" close the string early
            continue;
        }
        ++pos_;
    }

    diagnostics_.push_back(Diagnostic{
        Severity::Error,
        "unterminated string literal",
        Label{Span{start, pos_}, "string starts here"},
        {},
        std::nullopt,
    });
    return std::nullopt;
}

std::optional<Token> Lexer::illegal_byte(Offset start, char byte) {
    std::string message;
    unsigned char u = static_cast<unsigned char>(byte);
    if (u < 0x20 || u > 0x7e) {
        message = "illegal byte 0x" + to_hex_byte(u) + " in source (ASCII only outside strings and comments)";
    } else {
        message = std::string("unexpected character `") + byte + "`";
    }

    diagnostics_.push_back(Diagnostic{
        Severity::Error,
        message,
        Label{Span{start, pos_}, "unexpected"},
        {},
        std::nullopt,
    });
    return std::nullopt;
}

}  // namespace vex
