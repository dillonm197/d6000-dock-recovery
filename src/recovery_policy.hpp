// Copyright (c) d6000-dock-recovery contributors.
// SPDX-License-Identifier: Apache-2.0
//
// The pure decision policy: no IOKit, privileges or hardware side effects, so the unit tests can
// cover it exhaustively. Times are seconds on a monotonic clock that keeps counting during sleep.
#ifndef D6000_RECOVERY_POLICY_HPP
#define D6000_RECOVERY_POLICY_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iterator>

namespace d6000
{

// Detection and rate limits. The defaults are the tuning validated on real docks. Each field can
// be changed on the command line within the bounds in cli.cpp (see docs/configuration.md).
struct Tuning
{
    // A port is looping when its ConnectionCount rises at least minAttaches times inside a fully
    // observed window with no dock enumerating. A healthy plug-in attaches once and a fumbled
    // replug twice, so the default of 3 attaches never fires on either.
    double windowSeconds = 2.0;
    std::uint64_t minAttaches = 3;
    // How long to wait for the D6000 to enumerate after a restart before counting it as failed.
    double verifySeconds = 10.0;
    // Minimum time between restarts, per port and machine-wide.
    double cooldownSeconds = 30.0;
    // Restarts allowed in any hour, per port and machine-wide.
    std::size_t maxResetsPerHour = 6;
    // How long a port is left alone after kFailuresBeforePause restarts in a row fail.
    double failurePauseSeconds = 1800.0;
};

inline constexpr double kHourSeconds = 3600.0;
// A gap between polls longer than this (sleep, stall) is not sustained observation of a failure.
inline constexpr double kMaxPollGapSeconds = 2.5;
// Consecutive restarts that don't bring the dock back before the port is paused.
inline constexpr unsigned kFailuresBeforePause = 2;
// "Never happened" for times on the monotonic clock.
inline constexpr double kNever = -1e18;

// One port's state for "is this port in a sustained attach loop, and may it be restarted now?".
// observe() is fed every poll with the port's ConnectionCount, whether every safety guard passes
// and whether the dock is present. It returns true once the count has risen by minAttaches within a
// fully observed window, outside the cooldown and caps. A gap longer than kMaxPollGapSeconds, a
// count going backwards, the dock appearing or a guard failing restarts the observation.
struct RecoveryPolicy
{
    struct Observation
    {
        double at;
        std::uint64_t count;
    };

    explicit RecoveryPolicy(const Tuning &settings = {}) : tuning(settings)
    {
    }

    [[nodiscard]] bool observe(double now, std::uint64_t count, bool eligible, bool dockPresent)
    {
        if (dockPresent)
        {
            consecutiveFailures = 0;
        }
        if (!eligible || dockPresent)
        {
            samples.clear();
            return false;
        }
        if (!samples.empty() &&
            (count < samples.back().count || now < samples.back().at || now - samples.back().at > kMaxPollGapSeconds))
        {
            samples.clear();
        }
        samples.push_back({.at = now, .count = count});
        const double windowStart = now - tuning.windowSeconds;
        while (samples.size() > 1 && std::next(samples.begin())->at <= windowStart)
        {
            samples.pop_front();
        }
        if (samples.size() < 2 || samples.front().at > windowStart)
        {
            return false;
        }
        // The oldest sample proves the whole window was observed, but an attach counted from it may
        // have happened before the window began. Only rises seen between polls inside the window
        // are certain to fall in it, so count from the first sample at or after its start.
        const Observation &baseline = *std::ranges::find_if(
            samples, [windowStart](const Observation &sample) { return sample.at >= windowStart; });
        return count >= baseline.count && count - baseline.count >= tuning.minAttaches && now >= nextAllowed &&
               !capped(now);
    }

    // Records a restart sent now: starts the cooldown and counts a failure until recovered().
    void attempted(double now)
    {
        resets.push_back(now);
        lastAttempt = now;
        nextAllowed = now + tuning.cooldownSeconds;
        samples.clear();
        ++consecutiveFailures;
    }

    void recovered()
    {
        consecutiveFailures = 0;
        samples.clear();
    }

    // Start-up, wake and stalls get a settling period before any restart.
    void holdOff(double until)
    {
        nextAllowed = std::max(nextAllowed, until);
        samples.clear();
    }

    // Reloads a restart persisted by an earlier process, already mapped onto this clock.
    void restoreAttempt(double at)
    {
        resets.push_back(at);
        lastAttempt = std::max(lastAttempt, at);
        nextAllowed = std::max(nextAllowed, at + tuning.cooldownSeconds);
    }

    // Reloads the time of the last restart, kept apart from the hourly history so that a failure
    // pause longer than an hour still ends at its original deadline after the watcher restarts.
    void restoreLastAttempt(double at)
    {
        lastAttempt = std::max(lastAttempt, at);
    }

    // True while the hourly cap or the failure pause applies. The pause is not permanent: it lifts
    // failurePauseSeconds after the last restart (or at once if none is on record), so a port that
    // only ever fails is retried later.
    [[nodiscard]] bool capped(double now)
    {
        while (!resets.empty() && resets.front() <= now - kHourSeconds)
        {
            resets.pop_front();
        }
        if (consecutiveFailures >= kFailuresBeforePause && now - lastAttempt >= tuning.failurePauseSeconds)
        {
            consecutiveFailures = 0;
        }
        return consecutiveFailures >= kFailuresBeforePause || resets.size() >= tuning.maxResetsPerHour;
    }

    Tuning tuning;
    std::deque<Observation> samples; // polls inside the current window
    std::deque<double> resets;       // restart times in the last hour
    double nextAllowed = 0;          // end of the cooldown or hold-off
    unsigned consecutiveFailures = 0;
    double lastAttempt = kNever;
};

// Machine-wide limits shared by every watched port: the same spacing and hourly cap as one port.
struct ResetBudget
{
    explicit ResetBudget(const Tuning &settings = {})
        : spacing(settings.cooldownSeconds), maxPerHour(settings.maxResetsPerHour)
    {
    }

    [[nodiscard]] bool allows(double now)
    {
        while (!resets.empty() && resets.front() <= now - kHourSeconds)
        {
            resets.pop_front();
        }
        return now >= nextAllowed && resets.size() < maxPerHour;
    }

    void spend(double now)
    {
        resets.push_back(now);
        nextAllowed = std::max(nextAllowed, now + spacing);
    }

    std::deque<double> resets;
    double nextAllowed = 0;
    double spacing;
    std::size_t maxPerHour;
};

} // namespace d6000

#endif // D6000_RECOVERY_POLICY_HPP
