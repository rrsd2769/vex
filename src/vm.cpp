#include "vex/vm.hpp"

#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace vex {

namespace {

std::int64_t as_int(const Value& v) { return std::get<std::int64_t>(v.data); }
bool as_bool(const Value& v) { return std::get<bool>(v.data); }

bool values_equal(const Value& a, const Value& b) {
    if (const auto* x = std::get_if<std::int64_t>(&a.data)) return *x == std::get<std::int64_t>(b.data);
    if (const auto* x = std::get_if<double>(&a.data)) return *x == std::get<double>(b.data);
    if (const auto* x = std::get_if<bool>(&a.data)) return *x == std::get<bool>(b.data);
    return *std::get<const std::string*>(a.data) == *std::get<const std::string*>(b.data);
}

}  // namespace

VM::VM(const BytecodeProgram& program) : program_(program) {
    resolved_constants_.resize(program_.functions.size());
    for (std::size_t i = 0; i < program_.functions.size(); ++i) {
        const std::vector<Constant>& pool = program_.functions[i].chunk.constants();
        resolved_constants_[i].reserve(pool.size());
        for (const Constant& c : pool) resolved_constants_[i].push_back(resolve_constant(c));
    }
}

Value VM::resolve_constant(const Constant& constant) {
    if (const auto* v = std::get_if<std::int64_t>(&constant.value)) return Value{*v};
    if (const auto* v = std::get_if<double>(&constant.value)) return Value{*v};
    if (const auto* v = std::get_if<bool>(&constant.value)) return Value{*v};
    return Value{arena_.allocate(std::get<std::string>(constant.value))};
}

void VM::runtime_error(const Chunk& chunk, std::size_t instr_offset, std::string message) {
    const Span* span = chunk.span_at(instr_offset);
    Diagnostic diag;
    diag.severity = Severity::Error;
    diag.message = std::move(message);
    diag.primary = Label{span != nullptr ? *span : Span{0, 0}, "here"};
    throw VMError(std::move(diag));
}

Value VM::eval_arithmetic(OpCode op, const Value& a, const Value& b, const Chunk& chunk, std::size_t instr_offset) {
    if (std::holds_alternative<std::int64_t>(a.data)) {
        std::int64_t x = as_int(a);
        std::int64_t y = as_int(b);
        switch (op) {
            case OpCode::Add: return Value{x + y};
            case OpCode::Sub: return Value{x - y};
            case OpCode::Mul: return Value{x * y};
            case OpCode::Div:
                if (y == 0) runtime_error(chunk, instr_offset, "division by zero");
                return Value{x / y};
            case OpCode::Mod:
                if (y == 0) runtime_error(chunk, instr_offset, "modulo by zero");
                return Value{x % y};
            default: break;
        }
    } else {
        double x = std::get<double>(a.data);
        double y = std::get<double>(b.data);
        switch (op) {
            case OpCode::Add: return Value{x + y};
            case OpCode::Sub: return Value{x - y};
            case OpCode::Mul: return Value{x * y};
            case OpCode::Div: return Value{x / y};
            case OpCode::Mod: return Value{std::fmod(x, y)};
            default: break;
        }
    }
    throw std::runtime_error("vm: eval_arithmetic called with a non-arithmetic opcode");  // unreachable
}

bool VM::eval_comparison(OpCode op, const Value& a, const Value& b) {
    if (op == OpCode::Eq) return values_equal(a, b);
    if (op == OpCode::NotEq) return !values_equal(a, b);

    // Less/LessEq/Greater/GreaterEq: numeric-only, checker-enforced.
    if (std::holds_alternative<std::int64_t>(a.data)) {
        std::int64_t x = as_int(a);
        std::int64_t y = as_int(b);
        switch (op) {
            case OpCode::Less: return x < y;
            case OpCode::LessEq: return x <= y;
            case OpCode::Greater: return x > y;
            case OpCode::GreaterEq: return x >= y;
            default: break;
        }
    } else {
        double x = std::get<double>(a.data);
        double y = std::get<double>(b.data);
        switch (op) {
            case OpCode::Less: return x < y;
            case OpCode::LessEq: return x <= y;
            case OpCode::Greater: return x > y;
            case OpCode::GreaterEq: return x >= y;
            default: break;
        }
    }
    throw std::runtime_error("vm: eval_comparison called with a non-comparison opcode");  // unreachable
}

