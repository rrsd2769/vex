// The bytecode compiler -- week 6 of ROADMAP.md. Lowers an already-checked
// Program (see the precondition on the constructor below) into a
// BytecodeProgram (bytecode.hpp).
//
// Design calls, with reasoning:
//
// - Struct construction and array-literal construction emit no opcode at
//   all. A struct/array value's representation is exactly the concatenation
//   of its fields'/elements' slots, in order -- and check_call_to_struct
//   already requires positional-in-field-declaration-order arguments, and
//   the parser already requires array-literal elements in order, so
//   compiling each argument/element expression in sequence *already*
//   leaves the right bytes in the right place. This is also why the
//   language needs no heap or arena for structs and arrays (unlike
//   strings, which stay dynamically sized) -- see ROADMAP.md week 7's "the
//   runtime for strings and arrays": for arrays specifically, that's bounds
//   checking and indexed addressing (below), not allocation.
// - A local variable's declaration likewise emits no opcode: whatever the
//   initializer's compiled code leaves on top of the stack *is* the local's
//   home from then on (see current_top_ threading through compile_block).
// - Every value's size in stack slots ("width") is known at compile time --
//   int/float/bool/string are 1 slot each (a string's dynamic content is a
//   week 7 arena concern; the slot itself is a fixed-size handle), a
//   struct's width is the sum of its fields', an array's is
//   element_width * size. See type_width()/StructLayout.
// - Field access and array indexing both resolve to a frame-relative
//   address computed by resolve_address(), which walks a
//   Identifier/FieldAccessExpr/IndexExpr chain down to its root local,
//   accumulating: a base_slot (every field-access step, and every
//   *literal*-index step, are folded into this at compile time -- a
//   literal index's bounds were already statically checked by week 5), and
//   at most one dynamic (non-literal-index) component, carried as
//   (stride, array_size, extra_offset) -- extra_offset covers any further
//   *static* offset accumulated after the dynamic index (e.g. `arr[i].y`).
//   GetLocalIndexed/SetLocalIndexed do the index*stride+extra_offset
//   arithmetic and the runtime bounds check in one instruction.
//   **Limitation**: a chain with two or more dynamic indices (e.g.
//   `a[i].b[j]` where both `i` and `j` are non-literal) needs the second
//   index applied to a runtime-computed address, which this fixed-operand
//   instruction shape can't express. resolve_address() throws
//   std::runtime_error if it encounters this rather than silently
//   compiling something wrong -- no test or example program produces this
//   shape (it requires a struct field that is itself an array of another
//   struct with an array field), and it wasn't judged worth a more general
//   (and slower, indirection-based) addressing mode to unblock a case
//   nothing needs. Revisit if week 7+ ever wants it.
// - Arithmetic/comparison opcodes are generic (one Add, not AddInt +
//   AddFloat), even though the compiler always knows the operand types
//   statically. The week 7 VM's Value is a tagged union either way (per
//   ROADMAP.md), so runtime dispatch on the tag is free to add there and
//   keeps the instruction set from doubling for arithmetic and
//   quadrupling for equality across 4 primitive kinds -- "no ceremony
//   beyond what's needed" (the same call made elsewhere in this codebase,
//   e.g. Diagnostic/Label).
// - `print`'s arguments are required to be exactly 1 slot each
//   (CallPrint's argc operand, one per argument). The type checker accepts
//   "any known type" for a print argument, which technically allows a
//   struct or array, but no example or test ever prints one, and
//   supporting it would mean CallPrint needing a per-argument width list
//   instead of a flat argument count. compile_call() throws
//   std::runtime_error if it sees one -- same "loud failure over silent
//   wrong bytecode" call as the chained-dynamic-index limitation above.
// - Likewise, `==`/`!=` require both operands to be exactly 1 slot wide:
//   the checker allows comparing two structs or two arrays of the same
//   type, but Eq/NotEq's instruction encoding has no width operand (always
//   pops one slot per side), so a multi-slot operand would silently
//   compare only its first slot. compile_binary() throws
//   std::runtime_error rather than do that -- found while building the
//   week 7 VM, which is what first needed Eq/NotEq's precondition (every
//   operand is exactly 1 slot) to actually hold.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "vex/bytecode.hpp"
#include "vex/stmt.hpp"
#include "vex/type.hpp"
#include "vex/type_checker.hpp"

namespace vex {

class BytecodeCompiler {
public:
    // Precondition: `checker` has already run check() against `program` and
    // checker.diagnostics() is empty. This compiler has no error-recovery
    // path of its own -- every name it looks up is assumed to resolve and
    // every expression's type (via checker.expr_types()) is assumed to be
    // real, never Unknown. main.cpp's pipeline enforces this by only
    // constructing a BytecodeCompiler after the checker stage reports no
    // diagnostics.
    BytecodeCompiler(const Program& program, const TypeChecker& checker);

