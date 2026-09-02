// The symbol table -- week 4 of ROADMAP.md.
#pragma once

#include <string>
#include <unordered_map>

#include "vex/span.hpp"
#include "vex/type.hpp"

namespace vex {

// A named binding introduced by a declaration -- a variable or a parameter
// (CONTEXT.md's Symbol also covers functions and structs, but those are
// always top-level and never shadowed, so they're tracked separately by
// TypeChecker rather than through Scope -- see type_checker.hpp). decl_span
// is where it was declared, kept for week 5's secondary labels ("declared
// as `int` here").
struct Symbol {
    std::string name;
    Type type;
    bool is_mutable;
    Span decl_span;
};

// A region of the program over which a set of Symbols is visible
// (CONTEXT.md: Scope). Scopes form a parent chain, one per lexically
// nested block or function body -- chosen over a flat vector-of-scopes
// (ROADMAP.md flagged this as an open decision) because the checker itself
// is recursive descent, the same shape as the parser: entering a Block or
// function body pushes one Scope as a stack local and it pops on return,
// with no separate scope-index bookkeeping to get wrong.
class Scope {
public:
    explicit Scope(const Scope* parent = nullptr) : parent_(parent) {}

    // Returns false without declaring if `symbol.name` is already bound in
    // *this* scope (redeclaration). Shadowing a name from an outer scope is
    // always fine and always succeeds.
    bool declare(Symbol symbol);

    // Walks this scope, then its parent, then its parent's parent, ...
    // Returns nullptr if `name` isn't bound anywhere in the chain.
    const Symbol* resolve(const std::string& name) const;

private:
    std::unordered_map<std::string, Symbol> symbols_;
    const Scope* parent_;
};

}  // namespace vex
