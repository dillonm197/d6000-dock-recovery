// Copyright (c) d6000-dock-recovery contributors.
// SPDX-License-Identifier: Apache-2.0
#include "watcher.hpp"

#include "clock.hpp"
#include "constants.hpp"
#include "controller.hpp"
#include "log.hpp"
#include "ports.hpp"
#include "recovery_policy.hpp"
#include "state.hpp"
#include "stop.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace d6000
{
namespace
{

using namespace std::chrono_literals;

constexpr auto kPollInterval = 250ms;
// Start-up, wake and stalls get this long to settle before any restart.
constexpr double kSettleSeconds = 10;
// A pass that starts this long after the previous one means the Mac slept or the process stalled.
constexpr double kStallSeconds = 5;
// Ports are re-discovered every minute, so a port missing at boot is picked up later.
constexpr double kRediscoverSeconds = 60;
// A port whose structure is supported but stays blocked is re-explained at most this often.
constexpr double kReasonRelogSeconds = 600;
constexpr double kBudgetRelogSeconds = 600;
// A port logging more than kAttachLinesPerWindow attaches a minute is summarized instead.
constexpr double kAttachLogWindowSeconds = 60;
constexpr int kAttachLinesPerWindow = 10;

// Everything the watcher remembers about one port.
struct PortWatch
{
    explicit PortWatch(const Tuning &tuning) : policy(tuning)
    {
    }

    RecoveryPolicy policy;
    bool docked = false;
    bool suspended = false;
    std::string lastReason;
    double reasonLogged = kNever;
    double budgetLogged = kNever;
    // Attach log: last ConnectionCount seen, when it last rose, and a one-minute window that limits
    // how many attach lines one port can write.
    std::uint64_t lastCount = 0;
    bool haveCount = false;
    std::optional<double> lastAttach;
    double logWindowStart = kNever;
    int logWindowLines = 0;
    std::uint64_t suppressedAttaches = 0;
};

class Watcher
{
  public:
    explicit Watcher(Options options) : options_(std::move(options)), budget_(options_.tuning)
    {
    }

    int run()
    {
        installStopHandlers();
        const double started = monotonicSeconds();
        lastLoop_ = started;
        if (!options_.dryRun)
        {
            restoreState(started);
        }
        logInfo(std::format("{} started for {}", options_.dryRun ? "dry-run" : "automatic recovery",
                            options_.allPorts ? std::string("all USB-C ports") : options_.portName));
        logInfo(describe(options_.tuning));
        while (!stopRequested() &&
               (options_.durationSeconds == 0 || monotonicSeconds() - started < options_.durationSeconds))
        {
            const double now = monotonicSeconds();
            const bool stalled = now - lastLoop_ > kStallSeconds;
            if (stalled)
            {
                logInfo(std::format("sampling gap of {} s (sleep or stall); holding off {} s",
                                    static_cast<int>(now - lastLoop_), static_cast<int>(kSettleSeconds)));
            }
            const bool settle = firstPass_ || stalled;
            lastLoop_ = now;
            if (now - discovered_ >= kRediscoverSeconds)
            {
                discoverPorts(now);
            }
            for (const std::string &name : targets_)
            {
                pollPort(name, settle);
            }
            firstPass_ = false;
            std::this_thread::sleep_for(kPollInterval);
        }
        logInfo("watch stopped");
        return kExitOk;
    }

  private:
    PortWatch &watchFor(const std::string &name)
    {
        return watches_.try_emplace(name, options_.tuning).first->second;
    }

    void discoverPorts(double now)
    {
        std::vector<std::string> found = options_.allPorts ? usbCPorts() : std::vector<std::string>{options_.portName};
        if (found != targets_)
        {
            std::string list;
            for (const std::string &name : found)
            {
                list += " " + name;
            }
            logInfo("watching" + (list.empty() ? std::string(" no USB-C ports yet") : list));
        }
        targets_ = std::move(found);
        discovered_ = now;
    }

    void pollPort(const std::string &name, bool settle)
    {
        PortWatch &watch = watchFor(name);
        const Snapshot state = reader_.snapshot(name);
        const double now = monotonicSeconds();
        if (settle)
        {
            watch.policy.holdOff(now + kSettleSeconds);
            if (firstPass_)
            {
                watch.docked = state.dock;
            }
        }
        noteAttach(name, watch, state, now);
        flushAttachSummary(name, watch, now);
        if (state.dock != watch.docked)
        {
            const std::string after = state.dock && watch.lastAttach
                                          ? " " + fixed2(now - *watch.lastAttach) + " s after the last attach"
                                          : std::string();
            logInfo(name + (state.dock ? ": D6000 enumerated" : ": D6000 absent") + after);
            watch.docked = state.dock;
        }
        explainBlock(name, watch, state, now);
        const unsigned failuresBefore = watch.policy.consecutiveFailures;
        const bool act = watch.policy.observe(now, state.count, state.eligible(), state.dock);
        // The dock came back on its own: persist the cleared failure count right away.
        if (!options_.dryRun && failuresBefore != 0 && watch.policy.consecutiveFailures == 0)
        {
            persistState(now);
        }
        if (!act)
        {
            const bool capped = watch.policy.capped(now);
            if (capped && !watch.suspended)
            {
                logWarning(name + ": reset limit reached; further recovery suspended");
            }
            if (!capped && watch.suspended)
            {
                logInfo(name + ": reset limit lifted; recovery resumed");
            }
            watch.suspended = capped;
            return;
        }
        if (!budget_.allows(now))
        {
            if (now - watch.budgetLogged > kBudgetRelogSeconds)
            {
                logWarning(name + ": machine-wide reset budget exhausted; waiting");
                watch.budgetLogged = now;
            }
            return;
        }
        watch.policy.attempted(now);
        budget_.spend(now);
        if (options_.dryRun)
        {
            logInfo(name + ": would warm-restart sustained attachment loop; dry-run sends no commands");
            return;
        }
        persistState(now);
        restartPort(name, watch, state);
    }

    // Explains why a blocked port is ignored: a structure problem once, other reasons at most every
    // 10 minutes. An active data transport is the normal state of a working port and isn't logged.
    static void explainBlock(const std::string &name, PortWatch &watch, const Snapshot &state, double now)
    {
        const std::string reason = state.dock ? std::string() : state.reason();
        if (reason != watch.lastReason)
        {
            logDebug(std::format("{}: guards now {}", name, reason.empty() ? std::string("pass") : "block: " + reason));
        }
        if (!reason.empty() && reason != kDataTransportActive &&
            (reason != watch.lastReason || (state.isolated && now - watch.reasonLogged > kReasonRelogSeconds)))
        {
            const std::string text = name + ": recovery blocked: " + reason;
            if (state.isolated)
            {
                logInfo(text);
            }
            else
            {
                logWarning(text);
            }
            watch.reasonLogged = now;
        }
        watch.lastReason = reason;
    }

    void restartPort(const std::string &name, PortWatch &watch, const Snapshot &state)
    {
        const double resetStarted = monotonicSeconds();
        const IOReturn result = warmReset(reader_, name, state, false);
        const std::string returned =
            std::format("{}: warm-restart command return=0x{:08x}", name, static_cast<std::uint32_t>(result));
        if (result == kIOReturnSuccess)
        {
            logInfo(returned);
        }
        else
        {
            logWarning(returned);
        }
        if (result == kIOReturnSuccess &&
            verifyRecovery(reader_, name, state.appLoaded, resetStarted, options_.tuning.verifySeconds))
        {
            watch.policy.recovered();
            // verifyRecovery already reported the reconnect; resync so the next pass doesn't re-log
            // those attaches and the enumeration with timings skewed by the wait.
            const Snapshot after = reader_.snapshot(name);
            watch.lastCount = after.count;
            watch.haveCount = after.valid;
            watch.docked = after.dock;
            watch.lastAttach = monotonicSeconds();
        }
        else
        {
            logInfo(name + ": cooldown applies");
        }
        persistState(monotonicSeconds());
        // The verify wait is not a sleep or stall; don't let the next pass treat it as one.
        lastLoop_ = monotonicSeconds();
    }

    // Logs every ConnectionCount rise with the gap since the previous one and whether recovery was
    // eligible, so healthy plug-ins, near misses and false triggers can be read back from the log.
    static void noteAttach(const std::string &name, PortWatch &watch, const Snapshot &state, double now)
    {
        if (!state.valid)
        {
            return;
        }
        if (!watch.haveCount || state.count < watch.lastCount)
        {
            watch.lastCount = state.count;
            watch.haveCount = true;
            return;
        }
        if (state.count == watch.lastCount)
        {
            return;
        }
        const std::uint64_t added = state.count - watch.lastCount;
        watch.lastCount = state.count;
        const std::optional<double> gap =
            watch.lastAttach ? std::optional<double>(now - *watch.lastAttach) : std::nullopt;
        watch.lastAttach = now;
        flushAttachSummary(name, watch, now);
        if (watch.logWindowLines >= kAttachLinesPerWindow)
        {
            watch.suppressedAttaches += added;
            return;
        }
        ++watch.logWindowLines;
        const std::string extra = added > 1 ? std::format(" (+{})", added) : std::string();
        const std::string timing = gap ? fixed2(*gap) + " s after previous" : std::string("first since start");
        const std::string verdict = state.eligible() ? std::string("recovery eligible") : "blocked: " + state.reason();
        logInfo(std::format("{}: attach #{}{}, {}; dock={}, {}", name, state.count, extra, timing,
                            state.dock ? "yes" : "no", verdict));
    }

    // Ends the current one-minute attach-log window, reporting any attaches that were only counted.
    static void flushAttachSummary(const std::string &name, PortWatch &watch, double now)
    {
        if (now - watch.logWindowStart < kAttachLogWindowSeconds)
        {
            return;
        }
        if (watch.suppressedAttaches != 0)
        {
            logInfo(std::format("{}: {} more attaches in the last minute (not logged individually)", name,
                                watch.suppressedAttaches));
        }
        watch.suppressedAttaches = 0;
        watch.logWindowLines = 0;
        watch.logWindowStart = now;
    }

    // Wall-clock times from the state file are mapped onto the monotonic clock. A timestamp in the
    // future (the clock moved back) counts as "just now", which keeps the limit in force.
    void restoreState(double now)
    {
        const double unixNow = unixSeconds();
        for (const auto &[port, saved] : readStateFile(kStatePath, unixNow))
        {
            PortWatch &watch = watchFor(port);
            logDebug(std::format("restored {}: {} failed restarts in a row, {} restarts in the last hour", port,
                                 saved.consecutiveFailures, saved.resetTimes.size()));
            watch.policy.consecutiveFailures = saved.consecutiveFailures;
            for (const double unixAt : saved.resetTimes)
            {
                const double at = now - std::max(0.0, unixNow - unixAt);
                watch.policy.restoreAttempt(at);
                budget_.spend(at);
            }
        }
        std::ranges::sort(budget_.resets);
    }

    // Saved around every restart and whenever a port's failure count clears.
    void persistState(double now) const
    {
        const double unixNow = unixSeconds();
        SavedState saved;
        for (const auto &[port, watch] : watches_)
        {
            SavedPort &record = saved[port];
            record.consecutiveFailures = watch.policy.consecutiveFailures;
            for (const double at : watch.policy.resets)
            {
                record.resetTimes.push_back(unixNow - (now - at));
            }
        }
        if (writeStateFile(kStatePath, formatState(saved)))
        {
            logDebug(std::format("state saved for {} ports", saved.size()));
        }
        else
        {
            logWarning(
                std::format("could not save {}; rate limits restart from zero if the watcher restarts", kStatePath));
        }
    }

    Options options_;
    PortReader reader_;
    std::map<std::string, PortWatch> watches_;
    ResetBudget budget_;
    std::vector<std::string> targets_;
    double lastLoop_ = 0;
    double discovered_ = kNever;
    bool firstPass_ = true;
};

} // namespace

int runWatch(const Options &options)
{
    Watcher watcher(options);
    return watcher.run();
}

} // namespace d6000
