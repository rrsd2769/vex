// Recursive-descent parser with precedence climbing for binary operators --
// weeks 2 and 3 of ROADMAP.md. Week 2 was expressions only; week 3 adds
// statements, declarations, top-level items, and error recovery.
//
// On a parse error inside a single production, records one Diagnostic and
// returns nullptr/nullopt; the caller must check. Above the single
// statement or top-level item, parse_program() recovers: after a failure it
// calls synchronize() to skip to the next safe boundary (past a `;`, or
// right before a token that starts a new statement/item) and keeps going,
// so one run reports every syntax error instead of just the first.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "vex/ast.hpp"
#include "vex/diagnostic.hpp"
#include "vex/source_manager.hpp"
#include "vex/stmt.hpp"
#include "vex/token.hpp"

namespace vex {

class Parser {
public:
    Parser(std::vector<Token> tokens, const SourceManager& source);

    ExprPtr parse_expression();

    // The week-3 entry point: parses every top-level item (functions,
    // structs) to Eof, recovering from errors along the way.
    Program parse_program();

    const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }

private:
    ExprPtr parse_binary(int min_precedence);
    ExprPtr parse_unary();
    ExprPtr parse_postfix();
    ExprPtr parse_primary();
    ExprPtr parse_array_literal();

    ExprPtr make_int_literal(const Token& tok);
    ExprPtr make_float_literal(const Token& tok);
    ExprPtr make_string_literal(const Token& tok);

    std::optional<Item> parse_function_decl();
    std::optional<Item> parse_struct_decl();
    std::optional<TypeRef> parse_type_ref(const std::string& what);

    std::optional<Block> parse_block();
    StmtPtr parse_statement();
    StmtPtr parse_var_decl();
    StmtPtr parse_if();
    StmtPtr parse_while();
    StmtPtr parse_for();
    StmtPtr parse_return();
    StmtPtr parse_expr_or_assign_statement();

    // Recovers after a statement-level parse error: skips tokens until just
    // past a `;`, or until (without consuming) a token that starts a new
    // statement, a `}` that closes the enclosing block, or Eof.
    void synchronize_statement();

    // Recovers after a top-level parse error: skips tokens until (without
    // consuming) a token that starts a new item, or Eof. A separate
    // function from synchronize_statement() because that one treats every
    // statement keyword (`let`, `if`, ...) as an immediate safe stop, which
    // is right inside a block but would spin forever here -- those
    // keywords are never valid at top level, so if one is the very token
    // that made this item invalid, "stop without consuming" never makes
    // progress.
    void synchronize_item();

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
