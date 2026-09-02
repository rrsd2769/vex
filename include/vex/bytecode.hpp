// The bytecode representation -- week 6 of ROADMAP.md. This header defines
// the instruction set, the constant pool, and a disassembler. The compiler
// that emits this (BytecodeCompiler) lives in bytecode_compiler.hpp; the
// stack machine that will execute it is week 7.
//
// CONTEXT.md additions this week: Opcode, Chunk, Slot.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "vex/span.hpp"

namespace vex {

// One operation in the instruction set. Operands (in parentheses) are
// encoded as fixed-width little-endian integers immediately following the
// opcode byte, always in the order listed -- see Chunk::emit_* and
// disassemble_instruction() for the exact widths.
//
//   Constant(u16 const_idx)             push constants[idx]                  (1 slot)
//   GetLocal(u16 slot, u16 width)        push `width` slots from frame[slot..]
//   SetLocal(u16 slot, u16 width)        pop `width` slots into frame[slot..]
//   GetLocalIndexed(u16 base_slot, u16 stride, u32 array_size,
//                   u16 extra_offset, u16 width)
//       pop an int index, bounds-check it against array_size, then push
//       `width` slots from frame[base_slot + index*stride + extra_offset..].
//       Used for a dynamic (non-literal) array index -- see
//       bytecode_compiler.hpp's header comment for why base_slot/extra_offset
//       are split the way they are, and the one chain shape this can't
//       express.
//   SetLocalIndexed(...)                 same addressing, but pops a
//                                         `width`-slot value (pushed after
//                                         the index) and stores it there
//   Pop(u16 width)                       discard `width` slots
//   Add, Sub, Mul, Div, Mod, Neg          arithmetic on int or float (the
//                                         type is always known statically,
//                                         but the opcode is generic --
//                                         type-specific dispatch is a week 7
//                                         VM concern, once Value is a tagged
//                                         union anyway)
//   Not                                   logical not (bool)
//   Eq, NotEq, Less, LessEq,
//   Greater, GreaterEq                    comparisons, generic like the
//                                         arithmetic ops above
//   Jump(u32 target)                      unconditional; target is an
//                                         absolute byte offset into this
//                                         Chunk's code
//   JumpIfFalse(u32 target)               pop a bool; jump if false
//   JumpIfFalseNoPop(u32 target)          peek a bool; jump (without
//                                         popping) if false -- `&&`'s
//                                         short-circuit
//   JumpIfTrueNoPop(u32 target)           peek a bool; jump (without
//                                         popping) if true -- `||`'s
//                                         short-circuit
//   Call(u16 function_idx, u16 arg_width,
//        u16 ret_width)                   call BytecodeProgram::functions[idx];
//                                         args (arg_width slots) are already
//                                         on the stack; pushes ret_width
//                                         slots on return
//   CallPrint(u16 arg_count)              the `print` builtin -- each
//                                         argument is exactly 1 slot (see
//                                         bytecode_compiler.hpp for why this
//                                         doesn't generalize to
//                                         struct/array arguments yet)
//   Return(u16 width)                     return the top `width` slots to
//                                         the caller
enum class OpCode : std::uint8_t {
    Constant,
    GetLocal,
    SetLocal,
    GetLocalIndexed,
    SetLocalIndexed,
    Pop,
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    Neg,
    Not,
    Eq,
    NotEq,
    Less,
    LessEq,
    Greater,
    GreaterEq,
    Jump,
    JumpIfFalse,
    JumpIfFalseNoPop,
    JumpIfTrueNoPop,
    Call,
    CallPrint,
    Return,
};

const char* opcode_name(OpCode op);

// Little-endian fixed-width operand decoding, shared by the disassembler
// below and the week 7 VM's dispatch loop -- both must decode exactly the
// instruction encoding Chunk::emit_u16/emit_u32 produce, so this is the one
// place that does it (the same "one place, not two copies that can drift"
// call as stmt.hpp's try_resolve_type_ref).
std::uint16_t read_u16(const std::vector<std::uint8_t>& code, std::size_t offset);
std::uint32_t read_u32(const std::vector<std::uint8_t>& code, std::size_t offset);

// An entry in a Chunk's constant pool (CONTEXT.md: Constant) -- produced
// from a Literal, but distinct from it. No Struct/Array constants: those
// values are always built at their use site from already-executing code
// (see bytecode_compiler.hpp on why struct/array construction needs no
// opcode at all), never loaded whole from the pool.
struct Constant {
    std::variant<std::int64_t, double, bool, std::string> value;
};

std::string to_string(const Constant& constant);

// One function's compiled instructions and the constant pool it indexes
// into (CONTEXT.md: Chunk). Jump targets are absolute offsets within this
// Chunk's code, so backpatching never has to account for an enclosing
// function's length.
class Chunk {
public:
    void emit_op(OpCode op, const Span& span);
    void emit_u16(std::uint16_t value);
    void emit_u32(std::uint32_t value);

    // Emits `op` followed by a 4-byte placeholder for a jump target, and
    // returns the byte offset of that placeholder -- "emit a jump before
    // its target is known, record the hole" (ROADMAP.md). Pass the
    // returned offset to patch_jump() once the target is known.
    std::size_t emit_jump(OpCode op, const Span& span);

    // Backfills the 4-byte placeholder at `placeholder_offset` (as returned
    // by emit_jump) with the current end of code -- "...backfill it once
    // you do."
    void patch_jump(std::size_t placeholder_offset);

    // Emits `op` with a target that's already known -- a loop's backward
    // branch to its own condition check, which was compiled before this
    // jump and so never needs a hole/patch round-trip.
    void emit_jump_to(OpCode op, const Span& span, std::size_t target);

    std::uint16_t add_constant(Constant value);

    const std::vector<std::uint8_t>& code() const { return code_; }
    const std::vector<Constant>& constants() const { return constants_; }

    // The source span the instruction starting at `offset` was compiled
    // from, if recorded. Sparse (keyed by instruction-start offsets only) --
    // not consumed by anything yet, since runtime error reporting is week
    // 7, but the compiler records it as it emits so that stage doesn't need
    // a Chunk format change to get it.
    const Span* span_at(std::size_t offset) const;

private:
    std::vector<std::uint8_t> code_;
    std::vector<Constant> constants_;
    std::unordered_map<std::size_t, Span> spans_;
};

// One compiled function: its Chunk plus the frame metadata a caller (or the
// week 7 VM) needs without inspecting the Chunk's instructions --
// param_width/return_width describe the calling convention (Call's
// arg_width/ret_width operands are redundant with these, but repeating them
// on the instruction is what makes a single disassembled line
// self-explanatory instead of sending the reader back to the function
// table). frame_size is the high-water mark of live local slots (params +
// declared locals, at their point of maximum simultaneous scope) -- see
// bytecode_compiler.hpp's BytecodeCompiler::current_top_.
struct FunctionProto {
    std::string name;
    std::uint16_t param_width = 0;
    std::uint16_t return_width = 0;
    std::uint16_t frame_size = 0;
    Chunk chunk;
};

struct BytecodeProgram {
    std::vector<FunctionProto> functions;
    std::size_t main_index = 0;
};

// Renders every function in `program` as readable, one-instruction-per-line
// bytecode -- offset, opcode name, operands (Call's function_idx resolved
// to a name, Constant's index resolved to its value), matching dump_expr /
// dump_stmt's role of making internal representation legible without
// hand-deriving it.
std::string disassemble(const BytecodeProgram& program);

}  // namespace vex
