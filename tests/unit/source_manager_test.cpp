// Unit tests for SourceManager, focused on the edge cases that actually
// crash or misrender a diagnostic caret: offset at EOF, missing trailing
// newline, an empty file, and a tab-indented line.
//
// No test framework -- the project avoids libraries beyond the standard
// library, so this is a plain main() with a hand-rolled check_eq(). Run
// directly after building:
//
//     ./build/vex_unit_tests
#include "vex/source_manager.hpp"

#include <iostream>
#include <string>

namespace {

int g_failures = 0;

void check_eq(std::uint32_t actual, std::uint32_t expected, const std::string& what) {
    if (actual != expected) {
        std::cerr << "FAIL: " << what << " -- expected " << expected << ", got " << actual << "\n";
        ++g_failures;
    }
}

void check_eq(const std::string& actual, const std::string& expected, const std::string& what) {
    if (actual != expected) {
        std::cerr << "FAIL: " << what << " -- expected \"" << expected << "\", got \"" << actual << "\"\n";
        ++g_failures;
    }
}

// The "expected `}`" case: a diagnostic reported at the position one past
// the last byte of the source.
void test_offset_at_eof() {
    vex::SourceManager sm("t", "abc\ndef");
    vex::LineCol lc = sm.line_col(sm.size());
    check_eq(lc.line, 1u, "offset==size(): line");
    check_eq(lc.column, 3u, "offset==size(): column");
}

void test_no_trailing_newline() {
    vex::SourceManager sm("t", "abc");
    check_eq(sm.line_count(), 1u, "no trailing newline: line_count");
    check_eq(std::string(sm.line_text(0)), std::string("abc"), "no trailing newline: line_text");

    vex::LineCol lc = sm.line_col(3);  // one past 'c'
    check_eq(lc.line, 0u, "no trailing newline: eof line");
    check_eq(lc.column, 3u, "no trailing newline: eof column");
}

void test_empty_file() {
    vex::SourceManager sm("t", "");
    check_eq(sm.size(), 0u, "empty file: size");
    check_eq(sm.line_count(), 1u, "empty file: line_count");

    vex::LineCol lc = sm.line_col(0);
    check_eq(lc.line, 0u, "empty file: line");
    check_eq(lc.column, 0u, "empty file: column");
    check_eq(std::string(sm.line_text(0)), std::string(""), "empty file: line_text");
}

// Tabs are the one case where offset and display column diverge (ADR 0001).
// SourceManager must report the raw byte column; expanding it is the
// renderer's job, not this one's.
void test_tab_indented_line() {
    vex::SourceManager sm("t", "\tfoo = 1;\n\tbadtoken");
    vex::LineCol lc = sm.line_col(11);  // 'b' of badtoken, line 1
    check_eq(lc.line, 1u, "tab-indented line: line");
    check_eq(lc.column, 1u, "tab-indented line: raw byte column, not expanded");
    check_eq(std::string(sm.line_text(1)), std::string("\tbadtoken"), "tab-indented line: line_text keeps the tab");
}

// Sanity check on a plain multi-line file, including that a trailing
// newline produces a trailing empty line rather than an off-by-one.
void test_multiline_basic() {
    vex::SourceManager sm("t", "line0\nline1\nline2\n");
    check_eq(sm.line_count(), 4u, "trailing newline creates a trailing empty line");
    check_eq(std::string(sm.line_text(1)), std::string("line1"), "line_text excludes the newline");

    vex::LineCol lc = sm.line_col(6);  // 'l' of line1
    check_eq(lc.line, 1u, "multiline: line");
    check_eq(lc.column, 0u, "multiline: column");
}

}  // namespace

int main() {
    test_offset_at_eof();
    test_no_trailing_newline();
    test_empty_file();
    test_tab_indented_line();
    test_multiline_basic();

    if (g_failures > 0) {
        std::cerr << g_failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "all SourceManager tests passed\n";
    return 0;
}
