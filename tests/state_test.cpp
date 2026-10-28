// Copyright (c) d6000-dock-recovery contributors.
// SPDX-License-Identifier: Apache-2.0
#include "state.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <sys/stat.h>

namespace d6000
{
namespace
{

// A fresh directory for one test, removed afterwards.
struct ScratchDirectory
{
    ScratchDirectory()
        : path(std::filesystem::temp_directory_path() /
               std::format("d6000-state-test-{}", std::chrono::steady_clock::now().time_since_epoch().count()))
    {
        std::filesystem::create_directories(path);
    }
    ~ScratchDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    ScratchDirectory(const ScratchDirectory &) = delete;
    ScratchDirectory &operator=(const ScratchDirectory &) = delete;
    ScratchDirectory(ScratchDirectory &&) = delete;
    ScratchDirectory &operator=(ScratchDirectory &&) = delete;

    std::filesystem::path path;
};

std::string readText(const std::filesystem::path &path)
{
    std::ifstream file(path);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

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

TEST(State, WrittenFileIsOwnerWritableOnlyWhateverTheUmask)
{
    const ScratchDirectory scratch;
    const std::filesystem::path state = scratch.path / "state";
    const mode_t previous = ::umask(0);
    const bool written = writeStateFile(state.c_str(), "failures Port-USB-C@1 0\n");
    ::umask(previous);
    ASSERT_TRUE(written);
    struct stat info{};
    ASSERT_EQ(::stat(state.c_str(), &info), 0);
    EXPECT_EQ(info.st_mode & 0777, 0644U);
    EXPECT_EQ(readText(state), "failures Port-USB-C@1 0\n");
}

TEST(State, LeftoverTemporaryFileGetsTheRestrictedMode)
{
    const ScratchDirectory scratch;
    const std::filesystem::path state = scratch.path / "state";
    std::ofstream(state.string() + ".tmp") << "old";
    std::filesystem::permissions(state.string() + ".tmp", std::filesystem::perms::all);
    ASSERT_TRUE(writeStateFile(state.c_str(), "failures Port-USB-C@1 1\n"));
    struct stat info{};
    ASSERT_EQ(::stat(state.c_str(), &info), 0);
    EXPECT_EQ(info.st_mode & 0777, 0644U);
}

TEST(State, RefusesASymbolicLinkInPlaceOfTheTemporaryFile)
{
    const ScratchDirectory scratch;
    const std::filesystem::path state = scratch.path / "state";
    const std::filesystem::path target = scratch.path / "elsewhere";
    std::ofstream(target) << "untouched";
    std::filesystem::create_symlink(target, state.string() + ".tmp");
    EXPECT_FALSE(writeStateFile(state.c_str(), "failures Port-USB-C@1 0\n"));
    EXPECT_EQ(readText(target), "untouched");
    EXPECT_FALSE(std::filesystem::exists(state));
}

} // namespace
} // namespace d6000
