// Type checker tests -- week 4 of ROADMAP.md: symbol table with lexical
// scoping, type representation, expression typing, let/var inference, and
// mutability enforcement. The week's "done when" bar is accurate spans on
// the offending operand rather than the whole statement; test_span_is_on_
// offending_operand_not_whole_statement below is the literal check for
// that.
//
// CallExpr and IndexExpr are deliberately not validated this week (see
// type_checker.hpp) -- test_call_and_index_defer_to_week_5 pins that down
// so a future week 5 change doesn't accidentally start requiring `print`
// to be declared.
#include "vex/type_checker.hpp"

#include <string>
#include <vector>

#include "test_support.hpp"
#include "vex/lexer.hpp"
#include "vex/parser.hpp"
#include "vex/source_manager.hpp"
#include "vex/stmt.hpp"

using vex_test::check_eq;

namespace {

// Lexes, parses, and type-checks `source`, asserting no lexer/parser
// diagnostics (those stages aren't what's under test here), and returns the
// type checker's diagnostic messages joined one per line, or "<ok>" if
// there were none.
std::string check_source(const std::string& source, const std::string& what) {
    vex::SourceManager sm("t", source);
    vex::Lexer lexer(sm);
    std::vector<vex::Token> tokens = lexer.tokenize();
    check_eq(std::to_string(lexer.diagnostics().size()), std::string("0"), what + ": lexer diagnostics");

    vex::Parser parser(std::move(tokens), sm);
    vex::Program program = parser.parse_program();
    check_eq(std::to_string(parser.diagnostics().size()), std::string("0"), what + ": parser diagnostics");

    vex::TypeChecker checker(program);
    checker.check();
    if (checker.diagnostics().empty()) return "<ok>";

    std::string out;
    for (std::size_t i = 0; i < checker.diagnostics().size(); ++i) {
        if (i > 0) out += '\n';
        out += checker.diagnostics()[i].message;
    }
    return out;
}

void check_ok(const std::string& source, const std::string& what) {
    check_eq(check_source(source, what), std::string("<ok>"), what);
}

void check_error(const std::string& source, const std::string& expected, const std::string& what) {
    check_eq(check_source(source, what), expected, what);
}

void test_clean_program_has_no_diagnostics() {
    check_ok(
        "struct Point { x: int, y: int }\n"
        "fn fib(n: int) -> int {\n"
        "  if n < 2 { return n; }\n"
        "  return fib(n - 1) + fib(n - 2);\n"
        "}\n"
        "fn main() {\n"
        "  var total = 0;\n"
        "  for i in 0..10 { total = total + fib(i); }\n"
        "  print(total);\n"
        "}\n",
        "examples/fib.vx's actual shape type-checks clean");
}

void test_var_decl_explicit_type_ok() {
    check_ok("fn main() { let x: int = 5; }", "explicit-type var decl matching init");
}

void test_var_decl_type_mismatch() {
    check_error("fn main() { let x: int = \"hi\"; }",
                "cannot assign value of type `string` to variable of type `int`", "var decl type mismatch");
}

void test_var_decl_inferred() { check_ok("fn main() { var x = 5; var y = x + 1; }", "let/var inference"); }

void test_redeclaration_in_same_scope() {
    check_error("fn main() { let x = 1; let x = 2; }", "redeclaration of `x`", "redeclaration in same scope");
}

void test_shadowing_outer_scope_is_ok() {
    check_ok("fn main() { let x = 1; if true { let x = 2; } }", "shadowing an outer scope is not a redeclaration");
}

void test_undefined_variable() {
    check_error("fn main() { let x = y; }", "undefined variable `y`", "undefined variable");
}

void test_assign_to_immutable() {
    check_error("fn main() { let x = 1; x = 2; }", "cannot assign to immutable variable `x`",
                "assigning to a `let` binding");
}

void test_assign_to_mutable_ok() { check_ok("fn main() { var x = 1; x = 2; }", "assigning to a `var` binding"); }

void test_assign_type_mismatch() {
    check_error("fn main() { var x = 1; x = \"hi\"; }",
                "cannot assign value of type `string` to variable of type `int`", "assign type mismatch");
}

void test_binary_arithmetic_mismatch_reports_rhs() {
    check_error("fn main() { var x = 1 + 2.5; }", "expected `int`, found `float`",
                "arithmetic operand mismatch reports the operand that broke it");
}

void test_binary_arithmetic_non_numeric_reports_lhs() {
    check_error("fn main() { var x = true + 1; }", "expected `int` or `float`, found `bool`",
                "non-numeric lhs is the offending operand");
}

void test_comparison_yields_bool() { check_ok("fn main() { var x = 1 < 2; }", "comparison result is bool"); }

void test_comparison_mismatch() {
    check_error("fn main() { var x = 1 < true; }", "expected `int`, found `bool`", "comparison operand mismatch");
}

void test_equality_mismatch() {
    check_error("fn main() { var x = 1 == \"a\"; }", "cannot compare `int` with `string`", "equality type mismatch");
}

void test_logical_requires_bool() {
    check_error("fn main() { var x = 1 && true; }", "expected `bool`, found `int`", "logical operand must be bool");
}

void test_unary_minus_requires_numeric() {
    check_error("fn main() { var x = -true; }", "cannot negate `bool`; expected `int` or `float`",
                "unary minus on non-numeric");
}

void test_unary_bang_requires_bool() {
    check_error("fn main() { var x = !1; }", "expected `bool`, found `int`", "unary bang on non-bool");
}

void test_if_condition_must_be_bool() {
    check_error("fn main() { if 1 { } }", "if condition must be of type `bool`, found `int`",
                "if condition type");
}

void test_while_condition_must_be_bool() {
    check_error("fn main() { while 1 { } }", "while condition must be of type `bool`, found `int`",
                "while condition type");
}

void test_for_range_must_be_int() {
    check_error("fn main() { for i in true..10 { } }", "range start must be of type `int`, found `bool`",
                "for range start type");
}

void test_for_loop_var_is_int_and_immutable() {
    check_ok("fn main() { for i in 0..10 { var x = i + 1; } }", "loop var usable as int inside the body");
    check_error("fn main() { for i in 0..10 { i = 5; } }", "cannot assign to immutable variable `i`",
                "loop var is immutable");
}

void test_return_type_mismatch() {
    check_error("fn f() -> int { return \"hi\"; }",
                "cannot return value of type `string` from function returning `int`", "return type mismatch");
}

void test_return_value_from_void_function() {
    check_error("fn f() { return 1; }", "function returns `void`; unexpected return value of type `int`",
                "returning a value from a void function");
}

void test_bare_return_missing_value() {
    check_error("fn f() -> int { return; }", "expected a return value of type `int`, found none",
                "bare return in a non-void function");
}

void test_unknown_type_name() {
    check_error("fn main() { let x: NoSuchType = 1; }", "unknown type `NoSuchType`", "unknown type in var decl");
}

void test_struct_field_unknown_type() {
    check_error("struct S { x: NoSuchType }\nfn main() { }", "unknown type `NoSuchType`",
                "unknown type in struct field");
}

void test_struct_field_type_resolves() {
    check_ok("struct Point { x: int, y: int }\nfn main() { }", "struct with valid field types");
}

void test_param_type_and_immutability() {
    check_ok("fn f(n: int) -> int { return n; }", "parameter usable as its declared type");
    check_error("fn f(n: int) { n = 1; }", "cannot assign to immutable variable `n`", "parameters are immutable");
}

// The literal week-4 "done when" bar: a type error in an expression is
// caught with an accurate span on the offending operand, not the whole
// statement.
void test_span_is_on_offending_operand_not_whole_statement() {
    std::string source = "fn main() { let x: int = \"hi\"; }";
    vex::SourceManager sm("t", source);
    vex::Lexer lexer(sm);
    std::vector<vex::Token> tokens = lexer.tokenize();
    vex::Parser parser(std::move(tokens), sm);
    vex::Program program = parser.parse_program();
    vex::TypeChecker checker(program);
    checker.check();

    check_eq(std::to_string(checker.diagnostics().size()), std::string("1"), "exactly one diagnostic");
    const vex::Span& span = checker.diagnostics()[0].primary.span;

    std::string::size_type offending = source.find("\"hi\"");
    check_eq(span.start, static_cast<vex::Offset>(offending), "primary span starts at the offending operand");
    check_eq(span.end, static_cast<vex::Offset>(offending + 4), "primary span ends at the offending operand");
    // Not the whole `let x: int = "hi";` statement.
    if (span.start == 0) {
        check_eq(std::string("span on offending operand"), std::string("span on whole statement"),
                  "span must not cover the whole statement");
    }
}

// CallExpr and IndexExpr are unchecked this week (ROADMAP.md defers
// function-signature and array-indexing checks to week 5) -- pin that down
// so a partial week-5 change doesn't silently start requiring `print` to be
// a declared function.
void test_call_and_index_defer_to_week_5() {
    check_ok("fn main() { print(1); }", "calling an undeclared function is not (yet) an error");
    check_ok("fn main() { var arr = 1; var x = arr[0]; }", "indexing is not (yet) type-checked");
    // But a genuine type error nested inside a call argument is still
    // caught -- only the call itself is unchecked, not its arguments.
    check_error("fn main() { print(1 + true); }", "expected `int`, found `bool`",
                "errors nested inside a call argument are still caught");
}

}  // namespace

