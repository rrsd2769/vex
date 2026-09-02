#include "vex/source_manager.hpp"

#include <algorithm>
#include <cassert>

namespace vex {

SourceManager::SourceManager(std::string path, std::string text)
    : path_(std::move(path)), text_(std::move(text)) {
    line_starts_.push_back(0);
    for (Offset i = 0; i < text_.size(); ++i) {
        if (text_[i] == '\n') {
            line_starts_.push_back(i + 1);
        }
    }
}

LineCol SourceManager::line_col(Offset offset) const {
    assert(offset <= size());

    // Binary search for the largest line_starts_[i] <= offset: that i is
    // the line containing offset, and the remainder is the column.
    auto it = std::upper_bound(line_starts_.begin(), line_starts_.end(), offset);
    std::size_t line = static_cast<std::size_t>(it - line_starts_.begin()) - 1;

    return LineCol{static_cast<std::uint32_t>(line), offset - line_starts_[line]};
}

std::string_view SourceManager::line_text(std::uint32_t line) const {
    assert(line < line_starts_.size());

    Offset start = line_starts_[line];
    Offset end = (line + 1 < line_starts_.size())
                     ? line_starts_[line + 1] - 1  // exclude the '\n'
                     : size();

    return std::string_view(text_).substr(start, end - start);
}

}  // namespace vex
