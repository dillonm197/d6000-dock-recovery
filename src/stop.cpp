// Copyright (c) d6000-dock-recovery contributors.
// SPDX-License-Identifier: Apache-2.0
#include "stop.hpp"

#include <atomic>
#include <csignal>

namespace d6000
{
namespace
{

// Lock-free, so a signal handler may set it.
static_assert(std::atomic<bool>::is_always_lock_free);
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables): written by the signal handler
std::atomic<bool> stopFlag{false};

void onStopSignal(int /*signal*/) noexcept
{
    stopFlag.store(true);
}

} // namespace

void installStopHandlers() noexcept
{
    static_cast<void>(std::signal(SIGTERM, onStopSignal));
    static_cast<void>(std::signal(SIGINT, onStopSignal));
}

bool stopRequested() noexcept
{
    return stopFlag.load();
}

} // namespace d6000
