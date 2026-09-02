// Parser tests. ROADMAP.md's week-2 "done when" is the exact example in
// test_precedence_and_grouping below: `1 + 2 * (3 - 4)` parses to a
// correctly shaped tree, verified by dump_expr rather than hand-deriving
// trees by eye. The expression tests also pin down associativity, the full
// precedence ladder, unary/call/index, and literal parsing.
//
// The statement/item/program tests starting at test_var_decl below are
// week 3: declarations, assignment, if/while/for/return, blocks, function
// and struct declarations, and -- test_three_errors_all_reported, the
// literal week-3 "done when" bar -- error recovery.
#include "vex/parser.hpp"

#include <string>
#include <vector>

#include "test_support.hpp"
#include "vex/ast.hpp"
#include "vex/lexer.hpp"
#include "vex/stmt.hpp"

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

// Parses `source` as a whole program and returns its dump_program() text,
// asserting no diagnostics were produced by either stage.
std::string parse_program_ok(const std::string& source, const std::string& what) {
    vex::SourceManager sm("t", source);
    vex::Lexer lexer(sm);
    std::vector<vex::Token> tokens = lexer.tokenize();
    check_eq(std::to_string(lexer.diagnostics().size()), std::string("0"), what + ": lexer diagnostics");

    vex::Parser parser(std::move(tokens), sm);
    vex::Program program = parser.parse_program();
    check_eq(std::to_string(parser.diagnostics().size()), std::string("0"), what + ": parser diagnostics");
    return vex::dump_program(program);
}

// Wraps a bare statement in a minimal function so parse_program_ok can
// exercise the statement parsers without every test spelling out `fn`.
std::string parse_stmt_in_fn(const std::string& stmt_source, const std::string& what) {
    std::string wrapped = parse_program_ok("fn f() {\n" + stmt_source + "\n}", what);
    std::string prefix = "(fn f () _ (block ";
    std::string suffix = "))";
    if (wrapped.size() < prefix.size() + suffix.size() || wrapped.compare(0, prefix.size(), prefix) != 0) {
        check_eq(wrapped, prefix + "<stmt>" + suffix, what + ": unexpected function wrapper shape");
        return wrapped;
    }
    return wrapped.substr(prefix.size(), wrapped.size() - prefix.size() - suffix.size());
}

