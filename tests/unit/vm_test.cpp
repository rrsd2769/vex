// VM tests -- week 7 of ROADMAP.md. Its "done when" bar is `fib(30)`
// printing the right number and every example program running end to end;
// these tests run small programs through the full pipeline (lex -> parse
// -> check -> compile -> VM) and assert on actual program output, the way
// tests/bytecode's golden file asserts on disassembly text instead.
#include "vex/vm.hpp"

#include <sstream>
#include <string>
#include <vector>

#include "test_support.hpp"
#include "vex/bytecode_compiler.hpp"
#include "vex/lexer.hpp"
#include "vex/parser.hpp"
#include "vex/source_manager.hpp"
#include "vex/type_checker.hpp"

using vex_test::check_eq;

namespace {

// Lexes, parses, type-checks, compiles, and runs `source` end to end,
// asserting no diagnostics at any stage (VM's precondition, same as
// BytecodeCompiler's). Returns everything the program printed.
std::string run_source(const std::string& source, const std::string& what) {
    vex::SourceManager sm("t", source);
    vex::Lexer lexer(sm);
    std::vector<vex::Token> tokens = lexer.tokenize();
    check_eq(std::to_string(lexer.diagnostics().size()), std::string("0"), what + ": lexer diagnostics");

    vex::Parser parser(std::move(tokens), sm);
    vex::Program program = parser.parse_program();
    check_eq(std::to_string(parser.diagnostics().size()), std::string("0"), what + ": parser diagnostics");

    vex::TypeChecker checker(program);
    checker.check();
    check_eq(std::to_string(checker.diagnostics().size()), std::string("0"), what + ": checker diagnostics");

    vex::BytecodeCompiler compiler(program, checker);
    vex::BytecodeProgram bytecode = compiler.compile();  // VM only borrows this -- must outlive it
    vex::VM vm(bytecode);

    std::ostringstream out;
    vm.run(out);
    return out.str();
}

// Same pipeline, but expects VM::run() to throw a VMError. Returns its
// Diagnostic's message, or "<did not throw>".
std::string run_source_expect_runtime_error(const std::string& source) {
    vex::SourceManager sm("t", source);
    vex::Lexer lexer(sm);
    std::vector<vex::Token> tokens = lexer.tokenize();
    vex::Parser parser(std::move(tokens), sm);
    vex::Program program = parser.parse_program();
    vex::TypeChecker checker(program);
    checker.check();
    vex::BytecodeCompiler compiler(program, checker);
    vex::BytecodeProgram bytecode = compiler.compile();  // VM only borrows this -- must outlive it
    vex::VM vm(bytecode);

    std::ostringstream out;
    try {
        vm.run(out);
    } catch (const vex::VMError& e) {
        return e.diagnostic().message;
    }
    return "<did not throw>";
}

void test_recursive_fib_and_for_loop_sum() {
    // examples/fib.vx itself: recursive calls (exercising Call/Return's
    // frame-base arithmetic across many nested frames) plus a for-loop
    // accumulation. fib(0..9) sums to 88.
    std::string out = run_source(
        "fn fib(n: int) -> int {\n"
        "  if n < 2 { return n; }\n"
        "  return fib(n - 1) + fib(n - 2);\n"
        "}\n"
        "fn main() {\n"
        "  var total = 0;\n"
        "  for i in 0..10 {\n"
        "    total = total + fib(i);\n"
        "  }\n"
        "  print(total);\n"
        "}\n",
        "recursive fib + for-loop sum");
    check_eq(out, std::string("88\n"), "recursive fib + for-loop sum");
}

void test_struct_and_array_field_addressing() {
    // Mirrors tests/bytecode/control_flow_and_addressing.vx: a struct
    // returned by value (multi-slot Return), a dynamic array index into a
    // struct field followed by a static field access
    // (GetLocalIndexed's extra_offset), a while loop, and && short-circuit.
    std::string out = run_source(
        "struct Point { x: int, y: int }\n"
        "struct Segment { points: Point[2] }\n"
        "fn make_segment() -> Segment {\n"
        "    return Segment([Point(1, 2), Point(3, 4)]);\n"
        "}\n"
        "fn second_point_y(s: Segment, idx: int) -> int {\n"
        "    return s.points[idx].y;\n"
        "}\n"
        "fn in_range(n: int, low: int, high: int) -> bool {\n"
        "    if n >= low && n < high { return true; } else { return false; }\n"
        "}\n"
        "fn sum_while(n: int) -> int {\n"
        "    var total = 0;\n"
        "    var i = 0;\n"
        "    while i < n {\n"
        "        total = total + i;\n"
        "        i = i + 1;\n"
        "    }\n"
        "    return total;\n"
        "}\n"
        "fn main() {\n"
        "    var seg = make_segment();\n"
        "    print(second_point_y(seg, 1));\n"
        "    print(in_range(5, 0, 10));\n"
        "    print(sum_while(5));\n"
        "}\n",
        "struct/array field addressing");
    check_eq(out, std::string("4\ntrue\n10\n"), "struct/array field addressing");
}

void test_print_multiple_arguments_are_space_separated() {
    std::string out = run_source(
        "fn main() {\n"
        "  print(1, true, \"hi\");\n"
        "}\n",
        "print multiple arguments");
    check_eq(out, std::string("1 true hi\n"), "print multiple arguments");
}

void test_string_equality_compares_content_not_identity() {
    // Both "ab" constants are separate constant-pool entries (one per
    // function-local Chunk in general, though here the same Chunk), each
    // resolved to its own Arena allocation -- Eq must compare *content*,
    // not the two Values' pointers.
    std::string out = run_source(
        "fn main() {\n"
        "  print(\"ab\" == \"ab\");\n"
        "  print(\"ab\" == \"cd\");\n"
        "}\n",
        "string equality");
    check_eq(out, std::string("true\nfalse\n"), "string equality");
}

void test_array_index_out_of_bounds_is_a_runtime_error() {
    std::string message = run_source_expect_runtime_error(
        "fn main() {\n"
        "  let a = [1, 2, 3];\n"
        "  let i = 5;\n"
        "  print(a[i]);\n"
        "}\n");
    check_eq(message.find("out of bounds") != std::string::npos, true,
             "out-of-bounds array index reports a runtime error");
}

void test_integer_division_by_zero_is_a_runtime_error() {
    std::string message = run_source_expect_runtime_error(
        "fn main() {\n"
        "  let x = 1;\n"
        "  let y = 0;\n"
        "  print(x / y);\n"
        "}\n");
    check_eq(message, std::string("division by zero"), "integer division by zero reports a runtime error");
}

void test_integer_modulo_by_zero_is_a_runtime_error() {
    std::string message = run_source_expect_runtime_error(
        "fn main() {\n"
        "  let x = 1;\n"
        "  let y = 0;\n"
        "  print(x % y);\n"
        "}\n");
    check_eq(message, std::string("modulo by zero"), "integer modulo by zero reports a runtime error");
}

void test_float_division_by_zero_is_not_a_runtime_error() {
    // Unlike int, float division by zero is IEEE-defined (inf), not a
    // panic -- only the int Div/Mod cases check for a zero divisor.
    std::string out = run_source(
        "fn main() {\n"
        "  let x = 1.0;\n"
        "  let y = 0.0;\n"
        "  print(x / y > 0.0);\n"
        "}\n",
        "float division by zero");
    check_eq(out, std::string("true\n"), "float division by zero yields +inf, not an error");
}

}  // namespace

void run_vm_tests() {
    test_recursive_fib_and_for_loop_sum();
    test_struct_and_array_field_addressing();
    test_print_multiple_arguments_are_space_separated();
    test_string_equality_compares_content_not_identity();
    test_array_index_out_of_bounds_is_a_runtime_error();
    test_integer_division_by_zero_is_a_runtime_error();
    test_integer_modulo_by_zero_is_a_runtime_error();
    test_float_division_by_zero_is_not_a_runtime_error();
}
