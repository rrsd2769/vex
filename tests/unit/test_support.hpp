// Minimal shared plumbing for the unit tests in this directory. No test
// framework -- the project avoids libraries beyond the standard library.
#pragma once

#include <cstdint>
#include <string>

namespace vex_test {

// Total check_eq() failures across every test suite that ran this process.
extern int g_failures;

void check_eq(std::uint32_t actual, std::uint32_t expected, const std::string& what);
void check_eq(const std::string& actual, const std::string& expected, const std::string& what);

}  // namespace vex_test
