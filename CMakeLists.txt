# Copyright (c) d6000-dock-recovery contributors.
# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.28...4.4)

project(
  d6000-recovery
  VERSION 0.7.0
  DESCRIPTION "Automatic recovery for Dell D6000 docks stuck in a USB-C attach loop on Apple-silicon Macs"
  HOMEPAGE_URL "https://github.com/dillonm197/d6000-dock-recovery"
  LANGUAGES CXX)

if(NOT APPLE)
  message(FATAL_ERROR "d6000-recovery builds only on macOS.")
endif()

option(D6000_BUILD_TESTS "Build the unit tests" ${PROJECT_IS_TOP_LEVEL})
option(D6000_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

set(CMAKE_CXX_EXTENSIONS OFF)

# Third-party libraries, pinned by checksum and compiled into the binary. SYSTEM keeps their
# headers out of this project's warnings and lint.
include(FetchContent)
FetchContent_Declare(
  cli11
  URL https://github.com/CLIUtils/CLI11/releases/download/v2.7.2/CLI11-2.7.2-Source.tar.gz
  URL_HASH SHA256=0cb0ef44c4c7129ea972505ee81e6d09c71b2e7fe59ca3a13c47d8a994d682b2
  SYSTEM)
FetchContent_Declare(
  spdlog
  URL https://github.com/gabime/spdlog/archive/refs/tags/v1.17.0.tar.gz
  URL_HASH SHA256=d8862955c6d74e5846b3f580b1605d2428b11d97a410d86e2fb13e857cd3a744
  SYSTEM)
set(SPDLOG_USE_STD_FORMAT ON CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(cli11 spdlog)

configure_file(src/version.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/version.hpp" @ONLY)

# Warnings for this project's code only (third-party code is included as a system header).
add_library(d6000_warnings INTERFACE)
target_compile_options(
  d6000_warnings
  INTERFACE -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wconversion
            -Wsign-conversion
            -Wold-style-cast
            -Wnon-virtual-dtor
            -Woverloaded-virtual
            -Wnull-dereference
            -Wdouble-promotion
            -Wformat=2
            -Wimplicit-fallthrough
            $<$<BOOL:${D6000_WARNINGS_AS_ERRORS}>:-Werror>)

# Hardening: stack protector, fortified libc calls and libc++ bounds checks.
add_library(d6000_hardening INTERFACE)
target_compile_options(d6000_hardening INTERFACE -fstack-protector-strong
                                                 $<$<NOT:$<CONFIG:Debug>>:-D_FORTIFY_SOURCE=2>)
target_compile_definitions(d6000_hardening INTERFACE _LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_FAST)

add_library(
  d6000_core STATIC
  src/cli.cpp
  src/controller.cpp
  src/iokit.cpp
  src/log.cpp
  src/ports.cpp
  src/state.cpp
  src/stop.cpp
  src/watcher.cpp)
target_compile_features(d6000_core PUBLIC cxx_std_20)
target_include_directories(d6000_core PUBLIC src "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_include_directories(d6000_core SYSTEM PRIVATE src/third_party)
target_link_libraries(
  d6000_core
  PUBLIC "$<LINK_LIBRARY:FRAMEWORK,IOKit>" "$<LINK_LIBRARY:FRAMEWORK,CoreFoundation>"
  PRIVATE CLI11::CLI11 spdlog::spdlog d6000_warnings d6000_hardening)

add_executable(d6000-recovery src/main.cpp)
target_link_libraries(d6000-recovery PRIVATE d6000_core d6000_warnings d6000_hardening)

if(D6000_BUILD_TESTS)
  enable_testing()
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/releases/download/v1.18.0/googletest-1.18.0.tar.gz
    URL_HASH SHA256=6e3191c1455468b3fc35a417fb565c1c5071aee1b7e7f85e30cf48a98d37d8b5
    SYSTEM)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  add_executable(d6000_tests tests/cli_test.cpp tests/recovery_policy_test.cpp tests/state_test.cpp)
  target_link_libraries(d6000_tests PRIVATE d6000_core GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(d6000_tests)

  # The built command must reject invalid flag combinations with exit code 2 before any live action.
  set(invalid_arguments
      "--recover --dry-run --port 3"
      "--watch --evidence --port 3"
      "--check --evidence --port 3"
      "--watch --all-ports --port 3"
      "--list --all-ports"
      "--watch"
      "--recover --all-ports --dry-run"
      "--list --cooldown 60"
      "--watch --all-ports --cooldown 5"
      "--watch --all-ports --log-level verbose")
  foreach(arguments IN LISTS invalid_arguments)
    string(MAKE_C_IDENTIFIER "cli_rejects_${arguments}" test_name)
    separate_arguments(argument_list UNIX_COMMAND "${arguments}")
    add_test(NAME ${test_name} COMMAND sh -c "\"$0\" \"$@\" >/dev/null; test $? -eq 2" $<TARGET_FILE:d6000-recovery>
                                       ${argument_list})
  endforeach()
  add_test(NAME cli_version COMMAND d6000-recovery --version)
  add_test(NAME cli_list COMMAND d6000-recovery --list)
endif()
