#pragma once

#include "vex/span.hpp"

namespace vex {

enum class TokenKind {
    Eof,

    Identifier,
    IntLiteral,
    FloatLiteral,
    StringLiteral,

    // `int`, `float`, `bool`, and `string` are deliberately NOT keywords
    // here -- they lex as ordinary Identifiers, the same as a struct name
    // like `Point`. Telling a primitive type name from a struct name is a
    // type-checker question (week 4), not a lexer one; this keeps the
    // keyword table small and the Type grammar production just "an
    // Identifier" either way.
    KwStruct,
    KwFn,
    KwLet,
    KwVar,
    KwIf,
    KwElse,
    KwWhile,
    KwFor,
    KwIn,
    KwReturn,
    KwTrue,
    KwFalse,

    LParen,
    RParen,
    LBrace,
    RBrace,
    LBracket,
    RBracket,
    Comma,
    Semicolon,
    Colon,
    Dot,
    DotDot,  // `..`, the range operator: `0..10`
    Arrow,   // `->`, a function's return type

    Plus,
    Minus,
    Star,
    Slash,
    Percent,

    Eq,
    EqEq,
    Bang,
    BangEq,
    Less,
    LessEq,
    Greater,
    GreaterEq,
    AmpAmp,
    PipePipe,
};

// A classified lexical unit: a TokenKind plus the Span it covers. Carries
// neither text nor a parsed value -- the parser re-parses literals from the
// Lexeme, the Source text the Span covers (CONTEXT.md).
struct Token {
    TokenKind kind;
    Span span;
};

// Human-readable name for a TokenKind, for diagnostics like
// "expected `;`, found `identifier`".
const char* token_kind_name(TokenKind kind);

}  // namespace vex
