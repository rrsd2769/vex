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
file(GLOB_RECURSE VEX_SOURCES CONFIGURE_DEPENDS src/*.cpp)

add_executable(vex ${VEX_SOURCES})

target_include_directories(vex PRIVATE include)

target_compile_options(vex PRIVATE
  -Wall -Wextra -Wpedantic -Wshadow
  # ASan + UBSan in Debug. You are writing an arena allocator and a VM with a
  # tagged union -- these will catch use-after-free and bad reads long before
  # they turn into a confusing wrong answer.
  $<$<CONFIG:Debug>:-g -fsanitize=address,undefined -fno-omit-frame-pointer>
)

target_link_options(vex PRIVATE
  $<$<CONFIG:Debug>:-fsanitize=address,undefined>
)
