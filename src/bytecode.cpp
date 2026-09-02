#include "vex/bytecode.hpp"

#include <cstdio>
#include <sstream>

namespace vex {

const char* opcode_name(OpCode op) {
    switch (op) {
        case OpCode::Constant: return "Constant";
        case OpCode::GetLocal: return "GetLocal";
        case OpCode::SetLocal: return "SetLocal";
        case OpCode::GetLocalIndexed: return "GetLocalIndexed";
        case OpCode::SetLocalIndexed: return "SetLocalIndexed";
        case OpCode::Pop: return "Pop";
        case OpCode::Add: return "Add";
        case OpCode::Sub: return "Sub";
        case OpCode::Mul: return "Mul";
        case OpCode::Div: return "Div";
        case OpCode::Mod: return "Mod";
        case OpCode::Neg: return "Neg";
        case OpCode::Not: return "Not";
        case OpCode::Eq: return "Eq";
        case OpCode::NotEq: return "NotEq";
        case OpCode::Less: return "Less";
        case OpCode::LessEq: return "LessEq";
        case OpCode::Greater: return "Greater";
        case OpCode::GreaterEq: return "GreaterEq";
        case OpCode::Jump: return "Jump";
        case OpCode::JumpIfFalse: return "JumpIfFalse";
        case OpCode::JumpIfFalseNoPop: return "JumpIfFalseNoPop";
        case OpCode::JumpIfTrueNoPop: return "JumpIfTrueNoPop";
        case OpCode::Call: return "Call";
        case OpCode::CallPrint: return "CallPrint";
        case OpCode::Return: return "Return";
    }
    return "<unknown opcode>";  // unreachable -- every OpCode is handled above
}

std::string to_string(const Constant& constant) {
    if (const auto* v = std::get_if<std::int64_t>(&constant.value)) return std::to_string(*v);
    if (const auto* v = std::get_if<double>(&constant.value)) return std::to_string(*v);
    if (const auto* v = std::get_if<bool>(&constant.value)) return *v ? "true" : "false";
    return "\"" + std::get<std::string>(constant.value) + "\"";
}

namespace {

void push_u16(std::vector<std::uint8_t>& code, std::uint16_t value) {
    code.push_back(static_cast<std::uint8_t>(value & 0xFF));
    code.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

void push_u32(std::vector<std::uint8_t>& code, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        code.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFF));
    }
}

void patch_u32(std::vector<std::uint8_t>& code, std::size_t offset, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) code[offset + i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF);
}

}  // namespace

std::uint16_t read_u16(const std::vector<std::uint8_t>& code, std::size_t offset) {
    return static_cast<std::uint16_t>(code[offset]) | (static_cast<std::uint16_t>(code[offset + 1]) << 8);
}

std::uint32_t read_u32(const std::vector<std::uint8_t>& code, std::size_t offset) {
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(code[offset + i]) << (8 * i);
    return value;
}

void Chunk::emit_op(OpCode op, const Span& span) {
    spans_[code_.size()] = span;
    code_.push_back(static_cast<std::uint8_t>(op));
}

void Chunk::emit_u16(std::uint16_t value) { push_u16(code_, value); }

void Chunk::emit_u32(std::uint32_t value) { push_u32(code_, value); }

std::size_t Chunk::emit_jump(OpCode op, const Span& span) {
    emit_op(op, span);
    std::size_t placeholder_offset = code_.size();
    emit_u32(0xFFFFFFFFu);
    return placeholder_offset;
}

void Chunk::patch_jump(std::size_t placeholder_offset) {
    patch_u32(code_, placeholder_offset, static_cast<std::uint32_t>(code_.size()));
}

void Chunk::emit_jump_to(OpCode op, const Span& span, std::size_t target) {
    emit_op(op, span);
    emit_u32(static_cast<std::uint32_t>(target));
}

std::uint16_t Chunk::add_constant(Constant value) {
    constants_.push_back(std::move(value));
    return static_cast<std::uint16_t>(constants_.size() - 1);
}

const Span* Chunk::span_at(std::size_t offset) const {
    auto it = spans_.find(offset);
    return it == spans_.end() ? nullptr : &it->second;
}

