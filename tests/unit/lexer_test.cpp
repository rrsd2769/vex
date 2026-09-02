// Lexer tests. ROADMAP.md's week-1 "done when" is: the lexer tokenises
// every example file, and a deliberately bad character produces a properly
// rendered error with a caret in the right column -- test_tokenizes_
// example_files and test_illegal_character_recovers below are that,
// directly. The rest pin down the token-boundary decisions that are easy
// to get subtly wrong (range vs. float, maximal munch on operators,
// strings, keyword table).
#include "vex/lexer.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "vex/diagnostic_renderer.hpp"
#include "test_support.hpp"

using vex_test::check_eq;

namespace {

std::vector<vex::TokenKind> kinds_of(const std::vector<vex::Token>& tokens) {
    std::vector<vex::TokenKind> kinds;
    kinds.reserve(tokens.size());
    for (const vex::Token& t : tokens) kinds.push_back(t.kind);
    return kinds;
}

void check_kinds(const std::string& source, const std::vector<vex::TokenKind>& expected,
                  const std::string& what) {
    vex::SourceManager sm("t", source);
    vex::Lexer lexer(sm);
    std::vector<vex::Token> tokens = lexer.tokenize();

    check_eq(std::to_string(lexer.diagnostics().size()), std::string("0"), what + ": no diagnostics");

    std::vector<vex::TokenKind> actual = kinds_of(tokens);
    if (actual.size() != expected.size()) {
        check_eq(std::to_string(actual.size()), std::to_string(expected.size()), what + ": token count");
        return;
    }
    for (std::size_t i = 0; i < expected.size(); ++i) {
        check_eq(std::string(vex::token_kind_name(actual[i])), std::string(vex::token_kind_name(expected[i])),
                  what + ": token " + std::to_string(i));
    }
}

// int/float/bool/string are ordinary Identifiers, not keywords -- see the
// comment on TokenKind in token.hpp for why. Everything in the actual
// keyword table should still come back as its own kind, not Identifier.
void test_keywords_and_identifiers() {
    check_kinds("struct fn let var if else while for in return true false",
                {vex::TokenKind::KwStruct, vex::TokenKind::KwFn, vex::TokenKind::KwLet, vex::TokenKind::KwVar,
                 vex::TokenKind::KwIf, vex::TokenKind::KwElse, vex::TokenKind::KwWhile, vex::TokenKind::KwFor,
                 vex::TokenKind::KwIn, vex::TokenKind::KwReturn, vex::TokenKind::KwTrue, vex::TokenKind::KwFalse,
                 vex::TokenKind::Eof},
                "every reserved word");

    check_kinds("int float bool string Point",
                {vex::TokenKind::Identifier, vex::TokenKind::Identifier, vex::TokenKind::Identifier,
                 vex::TokenKind::Identifier, vex::TokenKind::Identifier, vex::TokenKind::Eof},
                "primitive type names lex as identifiers, same as a struct name");
}

// Maximal munch: two-character operators must win over their one-character
// prefixes.
void test_operators_maximal_munch() {
    check_kinds("-> == != <= >= && ||",
                {vex::TokenKind::Arrow, vex::TokenKind::EqEq, vex::TokenKind::BangEq, vex::TokenKind::LessEq,
                 vex::TokenKind::GreaterEq, vex::TokenKind::AmpAmp, vex::TokenKind::PipePipe, vex::TokenKind::Eof},
                "two-char operators");

    check_kinds("- = != < >= !", {vex::TokenKind::Minus, vex::TokenKind::Eq, vex::TokenKind::BangEq,
                                    vex::TokenKind::Less, vex::TokenKind::GreaterEq, vex::TokenKind::Bang,
                                    vex::TokenKind::Eof},
                "one-char operators not swallowed by a stray neighbour");
}

// "0..10" is IntLiteral, DotDot, IntLiteral -- not IntLiteral, Dot, Dot,
// IntLiteral. A '.' is only ever a decimal point when a digit follows it.
void test_number_literals_vs_range() {
    check_kinds("0..10", {vex::TokenKind::IntLiteral, vex::TokenKind::DotDot, vex::TokenKind::IntLiteral,
                            vex::TokenKind::Eof},
                "range operator between two int literals");

    check_kinds("3.14", {vex::TokenKind::FloatLiteral, vex::TokenKind::Eof}, "float literal");

    check_kinds("42", {vex::TokenKind::IntLiteral, vex::TokenKind::Eof}, "int literal");
}

// The Span of a StringLiteral covers the surrounding quotes -- the parser
// re-parses the Lexeme, including unescaping, later.
void test_string_literal_span_and_escapes() {
    vex::SourceManager sm("t", R"("a\"b")");  // source text: "a\"b"
    vex::Lexer lexer(sm);
    std::vector<vex::Token> tokens = lexer.tokenize();

    check_eq(std::to_string(tokens.size()), std::string("2"), "one string token plus Eof");
    check_eq(std::string(vex::token_kind_name(tokens[0].kind)), std::string(vex::token_kind_name(vex::TokenKind::StringLiteral)),
              "string literal kind");
    check_eq(tokens[0].span.start, 0u, "string literal starts at the opening quote");
    check_eq(tokens[0].span.end, sm.size(), "escaped quote does not close the string early");
}

void test_unterminated_string_reports_and_produces_no_token() {
    vex::SourceManager sm("t", "\"never closed\n");
    vex::Lexer lexer(sm);
    std::vector<vex::Token> tokens = lexer.tokenize();

    check_eq(std::to_string(tokens.size()), std::string("1"), "no string token, just Eof");
    check_eq(std::to_string(lexer.diagnostics().size()), std::string("1"), "one diagnostic");
    if (!lexer.diagnostics().empty()) {
        check_eq(lexer.diagnostics()[0].message, std::string("unterminated string literal"), "diagnostic message");
    }
}

// One bad byte shouldn't stop the whole file from lexing -- it's recorded
// and skipped, and lexing picks back up right after it.
void test_illegal_character_recovers() {
    vex::SourceManager sm("t", "x = 1 @ 2;\n");
    vex::Lexer lexer(sm);
    std::vector<vex::Token> tokens = lexer.tokenize();

    check_eq(std::to_string(lexer.diagnostics().size()), std::string("1"), "one diagnostic for `@`");
    if (!lexer.diagnostics().empty()) {
        check_eq(lexer.diagnostics()[0].message, std::string("unexpected character `@`"), "diagnostic message");
    }

    std::vector<vex::TokenKind> expected = {vex::TokenKind::Identifier, vex::TokenKind::Eq,
                                              vex::TokenKind::IntLiteral, vex::TokenKind::IntLiteral,
                                              vex::TokenKind::Semicolon, vex::TokenKind::Eof};
    std::vector<vex::TokenKind> actual = kinds_of(tokens);
    check_eq(std::to_string(actual.size()), std::to_string(expected.size()), "tokens around the bad byte");
    for (std::size_t i = 0; i < expected.size() && i < actual.size(); ++i) {
        check_eq(std::string(vex::token_kind_name(actual[i])), std::string(vex::token_kind_name(expected[i])),
                  "token " + std::to_string(i) + " around the bad byte");
    }
}

// The literal week-1 "done when" bar from ROADMAP.md, end to end: a
// deliberately bad character produces a properly rendered error with a
// caret in the right column -- lexer's Diagnostic fed straight into
// render_diagnostic, not hand-constructed like the renderer's own tests.
void test_illegal_character_renders_with_caret() {
    vex::SourceManager sm("t", "x = 1 @ 2;\n");
    vex::Lexer lexer(sm);
    lexer.tokenize();

    check_eq(std::to_string(lexer.diagnostics().size()), std::string("1"), "one diagnostic to render");
    if (lexer.diagnostics().empty()) return;

    std::string expected =
        "error: unexpected character `@`\n"
        "  --> t:1:7\n"
        "  |\n"
        "1 | x = 1 @ 2;\n"
        "  |       ^ unexpected\n";
    check_eq(vex::render_diagnostic(lexer.diagnostics()[0], sm), expected,
              "lexer diagnostic renders with a caret under `@`");
}

void test_comments_are_skipped() {
    check_kinds("1 // ignore this @@@ garbage\n+ 2",
                {vex::TokenKind::IntLiteral, vex::TokenKind::Plus, vex::TokenKind::IntLiteral, vex::TokenKind::Eof},
                "line comment consumes to end of line, not beyond");
}

void test_empty_source_is_just_eof() {
    check_kinds("", {vex::TokenKind::Eof}, "empty source");
}

// The actual acceptance bar from ROADMAP.md: every example file tokenises
// clean. Run from the repo root, same assumption tests/run_tests.sh makes.
void test_tokenizes_example_files() {
    for (const char* path : {"examples/fib.vx", "tests/smoke/hello.vx"}) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            check_eq(std::string("file opened"), std::string("file missing: ") + path, path);
            continue;
        }
        std::ostringstream buf;
        buf << in.rdbuf();

        vex::SourceManager sm(path, buf.str());
        vex::Lexer lexer(sm);
        std::vector<vex::Token> tokens = lexer.tokenize();

        check_eq(std::to_string(lexer.diagnostics().size()), std::string("0"), std::string(path) + ": no diagnostics");
        check_eq(tokens.empty() ? std::string("empty") : std::string("non-empty"), std::string("non-empty"),
                  std::string(path) + ": produced tokens");
    }
}

}  // namespace

void run_lexer_tests() {
    test_keywords_and_identifiers();
    test_operators_maximal_munch();
    test_number_literals_vs_range();
    test_string_literal_span_and_escapes();
    test_unterminated_string_reports_and_produces_no_token();
    test_illegal_character_recovers();
    test_illegal_character_renders_with_caret();
    test_comments_are_skipped();
    test_empty_source_is_just_eof();
    test_tokenizes_example_files();
}
