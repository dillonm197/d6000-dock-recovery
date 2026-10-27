// Copyright (c) d6000-dock-recovery contributors.
// SPDX-License-Identifier: Apache-2.0
//
// Logging facade over spdlog, so only log.cpp depends on it. Lines read
// "<UTC ISO-8601 time> <level> <message>" and are flushed as soon as they are written.
#ifndef D6000_LOG_HPP
#define D6000_LOG_HPP

#include <cstdint>
#include <string>
#include <string_view>

namespace d6000
{

enum class LogLevel : std::uint8_t
{
    Error,   // the command failed or can't run
    Warning, // a restart was refused or didn't work, or a limit was reached
    Info,    // the default: every attach, enumeration, restart and outcome
    Debug,   // adds guard changes and state-file activity
};

// Sends log lines at or above level to stdout, or, when path is not empty, to that file. The file
// rotates at 5 MB: the previous contents move to <name>.1.<ext> (events.1.log), replacing any older
// copy. Throws std::exception when the file can't be opened.
void configureLogging(LogLevel level, const std::string &path);

void logError(std::string_view text);
void logWarning(std::string_view text);
void logInfo(std::string_view text);
void logDebug(std::string_view text);

// A number with two decimals, as used for attach timings ("0.85").
[[nodiscard]] std::string fixed2(double value);

} // namespace d6000

#endif // D6000_LOG_HPP