namespace {

// Decodes the instruction at `offset`, writes its disassembly to `out`
// (without a trailing newline), and returns the offset of the next
// instruction. `functions`, if given, resolves Call's function_idx operand
// to a name; disassembling a single Chunk in isolation (e.g. from a unit
// test) can pass nullptr and get the bare index instead.
std::size_t disassemble_instruction(const Chunk& chunk, std::size_t offset, std::ostream& out,
                                     const std::vector<FunctionProto>* functions) {
    const std::vector<std::uint8_t>& code = chunk.code();
    auto op = static_cast<OpCode>(code[offset]);
    std::size_t cursor = offset + 1;

    std::ostringstream operands;
    switch (op) {
        case OpCode::Constant: {
            std::uint16_t idx = read_u16(code, cursor);
            cursor += 2;
            operands << idx << "  ; " << to_string(chunk.constants()[idx]);
            break;
        }
        case OpCode::GetLocal:
        case OpCode::SetLocal: {
            std::uint16_t slot = read_u16(code, cursor);
            cursor += 2;
            std::uint16_t width = read_u16(code, cursor);
            cursor += 2;
            operands << "slot=" << slot << " width=" << width;
            break;
        }
        case OpCode::GetLocalIndexed:
        case OpCode::SetLocalIndexed: {
            std::uint16_t base_slot = read_u16(code, cursor);
            cursor += 2;
            std::uint16_t stride = read_u16(code, cursor);
            cursor += 2;
            std::uint32_t array_size = read_u32(code, cursor);
            cursor += 4;
            std::uint16_t extra_offset = read_u16(code, cursor);
            cursor += 2;
            std::uint16_t width = read_u16(code, cursor);
            cursor += 2;
            operands << "base=" << base_slot << " stride=" << stride << " size=" << array_size
                      << " extra=" << extra_offset << " width=" << width;
            break;
        }
        case OpCode::Pop: {
            std::uint16_t width = read_u16(code, cursor);
            cursor += 2;
            operands << "width=" << width;
            break;
        }
        case OpCode::Jump:
        case OpCode::JumpIfFalse:
        case OpCode::JumpIfFalseNoPop:
        case OpCode::JumpIfTrueNoPop: {
            std::uint32_t target = read_u32(code, cursor);
            cursor += 4;
            char target_buf[16];
            std::snprintf(target_buf, sizeof(target_buf), "->%04u", target);
            operands << target_buf;
            break;
        }
        case OpCode::Call: {
            std::uint16_t function_idx = read_u16(code, cursor);
            cursor += 2;
            std::uint16_t arg_width = read_u16(code, cursor);
            cursor += 2;
            std::uint16_t ret_width = read_u16(code, cursor);
            cursor += 2;
            operands << "fn=" << function_idx;
            if (functions != nullptr && function_idx < functions->size()) {
                operands << "(" << (*functions)[function_idx].name << ")";
            }
            operands << " args=" << arg_width << " ret=" << ret_width;
            break;
        }
        case OpCode::CallPrint: {
            std::uint16_t arg_count = read_u16(code, cursor);
            cursor += 2;
            operands << "argc=" << arg_count;
            break;
        }
        case OpCode::Return: {
            std::uint16_t width = read_u16(code, cursor);
            cursor += 2;
            operands << "width=" << width;
            break;
        }
        // Add, Sub, Mul, Div, Mod, Neg, Not, Eq, NotEq, Less, LessEq,
        // Greater, GreaterEq -- no operands.
        default: break;
    }

    char offset_buf[16];
    std::snprintf(offset_buf, sizeof(offset_buf), "%04zu", offset);
    out << offset_buf << "  " << opcode_name(op);
    std::string operand_text = operands.str();
    if (!operand_text.empty()) {
        std::string name = opcode_name(op);
        for (std::size_t pad = name.size(); pad < 18; ++pad) out << ' ';
        out << operand_text;
    }

    return cursor;
}

void disassemble_chunk(const Chunk& chunk, const std::string& name, const std::vector<FunctionProto>& functions,
                        std::ostream& out) {
    out << "== " << name << " ==\n";
    std::size_t offset = 0;
    while (offset < chunk.code().size()) {
        offset = disassemble_instruction(chunk, offset, out, &functions);
        out << "\n";
    }
}

}  // namespace

std::string disassemble(const BytecodeProgram& program) {
    std::ostringstream out;
    for (const FunctionProto& fn : program.functions) {
        disassemble_chunk(fn.chunk, fn.name, program.functions, out);
    }
    return out.str();
}

}  // namespace vex
