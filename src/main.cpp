// vex -- entry point.
//
// This is a stub. It exists so the build and the test harness work end to end
// from day one. Replace the marked section as you build each stage:
//
//     source -> lexer -> parser -> type checker -> bytecode -> VM
//
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: vex <file.vx>\n";
        return 64;  // EX_USAGE
    }

    std::ifstream in(argv[1], std::ios::binary);
    if (!in) {
        std::cerr << "error: cannot open '" << argv[1] << "'\n";
        return 66;  // EX_NOINPUT
    }

    std::ostringstream buf;
    buf << in.rdbuf();
    const std::string source = buf.str();

    // ---------------------------------------------------------------
    // TODO(week 1): the lexer, which will produce real Tokens with real
    // Spans in place of `source` -- SourceManager and the Diagnostic
    // renderer are both done. See include/vex/source_manager.hpp and
    // include/vex/diagnostic_renderer.hpp.
    // ---------------------------------------------------------------

    std::cout << "vex: read " << source.size() << " bytes from " << argv[1]
              << " (no compiler yet)\n";
    return 0;
}
