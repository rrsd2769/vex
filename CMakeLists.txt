cmake_minimum_required(VERSION 3.20)
project(vex LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Lets clangd / your editor find headers and flags. Keep this on.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# GLOB is normally discouraged, but CONFIGURE_DEPENDS makes CMake re-scan on
# build. For a solo project this means you never touch the build system while
# adding compiler stages -- worth the tradeoff here.
#
# Everything except main() goes into vex_core, so the vex binary and the
# vex_unit_tests binary below can both link it without pulling in two
# main()s.
file(GLOB_RECURSE VEX_LIB_SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(REMOVE_ITEM VEX_LIB_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

add_library(vex_core STATIC ${VEX_LIB_SOURCES})
target_include_directories(vex_core PUBLIC include)

# Shared by every target below.
function(vex_apply_compile_options target)
  target_compile_options(${target} PRIVATE
    -Wall -Wextra -Wpedantic -Wshadow
    # ASan + UBSan in Debug. You are writing an arena allocator and a VM with
    # a tagged union -- these will catch use-after-free and bad reads long
    # before they turn into a confusing wrong answer.
    $<$<CONFIG:Debug>:-g -fsanitize=address,undefined -fno-omit-frame-pointer>
  )
  target_link_options(${target} PRIVATE
    $<$<CONFIG:Debug>:-fsanitize=address,undefined>
  )
endfunction()

vex_apply_compile_options(vex_core)

add_executable(vex src/main.cpp)
target_link_libraries(vex PRIVATE vex_core)
vex_apply_compile_options(vex)

# Unit tests for individual stages that don't fit the golden-file harness
# (tests/run_tests.sh diffs a compiled .vx program's output; these are
# assertions against a class directly). Run directly:
#   ./build/vex_unit_tests
file(GLOB_RECURSE VEX_TEST_SOURCES CONFIGURE_DEPENDS tests/unit/*.cpp)
if(VEX_TEST_SOURCES)
  add_executable(vex_unit_tests ${VEX_TEST_SOURCES})
  target_link_libraries(vex_unit_tests PRIVATE vex_core)
  vex_apply_compile_options(vex_unit_tests)
endif()
