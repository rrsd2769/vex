// Bytecode compiler tests -- week 6 of ROADMAP.md. Its "done when" bar is a
// disassembler that prints readable bytecode for every example with control
// flow landing on the correct offsets; most tests below assert on exact
// disassembly text for a small, tightly-controlled program, the same way
// dump_expr/dump_stmt tests assert on exact S-expression text rather than
// hand-inspecting a tree.
#include "vex/bytecode_compiler.hpp"

#include <stdexcept>
#include <string>
#include <vector>

#include "test_support.hpp"
#include "vex/bytecode.hpp"
#include "vex/lexer.hpp"
#include "vex/parser.hpp"
#include "vex/source_manager.hpp"
#include "vex/type_checker.hpp"

using vex_test::check_eq;

namespace {

// Lexes, parses, type-checks, and compiles `source` to bytecode, asserting
// no diagnostics at any stage (this file is only testing the compiler, on
// programs known to be valid -- BytecodeCompiler's precondition), and
// returns its full disassembly.
std::string compile_source(const std::string& source, const std::string& what) {
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
    return vex::disassemble(compiler.compile());
}

// Runs the pipeline like compile_source(), but expects BytecodeCompiler to
// throw (the documented "not supported" limitations) rather than returning.
// Returns the exception's what(), or "<did not throw>".
std::string expect_throw(const std::string& source) {
    vex::SourceManager sm("t", source);
    vex::Lexer lexer(sm);
    std::vector<vex::Token> tokens = lexer.tokenize();
    vex::Parser parser(std::move(tokens), sm);
    vex::Program program = parser.parse_program();
    vex::TypeChecker checker(program);
    checker.check();
    vex::BytecodeCompiler compiler(program, checker);
    try {
        compiler.compile();
    } catch (const std::runtime_error& e) {
        return e.what();
    }
    return "<did not throw>";
}

void test_struct_construction_and_array_literal_emit_no_opcode() {
    // Point(1, 2) and [1, 2, 3] should compile to nothing but their
    // elements' Constants -- no Call, no dedicated "build" opcode -- see
    // bytecode_compiler.hpp's header comment on why.
    std::string out = compile_source(
        "struct Point { x: int, y: int }\n"
        "fn main() {\n"
        "  let p = Point(1, 2);\n"
        "  let a = [1, 2, 3];\n"
        "}\n",
        "struct/array literal construction");
    check_eq(out,
             std::string("== main ==\n"
                          "0000  Constant          0  ; 1\n"
                          "0003  Constant          1  ; 2\n"
                          "0006  Constant          2  ; 1\n"
                          "0009  Constant          3  ; 2\n"
                          "0012  Constant          4  ; 3\n"
                          "0015  Pop               width=5\n"
                          "0018  Return            width=0\n"),
             "struct/array literal construction");
}

void test_locals_get_sequential_frame_slots() {
    std::string out = compile_source(
        "fn main() {\n"
        "  let a = 1;\n"
        "  let b = 2;\n"
        "  let c = a + b;\n"
        "}\n",
        "sequential locals");
    check_eq(out,
             std::string("== main ==\n"
                          "0000  Constant          0  ; 1\n"
                          "0003  Constant          1  ; 2\n"
                          "0006  GetLocal          slot=0 width=1\n"
                          "0011  GetLocal          slot=1 width=1\n"
                          "0016  Add\n"
                          "0017  Pop               width=3\n"
                          "0020  Return            width=0\n"),
             "sequential locals");
}

void test_block_scope_pops_locals_on_exit() {
    // `y` is declared inside the `if`'s then-block and must not leak a
    // slot (or a name) past it -- the block's own Pop(width=1) should
    // appear right after `y`'s declaration, before the function-level Pop.
    std::string out = compile_source(
        "fn main() {\n"
        "  let x = 1;\n"
        "  if x == 1 {\n"
        "    let y = 2;\n"
        "  }\n"
        "}\n",
        "block scope");
    check_eq(out,
             std::string("== main ==\n"
                          "0000  Constant          0  ; 1\n"
                          "0003  GetLocal          slot=0 width=1\n"
                          "0008  Constant          1  ; 1\n"
                          "0011  Eq\n"
                          "0012  JumpIfFalse       ->0023\n"
                          "0017  Constant          2  ; 2\n"
                          "0020  Pop               width=1\n"
                          "0023  Pop               width=1\n"
                          "0026  Return            width=0\n"),
             "block scope");
}

void test_if_else_jump_targets() {
    std::string out = compile_source(
        "fn f(n: int) -> int {\n"
        "  if n < 0 {\n"
        "    return 0;\n"
        "  } else {\n"
        "    return 1;\n"
        "  }\n"
        "}\n"
        "fn main() {}\n",
        "if/else jump targets");
    check_eq(out,
             std::string("== f ==\n"
                          "0000  GetLocal          slot=0 width=1\n"
                          "0005  Constant          0  ; 0\n"
                          "0008  Less\n"
                          "0009  JumpIfFalse       ->0025\n"
                          "0014  Constant          1  ; 0\n"
                          "0017  Return            width=1\n"
                          "0020  Jump              ->0031\n"
                          "0025  Constant          2  ; 1\n"
                          "0028  Return            width=1\n"
                          "0031  Return            width=0\n"
                          "== main ==\n"
                          "0000  Return            width=0\n"),
             "if/else jump targets");
    // The else-branch's JumpIfFalse target (25) is exactly where the
    // Constant `0`'s else-block begins, and the then-branch's escape Jump
    // (31) is exactly the trailing Return after the if/else -- both control
    // flow paths land on real instruction boundaries, not into the middle
    // of one.
}

void test_while_loop_backward_jump_targets_condition_check() {
    std::string out = compile_source(
        "fn main() {\n"
        "  var i = 0;\n"
        "  while i < 3 {\n"
        "    i = i + 1;\n"
        "  }\n"
        "}\n",
        "while loop backward jump");
    check_eq(out,
             std::string("== main ==\n"
                          "0000  Constant          0  ; 0\n"
                          "0003  GetLocal          slot=0 width=1\n"
                          "0008  Constant          1  ; 3\n"
                          "0011  Less\n"
                          "0012  JumpIfFalse       ->0036\n"
                          "0017  GetLocal          slot=0 width=1\n"
                          "0022  Constant          2  ; 1\n"
                          "0025  Add\n"
                          "0026  SetLocal          slot=0 width=1\n"
                          "0031  Jump              ->0003\n"
                          "0036  Pop               width=1\n"
                          "0039  Return            width=0\n"),
             "while loop backward jump");
    // offset 3 (Jump's target) is exactly the condition check's first
    // instruction, i.e. the loop actually re-checks the condition each
    // iteration instead of jumping into the middle of the body.
}

void test_short_circuit_and_does_not_evaluate_rhs_unconditionally() {
    std::string out = compile_source(
        "fn main() {\n"
        "  let x = true && false;\n"
        "}\n",
        "&& short circuit");
    check_eq(out,
             std::string("== main ==\n"
                          "0000  Constant          0  ; true\n"
                          "0003  JumpIfFalseNoPop  ->0014\n"
                          "0008  Pop               width=1\n"
                          "0011  Constant          1  ; false\n"
                          "0014  Pop               width=1\n"
                          "0017  Return            width=0\n"),
             "&& short circuit");
}

void test_short_circuit_or() {
    std::string out = compile_source(
        "fn main() {\n"
        "  let x = true || false;\n"
        "}\n",
        "|| short circuit");
    check_eq(out,
             std::string("== main ==\n"
                          "0000  Constant          0  ; true\n"
                          "0003  JumpIfTrueNoPop   ->0014\n"
                          "0008  Pop               width=1\n"
                          "0011  Constant          1  ; false\n"
                          "0014  Pop               width=1\n"
                          "0017  Return            width=0\n"),
             "|| short circuit");
}

void test_literal_array_index_folds_to_plain_getlocal() {
    // A literal index is already statically bounds-checked by the type
    // checker, so it should compile to a direct GetLocal at the folded
    // offset -- no GetLocalIndexed, no runtime bounds check.
    std::string out = compile_source(
        "fn main() {\n"
        "  let a = [10, 20, 30];\n"
        "  let x = a[1];\n"
        "}\n",
        "literal array index");
    check_eq(out,
             std::string("== main ==\n"
                          "0000  Constant          0  ; 10\n"
                          "0003  Constant          1  ; 20\n"
                          "0006  Constant          2  ; 30\n"
                          "0009  GetLocal          slot=1 width=1\n"
                          "0014  Pop               width=4\n"
                          "0017  Return            width=0\n"),
             "literal array index");
}

void test_dynamic_array_index_uses_indexed_opcode() {
    std::string out = compile_source(
        "fn at(a: int[3], i: int) -> int {\n"
        "  return a[i];\n"
        "}\n"
        "fn main() {}\n",
        "dynamic array index");
    check_eq(out,
             std::string("== at ==\n"
                          "0000  GetLocal          slot=3 width=1\n"
                          "0005  GetLocalIndexed   base=0 stride=1 size=3 extra=0 width=1\n"
                          "0018  Return            width=1\n"
                          "0021  Return            width=0\n"
                          "== main ==\n"
                          "0000  Return            width=0\n"),
             "dynamic array index");
}

void test_field_access_offset() {
    std::string out = compile_source(
        "struct Point { x: int, y: int }\n"
        "fn get_y(p: Point) -> int {\n"
        "  return p.y;\n"
        "}\n"
        "fn main() {}\n",
        "field access offset");
    check_eq(out,
             std::string("== get_y ==\n"
                          "0000  GetLocal          slot=1 width=1\n"
                          "0005  Return            width=1\n"
                          "0008  Return            width=0\n"
                          "== main ==\n"
                          "0000  Return            width=0\n"),
             "field access offset");
}

void test_assign_through_field_and_dynamic_index() {
    std::string out = compile_source(
        "struct Point { x: int, y: int }\n"
        "fn main() {\n"
        "  var p = Point(0, 0);\n"
        "  p.y = 5;\n"
        "  var a = [1, 2, 3];\n"
        "  var i = 0;\n"
        "  a[i] = 9;\n"
        "}\n",
        "assign through field/dynamic index");
    check_eq(out,
             std::string("== main ==\n"
                          "0000  Constant          0  ; 0\n"
                          "0003  Constant          1  ; 0\n"
                          "0006  Constant          2  ; 5\n"
                          "0009  SetLocal          slot=1 width=1\n"
                          "0014  Constant          3  ; 1\n"
                          "0017  Constant          4  ; 2\n"
                          "0020  Constant          5  ; 3\n"
                          "0023  Constant          6  ; 0\n"
                          "0026  GetLocal          slot=5 width=1\n"
                          "0031  Constant          7  ; 9\n"
                          "0034  SetLocalIndexed   base=2 stride=1 size=3 extra=0 width=1\n"
                          "0047  Pop               width=6\n"
                          "0050  Return            width=0\n"),
             "assign through field/dynamic index");
}

void test_call_operands_report_callee_arity_and_return_width() {
    std::string out = compile_source(
        "fn add(a: int, b: int) -> int {\n"
        "  return a + b;\n"
        "}\n"
        "fn main() {\n"
        "  let r = add(1, 2);\n"
        "}\n",
        "call operands");
    check_eq(out,
             std::string("== add ==\n"
                          "0000  GetLocal          slot=0 width=1\n"
                          "0005  GetLocal          slot=1 width=1\n"
                          "0010  Add\n"
                          "0011  Return            width=1\n"
                          "0014  Return            width=0\n"
                          "== main ==\n"
                          "0000  Constant          0  ; 1\n"
                          "0003  Constant          1  ; 2\n"
                          "0006  Call              fn=0(add) args=2 ret=1\n"
                          "0013  Pop               width=1\n"
                          "0016  Return            width=0\n"),
             "call operands");
}

void test_forward_and_recursive_calls_resolve_by_declaration_order_index() {
    // `main` (declared second, function_idx 1) calls `helper` (declared
    // first, function_idx 0) -- and helper calls itself recursively.
    std::string out = compile_source(
        "fn helper(n: int) -> int {\n"
        "  if n <= 0 { return 0; }\n"
        "  return helper(n - 1);\n"
        "}\n"
        "fn main() {\n"
        "  let r = helper(3);\n"
        "}\n",
        "forward/recursive calls");
    check_eq(out.find("Call              fn=0(helper)") != std::string::npos, true, "recursive call resolves to fn=0");
    check_eq(out.find("== main ==\n0000  Constant          0  ; 3\n0003  Call              fn=0(helper)") !=
                 std::string::npos,
             true, "main's call to helper resolves to fn=0");
}

void test_print_of_scalar_uses_call_print_with_arg_count() {
    std::string out = compile_source(
        "fn main() {\n"
        "  print(1, true);\n"
        "}\n",
        "print arg count");
    check_eq(out,
             std::string("== main ==\n"
                          "0000  Constant          0  ; 1\n"
                          "0003  Constant          1  ; true\n"
                          "0006  CallPrint         argc=2\n"
                          "0009  Return            width=0\n"),
             "print arg count");
}

void test_chained_dynamic_index_is_a_documented_compiler_limitation() {
    // `s.points[i].points[j]` -- two non-literal indices in the same
    // lvalue/rvalue chain -- is exactly the shape resolve_address() can't
    // express (bytecode_compiler.hpp's header comment). It should fail
    // loudly, not silently compile something wrong.
    std::string result = expect_throw(
        "struct Inner { points: int[2] }\n"
        "struct Outer { points: Inner[2] }\n"
        "fn f(o: Outer, i: int, j: int) -> int {\n"
        "  return o.points[i].points[j];\n"
        "}\n"
        "fn main() {}\n");
    check_eq(result != "<did not throw>", true, "chained dynamic index throws rather than miscompiling");
}

void test_print_of_struct_is_a_documented_compiler_limitation() {
    std::string result = expect_throw(
        "struct Point { x: int, y: int }\n"
        "fn main() {\n"
        "  print(Point(1, 2));\n"
        "}\n");
    check_eq(result != "<did not throw>", true, "print(struct) throws rather than miscompiling");
}

void test_struct_equality_is_a_documented_compiler_limitation() {
    // The checker allows `==` on two same-typed structs (check_binary only
    // requires rhs_type == lhs_type), but Eq's instruction encoding has no
    // width operand -- see this week's fix in compile_binary().
    std::string result = expect_throw(
        "struct Point { x: int, y: int }\n"
        "fn main() {\n"
        "  let eq = Point(1, 2) == Point(1, 2);\n"
        "}\n");
    check_eq(result != "<did not throw>", true, "struct == struct throws rather than miscompiling");
}

}  // namespace

void run_bytecode_compiler_tests() {
    test_struct_construction_and_array_literal_emit_no_opcode();
    test_locals_get_sequential_frame_slots();
    test_block_scope_pops_locals_on_exit();
    test_if_else_jump_targets();
    test_while_loop_backward_jump_targets_condition_check();
    test_short_circuit_and_does_not_evaluate_rhs_unconditionally();
    test_short_circuit_or();
    test_literal_array_index_folds_to_plain_getlocal();
    test_dynamic_array_index_uses_indexed_opcode();
    test_field_access_offset();
    test_assign_through_field_and_dynamic_index();
    test_call_operands_report_callee_arity_and_return_width();
    test_forward_and_recursive_calls_resolve_by_declaration_order_index();
    test_print_of_scalar_uses_call_print_with_arg_count();
    test_chained_dynamic_index_is_a_documented_compiler_limitation();
    test_print_of_struct_is_a_documented_compiler_limitation();
    test_struct_equality_is_a_documented_compiler_limitation();
}
