// Entry point for vex_unit_tests. Each test file in this directory exposes
// one run_*_tests() function that runs its own asserts against the shared
// vex_test::g_failures counter; this just calls all of them and reports.
#include <iostream>

#include "test_support.hpp"

void run_source_manager_tests();
void run_diagnostic_renderer_tests();

int main() {
    run_source_manager_tests();
    run_diagnostic_renderer_tests();

    if (vex_test::g_failures > 0) {
        std::cerr << vex_test::g_failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "all unit tests passed\n";
    return 0;
}
