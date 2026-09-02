#include "vex/bytecode_compiler.hpp"

#include <algorithm>
#include <stdexcept>

namespace vex {

namespace {
// Mirrors TypeChecker's own kBuiltinPrint (type_checker.cpp) -- both are
// the single string literal naming the one builtin this language has; not
// worth sharing a symbol for.
constexpr const char* kBuiltinPrint = "print";

OpCode binary_opcode(TokenKind op) {
    switch (op) {
        case TokenKind::Plus: return OpCode::Add;
        case TokenKind::Minus: return OpCode::Sub;
        case TokenKind::Star: return OpCode::Mul;
        case TokenKind::Slash: return OpCode::Div;
        case TokenKind::Percent: return OpCode::Mod;
        case TokenKind::EqEq: return OpCode::Eq;
        case TokenKind::BangEq: return OpCode::NotEq;
        case TokenKind::Less: return OpCode::Less;
        case TokenKind::LessEq: return OpCode::LessEq;
        case TokenKind::Greater: return OpCode::Greater;
        case TokenKind::GreaterEq: return OpCode::GreaterEq;
        default:
            throw std::runtime_error("bytecode compiler: not a simple binary opcode (should be && / || instead)");
    }
}

}  // namespace

BytecodeCompiler::BytecodeCompiler(const Program& program, const TypeChecker& checker)
    : program_(program), expr_types_(checker.expr_types()) {}

void BytecodeCompiler::register_structs() {
    for (const Item& item : program_.items) {
        if (const auto* st = std::get_if<StructDecl>(&item.node)) {
            structs_[st->name] = st;
        }
    }
}

void BytecodeCompiler::register_functions() {
    for (const Item& item : program_.items) {
        if (const auto* fn = std::get_if<FunctionDecl>(&item.node)) {
            FunctionSignature sig;
            sig.function_idx = function_decls_.size();
            for (const Param& param : fn->params) {
                sig.param_width += type_width(*try_resolve_type_ref(param.type, structs_));
            }
            sig.return_width =
                fn->return_type ? type_width(*try_resolve_type_ref(*fn->return_type, structs_)) : 0;
            functions_[fn->name] = sig;
            function_decls_.push_back(fn);
        }
    }
}

const BytecodeCompiler::StructLayout& BytecodeCompiler::layout_of(const std::string& name) {
    if (auto it = struct_layouts_.find(name); it != struct_layouts_.end()) return it->second;

    StructLayout layout;
    std::uint16_t offset = 0;
    const StructDecl& decl = *structs_.at(name);
    for (const Field& field : decl.fields) {
        Type field_type = *try_resolve_type_ref(field.type, structs_);
        std::uint16_t width = type_width(field_type);
        layout.field_index[field.name] = layout.fields.size();
        layout.fields.push_back(FieldLayout{offset, width, std::move(field_type)});
        offset += width;
    }
    layout.width = offset;

    return struct_layouts_.emplace(name, std::move(layout)).first->second;
}

std::uint16_t BytecodeCompiler::type_width(const Type& type) {
    switch (type.kind) {
        case TypeKind::Int:
        case TypeKind::Float:
        case TypeKind::Bool:
        case TypeKind::String: return 1;
        case TypeKind::Struct: return layout_of(type.struct_name).width;
        case TypeKind::Array: return static_cast<std::uint16_t>(type.array_size * type_width(*type.element_type));
        case TypeKind::Void:
        case TypeKind::Unknown: return 0;
    }
    return 0;  // unreachable -- every TypeKind is handled above
}

const BytecodeCompiler::Local* BytecodeCompiler::find_local(const FunctionCompileState& fs,
                                                              const std::string& name) const {
    for (auto it = fs.locals.rbegin(); it != fs.locals.rend(); ++it) {
        if (it->name == name) return &*it;
    }
    return nullptr;  // unreachable for a type-checked program -- every Identifier resolved once already
}

BytecodeCompiler::AddressPlan BytecodeCompiler::resolve_address(const Expr& expr, FunctionCompileState& fs) {
    if (const auto* ident = std::get_if<Identifier>(&expr.node)) {
        const Local* local = find_local(fs, ident->name);
        AddressPlan plan;
        plan.base_slot = local->slot;
        plan.width = local->width;
        return plan;
    }

    if (const auto* field = std::get_if<FieldAccessExpr>(&expr.node)) {
        AddressPlan inner = resolve_address(*field->object, fs);
        const Type& object_type = expr_types_.at(field->object.get());
        const FieldLayout& fl = layout_of(object_type.struct_name).field(field->field);

        if (!inner.has_dynamic) {
            AddressPlan plan;
            plan.base_slot = static_cast<std::uint16_t>(inner.base_slot + fl.offset);
            plan.width = fl.width;
            return plan;
        }
        AddressPlan plan = inner;
        plan.width = fl.width;
        plan.extra_offset = static_cast<std::uint16_t>(inner.extra_offset + fl.offset);
        return plan;
    }

    if (const auto* index = std::get_if<IndexExpr>(&expr.node)) {
        AddressPlan inner = resolve_address(*index->object, fs);
        if (inner.has_dynamic) {
            throw std::runtime_error(
                "bytecode compiler: chained dynamic array indexing (more than one non-literal index "
                "in the same expression, e.g. `a[i].b[j]`) is not supported -- see "
                "bytecode_compiler.hpp's header comment");
        }

        const Type& object_type = expr_types_.at(index->object.get());
        std::uint16_t elem_width = type_width(*object_type.element_type);

        if (const auto* literal = std::get_if<IntLiteral>(&index->index->node)) {
            // Already statically bounds-checked by the type checker.
            AddressPlan plan;
            plan.base_slot =
                static_cast<std::uint16_t>(inner.base_slot + static_cast<std::uint16_t>(literal->value) * elem_width);
            plan.width = elem_width;
            return plan;
        }

        AddressPlan plan;
        plan.base_slot = inner.base_slot;
        plan.width = elem_width;
        plan.has_dynamic = true;
        plan.dynamic_index_expr = index->index.get();
        plan.stride = elem_width;
        plan.array_size = object_type.array_size;
        plan.extra_offset = 0;
        return plan;
    }

    throw std::runtime_error("bytecode compiler: not an addressable expression");  // unreachable
}

BytecodeProgram BytecodeCompiler::compile() {
    register_structs();
    register_functions();

    BytecodeProgram program;
    program.functions.resize(function_decls_.size());
    for (std::size_t i = 0; i < function_decls_.size(); ++i) {
        const FunctionDecl& fn = *function_decls_[i];
        const FunctionSignature& sig = functions_.at(fn.name);

        FunctionCompileState fs;
        fs.return_width = sig.return_width;
        for (const Param& param : fn.params) {
            std::uint16_t width = type_width(*try_resolve_type_ref(param.type, structs_));
            fs.locals.push_back(Local{param.name, fs.current_top, width});
            fs.current_top += width;
        }
        fs.max_top = fs.current_top;

        compile_block(fn.body, fs);
        // Safety net for a void function that falls off the end of its body
        // without an explicit `return;` -- dead code for a non-void
        // function, which the checker guarantees always returns before
        // reaching here.
        fs.chunk.emit_op(OpCode::Return, fn.body.span);
        fs.chunk.emit_u16(0);

        FunctionProto proto;
        proto.name = fn.name;
        proto.param_width = sig.param_width;
        proto.return_width = sig.return_width;
        proto.frame_size = fs.max_top;
        proto.chunk = std::move(fs.chunk);
        program.functions[i] = std::move(proto);

        if (fn.name == "main") program.main_index = i;
    }
    return program;
}

void BytecodeCompiler::compile_block(const Block& block, FunctionCompileState& fs) {
    std::size_t locals_at_entry = fs.locals.size();
    for (const StmtPtr& stmt : block.statements) compile_stmt(*stmt, fs);

    std::uint16_t width = 0;
    while (fs.locals.size() > locals_at_entry) {
        width += fs.locals.back().width;
        fs.locals.pop_back();
    }
    if (width > 0) {
        fs.chunk.emit_op(OpCode::Pop, block.span);
        fs.chunk.emit_u16(width);
        fs.current_top -= width;
    }
}

void BytecodeCompiler::compile_stmt(const Stmt& stmt, FunctionCompileState& fs) {
    if (const auto* n = std::get_if<VarDecl>(&stmt.node)) {
        compile_var_decl(*n, fs);
        return;
    }
    if (const auto* n = std::get_if<AssignStmt>(&stmt.node)) {
        compile_assign(*n, fs);
        return;
    }
    if (const auto* n = std::get_if<ExprStmt>(&stmt.node)) {
        compile_expr(*n->expr, fs);
        std::uint16_t width = type_width(expr_types_.at(n->expr.get()));
        if (width > 0) {
            fs.chunk.emit_op(OpCode::Pop, stmt.span);
            fs.chunk.emit_u16(width);
        }
        return;
    }
    if (const auto* n = std::get_if<IfStmt>(&stmt.node)) {
        compile_if(*n, fs);
        return;
    }
    if (const auto* n = std::get_if<WhileStmt>(&stmt.node)) {
        compile_while(*n, fs);
        return;
    }
    if (const auto* n = std::get_if<ForStmt>(&stmt.node)) {
        compile_for(*n, stmt.span, fs);
        return;
    }
    if (const auto* n = std::get_if<ReturnStmt>(&stmt.node)) {
        compile_return(*n, stmt.span, fs);
        return;
    }
}

void BytecodeCompiler::compile_var_decl(const VarDecl& decl, FunctionCompileState& fs) {
    compile_expr(*decl.init, fs);
    Type var_type = decl.type ? *try_resolve_type_ref(*decl.type, structs_) : expr_types_.at(decl.init.get());
    std::uint16_t width = type_width(var_type);

    fs.locals.push_back(Local{decl.name, fs.current_top, width});
    fs.current_top += width;
    fs.max_top = std::max(fs.max_top, fs.current_top);
}

void BytecodeCompiler::compile_assign(const AssignStmt& stmt, FunctionCompileState& fs) {
    AddressPlan addr = resolve_address(*stmt.target, fs);
    if (!addr.has_dynamic) {
        compile_expr(*stmt.value, fs);
        fs.chunk.emit_op(OpCode::SetLocal, stmt.target->span);
        fs.chunk.emit_u16(addr.base_slot);
        fs.chunk.emit_u16(addr.width);
        return;
    }

    compile_expr(*addr.dynamic_index_expr, fs);  // index pushed first...
    compile_expr(*stmt.value, fs);                // ...value pushed on top of it
    fs.chunk.emit_op(OpCode::SetLocalIndexed, stmt.target->span);
    fs.chunk.emit_u16(addr.base_slot);
    fs.chunk.emit_u16(addr.stride);
    fs.chunk.emit_u32(addr.array_size);
    fs.chunk.emit_u16(addr.extra_offset);
    fs.chunk.emit_u16(addr.width);
}

void BytecodeCompiler::compile_if(const IfStmt& stmt, FunctionCompileState& fs) {
    compile_expr(*stmt.condition, fs);
    std::size_t else_jump = fs.chunk.emit_jump(OpCode::JumpIfFalse, stmt.condition->span);

    compile_block(stmt.then_block, fs);

    if (stmt.else_block) {
        std::size_t end_jump = fs.chunk.emit_jump(OpCode::Jump, stmt.then_block.span);
        fs.chunk.patch_jump(else_jump);
        compile_block(*stmt.else_block, fs);
        fs.chunk.patch_jump(end_jump);
    } else {
        fs.chunk.patch_jump(else_jump);
    }
}

void BytecodeCompiler::compile_while(const WhileStmt& stmt, FunctionCompileState& fs) {
    std::size_t loop_start = fs.chunk.code().size();
    compile_expr(*stmt.condition, fs);
    std::size_t exit_jump = fs.chunk.emit_jump(OpCode::JumpIfFalse, stmt.condition->span);

    compile_block(stmt.body, fs);
    fs.chunk.emit_jump_to(OpCode::Jump, stmt.body.span, loop_start);

    fs.chunk.patch_jump(exit_jump);
}

void BytecodeCompiler::compile_for(const ForStmt& stmt, const Span& stmt_span, FunctionCompileState& fs) {
    // Two hidden locals live for the loop's duration: the loop variable
    // itself, and the range's end value, evaluated once up front rather
    // than re-evaluated every iteration (it may not be a bare literal).
    compile_expr(*stmt.range_start, fs);
    std::uint16_t i_slot = fs.current_top;
    fs.locals.push_back(Local{stmt.var_name, i_slot, 1});
    fs.current_top += 1;

    compile_expr(*stmt.range_end, fs);
    std::uint16_t end_slot = fs.current_top;
    // "<for-end>" can't collide with a source identifier -- the lexer never
    // produces '<' inside one.
    fs.locals.push_back(Local{"<for-end>", end_slot, 1});
    fs.current_top += 1;
    fs.max_top = std::max(fs.max_top, fs.current_top);

    std::size_t loop_start = fs.chunk.code().size();
    fs.chunk.emit_op(OpCode::GetLocal, stmt_span);
    fs.chunk.emit_u16(i_slot);
    fs.chunk.emit_u16(1);
    fs.chunk.emit_op(OpCode::GetLocal, stmt_span);
    fs.chunk.emit_u16(end_slot);
    fs.chunk.emit_u16(1);
    fs.chunk.emit_op(OpCode::Less, stmt_span);
    std::size_t exit_jump = fs.chunk.emit_jump(OpCode::JumpIfFalse, stmt_span);

    compile_block(stmt.body, fs);

    fs.chunk.emit_op(OpCode::GetLocal, stmt_span);
    fs.chunk.emit_u16(i_slot);
    fs.chunk.emit_u16(1);
    std::uint16_t one_idx = fs.chunk.add_constant(Constant{std::int64_t{1}});
    fs.chunk.emit_op(OpCode::Constant, stmt_span);
    fs.chunk.emit_u16(one_idx);
    fs.chunk.emit_op(OpCode::Add, stmt_span);
    fs.chunk.emit_op(OpCode::SetLocal, stmt_span);
    fs.chunk.emit_u16(i_slot);
    fs.chunk.emit_u16(1);

    fs.chunk.emit_jump_to(OpCode::Jump, stmt_span, loop_start);
    fs.chunk.patch_jump(exit_jump);

    fs.chunk.emit_op(OpCode::Pop, stmt_span);
    fs.chunk.emit_u16(2);
    fs.locals.pop_back();
    fs.locals.pop_back();
    fs.current_top -= 2;
}

void BytecodeCompiler::compile_return(const ReturnStmt& stmt, const Span& stmt_span, FunctionCompileState& fs) {
    if (!stmt.value) {
        fs.chunk.emit_op(OpCode::Return, stmt_span);
        fs.chunk.emit_u16(0);
        return;
    }
    compile_expr(*stmt.value, fs);
    fs.chunk.emit_op(OpCode::Return, stmt_span);
    fs.chunk.emit_u16(fs.return_width);
}

void BytecodeCompiler::compile_expr(const Expr& expr, FunctionCompileState& fs) {
    if (const auto* n = std::get_if<IntLiteral>(&expr.node)) {
        std::uint16_t idx = fs.chunk.add_constant(Constant{n->value});
        fs.chunk.emit_op(OpCode::Constant, expr.span);
        fs.chunk.emit_u16(idx);
        return;
    }
    if (const auto* n = std::get_if<FloatLiteral>(&expr.node)) {
        std::uint16_t idx = fs.chunk.add_constant(Constant{n->value});
        fs.chunk.emit_op(OpCode::Constant, expr.span);
        fs.chunk.emit_u16(idx);
        return;
    }
    if (const auto* n = std::get_if<BoolLiteral>(&expr.node)) {
        std::uint16_t idx = fs.chunk.add_constant(Constant{n->value});
        fs.chunk.emit_op(OpCode::Constant, expr.span);
        fs.chunk.emit_u16(idx);
        return;
    }
    if (const auto* n = std::get_if<StringLiteral>(&expr.node)) {
        std::uint16_t idx = fs.chunk.add_constant(Constant{n->value});
        fs.chunk.emit_op(OpCode::Constant, expr.span);
        fs.chunk.emit_u16(idx);
        return;
    }
    if (std::get_if<Identifier>(&expr.node) || std::get_if<FieldAccessExpr>(&expr.node) ||
        std::get_if<IndexExpr>(&expr.node)) {
        compile_load(expr, fs);
        return;
    }
    if (const auto* n = std::get_if<UnaryExpr>(&expr.node)) {
        compile_unary(*n, expr.span, fs);
        return;
    }
    if (const auto* n = std::get_if<BinaryExpr>(&expr.node)) {
        compile_binary(*n, expr.span, fs);
        return;
    }
    if (const auto* n = std::get_if<CallExpr>(&expr.node)) {
        compile_call(*n, expr.span, fs);
        return;
    }
    if (const auto* n = std::get_if<ArrayLiteral>(&expr.node)) {
        // No opcode -- see this header's design-call comment.
        for (const ExprPtr& element : n->elements) compile_expr(*element, fs);
        return;
    }
}

void BytecodeCompiler::compile_load(const Expr& expr, FunctionCompileState& fs) {
    AddressPlan addr = resolve_address(expr, fs);
    if (!addr.has_dynamic) {
        fs.chunk.emit_op(OpCode::GetLocal, expr.span);
        fs.chunk.emit_u16(addr.base_slot);
        fs.chunk.emit_u16(addr.width);
        return;
    }

    compile_expr(*addr.dynamic_index_expr, fs);
    fs.chunk.emit_op(OpCode::GetLocalIndexed, expr.span);
    fs.chunk.emit_u16(addr.base_slot);
    fs.chunk.emit_u16(addr.stride);
    fs.chunk.emit_u32(addr.array_size);
    fs.chunk.emit_u16(addr.extra_offset);
    fs.chunk.emit_u16(addr.width);
}

void BytecodeCompiler::compile_unary(const UnaryExpr& node, const Span& span, FunctionCompileState& fs) {
    compile_expr(*node.operand, fs);
    fs.chunk.emit_op(node.op == TokenKind::Minus ? OpCode::Neg : OpCode::Not, span);
}

void BytecodeCompiler::compile_binary(const BinaryExpr& node, const Span& span, FunctionCompileState& fs) {
    if (node.op == TokenKind::AmpAmp || node.op == TokenKind::PipePipe) {
        compile_expr(*node.lhs, fs);
        std::size_t short_circuit =
            fs.chunk.emit_jump(node.op == TokenKind::AmpAmp ? OpCode::JumpIfFalseNoPop : OpCode::JumpIfTrueNoPop,
                                node.lhs->span);
        fs.chunk.emit_op(OpCode::Pop, node.lhs->span);
        fs.chunk.emit_u16(1);
        compile_expr(*node.rhs, fs);
        fs.chunk.patch_jump(short_circuit);
        return;
    }

    compile_expr(*node.lhs, fs);
    compile_expr(*node.rhs, fs);
    fs.chunk.emit_op(binary_opcode(node.op), span);
}

void BytecodeCompiler::compile_call(const CallExpr& node, const Span& span, FunctionCompileState& fs) {
    const auto& callee = std::get<Identifier>(node.callee->node);
    const std::string& name = callee.name;

    if (name == kBuiltinPrint) {
        for (const ExprPtr& arg : node.args) {
            if (type_width(expr_types_.at(arg.get())) != 1) {
                throw std::runtime_error(
                    "bytecode compiler: print() of a struct or array argument is not supported -- see "
                    "bytecode_compiler.hpp's header comment");
            }
            compile_expr(*arg, fs);
        }
        fs.chunk.emit_op(OpCode::CallPrint, span);
        fs.chunk.emit_u16(static_cast<std::uint16_t>(node.args.size()));
        return;
    }

    if (auto it = functions_.find(name); it != functions_.end()) {
        for (const ExprPtr& arg : node.args) compile_expr(*arg, fs);
        fs.chunk.emit_op(OpCode::Call, span);
        fs.chunk.emit_u16(static_cast<std::uint16_t>(it->second.function_idx));
        fs.chunk.emit_u16(it->second.param_width);
        fs.chunk.emit_u16(it->second.return_width);
        return;
    }

    // Struct construction -- no opcode, see this header's design-call
    // comment. Precondition guarantees `name` is a struct if it's neither
    // the print builtin nor a function.
    for (const ExprPtr& arg : node.args) compile_expr(*arg, fs);
}

}  // namespace vex
