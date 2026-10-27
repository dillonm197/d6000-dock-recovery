// Copyright (c) d6000-dock-recovery contributors.
// SPDX-License-Identifier: Apache-2.0
#include "cli.hpp"
#include "constants.hpp"

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace d6000
{
namespace
{

ParseResult parse(std::string_view line)
{
    std::vector<std::string> words{"d6000-recovery"};
    std::istringstream stream{std::string(line)};
    for (std::string word; stream >> word;)
    {
        words.push_back(word);
    }
    std::vector<const char *> argv;
    for (const std::string &word : words)
    {
        argv.push_back(word.c_str());
    }
    return parseArguments(argv);
}

void expectUsageError(std::string_view line)
{
    const ParseResult result = parse(line);
    EXPECT_FALSE(result.options) << line;
    EXPECT_EQ(result.exitCode, kExitUsage) << line;
    EXPECT_TRUE(result.error) << line;
}

TEST(Cli, HelpAndVersionExitCleanly)
{
    const ParseResult version = parse("--version");
    EXPECT_FALSE(version.options);
    EXPECT_EQ(version.exitCode, kExitOk);
    EXPECT_NE(version.output.find("d6000-recovery "), std::string::npos);

    const ParseResult help = parse("--help");
    EXPECT_FALSE(help.options);
    EXPECT_EQ(help.exitCode, kExitOk);
    EXPECT_NE(help.output.find("--watch"), std::string::npos);
}

TEST(Cli, ValidCommands)
{
    const ParseResult watch = parse("--watch --all-ports");
    ASSERT_TRUE(watch.options);
    EXPECT_EQ(watch.options->action, Action::Watch);
    EXPECT_TRUE(watch.options->allPorts);
    EXPECT_EQ(watch.options->logLevel, LogLevel::Info);
    EXPECT_TRUE(watch.options->logFile.empty());

    const ParseResult dryRun = parse("--watch --port 3 --dry-run --duration 60");
    ASSERT_TRUE(dryRun.options);
    EXPECT_EQ(dryRun.options->portName, "Port-USB-C@3");
    EXPECT_TRUE(dryRun.options->dryRun);
    EXPECT_EQ(dryRun.options->durationSeconds, 60);

    EXPECT_TRUE(parse("--list").options);
    EXPECT_TRUE(parse("--check --port 1 --require-dock").options);
    EXPECT_TRUE(parse("--recover --all-ports --evidence").options);
}

TEST(Cli, RejectsFlagsThatDoNotFitTheAction)
{
    for (const std::string_view line :
         {"--recover --dry-run --port 3", "--watch --evidence --port 3", "--check --evidence --port 3",
          "--watch --all-ports --port 3", "--list --all-ports", "--watch", "--recover --all-ports --dry-run",
          "--check --port 1 --duration 5", "--watch --port 1 --require-dock", "--list --cooldown 60", "", "--port 3",
          "--watch --list --port 3", "--watch --all-ports --unknown", "--list --log-level debug"})
    {
        expectUsageError(line);
    }
}

TEST(Cli, RejectsOutOfRangeOrMalformedNumbers)
{
    for (const std::string_view line :
         {"--watch --port 0", "--watch --port 33", "--watch --port abc", "--watch --port 3x", "--watch --port",
          "--watch --port 3 --dry-run --duration 0", "--watch --port 3 --dry-run --duration 86401"})
    {
        expectUsageError(line);
    }
    EXPECT_TRUE(parse("--watch --port 32").options);
}

TEST(Cli, SettingsChangeTheTuning)
{
    const ParseResult result = parse("--watch --all-ports --detect-window 3 --detect-attaches 4 --verify-timeout 20 "
                                     "--cooldown 60 --max-resets-per-hour 3 --failure-pause 600");
    ASSERT_TRUE(result.options);
    const Tuning &tuning = result.options->tuning;
    EXPECT_DOUBLE_EQ(tuning.windowSeconds, 3);
    EXPECT_EQ(tuning.minAttaches, 4U);
    EXPECT_DOUBLE_EQ(tuning.verifySeconds, 20);
    EXPECT_DOUBLE_EQ(tuning.cooldownSeconds, 60);
    EXPECT_EQ(tuning.maxResetsPerHour, 3U);
    EXPECT_DOUBLE_EQ(tuning.failurePauseSeconds, 600);
}

TEST(Cli, SettingsDefaultToTheValidatedTuning)
{
    const ParseResult result = parse("--watch --all-ports");
    ASSERT_TRUE(result.options);
    const Tuning defaults;
    EXPECT_DOUBLE_EQ(result.options->tuning.windowSeconds, defaults.windowSeconds);
    EXPECT_EQ(result.options->tuning.minAttaches, defaults.minAttaches);
    EXPECT_DOUBLE_EQ(result.options->tuning.cooldownSeconds, defaults.cooldownSeconds);
    EXPECT_EQ(result.options->tuning.maxResetsPerHour, defaults.maxResetsPerHour);
}

TEST(Cli, SettingsMustStayInBounds)
{
    for (const std::string_view line :
         {"--watch --all-ports --detect-window 1", "--watch --all-ports --detect-window 6",
          "--watch --all-ports --detect-attaches 2", "--watch --all-ports --detect-attaches 3.5",
          "--watch --all-ports --verify-timeout 4", "--watch --all-ports --cooldown 9",
          "--watch --all-ports --max-resets-per-hour 0", "--watch --all-ports --max-resets-per-hour 21",
          "--watch --all-ports --failure-pause 59", "--watch --all-ports --cooldown nan",
          "--watch --all-ports --cooldown inf", "--watch --all-ports --cooldown 30s", "--watch --all-ports --cooldown"})
    {
        expectUsageError(line);
    }
    EXPECT_TRUE(parse("--check --all-ports --cooldown 600 --failure-pause 7200").options);
    EXPECT_TRUE(parse("--recover --port 2 --verify-timeout 30").options);
}

TEST(Cli, LoggingOptions)
{
    const ParseResult result = parse("--watch --all-ports --log-level debug --log-file /tmp/events.log");
    ASSERT_TRUE(result.options);
    EXPECT_EQ(result.options->logLevel, LogLevel::Debug);
    EXPECT_EQ(result.options->logFile, "/tmp/events.log");
    expectUsageError("--watch --all-ports --log-level verbose");
}

TEST(Cli, UsageListsEverySetting)
{
    const std::string text = usage();
    for (const std::string_view flag : {"--detect-window", "--detect-attaches", "--verify-timeout", "--cooldown",
                                        "--max-resets-per-hour", "--failure-pause", "--log-level", "--log-file"})
    {
        EXPECT_NE(text.find(flag), std::string::npos) << flag;
    }
}

TEST(Cli, DescribeNamesTheValuesInEffect)
{
    EXPECT_EQ(describe(Tuning{}), "settings: 3 attaches within 2.5 s, dock wait 10 s, cooldown 30 s, at most 6 "
                                  "restarts per hour, 1800 s pause after 2 failed restarts in a row");
}

} // namespace
} // namespace d6000