void check_stmt_parses_to(const std::string& stmt_source, const std::string& expected, const std::string& what) {
    check_eq(parse_stmt_in_fn(stmt_source, what), expected, what);
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

// Week 5: `.field` postfix access and `[e1, e2, ...]` array literals.
void test_field_access_and_array_literal() {
    check_parses_to("p.x", "(field p x)", "field access");
    check_parses_to("a.b.c", "(field (field a b) c)", "chained field access");
    check_parses_to("f().x[0]", "(index (field (call f) x) 0)", "call, field, index, left to right");
    check_parses_to("[1, 2, 3]", "(array 1 2 3)", "array literal");
    check_parses_to("[]", "(array)", "empty array literal");
    check_parses_to("[1, 2,]", "(array 1 2)", "array literal trailing comma");
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

void test_var_decl() {
    check_stmt_parses_to("let x: int = 5;", "(let x int 5)", "let with explicit type");
    check_stmt_parses_to("var total = 0;", "(var total _ 0)", "var with inferred type");
}

void test_assign_and_expr_statement() {
    check_stmt_parses_to("total = total + 1;", "(= total (+ total 1))", "assignment");
    check_stmt_parses_to("arr[0] = 5;", "(= (index arr 0) 5)", "assignment to an index target");
    check_stmt_parses_to("p.x = 5;", "(= (field p x) 5)", "assignment to a field target");
    check_stmt_parses_to("print(total);", "(call print total)", "expression statement");
}

// Week 5: `T[N]` array type syntax in a var decl's type annotation.
void test_array_type_ref() {
    check_stmt_parses_to("let a: int[5] = [1, 2, 3, 4, 5];", "(let a int[5] (array 1 2 3 4 5))",
                          "array type annotation and literal");
}

void test_if_else() {
    check_stmt_parses_to("if n < 2 { return n; }", "(if (< n 2) (block (return n)) -)", "if with no else");
    check_stmt_parses_to("if a { return 1; } else { return 2; }",
                          "(if a (block (return 1)) (block (return 2)))", "if/else");
    // `else if` desugars to a Block wrapping the nested IfStmt.
    check_stmt_parses_to("if a { return 1; } else if b { return 2; } else { return 3; }",
                          "(if a (block (return 1)) (block (if b (block (return 2)) (block (return 3)))))",
                          "else-if chain");
}

void test_while() {
    check_stmt_parses_to("while i < 10 { i = i + 1; }", "(while (< i 10) (block (= i (+ i 1))))", "while loop");
}

void test_for_range() {
    check_stmt_parses_to("for i in 0..10 { total = total + i; }",
                          "(for i 0 10 (block (= total (+ total i))))", "for-range loop");
}

void test_return() {
    check_stmt_parses_to("return 42;", "(return 42)", "return with a value");
    check_stmt_parses_to("return;", "(return -)", "bare return");
}

// The literal week-2/week-3 shared verification method applied to a whole
// program: examples/fib.vx's exact shape, function and struct declarations
// included.
void test_function_and_struct_decl() {
    check_eq(parse_program_ok("struct Point { x: int, y: int }\n"
                               "fn fib(n: int) -> int {\n"
                               "  if n < 2 { return n; }\n"
                               "  return fib(n - 1) + fib(n - 2);\n"
                               "}\n",
                               "fib.vx shape"),
              "(struct Point (x int) (y int))\n"
              "(fn fib ((n int)) int (block (if (< n 2) (block (return n)) -) "
              "(return (+ (call fib (- n 1)) (call fib (- n 2))))))",
              "fib.vx shape");
}

void test_function_with_no_params_or_return_type() {
    check_eq(parse_program_ok("fn main() { print(1); }", "no params or return type"),
              "(fn main () _ (block (call print 1)))", "no params or return type");
}

void test_struct_trailing_comma() {
    check_eq(parse_program_ok("struct Pair { a: int, b: int, }", "trailing comma"), "(struct Pair (a int) (b int))",
              "trailing comma");
}

void test_struct_field_array_type() {
    check_eq(parse_program_ok("struct Grid { cells: int[9] }", "array-typed field"), "(struct Grid (cells int[9]))",
              "array-typed field");
}

// Spans are the whole point of the position model (ADR 0001) -- a
// statement's Span should cover the full statement, not just its last
// token.
void test_stmt_span_covers_whole_statement() {
    vex::SourceManager sm("t", "let x = 1;");
    vex::Lexer lexer(sm);
    std::vector<vex::Token> tokens = lexer.tokenize();
    vex::Parser parser(std::move(tokens), sm);
    vex::Program program = parser.parse_program();

    check_eq(std::to_string(parser.diagnostics().size()), std::string("1"), "bare statement is not a valid program");
    // parse_program() only accepts `fn`/`struct` at top level, so re-parse
    // the same source as a single statement inside a function to check the
    // Stmt's own Span in isolation.
    vex::SourceManager sm2("t", "fn f() { let x = 1; }");
    vex::Lexer lexer2(sm2);
    std::vector<vex::Token> tokens2 = lexer2.tokenize();
    vex::Parser parser2(std::move(tokens2), sm2);
    vex::Program program2 = parser2.parse_program();
    check_eq(std::to_string(parser2.diagnostics().size()), std::string("0"), "wrapped statement diagnostics");

    if (program2.items.empty()) {
        check_eq(std::string("has items"), std::string("empty"), "wrapped statement: expected one item");
        return;
    }
    const auto& fn = std::get<vex::FunctionDecl>(program2.items[0].node);
    if (fn.body.statements.empty()) {
        check_eq(std::string("has statements"), std::string("empty"), "wrapped statement: expected one statement");
        return;
    }
    const vex::Stmt& stmt = *fn.body.statements[0];
    // "let x = 1;" starts right after "fn f() { " -- offset 9 -- and ends
    // with its own `;` at offset 19, not the enclosing block's `}`.
    check_eq(stmt.span.start, 9u, "statement span starts at `let`, not the enclosing block");
    check_eq(stmt.span.end, 19u, "statement span ends at its own `;`, not the enclosing `}`");
}

// The literal week-3 "done when" bar from ROADMAP.md: a file with three
// separate syntax errors reports all three, not just the first --
// synchronize() must recover at each statement boundary and keep going,
// and it must not eat a legitimate `}` while looking for one.
void test_three_errors_all_reported() {
    vex::SourceManager sm("t",
                           "fn broken_missing_operand() {\n"
                           "    let x = ;\n"
                           "}\n"
                           "fn broken_missing_semicolon() {\n"
                           "    return 1\n"
                           "}\n"
                           "fn broken_bad_name() {\n"
                           "    let 5 = 3;\n"
                           "}\n"
                           "fn fine() {\n"
                           "    return 42;\n"
                           "}\n");
    vex::Lexer lexer(sm);
    std::vector<vex::Token> tokens = lexer.tokenize();
    check_eq(std::to_string(lexer.diagnostics().size()), std::string("0"), "three errors: lexer diagnostics");

    vex::Parser parser(std::move(tokens), sm);
    vex::Program program = parser.parse_program();

    check_eq(std::to_string(parser.diagnostics().size()), std::string("3"), "three separate syntax errors reported");
    // All four functions are still recognised as top-level items -- a
    // broken statement doesn't take down the rest of the file.
    check_eq(std::to_string(program.items.size()), std::string("4"), "all four functions still parsed as items");

    // synchronize() must not have eaten the `}` that legitimately closes
    // broken_missing_semicolon's body: `fine`'s own body should be intact.
    const auto& fine = std::get<vex::FunctionDecl>(program.items[3].node);
    check_eq(std::to_string(fine.body.statements.size()), std::string("1"),
              "recovery does not eat a legitimate `}`: fine() keeps its return statement");
}

}  // namespace

void run_parser_tests() {
    test_precedence_and_grouping();
    test_left_associativity();
    test_full_precedence_ladder();
    test_unary();
    test_call_and_index();
    test_field_access_and_array_literal();
    test_grouping_does_not_change_shape();
    test_literals();
    test_span_covers_whole_expression();
    test_missing_operand_reports_and_returns_null();
    test_unclosed_paren_reports();

    test_var_decl();
    test_assign_and_expr_statement();
    test_array_type_ref();
    test_if_else();
    test_while();
    test_for_range();
    test_return();
    test_function_and_struct_decl();
    test_function_with_no_params_or_return_type();
    test_struct_trailing_comma();
    test_struct_field_array_type();
    test_stmt_span_covers_whole_statement();
    test_three_errors_all_reported();
}
