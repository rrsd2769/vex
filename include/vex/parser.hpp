// Recursive-descent parser with precedence climbing for binary operators --
// week 2 of ROADMAP.md. Expressions only: statements, declarations, and
// error recovery (synchronising past a bad token instead of stopping at the
// first) are week 3.
//
// On a parse error, records one Diagnostic and returns nullptr; the caller
// must check. There is deliberately no recovery yet, so a single error is
// all any one parse produces.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "vex/ast.hpp"
#include "vex/diagnostic.hpp"
#include "vex/source_manager.hpp"
#include "vex/token.hpp"

namespace vex {

class Parser {
public:
    Parser(std::vector<Token> tokens, const SourceManager& source);

    ExprPtr parse_expression();

    const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }

private:
    ExprPtr parse_binary(int min_precedence);
    ExprPtr parse_unary();
    ExprPtr parse_postfix();
    ExprPtr parse_primary();

    ExprPtr make_int_literal(const Token& tok);
    ExprPtr make_float_literal(const Token& tok);
    ExprPtr make_string_literal(const Token& tok);

    const Token& peek() const;
    const Token& advance();
    bool check(TokenKind kind) const;
    bool match(TokenKind kind);
    const Token* expect(TokenKind kind, const std::string& what);

    std::string_view lexeme(const Token& tok) const;
    void error(const Token& tok, const std::string& message);

    std::vector<Token> tokens_;
    std::size_t pos_ = 0;
    const SourceManager& source_;
    std::vector<Diagnostic> diagnostics_;
};

}  // namespace vex
