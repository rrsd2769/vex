// SourceManager -- owns Source text and maps Offsets to display positions.
//
// See CONTEXT.md for the Source/Offset/Span/Line/Column vocabulary and
// docs/adr/0001-source-positions-are-byte-offsets.md for why offsets (not
// line/column) are what every Token and syntax node will store.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace vex {

// A byte index into a Source. Zero-based.
using Offset = std::uint32_t;

// Display coordinates derived from an Offset. Zero-based; the diagnostic
// renderer is the only place that adds 1 for display.
struct LineCol {
    std::uint32_t line;
    std::uint32_t column;
};

// Owns the full text of one .vx file for the lifetime of a compilation, and
// converts byte Offsets to line/column on demand.
//
// Construction scans the text once to record where each line starts;
// line_col() then binary-searches that table instead of the lexer counting
// newlines as it goes.
class SourceManager {
public:
    SourceManager(std::string path, std::string text);

    const std::string& path() const { return path_; }
    std::string_view text() const { return text_; }
    Offset size() const { return static_cast<Offset>(text_.size()); }

    // offset must be <= size(). offset == size() is valid: it names the
    // position one past the last byte, used for diagnostics like
    // "expected `}`" reported at end of file.
    LineCol line_col(Offset offset) const;

    // The text of `line`, excluding its terminating newline if any. Does
    // not expand tabs -- the renderer does that when building the display
    // line (ADR 0001).
    std::string_view line_text(std::uint32_t line) const;

    std::uint32_t line_count() const {
        return static_cast<std::uint32_t>(line_starts_.size());
    }

private:
    std::string path_;
    std::string text_;

    // line_starts_[i] is the offset of the first byte of line i.
    // line_starts_[0] is always 0. A trailing newline in the source makes
    // the last entry the (empty) line after it, which is correct: an
    // offset right after that newline is on that line, at column 0.
    std::vector<Offset> line_starts_;
};

}  // namespace vex
