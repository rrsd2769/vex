#pragma once

#include "vex/source_manager.hpp"

namespace vex {

// A half-open byte range [start, end) within a Source. Every Token and
// syntax node carries one. An empty Span has start == end.
struct Span {
    Offset start;
    Offset end;
};

}  // namespace vex
