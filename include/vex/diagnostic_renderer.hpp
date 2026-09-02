#pragma once

#include <string>

#include "vex/diagnostic.hpp"
#include "vex/source_manager.hpp"

namespace vex {

// Renders a Diagnostic against the Source it refers to: a header line, then
// for the primary Label and each secondary Label, the source line it
// points at (tabs expanded per ADR 0001) with a caret/underline beneath it.
// Primary underlines with '^', secondary with '-'.
//
// Non-goal for now: merging Labels that land on the same source line into
// one combined underline row, the way the "declared as `int` here" example
// in ROADMAP.md does. Each Label gets its own block instead -- correct,
// just not that compact. That coalescing is week 5's diagnostics-polish
// problem, not week 1's.
std::string render_diagnostic(const Diagnostic& diagnostic, const SourceManager& source);

}  // namespace vex
