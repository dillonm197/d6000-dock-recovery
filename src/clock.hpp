// Copyright (c) d6000-dock-recovery contributors.
// SPDX-License-Identifier: Apache-2.0
#ifndef D6000_CLOCK_HPP
#define D6000_CLOCK_HPP

#include <chrono>
#include <ctime>

namespace d6000
{

// Seconds on a monotonic clock that keeps advancing while the Mac sleeps, so a sleep shows up as a
// gap between polls instead of disappearing.
[[nodiscard]] inline double monotonicSeconds() noexcept
{
    constexpr double kNanosecondsPerSecond = 1e9;
    return static_cast<double>(clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW)) / kNanosecondsPerSecond;
}

// Wall-clock seconds since the Unix epoch. Used only to persist reset times across restarts.
[[nodiscard]] inline double unixSeconds() noexcept
{
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace d6000

#endif // D6000_CLOCK_HPP