void VM::run(std::ostream& out) {
    frames_.push_back(CallFrame{program_.main_index, 0, 0});

    while (!frames_.empty()) {
        CallFrame& frame = frames_.back();
        const Chunk& chunk = program_.functions[frame.function_idx].chunk;
        const std::vector<std::uint8_t>& code = chunk.code();

        std::size_t instr_start = frame.ip;
        auto op = static_cast<OpCode>(code[instr_start]);
        std::size_t cursor = instr_start + 1;

        switch (op) {
            case OpCode::Constant: {
                std::uint16_t idx = read_u16(code, cursor);
                cursor += 2;
                stack_.push_back(resolved_constants_[frame.function_idx][idx]);
                frame.ip = cursor;
                break;
            }
            case OpCode::GetLocal: {
                std::uint16_t slot = read_u16(code, cursor);
                cursor += 2;
                std::uint16_t width = read_u16(code, cursor);
                cursor += 2;
                for (std::uint16_t i = 0; i < width; ++i) stack_.push_back(stack_[frame.base + slot + i]);
                frame.ip = cursor;
                break;
            }
            case OpCode::SetLocal: {
                std::uint16_t slot = read_u16(code, cursor);
                cursor += 2;
                std::uint16_t width = read_u16(code, cursor);
                cursor += 2;
                for (std::uint16_t i = width; i-- > 0;) {
                    stack_[frame.base + slot + i] = std::move(stack_.back());
                    stack_.pop_back();
                }
                frame.ip = cursor;
                break;
            }
            case OpCode::GetLocalIndexed: {
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

                std::int64_t index = as_int(stack_.back());
                stack_.pop_back();
                if (index < 0 || static_cast<std::uint64_t>(index) >= array_size) {
                    runtime_error(chunk, instr_start,
                                  "array index " + std::to_string(index) + " out of bounds for array of size " +
                                      std::to_string(array_size));
                }
                std::size_t addr = frame.base + base_slot + static_cast<std::size_t>(index) * stride + extra_offset;
                for (std::uint16_t i = 0; i < width; ++i) stack_.push_back(stack_[addr + i]);
                frame.ip = cursor;
                break;
            }
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

                std::vector<Value> value(width);
                for (std::uint16_t i = width; i-- > 0;) {
                    value[i] = std::move(stack_.back());
                    stack_.pop_back();
                }
                std::int64_t index = as_int(stack_.back());
                stack_.pop_back();
                if (index < 0 || static_cast<std::uint64_t>(index) >= array_size) {
                    runtime_error(chunk, instr_start,
                                  "array index " + std::to_string(index) + " out of bounds for array of size " +
                                      std::to_string(array_size));
                }
                std::size_t addr = frame.base + base_slot + static_cast<std::size_t>(index) * stride + extra_offset;
                for (std::uint16_t i = 0; i < width; ++i) stack_[addr + i] = std::move(value[i]);
                frame.ip = cursor;
                break;
            }
            case OpCode::Pop: {
                std::uint16_t width = read_u16(code, cursor);
                cursor += 2;
                stack_.resize(stack_.size() - width);
                frame.ip = cursor;
                break;
            }
            case OpCode::Add:
            case OpCode::Sub:
            case OpCode::Mul:
            case OpCode::Div:
            case OpCode::Mod: {
                Value b = std::move(stack_.back());
                stack_.pop_back();
                Value a = std::move(stack_.back());
                stack_.pop_back();
                stack_.push_back(eval_arithmetic(op, a, b, chunk, instr_start));
                frame.ip = cursor;
                break;
            }
            case OpCode::Neg: {
                Value a = std::move(stack_.back());
                stack_.pop_back();
                if (const auto* v = std::get_if<std::int64_t>(&a.data)) {
                    stack_.push_back(Value{-*v});
                } else {
                    stack_.push_back(Value{-std::get<double>(a.data)});
                }
                frame.ip = cursor;
                break;
            }
            case OpCode::Not: {
                bool v = as_bool(stack_.back());
                stack_.pop_back();
                stack_.push_back(Value{!v});
                frame.ip = cursor;
                break;
            }
            case OpCode::Eq:
            case OpCode::NotEq:
            case OpCode::Less:
            case OpCode::LessEq:
            case OpCode::Greater:
            case OpCode::GreaterEq: {
                Value b = std::move(stack_.back());
                stack_.pop_back();
                Value a = std::move(stack_.back());
                stack_.pop_back();
                stack_.push_back(Value{eval_comparison(op, a, b)});
                frame.ip = cursor;
                break;
            }
            case OpCode::Jump: {
                frame.ip = read_u32(code, cursor);
                break;
            }
            case OpCode::JumpIfFalse: {
                std::uint32_t target = read_u32(code, cursor);
                cursor += 4;
                bool cond = as_bool(stack_.back());
                stack_.pop_back();
                frame.ip = cond ? cursor : target;
                break;
            }
            case OpCode::JumpIfFalseNoPop: {
                std::uint32_t target = read_u32(code, cursor);
                cursor += 4;
                frame.ip = as_bool(stack_.back()) ? cursor : target;
                break;
            }
            case OpCode::JumpIfTrueNoPop: {
                std::uint32_t target = read_u32(code, cursor);
                cursor += 4;
                frame.ip = as_bool(stack_.back()) ? target : cursor;
                break;
            }
            case OpCode::Call: {
                std::uint16_t function_idx = read_u16(code, cursor);
                cursor += 2;
                std::uint16_t arg_width = read_u16(code, cursor);
                cursor += 2;
                cursor += 2;  // ret_width -- informational; Return's own width operand governs the stack shape

                frame.ip = cursor;
                std::size_t new_base = stack_.size() - arg_width;
                frames_.push_back(CallFrame{function_idx, 0, new_base});
                break;
            }
            case OpCode::CallPrint: {
                std::uint16_t argc = read_u16(code, cursor);
                cursor += 2;
                std::vector<Value> args(argc);
                for (std::uint16_t i = argc; i-- > 0;) {
                    args[i] = std::move(stack_.back());
                    stack_.pop_back();
                }
                for (std::size_t i = 0; i < args.size(); ++i) {
                    if (i > 0) out << ' ';
                    print_value(out, args[i]);
                }
                out << '\n';
                frame.ip = cursor;
                break;
            }
            case OpCode::Return: {
                std::uint16_t width = read_u16(code, cursor);
                cursor += 2;
                std::vector<Value> ret(width);
                for (std::uint16_t i = width; i-- > 0;) {
                    ret[i] = std::move(stack_.back());
                    stack_.pop_back();
                }
                std::size_t ret_base = frame.base;
                stack_.resize(ret_base);
                for (Value& v : ret) stack_.push_back(std::move(v));
                frames_.pop_back();
                break;
            }
        }
    }
}

}  // namespace vex
