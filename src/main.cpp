// vex -- entry point.
//
// Replace the marked section as you build each stage:
//
//     source -> lexer -> parser -> type checker -> bytecode -> VM
//
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "vex/bytecode.hpp"
#include "vex/bytecode_compiler.hpp"
#include "vex/diagnostic_renderer.hpp"
#include "vex/lexer.hpp"
#include "vex/parser.hpp"
#include "vex/source_manager.hpp"
#include "vex/token.hpp"
#include "vex/type_checker.hpp"
#include "vex/vm.hpp"

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
    vex::SourceManager source(argv[1], buf.str());

    vex::Lexer lexer(source);
    std::vector<vex::Token> tokens = lexer.tokenize();

    if (!lexer.diagnostics().empty()) {
        for (const vex::Diagnostic& diagnostic : lexer.diagnostics()) {
            std::cerr << vex::render_diagnostic(diagnostic, source);
        }
        return 65;  // EX_DATAERR
    }

    vex::Parser parser(std::move(tokens), source);
    vex::Program program = parser.parse_program();

    if (!parser.diagnostics().empty()) {
        for (const vex::Diagnostic& diagnostic : parser.diagnostics()) {
            std::cerr << vex::render_diagnostic(diagnostic, source);
        }
        return 65;  // EX_DATAERR
    }

    vex::TypeChecker checker(program);
    checker.check();

    if (!checker.diagnostics().empty()) {
        for (const vex::Diagnostic& diagnostic : checker.diagnostics()) {
            std::cerr << vex::render_diagnostic(diagnostic, source);
        }
        return 65;  // EX_DATAERR
    }

    vex::BytecodeCompiler compiler(program, checker);
    vex::BytecodeProgram bytecode = compiler.compile();

    vex::VM vm(bytecode);
    try {
        vm.run(std::cout);
    } catch (const vex::VMError& e) {
        std::cerr << vex::render_diagnostic(e.diagnostic(), source);
        return 70;  // EX_SOFTWARE -- the program compiled clean but failed at runtime
    }

    return 0;
}
