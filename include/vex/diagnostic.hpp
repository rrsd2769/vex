#pragma once

#include <optional>
#include <string>
#include <vector>

#include "vex/span.hpp"

namespace vex {

// There are exactly two Severities -- "note" is not one (CONTEXT.md).
enum class Severity { Error, Warning };

// A message attached to a Span. The primary Label marks where the problem
// is; secondary Labels supply supporting context, such as where a
// conflicting declaration appeared.
struct Label {
    Span span;
    std::string message;
};

// A proposed edit that would resolve a Diagnostic, expressed as replacement
// source. Rendering it as a diff is week 5's job; for now the renderer just
// prints its message.
struct Suggestion {
    std::string message;
    Span span;
    std::string replacement;
};

// One reported problem: a Severity, a message, a primary Label, zero or
// more secondary Labels, and an optional Suggestion.
struct Diagnostic {
    Severity severity;
    std::string message;
    Label primary;
    std::vector<Label> secondary;
    std::optional<Suggestion> suggestion;
};

}  // namespace vex
