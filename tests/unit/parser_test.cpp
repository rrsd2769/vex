// Parser tests. ROADMAP.md's week-2 "done when" is the exact example in
// test_precedence_and_grouping below: `1 + 2 * (3 - 4)` parses to a
// correctly shaped tree, verified by dump_expr rather than hand-deriving
// trees by eye. The rest pin down associativity, the full precedence
// ladder, unary/call/index, literal parsing, and the no-recovery-yet error
// path.
#include "vex/parser.hpp"

#include <string>
#include <vector>

#include "test_support.hpp"
#include "vex/ast.hpp"
#include "vex/lexer.hpp"

using vex_test::check_eq;

namespace {

// Parses `source` as a single expression and returns its dump_expr() text,
// asserting no diagnostics were produced by either stage.
std::string parse_ok(const std::string& source, const std::string& what) {
    vex::SourceManager sm("t", source);
    vex::Lexer lexer(sm);
    std::vector<vex::Token> tokens = lexer.tokenize();
    check_eq(std::to_string(lexer.diagnostics().size()), std::string("0"), what + ": lexer diagnostics");

    vex::Parser parser(std::move(tokens), sm);
    vex::ExprPtr expr = parser.parse_expression();
    check_eq(std::to_string(parser.diagnostics().size()), std::string("0"), what + ": parser diagnostics");
    if (!expr) return "<null>";
    return vex::dump_expr(*expr);
}

void check_parses_to(const std::string& source, const std::string& expected, const std::string& what) {
    check_eq(parse_ok(source, what), expected, what);
}

// The literal week-2 "done when" bar from ROADMAP.md.
void test_precedence_and_grouping() {
    check_parses_to("1 + 2 * (3 - 4)", "(+ 1 (* 2 (- 3 4)))", "precedence with grouping");
}

void test_left_associativity() {
    check_parses_to("10 - 3 - 2", "(- (- 10 3) 2)", "left-associative subtraction");
    check_parses_to("2 * 3 * 4", "(* (* 2 3) 4)", "left-associative multiplication");
}

// Every rung of the precedence ladder in one expression: || loosest, then
// &&, then comparison, then +/-, then * / %.
void test_full_precedence_ladder() {
    check_parses_to("a < b && c > d || e", "(|| (&& (< a b) (> c d)) e)", "|| loosest, && tighter, comparison tightest");
    check_parses_to("1 + 2 == 3", "(== (+ 1 2) 3)", "arithmetic binds tighter than comparison");
    check_parses_to("1 + 2 * 3 % 4", "(+ 1 (% (* 2 3) 4))", "* and % bind tighter than +");
}

void test_unary() {
    check_parses_to("-1 + !flag", "(+ (- 1) (! flag))", "unary minus and not");
    check_parses_to("- -1", "(- (- 1))", "stacked unary minus");
}

void test_call_and_index() {
    check_parses_to("f(1, 2)[0]", "(index (call f 1 2) 0)", "call then index, left to right");
    check_parses_to("f()", "(call f)", "call with no arguments");
    check_parses_to("a[b][c]", "(index (index a b) c)", "chained index");
}

void test_grouping_does_not_change_shape() {
    check_parses_to("(1 + 2)", "(+ 1 2)", "redundant grouping");
    check_parses_to("((1))", "1", "nested redundant grouping around a literal");
}

void test_literals() {
    check_parses_to("42", "42", "int literal");
    check_parses_to("3.14", "3.14", "float literal");
    check_parses_to("true", "true", "bool literal true");
    check_parses_to("false", "false", "bool literal false");
    check_parses_to(R"("a\"b")", "\"a\"b\"", "string literal unescapes \\\"");
    check_parses_to(R"("line\n")", "\"line\n\"", "string literal unescapes \\n");
}

// Spans are the whole point of the position model (ADR 0001) -- the top
// node's Span should cover the full source, not just its last token.
void test_span_covers_whole_expression() {
    vex::SourceManager sm("t", "1 + 2");
    vex::Lexer lexer(sm);
    std::vector<vex::Token> tokens = lexer.tokenize();
    vex::Parser parser(std::move(tokens), sm);
    vex::ExprPtr expr = parser.parse_expression();

    if (!expr) {
        check_eq(std::string("parsed"), std::string("null"), "span test: expected a parsed expression");
        return;
    }
    check_eq(expr->span.start, 0u, "top-level span starts at the first token");
    check_eq(expr->span.end, sm.size(), "top-level span ends at the last token");
}

// Week 3 is where error recovery (synchronising past a bad token) arrives --
// for now a parse error just stops and reports the one Diagnostic.
void test_missing_operand_reports_and_returns_null() {
    vex::SourceManager sm("t", "1 +");
    vex::Lexer lexer(sm);
    std::vector<vex::Token> tokens = lexer.tokenize();

    vex::Parser parser(std::move(tokens), sm);
    vex::ExprPtr expr = parser.parse_expression();

    check_eq(expr ? std::string("parsed") : std::string("null"), std::string("null"), "missing operand: no tree produced");
    check_eq(std::to_string(parser.diagnostics().size()), std::string("1"), "missing operand: one diagnostic");
}

void test_unclosed_paren_reports() {
    vex::SourceManager sm("t", "(1 + 2");
    vex::Lexer lexer(sm);
    std::vector<vex::Token> tokens = lexer.tokenize();

    vex::Parser parser(std::move(tokens), sm);
    vex::ExprPtr expr = parser.parse_expression();

    check_eq(expr ? std::string("parsed") : std::string("null"), std::string("null"), "unclosed paren: no tree produced");
    check_eq(std::to_string(parser.diagnostics().size()), std::string("1"), "unclosed paren: one diagnostic");
}

}  // namespace

void run_parser_tests() {
    test_precedence_and_grouping();
    test_left_associativity();
    test_full_precedence_ladder();
    test_unary();
    test_call_and_index();
    test_grouping_does_not_change_shape();
    test_literals();
    test_span_covers_whole_expression();
    test_missing_operand_reports_and_returns_null();
    test_unclosed_paren_reports();
}
