// Runtime values -- week 7 of ROADMAP.md. A Value is what occupies one
// Slot (CONTEXT.md) on the VM's stack; Arena is what a Value's string case
// points into. See vm.hpp for the dispatch loop that produces and consumes
// these.
//
// CONTEXT.md additions this week: Value, Arena.
#pragma once

#include <cstdint>
#include <deque>
#include <ostream>
#include <string>
#include <variant>

namespace vex {

// One VM stack slot's contents. A struct or array value is never a single
// Value -- per bytecode_compiler.hpp's week-6 design, its representation
// is several contiguous slots, each an independent Value. This variant is
// exactly the four primitive Types that fit in one slot on their own; the
// string case is a pointer into an Arena, never an owned std::string, so a
// Value stays cheap to copy the same way every other alternative already is
// (copying a struct/array's slots is already just N Value copies, per
// GetLocal/SetLocal -- a Value itself needs to stay copyable for that to be
// correct).
struct Value {
    std::variant<std::int64_t, double, bool, const std::string*> data;
};

// Owns every string a running program can produce -- ROADMAP.md's "arena
// allocator", the defensible stand-in for a GC this project's scope
// deliberately excludes (see ROADMAP.md's "After placements"). A bump
// allocator: allocate() only ever appends, nothing is ever freed until the
// Arena itself is destroyed (at VM teardown). std::deque never moves or
// invalidates an element already pushed, no matter how much more is pushed
// after it, so a pointer handed out by allocate() stays valid for the
// Arena's entire lifetime -- exactly what a Value's string slot needs,
// without the pointer-stability bugs a std::vector<std::string> would
// introduce on reallocation.
//
// The language has no runtime string construction today -- no
// concatenation, no interpolation (type_checker.cpp's check_binary only
// accepts `+` on numeric operands) -- so today the Arena only ever holds
// copies of constant-pool strings, resolved once at VM startup (see
// vm.hpp's VM::resolve_constant). It earns its place anyway: a runtime
// Value shouldn't stay alive by reaching back into the BytecodeProgram
// that compiled it (that would couple the VM's value lifetime to the
// compiler's owned data for no reason), and it's the obvious place a
// future string-building runtime op would allocate into.
class Arena {
public:
    const std::string* allocate(std::string value);

private:
    std::deque<std::string> storage_;
};

// Formats `value` the way `print` renders it at runtime: bare digits for a
// number, `true`/`false` for a bool, and a string's raw content with no
// surrounding quotes (unlike to_string(Constant) in bytecode.hpp, which
// quotes a string because it's rendering disassembly, not program output).
void print_value(std::ostream& out, const Value& value);

}  // namespace vex
