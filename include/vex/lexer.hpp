#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "vex/diagnostic.hpp"
#include "vex/source_manager.hpp"
#include "vex/token.hpp"

namespace vex {

// Turns a Source's text into a flat list of Tokens, always ending with one
// Eof Token whose Span is empty at offset size() -- so the parser never
// needs a separate "ran out of tokens" check.
//
// On an illegal byte (an unrecognised character, or anything outside ASCII
// -- ADR 0001 restricts Source to ASCII outside strings and comments) it
// records a Diagnostic and skips just that byte, so one run surfaces every
// lexical error instead of stopping at the first.
class Lexer {
public:
    explicit Lexer(const SourceManager& source);

    std::vector<Token> tokenize();

    const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }

private:
    char peek(std::size_t ahead = 0) const;
    char advance();
    bool match(char expected);

    void skip_whitespace_and_comments();
    std::optional<Token> scan_token();
    Token scan_identifier_or_keyword(Offset start);
    Token scan_number(Offset start);
    std::optional<Token> scan_string(Offset start);
    std::optional<Token> illegal_byte(Offset start, char byte);

    Token make(TokenKind kind, Offset start) const;

    std::string_view text_;
    Offset pos_ = 0;
    std::vector<Diagnostic> diagnostics_;
};

}  // namespace vex
