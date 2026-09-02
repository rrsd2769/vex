// Type checker tests -- week 4 (core) and week 5 (completion + diagnostics
// polish) of ROADMAP.md. Week 4 built the symbol table with lexical
// scoping, type representation, expression typing, let/var inference, and
// mutability enforcement; its "done when" bar (accurate spans on the
// offending operand rather than the whole statement) is checked by
// test_span_is_on_offending_operand_not_whole_statement below.
//
// Week 5 adds function-call and struct-construction signature checking
// (including the `print` builtin), struct field access, array literals and
// indexing, definite-return analysis, and "did you mean" suggestions.
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

// Runs the full pipeline like check_source(), but returns the first
// diagnostic's suggested replacement (or "<none>") -- for "did you mean"
// tests, which check_source()'s message-only comparison can't see.
std::string first_suggestion(const std::string& source) {
    vex::SourceManager sm("t", source);
    vex::Lexer lexer(sm);
    std::vector<vex::Token> tokens = lexer.tokenize();
    vex::Parser parser(std::move(tokens), sm);
    vex::Program program = parser.parse_program();
    vex::TypeChecker checker(program);
    checker.check();
    if (checker.diagnostics().empty() || !checker.diagnostics()[0].suggestion) return "<none>";
    return checker.diagnostics()[0].suggestion->replacement;
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

// ---------------------------------------------------------------------
// Week 5: function-call and struct-construction signature checking
// ---------------------------------------------------------------------

void test_call_checks_arity_and_argument_types() {
    check_ok("fn add(a: int, b: int) -> int { return a + b; } fn main() { var x = add(1, 2); }",
             "correct call arity and argument types");
    check_error("fn add(a: int, b: int) -> int { return a + b; } fn main() { add(1); }",
                "function `add` expects 2 argument(s), found 1", "too few arguments");
    check_error("fn add(a: int, b: int) -> int { return a + b; } fn main() { add(1, 2, 3); }",
                "function `add` expects 2 argument(s), found 3", "too many arguments");
    check_error("fn add(a: int, b: int) -> int { return a + b; } fn main() { add(1, true); }",
                "argument 2 to `add` should be `int`, found `bool`", "wrong argument type");
}

void test_call_return_type_flows_through() {
    check_error("fn add(a: int, b: int) -> int { return a + b; } fn main() { var x: bool = add(1, 2); }",
                "cannot assign value of type `int` to variable of type `bool`",
                "a call's type is its function's declared return type");
}

void test_recursive_and_forward_calls_resolve() {
    check_ok("fn fib(n: int) -> int { if n < 2 { return n; } return fib(n - 1) + fib(n - 2); } fn main() { fib(5); }",
             "a function can call itself");
    check_ok("fn main() { helper(); } fn helper() {}",
             "a function can call one declared later in the file");
}

void test_call_to_builtin_print() {
    check_ok("fn main() { print(1); print(\"hi\", true); }", "print accepts any argument(s)");
    check_error("fn main() { print(); }", "`print` expects at least one argument", "print needs an argument");
    check_error("fn main() { print(1 + true); }", "expected `int`, found `bool`",
                "a type error nested inside a print argument is still caught");
}

void test_undefined_function_call() {
    check_error("fn main() { nope(); }", "undefined function `nope`", "calling an undeclared function");
}

void test_undefined_function_suggests_closest_name() {
    check_eq(first_suggestion("fn helper() {} fn main() { helpr(); }"), std::string("helper"),
             "typo'd call suggests the declared function");
}

// ---------------------------------------------------------------------
// Week 5: struct construction and field access
// ---------------------------------------------------------------------

void test_struct_construction_and_field_access() {
    check_ok("struct Point { x: int, y: int } fn main() { var p = Point(1, 2); var x = p.x; }",
             "constructing a struct and reading its fields");
    check_error("struct Point { x: int, y: int } fn main() { Point(1); }",
                "`Point` has 2 field(s), found 1 argument(s)", "wrong construction arity");
    check_error("struct Point { x: int, y: int } fn main() { Point(1, true); }",
                "field `y` of `Point` should be `int`, found `bool`", "wrong field type at construction");
}

void test_field_access_unknown_field() {
    check_error("struct Point { x: int, y: int } fn main() { var p = Point(1, 2); var z = p.z; }",
                "no field `z` on struct `Point`", "accessing a field that doesn't exist");
}

void test_field_access_suggests_closest_field() {
    check_eq(first_suggestion("struct Point { x: int, y: int } fn main() { var p = Point(1, 2); var z = p.xx; }"),
             std::string("x"), "typo'd field suggests the declared field");
}

void test_field_access_on_non_struct() {
    check_error("fn main() { var x = 1; var y = x.field; }", "cannot access field `field` on type `int`",
                "field access on a non-struct type");
}

void test_assign_through_field_access() {
    check_ok("struct Point { x: int, y: int } fn main() { var p = Point(1, 2); p.x = 5; }",
             "assigning through a field on a mutable struct");
    check_error("struct Point { x: int, y: int } fn main() { let p = Point(1, 2); p.x = 5; }",
                "cannot assign to immutable variable `p`", "assigning through a field on an immutable struct");
    check_error("struct Point { x: int, y: int } fn main() { var p = Point(1, 2); p.x = true; }",
                "cannot assign value of type `bool` to variable of type `int`",
                "assigning a mismatched type through a field");
}

// ---------------------------------------------------------------------
// Week 5: array literals and indexing
// ---------------------------------------------------------------------

void test_array_literal_and_index() {
    check_ok("fn main() { var a: int[3] = [1, 2, 3]; var x = a[0]; }", "array literal matching its declared type");
    check_ok("fn main() { var a = [1, 2, 3]; var x = a[0] + 1; }", "array type inferred, element usable as int");
}

void test_empty_array_literal_cannot_infer_type() {
    check_error("fn main() { var a = []; }", "cannot infer the element type of an empty array literal",
                "an empty array literal has nothing to infer an element type from");
}

void test_array_literal_element_type_mismatch() {
    check_error("fn main() { var a = [1, true, 3]; }", "array element should be `int`, found `bool`",
                "inconsistent array element types");
}

void test_array_literal_size_mismatch_against_declared_type() {
    check_error("fn main() { var a: int[5] = [1, 2, 3]; }",
                "cannot assign value of type `int[3]` to variable of type `int[5]`",
                "array literal size must match its declared type's size");
}

void test_index_requires_int() {
    check_error("fn main() { var a = [1, 2, 3]; var x = a[true]; }",
                "array index must be of type `int`, found `bool`", "non-int index");
}

void test_index_into_non_array() {
    check_error("fn main() { var x = 1; var y = x[0]; }", "cannot index into type `int`", "indexing a non-array");
}

void test_index_out_of_bounds_literal() {
    check_error("fn main() { var a = [1, 2, 3]; var x = a[5]; }",
                "index 5 out of bounds for array of size 3", "statically-known out-of-bounds index");
    check_ok("fn main() { var a = [1, 2, 3]; var x = a[2]; }", "index equal to size-1 is in bounds");
}

// ---------------------------------------------------------------------
// Week 5: definite-return analysis
// ---------------------------------------------------------------------

void test_definite_return_missing() {
    check_error("fn f() -> int { if true { return 1; } }",
                "function `f` doesn't return a value on all code paths", "missing return on the else-less path");
}

void test_definite_return_only_inside_loop_is_not_enough() {
    check_error("fn f() -> int { while true { return 1; } }",
                "function `f` doesn't return a value on all code paths",
                "a loop is never assumed to run, so returning only inside one doesn't count");
}

void test_definite_return_if_else_both_return() {
    check_ok("fn f(n: int) -> int { if n < 0 { return -1; } else { return 1; } }",
             "both branches of an if/else return");
    check_ok("fn classify(n: int) -> int {\n"
             "  if n < 0 { return -1; } else { if n == 0 { return 0; } else { return 1; } }\n"
             "}",
             "else-if chain (desugared as nested if/else) where every branch returns");
}

void test_definite_return_not_required_for_void() {
    check_ok("fn f() { if true { return; } }", "a void function is never required to return on every path");
}

// ---------------------------------------------------------------------
// Week 5: diagnostics polish -- suggestions and cross-namespace redeclaration
// ---------------------------------------------------------------------

void test_undefined_variable_suggests_closest_name() {
    check_eq(first_suggestion("fn main() { let count = 1; let x = counnt; }"), std::string("count"),
             "typo'd variable suggests the declared one");
}

void test_unknown_type_suggests_closest_name() {
    check_eq(first_suggestion("fn main() { let v: flot = 1.0; }"), std::string("float"),
             "typo'd type name suggests the real one");
}

void test_struct_and_function_share_one_namespace() {
    check_error("struct Point { x: int } fn Point() {}", "redeclaration of `Point`",
                "a function colliding with a struct name is a redeclaration");
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

    test_call_checks_arity_and_argument_types();
    test_call_return_type_flows_through();
    test_recursive_and_forward_calls_resolve();
    test_call_to_builtin_print();
    test_undefined_function_call();
    test_undefined_function_suggests_closest_name();

    test_struct_construction_and_field_access();
    test_field_access_unknown_field();
    test_field_access_suggests_closest_field();
    test_field_access_on_non_struct();
    test_assign_through_field_access();

    test_array_literal_and_index();
    test_empty_array_literal_cannot_infer_type();
    test_array_literal_element_type_mismatch();
    test_array_literal_size_mismatch_against_declared_type();
    test_index_requires_int();
    test_index_into_non_array();
    test_index_out_of_bounds_literal();

    test_definite_return_missing();
    test_definite_return_only_inside_loop_is_not_enough();
    test_definite_return_if_else_both_return();
    test_definite_return_not_required_for_void();

    test_undefined_variable_suggests_closest_name();
    test_unknown_type_suggests_closest_name();
    test_struct_and_function_share_one_namespace();
}
