// Copyright (c) d6000-dock-recovery contributors.
// SPDX-License-Identifier: Apache-2.0
//
// The background watcher (--watch). Every 250 ms it reads each watched port's cached registry
// state, logs attaches, and restarts a port's controller when RecoveryPolicy reports a sustained
// attach loop and every guard passes. See docs/how-it-works.md.
#ifndef D6000_WATCHER_HPP
#define D6000_WATCHER_HPP

#include "cli.hpp"

namespace d6000
{

// Runs until SIGTERM/SIGINT or options.durationSeconds. Returns the process exit code.
[[nodiscard]] int runWatch(const Options &options);

} // namespace d6000

#endif // D6000_WATCHER_HPP