    BytecodeProgram compile();

private:
    // One field within a compiled struct layout.
    struct FieldLayout {
        std::uint16_t offset;
        std::uint16_t width;
        Type type;
    };

    struct StructLayout {
        std::uint16_t width = 0;
        std::vector<FieldLayout> fields;              // in declaration order
        std::unordered_map<std::string, std::size_t> field_index;

        const FieldLayout& field(const std::string& name) const { return fields[field_index.at(name)]; }
    };

    struct FunctionSignature {
        std::uint16_t param_width = 0;
        std::uint16_t return_width = 0;
        std::size_t function_idx = 0;
    };

    // A named local's location within the function currently being
    // compiled -- literally its position in FunctionCompileState::locals,
    // which mirrors its live position on the VM's value stack the same way
    // clox's locals-live-on-the-stack design does, generalized to a
    // multi-slot width per local instead of always 1.
    struct Local {
        std::string name;
        std::uint16_t slot;
        std::uint16_t width;
    };

    // Per-function compilation state, threaded through compile_stmt/
    // compile_expr the same way TypeChecker threads a Scope&.
    struct FunctionCompileState {
        Chunk chunk;
        std::vector<Local> locals;
        // The compile-time stack height, in slots, of every currently-live
        // local (params + locals declared so far in the current scope
        // chain) -- equivalently, the slot a newly-declared local will get
        // next. Tracking this needs no bookkeeping through expression
        // evaluation itself (see this header's design-call comment on why
        // a declaration needs no opcode): it only changes at a local's
        // declaration (+= width) and a block's exit (-= the width of the
        // locals it declared).
        std::uint16_t current_top = 0;
        std::uint16_t max_top = 0;  // high-water mark -> FunctionProto::frame_size
        std::uint16_t return_width = 0;
    };

    // The frame-relative address (or, if has_dynamic, the address-minus-one-
    // runtime-index) of an Identifier/FieldAccessExpr/IndexExpr -- see this
    // header's comment on resolve_address() above. Shared between reading
    // such an expression's value (compile_load) and writing to it as an
    // AssignStmt target (compile_assign).
    struct AddressPlan {
        std::uint16_t base_slot = 0;
        std::uint16_t width = 0;
        bool has_dynamic = false;
        const Expr* dynamic_index_expr = nullptr;  // valid iff has_dynamic
        std::uint16_t stride = 0;                  // valid iff has_dynamic
        std::uint32_t array_size = 0;               // valid iff has_dynamic
        std::uint16_t extra_offset = 0;              // valid iff has_dynamic
    };

    void register_structs();
    void register_functions();

    // Computes (and memoizes in struct_layouts_) the field offsets of
    // struct `name`. Safe against recursion because BytecodeCompiler's
    // precondition guarantees the checker already ran
    // check_no_cyclic_structs() -- a struct can't reach itself through its
    // own fields.
    const StructLayout& layout_of(const std::string& name);
    std::uint16_t type_width(const Type& type);

    const Local* find_local(const FunctionCompileState& fs, const std::string& name) const;
    AddressPlan resolve_address(const Expr& expr, FunctionCompileState& fs);

    void compile_function(const FunctionDecl& fn);
    void compile_block(const Block& block, FunctionCompileState& fs);
    void compile_stmt(const Stmt& stmt, FunctionCompileState& fs);
    void compile_var_decl(const VarDecl& decl, FunctionCompileState& fs);
    void compile_assign(const AssignStmt& stmt, FunctionCompileState& fs);
    void compile_if(const IfStmt& stmt, FunctionCompileState& fs);
    void compile_while(const WhileStmt& stmt, FunctionCompileState& fs);
    void compile_for(const ForStmt& stmt, const Span& stmt_span, FunctionCompileState& fs);
    void compile_return(const ReturnStmt& stmt, const Span& stmt_span, FunctionCompileState& fs);

    // Every one of these leaves exactly type_width(expr's type) slots on
    // top of the stack.
    void compile_expr(const Expr& expr, FunctionCompileState& fs);
    void compile_unary(const UnaryExpr& node, const Span& span, FunctionCompileState& fs);
    void compile_binary(const BinaryExpr& node, const Span& span, FunctionCompileState& fs);
    void compile_call(const CallExpr& node, const Span& span, FunctionCompileState& fs);
    // Shared by Identifier, FieldAccessExpr, and IndexExpr reads -- see
    // resolve_address().
    void compile_load(const Expr& expr, FunctionCompileState& fs);

    const Program& program_;
    const std::unordered_map<const Expr*, Type>& expr_types_;

    std::unordered_map<std::string, const StructDecl*> structs_;
    std::unordered_map<std::string, StructLayout> struct_layouts_;
    std::unordered_map<std::string, FunctionSignature> functions_;
    std::vector<const FunctionDecl*> function_decls_;  // index == FunctionSignature::function_idx
};

}  // namespace vex
