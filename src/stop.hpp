// Copyright (c) d6000-dock-recovery contributors.
// SPDX-License-Identifier: Apache-2.0
#ifndef D6000_STOP_HPP
#define D6000_STOP_HPP

namespace d6000
{

// Installs SIGTERM (launchd bootout) and SIGINT (Ctrl-C) handlers that request a clean stop.
void installStopHandlers() noexcept;

// True once a stop was requested. Long-running loops poll it and exit cleanly.
[[nodiscard]] bool stopRequested() noexcept;

} // namespace d6000

#endif // D6000_STOP_HPP
