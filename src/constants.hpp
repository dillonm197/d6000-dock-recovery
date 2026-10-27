// Copyright (c) d6000-dock-recovery contributors.
// SPDX-License-Identifier: Apache-2.0
//
// Names and paths shared by the helper, the installer and the documentation.
#ifndef D6000_CONSTANTS_HPP
#define D6000_CONSTANTS_HPP

#include <string_view>

namespace d6000
{

// The OS's names for built-in USB-C ports start with this, e.g. "Port-USB-C@1". The number is the
// OS's port index, not a left-to-right label; --list shows which is which.
inline constexpr std::string_view kUsbCPortPrefix = "Port-USB-C@";

// Where the LaunchDaemon writes its log (stdout and stderr) and the persisted rate-limit state.
inline constexpr const char *kLogPath = "/Library/Logs/D6000Recovery/events.log";
inline constexpr const char *kStatePath = "/Library/Logs/D6000Recovery/state";

// Process exit codes. A permanent refusal in --watch exits 0 so launchd does not relaunch it.
inline constexpr int kExitOk = 0;
inline constexpr int kExitFailed = 1;
inline constexpr int kExitUsage = 2;

} // namespace d6000

#endif // D6000_CONSTANTS_HPP
