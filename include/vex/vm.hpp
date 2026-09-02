// The bytecode virtual machine -- week 7 of ROADMAP.md, "the week the
// project becomes real": a dispatch loop that runs a BytecodeProgram
// (bytecode.hpp) to completion instead of just disassembling it.
//
// Design calls, with reasoning:
//
// - Call frames are index-based, not pointer-based: VM::run() holds a
//   std::vector<CallFrame> as its call stack and always re-fetches
//   `frames_.back()` at the top of each dispatch-loop iteration, never
//   holding a CallFrame& across a Call or Return -- both push_back and
//   pop_back can reallocate/invalidate. The value stack (std::vector<Value>)
//   is addressed the same way: every Get/SetLocal(Indexed) is relative to
//   the current frame's `base`, clox-style, matching how
//   bytecode_compiler.hpp's FunctionCompileState::locals already models a
//   local's position at compile time.
// - Call/Return's calling convention follows directly from bytecode.hpp's
//   operand comments: Call's `arg_width` slots are already on the stack
//   when Call executes, so the callee's frame base is simply
//   `stack.size() - arg_width` -- no copying, the args *become* the
//   callee's param locals in place. Return(width) moves the top `width`
//   slots down to the caller's frame base and truncates the stack there,
//   which is what turns "args occupied these slots" back into "the return
//   value occupies these slots" for the caller to read after Call returns.
//   Call's own `ret_width` operand is therefore purely informational (for
//   disassembly); Return's `width` operand is what actually governs the
//   stack shape, and the two agree by construction (both come from the
//   same FunctionSignature).
// - Runtime failures (an out-of-bounds array index, integer division or
//   modulo by zero) are reported as a Diagnostic, not a bare exception
//   message -- ROADMAP.md's stated differentiator ("the differentiator is
//   the diagnostics") doesn't stop applying just because the program
//   passed type-checking. Chunk::span_at(), recorded since week 6 but
//   unconsumed until now, is exactly what makes this possible without a
//   Chunk format change.
// - String constants are resolved into the Arena once, at VM construction
//   (see resolve_constant()), not lazily every time a Constant instruction
//   executes -- a string constant used inside a loop or a recursive call
//   would otherwise get a fresh Arena allocation on every execution of
//   that one instruction. See value.hpp's Arena for the rest of the
//   reasoning.
#pragma once

#include <cstddef>
#include <exception>
#include <ostream>
#include <vector>

#include "vex/bytecode.hpp"
#include "vex/diagnostic.hpp"
#include "vex/value.hpp"

namespace vex {

// A runtime failure, carrying a ready-to-render Diagnostic (CONTEXT.md) --
// see this header's comment on why runtime errors go through the same
// diagnostic machinery as every compile-time stage.
class VMError : public std::exception {
public:
    explicit VMError(Diagnostic diagnostic) : diagnostic_(std::move(diagnostic)) {}

    const Diagnostic& diagnostic() const { return diagnostic_; }
    const char* what() const noexcept override { return diagnostic_.message.c_str(); }

private:
    Diagnostic diagnostic_;
};

class VM {
public:
    // Precondition: `program` was produced by BytecodeCompiler from a
    // program that type-checked clean -- same trust boundary
    // BytecodeCompiler itself has with TypeChecker. `program` must outlive
    // this VM (its Chunks are read every dispatch-loop iteration).
    explicit VM(const BytecodeProgram& program);

    // Runs `program.functions[program.main_index]` to completion, writing
    // every `print` call's output to `out` (multiple arguments
    // space-separated, one `\n` per call -- print's own choice, since
    // nothing before this week ever executed it). Throws VMError on a
    // runtime failure.
    void run(std::ostream& out);

private:
    // One call's activation record: which function is running, where
    // execution resumes in its Chunk, and where its slot 0 sits on the
    // (shared, single) value stack. See this header's comment on why this
    // is index-based rather than holding any pointer/reference into
    // frames_ or stack_ across an iteration.
    struct CallFrame {
        std::size_t function_idx;
        std::size_t ip;
        std::size_t base;
    };

    Value resolve_constant(const Constant& constant);

    // Both throw std::runtime_error if `op` isn't actually an arithmetic /
    // comparison opcode -- unreachable given the switch in run() only ever
    // calls them from the matching case labels.
    Value eval_arithmetic(OpCode op, const Value& a, const Value& b, const Chunk& chunk, std::size_t instr_offset);
    bool eval_comparison(OpCode op, const Value& a, const Value& b);

    [[noreturn]] void runtime_error(const Chunk& chunk, std::size_t instr_offset, std::string message);

    const BytecodeProgram& program_;
    Arena arena_;
    std::vector<std::vector<Value>> resolved_constants_;  // [function_idx][constant_idx]
    std::vector<Value> stack_;
    std::vector<CallFrame> frames_;
};

}  // namespace vex