void run_type_checker_tests() {
    test_clean_program_has_no_diagnostics();
    test_var_decl_explicit_type_ok();
    test_var_decl_type_mismatch();
    test_var_decl_inferred();
    test_redeclaration_in_same_scope();
    test_shadowing_outer_scope_is_ok();
    test_undefined_variable();
    test_assign_to_immutable();
    test_assign_to_mutable_ok();
    test_assign_type_mismatch();
    test_binary_arithmetic_mismatch_reports_rhs();
    test_binary_arithmetic_non_numeric_reports_lhs();
    test_comparison_yields_bool();
    test_comparison_mismatch();
    test_equality_mismatch();
    test_logical_requires_bool();
    test_unary_minus_requires_numeric();
    test_unary_bang_requires_bool();
    test_if_condition_must_be_bool();
    test_while_condition_must_be_bool();
    test_for_range_must_be_int();
    test_for_loop_var_is_int_and_immutable();
    test_return_type_mismatch();
    test_return_value_from_void_function();
    test_bare_return_missing_value();
    test_unknown_type_name();
    test_struct_field_unknown_type();
    test_struct_field_type_resolves();
    test_param_type_and_immutability();
    test_span_is_on_offending_operand_not_whole_statement();
    test_call_and_index_defer_to_week_5();
}
