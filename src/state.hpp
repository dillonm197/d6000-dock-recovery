// Copyright (c) d6000-dock-recovery contributors.
// SPDX-License-Identifier: Apache-2.0
//
// The persisted rate-limit state, so restarting the watcher (reboot, reinstall, crash) can't
// reset the cooldown, hourly cap or failure pause.
//
// File format, one record per line:
//   failures <port> <n>           consecutive failed restarts
//   reset <port> <unix seconds>   a restart in the last hour
//   last <port> <unix seconds>    the most recent restart, however old (times the failure pause)
// Reading stops at the first malformed line; records for other port names are ignored.
#ifndef D6000_STATE_HPP
#define D6000_STATE_HPP

#include <istream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace d6000
{

struct SavedPort
{
    unsigned consecutiveFailures = 0;
    std::vector<double> resetTimes;  // Unix seconds, in file order
    std::optional<double> lastReset; // Unix seconds
};

using SavedState = std::map<std::string, SavedPort>;

// Parses the state format. Restarts older than an hour before unixNow are dropped from resetTimes;
// lastReset is kept at any age.
[[nodiscard]] SavedState parseState(std::istream &input, double unixNow);

// Formats the state, ports in name order.
[[nodiscard]] std::string formatState(const SavedState &state);

// Reads the state file; a missing or unreadable file is an empty state.
[[nodiscard]] SavedState readStateFile(const char *path, double unixNow);

// Writes the file atomically and durably (temporary file, fsync, rename). On any error the
// previous file is kept and false is returned.
bool writeStateFile(const char *path, std::string_view contents);

} // namespace d6000

#endif // D6000_STATE_HPP
