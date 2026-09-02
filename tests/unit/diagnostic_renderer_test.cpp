// Hand-constructed fake errors, proving the Diagnostic renderer before any
// lexer exists to supply real ones (ROADMAP.md week 1, step 2). Exercises
// carets, a secondary label, a suggestion, tab expansion, and the offset ==
// size() EOF case.
#include "vex/diagnostic.hpp"
#include "vex/diagnostic_renderer.hpp"
#include "vex/source_manager.hpp"

#include <string>

#include "test_support.hpp"

using vex_test::check_eq;

namespace {

void test_primary_only() {
    vex::SourceManager sm("fake.vx", "let x = 1 + @;\n");
    vex::Diagnostic diag{
        vex::Severity::Error,
        "unexpected character `@`",
        vex::Label{vex::Span{12, 13}, "unexpected character"},
        {},
        std::nullopt,
    };

    std::string expected =
        "error: unexpected character `@`\n"
        "  --> fake.vx:1:13\n"
        "  |\n"
        "1 | let x = 1 + @;\n"
        "  |             ^ unexpected character\n";

    check_eq(vex::render_diagnostic(diag, sm), expected, "primary-only caret lands under `@`");
}

void test_primary_and_secondary_and_suggestion() {
    vex::SourceManager sm("fake.vx", "let count: int = 0;\ncount = \"hello\";\n");
    vex::Diagnostic diag{
        vex::Severity::Error,
        "cannot assign value of type `string` to variable of type `int`",
        vex::Label{vex::Span{28, 35}, "this is a `string`"},
        {vex::Label{vex::Span{11, 14}, "declared as `int` here"}},
        vex::Suggestion{"did you mean to declare a new variable?", vex::Span{0, 0}, ""},
    };

    std::string expected =
        "error: cannot assign value of type `string` to variable of type `int`\n"
        "  --> fake.vx:2:9\n"
        "  |\n"
        "1 | let count: int = 0;\n"
        "  |            --- declared as `int` here\n"
        "2 | count = \"hello\";\n"
        "  |         ^^^^^^^ this is a `string`\n"
        "\n"
        "help: did you mean to declare a new variable?\n";

    check_eq(vex::render_diagnostic(diag, sm), expected,
             "secondary label renders earlier in source, before the primary block");
}

void test_tab_indented_line() {
    vex::SourceManager sm("fake.vx", "fn main() {\n\treturn @;\n}\n");
    vex::Diagnostic diag{
        vex::Severity::Error,
        "unexpected character `@`",
        vex::Label{vex::Span{20, 21}, "unexpected character"},
        {},
        std::nullopt,
    };

    std::string expected =
        "error: unexpected character `@`\n"
        "  --> fake.vx:2:9\n"
        "  |\n"
        "2 |     return @;\n"
        "  |            ^ unexpected character\n";

    check_eq(vex::render_diagnostic(diag, sm), expected,
             "tab expands to spaces and the caret column shifts to match (ADR 0001)");
}

void test_eof_span() {
    vex::SourceManager sm("fake.vx", "fn main() {\n");
    vex::Diagnostic diag{
        vex::Severity::Error,
        "expected `}`",
        vex::Label{vex::Span{sm.size(), sm.size()}, "expected `}` here"},
        {},
        std::nullopt,
    };

    std::string expected =
        "error: expected `}`\n"
        "  --> fake.vx:2:1\n"
        "  |\n"
        "2 | \n"
        "  | ^ expected `}` here\n";

    check_eq(vex::render_diagnostic(diag, sm), expected,
             "an empty span at offset == size() still renders a single caret");
}

}  // namespace

void run_diagnostic_renderer_tests() {
    test_primary_only();
    test_primary_and_secondary_and_suggestion();
    test_tab_indented_line();
    test_eof_span();
}
