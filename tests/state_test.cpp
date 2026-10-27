// Copyright (c) d6000-dock-recovery contributors.
// SPDX-License-Identifier: Apache-2.0
#include "state.hpp"

#include <gtest/gtest.h>

#include <sstream>

namespace d6000
{
namespace
{

constexpr double kNow = 1'800'000'000.0;

SavedState parseText(const std::string &text)
{
    std::istringstream input(text);
    return parseState(input, kNow);
}

TEST(State, ParsesFailuresAndRecentResets)
{
    const SavedState state = parseText("failures Port-USB-C@1 1\n"
                                       "reset Port-USB-C@1 1799999000.000\n"
                                       "reset Port-USB-C@1 1799990000.000\n" // older than an hour
                                       "failures Port-USB-C@2 0\n");
    ASSERT_EQ(state.size(), 2U);
    EXPECT_EQ(state.at("Port-USB-C@1").consecutiveFailures, 1U);
    ASSERT_EQ(state.at("Port-USB-C@1").resetTimes.size(), 1U);
    EXPECT_DOUBLE_EQ(state.at("Port-USB-C@1").resetTimes.front(), 1799999000.0);
    EXPECT_EQ(state.at("Port-USB-C@2").consecutiveFailures, 0U);
}

TEST(State, IgnoresOtherPortsAndCorruptCounts)
{
    const SavedState state = parseText("failures MagSafe@1 1\n"
                                       "failures Port-USB-C@1 250\n"
                                       "failures Port-USB-C@2 -1\n"
                                       "other Port-USB-C@3 1\n");
    EXPECT_TRUE(state.empty());
}

TEST(State, StopsAtTheFirstMalformedLine)
{
    const SavedState state = parseText("failures Port-USB-C@1 1\n"
                                       "failures Port-USB-C@2 two\n"
                                       "failures Port-USB-C@3 1\n");
    ASSERT_EQ(state.size(), 1U);
    EXPECT_TRUE(state.contains("Port-USB-C@1"));
}

TEST(State, FutureTimestampsAreKept)
{
    const SavedState state = parseText("reset Port-USB-C@1 1800000100.000\n");
    ASSERT_EQ(state.at("Port-USB-C@1").resetTimes.size(), 1U);
}

TEST(State, FormatRoundTrips)
{
    SavedState state;
    state["Port-USB-C@2"] = {.consecutiveFailures = 1, .resetTimes = {1799999500.25}};
    state["Port-USB-C@1"] = {.consecutiveFailures = 0, .resetTimes = {}};
    const std::string text = formatState(state);
    EXPECT_EQ(text, "failures Port-USB-C@1 0\n"
                    "failures Port-USB-C@2 1\n"
                    "reset Port-USB-C@2 1799999500.250\n");
    const SavedState parsed = parseText(text);
    EXPECT_EQ(parsed.at("Port-USB-C@2").consecutiveFailures, 1U);
    EXPECT_DOUBLE_EQ(parsed.at("Port-USB-C@2").resetTimes.front(), 1799999500.25);
}

TEST(State, LastResetSurvivesAtAnyAge)
{
    SavedState state;
    state["Port-USB-C@1"] = {.consecutiveFailures = 2, .resetTimes = {}, .lastReset = kNow - 3670};
    const std::string text = formatState(state);
    EXPECT_EQ(text, "failures Port-USB-C@1 2\n"
                    "last Port-USB-C@1 1799996330.000\n");
    const SavedPort parsed = parseText(text).at("Port-USB-C@1");
    EXPECT_EQ(parsed.consecutiveFailures, 2U);
    EXPECT_TRUE(parsed.resetTimes.empty());
    ASSERT_TRUE(parsed.lastReset.has_value());
    EXPECT_DOUBLE_EQ(*parsed.lastReset, kNow - 3670);
}

TEST(State, FilesWithoutLastResetStillParse)
{
    const SavedState state = parseText("failures Port-USB-C@1 1\n"
                                       "reset Port-USB-C@1 1799999000.000\n");
    EXPECT_FALSE(state.at("Port-USB-C@1").lastReset.has_value());
}

} // namespace
} // namespace d6000
