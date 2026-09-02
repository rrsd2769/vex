// The type representation -- week 4 of ROADMAP.md.
#pragma once

#include <string>

namespace vex {

// Unknown is a checker-internal sentinel, never a real value's type: it
// means "already reported, or not checkable yet this week" (CallExpr and
// IndexExpr type as Unknown for now -- see type_checker.hpp). It never
// itself triggers a mismatch diagnostic, so one bad or not-yet-checkable
// subexpression doesn't cascade into a wall of downstream errors, the same
// "error, don't cascade" call the parser makes returning nullptr.
//
// Void is likewise never a value's type -- only a function's declared (or
// absent) return type, used to check `return;` vs `return expr;` against
// it.
enum class TypeKind { Int, Float, Bool, String, Struct, Void, Unknown };

struct Type {
    TypeKind kind;
    std::string struct_name;  // meaningful only when kind == TypeKind::Struct

    static Type primitive(TypeKind kind);
    static Type make_struct(std::string name);
    static Type unknown();

    bool is_numeric() const;
    bool operator==(const Type& other) const;
    bool operator!=(const Type& other) const { return !(*this == other); }
};

// Renders a Type the way diagnostics quote it, e.g. "int", "Point". The
// backtick wrapping (`` `int` ``) happens at the call site, since not every
// message wraps every embedded type the same way.
std::string to_string(const Type& type);

}  // namespace vex
