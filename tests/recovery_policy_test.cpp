// Copyright (c) d6000-dock-recovery contributors.
// SPDX-License-Identifier: Apache-2.0
//
// RecoveryPolicy and ResetBudget. Counts and times are synthetic: "count" is the port's
// ConnectionCount and every argument after it says whether all guards pass and whether the dock is
// present. Most cases feed the worst case for false positives: the dock never appears and every
// guard passes.
#include "recovery_policy.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace d6000
{
namespace
{

// A slow, conservative tuning used to test the policy's mechanics independently of the defaults.
const Tuning kConservative{.windowSeconds = 8, .minAttaches = 4, .verifySeconds = 10, .cooldownSeconds = 60};

std::uint64_t at(int base, int offset)
{
    return static_cast<std::uint64_t>(base + offset);
}

// Polls every 250 ms from 0 to `until` with attaches at the given times, all guards passing and no
// dock. Returns the first time the policy asks for a restart, or -1.
double firstTrigger(RecoveryPolicy policy, const std::vector<double> &attaches, double until)
{
    for (int poll = 0; poll * 0.25 <= until; ++poll)
    {
        const double now = poll * 0.25;
        std::uint64_t count = 50;
        for (const double attach : attaches)
        {
            if (attach <= now)
            {
                ++count;
            }
        }
        if (policy.observe(now, count, true, false))
        {
            return now;
        }
    }
    return -1;
}

TEST(RecoveryPolicy, IdlePortNeverRestarts)
{
    RecoveryPolicy idle{kConservative};
    for (int t = 0; t < 120; ++t)
    {
        EXPECT_FALSE(idle.observe(t, 77, true, false)) << "t=" << t;
    }
}

TEST(RecoveryPolicy, BlockedOrDockedPortNeverRestartsEvenWhileAttaching)
{
    RecoveryPolicy blocked{kConservative};
    RecoveryPolicy healthy{kConservative};
    for (int t = 0; t < 120; ++t)
    {
        EXPECT_FALSE(blocked.observe(t, at(0, t), false, false)) << "t=" << t;
        EXPECT_FALSE(healthy.observe(t, at(0, t), true, true)) << "t=" << t;
    }
}

TEST(RecoveryPolicy, NeedsAFullWindowThenRespectsCooldownAndFailurePause)
{
    RecoveryPolicy flap{kConservative};
    for (int t = 0; t < 8; ++t)
    {
        ASSERT_FALSE(flap.observe(t, at(100, t), true, false)) << "t=" << t;
    }
    ASSERT_TRUE(flap.observe(8, 109, true, false));
    flap.attempted(8);
    for (int t = 9; t < 68; ++t)
    {
        ASSERT_FALSE(flap.observe(t, at(109, t), true, false)) << "cooldown, t=" << t;
    }
    ASSERT_TRUE(flap.observe(68, 200, true, false));
    flap.attempted(68);
    // Two failed restarts pause the port until the dock is seen working.
    for (int t = 69; t < 180; ++t)
    {
        ASSERT_FALSE(flap.observe(t, at(200, t), true, false)) << "paused, t=" << t;
    }
    ASSERT_FALSE(flap.observe(180, 500, false, true));
    for (int t = 181; t < 189; ++t)
    {
        ASSERT_FALSE(flap.observe(t, at(500, t), true, false)) << "t=" << t;
    }
    EXPECT_TRUE(flap.observe(189, 800, true, false));
}

TEST(RecoveryPolicy, CounterResetIsNotAHugeDelta)
{
    RecoveryPolicy wrap{kConservative};
    for (int t = 0; t < 8; ++t)
    {
        ASSERT_FALSE(wrap.observe(t, 1000, true, false));
    }
    EXPECT_FALSE(wrap.observe(8, 1, true, false));
}

TEST(RecoveryPolicy, SleepIsNotObservedTime)
{
    RecoveryPolicy sleeping{kConservative};
    ASSERT_FALSE(sleeping.observe(0, 100, true, false));
    ASSERT_FALSE(sleeping.observe(1000, 900, true, false));
    for (int t = 1001; t < 1008; ++t)
    {
        ASSERT_FALSE(sleeping.observe(t, at(900, t), true, false)) << "t=" << t;
    }
    EXPECT_TRUE(sleeping.observe(1008, 2000, true, false));
}

TEST(RecoveryPolicy, HourlyCapHoldsAcrossSuccessfulRecoveries)
{
    RecoveryPolicy limited{kConservative};
    for (int n = 0; n < 6; ++n)
    {
        limited.attempted(n * 70);
        limited.recovered();
    }
    for (int t = 500; t < 700; ++t)
    {
        ASSERT_FALSE(limited.observe(t, at(0, t), true, false)) << "t=" << t;
    }
    ASSERT_FALSE(limited.observe(3601, 4000, true, false));
    for (int t = 3602; t < 3609; ++t)
    {
        ASSERT_FALSE(limited.observe(t, at(4000, t), true, false)) << "t=" << t;
    }
    EXPECT_TRUE(limited.observe(3609, 8000, true, false));
}

TEST(RecoveryPolicy, HourlyCapLiftsWithoutANewObservation)
{
    RecoveryPolicy aging{kConservative};
    for (int n = 0; n < 6; ++n)
    {
        aging.attempted(n * 70);
        aging.recovered();
    }
    EXPECT_TRUE(aging.capped(3000));
    EXPECT_FALSE(aging.capped(3951));
}

TEST(RecoveryPolicy, HoldOffBlocksRestarts)
{
    RecoveryPolicy woke{kConservative};
    woke.holdOff(45);
    for (int t = 0; t < 45; ++t)
    {
        ASSERT_FALSE(woke.observe(t, at(100, t), true, false)) << "t=" << t;
    }
    EXPECT_TRUE(woke.observe(45, 200, true, false));
}

TEST(RecoveryPolicy, RestoredHistoryKeepsCapAndCooldown)
{
    RecoveryPolicy restarted{kConservative};
    for (int n = 0; n < 6; ++n)
    {
        restarted.restoreAttempt(-600 + n * 70);
    }
    for (int t = 0; t < 200; ++t)
    {
        ASSERT_FALSE(restarted.observe(t, at(0, t), true, false)) << "t=" << t;
    }
    RecoveryPolicy cooling{kConservative};
    cooling.restoreAttempt(-10);
    for (int t = 0; t < 50; ++t)
    {
        ASSERT_FALSE(cooling.observe(t, at(100, t), true, false)) << "t=" << t;
    }
    EXPECT_TRUE(cooling.observe(50, 200, true, false));
}

TEST(RecoveryPolicy, RecentFailurePauseBlocks)
{
    RecoveryPolicy stopped{kConservative};
    stopped.consecutiveFailures = 2;
    stopped.lastAttempt = 0;
    for (int t = 0; t < 120; ++t)
    {
        ASSERT_FALSE(stopped.observe(t, at(0, t), true, false)) << "t=" << t;
    }
}

TEST(RecoveryPolicy, DefaultsCatchAStuckLoopWithinTwoSecondsInEveryPhase)
{
    for (int phase = 0; phase < 20; ++phase)
    {
        const double first = 3.0 + phase * 0.0425;
        std::vector<double> loop;
        for (int n = 0; n < 12; ++n)
        {
            loop.push_back(first + n * 0.85);
        }
        const double hit = firstTrigger(RecoveryPolicy{}, loop, 20);
        EXPECT_GE(hit, first + 1.70) << "phase " << phase;
        EXPECT_LE(hit, first + 2.25) << "phase " << phase;
    }
}

TEST(RecoveryPolicy, DefaultsIgnoreAFumbledReplug)
{
    for (int phase = 0; phase < 20; ++phase)
    {
        for (int step = 1; step <= 7; ++step)
        {
            const double first = 3.0 + phase * 0.0125;
            const double gap = step * 0.25;
            EXPECT_LT(firstTrigger(RecoveryPolicy{}, {first, first + gap}, 30), 0)
                << "phase " << phase << ", gap " << gap;
        }
    }
}

TEST(RecoveryPolicy, DefaultsIgnoreASingleAttach)
{
    EXPECT_LT(firstTrigger(RecoveryPolicy{}, {3.0}, 30), 0);
}

TEST(RecoveryPolicy, DefaultsRetryAfterThirtySecondCooldown)
{
    RecoveryPolicy loop;
    int hit = -1;
    // An attach every 0.75 s (every third poll) until the first trigger.
    for (int poll = 0; poll < 200 && hit < 0; ++poll)
    {
        if (loop.observe(poll * 0.25, at(50, poll / 3), true, false))
        {
            hit = poll;
        }
    }
    ASSERT_GT(hit, 0);
    loop.attempted(hit * 0.25);
    for (int poll = hit + 1; poll < hit + 120; ++poll)
    {
        ASSERT_FALSE(loop.observe(poll * 0.25, at(50, poll / 3), true, false)) << "poll " << poll;
    }
    EXPECT_TRUE(loop.observe((hit + 120) * 0.25, at(50, (hit + 120) / 3), true, false));
}

TEST(RecoveryPolicy, FirstSamplesCannotTriggerBeforeAFullWindow)
{
    RecoveryPolicy fresh;
    EXPECT_FALSE(fresh.observe(0, 50, true, false));
    EXPECT_FALSE(fresh.observe(1.0, 53, true, false));
    EXPECT_TRUE(fresh.observe(2.0, 53, true, false));
}

TEST(RecoveryPolicy, AttachesBeforeTheWindowDoNotCount)
{
    // Polls every 260 ms and attaches 1.02 s apart: no 2 s window holds all three, although the
    // poll kept from before the window (13.00 at 15.08) precedes the first attach (13.01).
    RecoveryPolicy irregular;
    for (int poll = 0; poll <= 100; ++poll)
    {
        const double now = poll * 0.26;
        std::uint64_t count = 50;
        for (const double attach : {13.010, 14.030, 15.050})
        {
            if (attach <= now)
            {
                ++count;
            }
        }
        ASSERT_FALSE(irregular.observe(now, count, true, false)) << "t=" << now;
    }

    // A 2.25 s gap between polls is under the stall limit, but the three attaches it hides
    // (11.8, 12.9, 14.0) span more than the window.
    RecoveryPolicy gap;
    for (int poll = 0; poll <= 7; ++poll)
    {
        ASSERT_FALSE(gap.observe(10 + poll * 0.25, 50, true, false));
    }
    EXPECT_FALSE(gap.observe(14, 53, true, false));
    EXPECT_FALSE(gap.observe(14.25, 53, true, false));
}

TEST(RecoveryPolicy, AttachesOnTheWindowStartCount)
{
    // A poll exactly on the window's start is inside it, so rises after it count.
    RecoveryPolicy boundary;
    EXPECT_FALSE(boundary.observe(0, 50, true, false));
    EXPECT_FALSE(boundary.observe(1.0, 52, true, false));
    EXPECT_TRUE(boundary.observe(2.0, 53, true, false));

    // Without a poll at the start, rises before the first poll inside the window don't count.
    RecoveryPolicy late;
    EXPECT_FALSE(late.observe(0, 50, true, false));
    EXPECT_FALSE(late.observe(0.5, 51, true, false));
    EXPECT_FALSE(late.observe(2.4, 53, true, false));
    EXPECT_TRUE(late.observe(2.5, 54, true, false));
}

TEST(RecoveryPolicy, FailurePauseLiftsAfterItsDuration)
{
    RecoveryPolicy suspended;
    suspended.attempted(0);
    suspended.attempted(40);
    EXPECT_TRUE(suspended.capped(41));
    EXPECT_TRUE(suspended.capped(40 + 1799));
    EXPECT_FALSE(suspended.capped(40 + 1800));
    EXPECT_EQ(suspended.consecutiveFailures, 0U);
}

TEST(RecoveryPolicy, RestoredFailurePauseOnlyWhileRecent)
{
    RecoveryPolicy recent;
    recent.consecutiveFailures = 2;
    recent.restoreAttempt(-60);
    EXPECT_TRUE(recent.capped(0));
    RecoveryPolicy stale;
    stale.consecutiveFailures = 2;
    EXPECT_FALSE(stale.capped(0));
}

TEST(RecoveryPolicy, RestoredFailurePauseLongerThanAnHourKeepsItsDeadline)
{
    // Failed restarts at 0 and 30 with a 7200 s pause, restored at 3700: the hourly history has
    // dropped both, but the last restart time keeps the pause until 7230.
    RecoveryPolicy restored{Tuning{.failurePauseSeconds = 7200}};
    restored.consecutiveFailures = 2;
    restored.restoreLastAttempt(30);
    EXPECT_TRUE(restored.capped(3700));
    EXPECT_TRUE(restored.capped(7229));
    EXPECT_FALSE(restored.capped(7230));
    EXPECT_EQ(restored.consecutiveFailures, 0U);
}

TEST(RecoveryPolicy, TuningChangesDetectionAndLimits)
{
    // A slower loop (one attach every 1.2 s) is missed by the defaults but caught with a 3 s window.
    std::vector<double> slow;
    for (int n = 0; n < 12; ++n)
    {
        slow.push_back(3.0 + n * 1.2);
    }
    EXPECT_LT(firstTrigger(RecoveryPolicy{}, slow, 20), 0);
    EXPECT_GT(firstTrigger(RecoveryPolicy{Tuning{.windowSeconds = 3}}, slow, 20), 0);

    RecoveryPolicy pausedBriefly{Tuning{.failurePauseSeconds = 60}};
    pausedBriefly.attempted(0);
    pausedBriefly.attempted(40);
    EXPECT_TRUE(pausedBriefly.capped(99));
    EXPECT_FALSE(pausedBriefly.capped(100));

    RecoveryPolicy oncePerHour{Tuning{.maxResetsPerHour = 1}};
    oncePerHour.attempted(0);
    oncePerHour.recovered();
    EXPECT_TRUE(oncePerHour.capped(3599));
    EXPECT_FALSE(oncePerHour.capped(3600));
}

TEST(ResetBudget, SharedSpacingAndHourlyCap)
{
    ResetBudget budget{Tuning{.cooldownSeconds = 60}};
    EXPECT_TRUE(budget.allows(0));
    budget.spend(0);
    EXPECT_FALSE(budget.allows(59));
    EXPECT_TRUE(budget.allows(60));
    for (int n = 1; n < 6; ++n)
    {
        budget.spend(n * 60);
    }
    EXPECT_FALSE(budget.allows(1000));
    EXPECT_FALSE(budget.allows(3599));
    EXPECT_TRUE(budget.allows(3600));
}

TEST(ResetBudget, DefaultSpacingIsTheCooldown)
{
    ResetBudget quick;
    quick.spend(0);
    EXPECT_FALSE(quick.allows(29.9));
    EXPECT_TRUE(quick.allows(30));
}

} // namespace
} // namespace d6000
