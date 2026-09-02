#include "test_support.hpp"

#include <iostream>

namespace vex_test {

int g_failures = 0;

void check_eq(std::uint32_t actual, std::uint32_t expected, const std::string& what) {
    if (actual != expected) {
        std::cerr << "FAIL: " << what << " -- expected " << expected << ", got " << actual << "\n";
        ++g_failures;
    }
}

void check_eq(const std::string& actual, const std::string& expected, const std::string& what) {
    if (actual != expected) {
        std::cerr << "FAIL: " << what << "\n--- expected ---\n"
                   << expected << "--- got ---\n"
                   << actual << "----------------\n";
        ++g_failures;
    }
}

}  // namespace vex_test
