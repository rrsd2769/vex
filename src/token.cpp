#include "vex/token.hpp"

namespace vex {

const char* token_kind_name(TokenKind kind) {
    switch (kind) {
        case TokenKind::Eof: return "end of file";
        case TokenKind::Identifier: return "identifier";
        case TokenKind::IntLiteral: return "integer literal";
        case TokenKind::FloatLiteral: return "float literal";
        case TokenKind::StringLiteral: return "string literal";
        case TokenKind::KwStruct: return "`struct`";
        case TokenKind::KwFn: return "`fn`";
        case TokenKind::KwLet: return "`let`";
        case TokenKind::KwVar: return "`var`";
        case TokenKind::KwIf: return "`if`";
        case TokenKind::KwElse: return "`else`";
        case TokenKind::KwWhile: return "`while`";
        case TokenKind::KwFor: return "`for`";
        case TokenKind::KwIn: return "`in`";
        case TokenKind::KwReturn: return "`return`";
        case TokenKind::KwTrue: return "`true`";
        case TokenKind::KwFalse: return "`false`";
        case TokenKind::LParen: return "`(`";
        case TokenKind::RParen: return "`)`";
        case TokenKind::LBrace: return "`{`";
        case TokenKind::RBrace: return "`}`";
        case TokenKind::LBracket: return "`[`";
        case TokenKind::RBracket: return "`]`";
        case TokenKind::Comma: return "`,`";
        case TokenKind::Semicolon: return "`;`";
        case TokenKind::Colon: return "`:`";
        case TokenKind::Dot: return "`.`";
        case TokenKind::DotDot: return "`..`";
        case TokenKind::Arrow: return "`->`";
        case TokenKind::Plus: return "`+`";
        case TokenKind::Minus: return "`-`";
        case TokenKind::Star: return "`*`";
        case TokenKind::Slash: return "`/`";
        case TokenKind::Percent: return "`%`";
        case TokenKind::Eq: return "`=`";
        case TokenKind::EqEq: return "`==`";
        case TokenKind::Bang: return "`!`";
        case TokenKind::BangEq: return "`!=`";
        case TokenKind::Less: return "`<`";
        case TokenKind::LessEq: return "`<=`";
        case TokenKind::Greater: return "`>`";
        case TokenKind::GreaterEq: return "`>=`";
        case TokenKind::AmpAmp: return "`&&`";
        case TokenKind::PipePipe: return "`||`";
    }
    return "unknown token";
}

}  // namespace vex
