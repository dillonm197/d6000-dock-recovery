// Copyright (c) d6000-dock-recovery contributors.
// SPDX-License-Identifier: Apache-2.0
//
// Command-line parsing (CLI11), kept free of IOKit so every rule can be unit tested.
#ifndef D6000_CLI_HPP
#define D6000_CLI_HPP

#include "log.hpp"
#include "recovery_policy.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace d6000
{

enum class Action : std::uint8_t
{
    List,    // print every USB-C port and its state (read-only, any user)
    Check,   // preflight: confirm usable ports and validate the settings (read-only, any user)
    Recover, // restart one looping port's controller once (root)
    Watch,   // the background watcher (root, or any user with --dry-run)
};

struct Options
{
    Action action = Action::List;
    std::string portName; // "Port-USB-C@N"; empty with --all-ports
    bool allPorts = false;
    bool dryRun = false;
    bool requireDock = false;
    bool evidence = false;
    int durationSeconds = 0; // 0 runs until stopped
    Tuning tuning;
    LogLevel logLevel = LogLevel::Info;
    std::string logFile; // empty logs to stdout
};

// What to do after parsing: run with options, or print output and exit with exitCode. Help and
// --version exit 0 with their text on stdout; invalid arguments exit kExitUsage with an
// explanation for stderr.
struct ParseResult
{
    std::optional<Options> options;
    int exitCode = 0;
    std::string output;
    bool error = false;
};

// Parses a full argument vector, program name first.
[[nodiscard]] ParseResult parseArguments(std::span<const char *const> argv);

// The --help text, including each setting's range and default.
[[nodiscard]] std::string usage();

// The --version text.
[[nodiscard]] std::string versionText();

// One line describing the settings in effect, for the log.
[[nodiscard]] std::string describe(const Tuning &tuning);

} // namespace d6000

#endif // D6000_CLI_HPP
