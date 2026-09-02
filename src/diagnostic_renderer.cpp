#include "vex/diagnostic_renderer.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace vex {

namespace {

constexpr std::uint32_t kTabWidth = 4;

// byte_to_display[i] is the display column of byte i, for i in
// [0, line.size()] -- the extra trailing entry is the column one past the
// last character, needed for a Span that ends at the end of a line. Source
// outside strings/comments is ASCII (ADR 0001), so every byte but tab is
// exactly one display column.
struct ExpandedLine {
    std::string text;
    std::vector<std::uint32_t> byte_to_display;
};

ExpandedLine expand_tabs(std::string_view line) {
    ExpandedLine result;
    result.byte_to_display.reserve(line.size() + 1);

    std::uint32_t column = 0;
    for (char c : line) {
        result.byte_to_display.push_back(column);
        if (c == '\t') {
            std::uint32_t advance = kTabWidth - (column % kTabWidth);
            result.text.append(advance, ' ');
            column += advance;
        } else {
            result.text.push_back(c);
            column += 1;
        }
    }
    result.byte_to_display.push_back(column);

    return result;
}

std::string severity_word(Severity severity) {
    return severity == Severity::Error ? "error" : "warning";
}

// Byte column of `offset`, clipped to the end of `line` if offset actually
// falls on a later line -- a Span crossing a newline is underlined only up
// to the end of its first line.
std::uint32_t clipped_column(const SourceManager& source, std::uint32_t line, Offset offset) {
    LineCol pos = source.line_col(offset);
    if (pos.line != line) {
        return static_cast<std::uint32_t>(source.line_text(line).size());
    }
    return pos.column;
}

void append_block(std::string& out, const SourceManager& source, const Span& span,
                   const std::string& message, char underline_char, std::size_t gutter_width) {
    LineCol start_pos = source.line_col(span.start);
    ExpandedLine expanded = expand_tabs(source.line_text(start_pos.line));

    std::uint32_t start_col = start_pos.column;
    std::uint32_t end_col = std::max(start_col, clipped_column(source, start_pos.line, span.end));

    std::uint32_t start_disp = expanded.byte_to_display[start_col];
    std::uint32_t end_disp = expanded.byte_to_display[end_col];
    std::uint32_t underline_len = std::max<std::uint32_t>(1, end_disp - start_disp);

    std::string line_no = std::to_string(start_pos.line + 1);
    out += std::string(gutter_width - line_no.size(), ' ');
    out += line_no;
    out += " | ";
    out += expanded.text;
    out += '\n';

    out += std::string(gutter_width, ' ');
    out += " | ";
    out += std::string(start_disp, ' ');
    out += std::string(underline_len, underline_char);
    out += ' ';
    out += message;
    out += '\n';
}

struct Block {
    Span span;
    const std::string* message;
    char underline_char;
};

}  // namespace

std::string render_diagnostic(const Diagnostic& diagnostic, const SourceManager& source) {
    std::string out;

    out += severity_word(diagnostic.severity);
    out += ": ";
    out += diagnostic.message;
    out += '\n';

    LineCol primary_pos = source.line_col(diagnostic.primary.span.start);
    out += "  --> ";
    out += source.path();
    out += ':';
    out += std::to_string(primary_pos.line + 1);
    out += ':';
    out += std::to_string(primary_pos.column + 1);
    out += '\n';

    std::vector<Block> blocks;
    blocks.push_back({diagnostic.primary.span, &diagnostic.primary.message, '^'});
    for (const Label& label : diagnostic.secondary) {
        blocks.push_back({label.span, &label.message, '-'});
    }
    std::sort(blocks.begin(), blocks.end(),
              [](const Block& a, const Block& b) { return a.span.start < b.span.start; });

    std::size_t max_line = primary_pos.line;
    for (const Block& block : blocks) {
        max_line = std::max<std::size_t>(max_line, source.line_col(block.span.start).line);
    }
    std::size_t gutter_width = std::to_string(max_line + 1).size();

    out += std::string(gutter_width, ' ');
    out += " |\n";

    for (const Block& block : blocks) {
        append_block(out, source, block.span, *block.message, block.underline_char, gutter_width);
    }

    if (diagnostic.suggestion) {
        out += '\n';
        out += "help: ";
        out += diagnostic.suggestion->message;
        out += '\n';
    }

    return out;
}

}  // namespace vex
