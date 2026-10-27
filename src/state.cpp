// Copyright (c) d6000-dock-recovery contributors.
// SPDX-License-Identifier: Apache-2.0
#include "state.hpp"

#include "constants.hpp"
#include "recovery_policy.hpp"

#include <cstdio>
#include <format>
#include <fstream>
#include <unistd.h>

namespace d6000
{
namespace
{

// A stored failure count at or above this is treated as corrupt and ignored.
constexpr double kMaxStoredFailures = 100;

} // namespace

SavedState parseState(std::istream &input, double unixNow)
{
    SavedState state;
    std::string key;
    std::string port;
    double value = 0;
    while (input >> key >> port >> value)
    {
        if (!port.starts_with(kUsbCPortPrefix))
        {
            continue;
        }
        if (key == "failures" && value >= 0 && value < kMaxStoredFailures)
        {
            state[port].consecutiveFailures = static_cast<unsigned>(value);
        }
        else if (key == "reset" && unixNow - value < kHourSeconds)
        {
            state[port].resetTimes.push_back(value);
        }
        else if (key == "last")
        {
            state[port].lastReset = value;
        }
    }
    return state;
}

std::string formatState(const SavedState &state)
{
    std::string text;
    for (const auto &[port, saved] : state)
    {
        text += std::format("failures {} {}\n", port, saved.consecutiveFailures);
        for (const double at : saved.resetTimes)
        {
            text += std::format("reset {} {:.3f}\n", port, at);
        }
        if (saved.lastReset)
        {
            text += std::format("last {} {:.3f}\n", port, *saved.lastReset);
        }
    }
    return text;
}

SavedState readStateFile(const char *path, double unixNow)
{
    std::ifstream file(path);
    return parseState(file, unixNow);
}

bool writeStateFile(const char *path, std::string_view contents)
{
    const std::string temporary = std::string(path) + ".tmp";
    // A C stream rather than std::ofstream because fsync needs its descriptor; it is closed on every
    // path below.
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    std::FILE *file = std::fopen(temporary.c_str(), "w");
    if (file == nullptr)
    {
        return false;
    }
    bool ok = std::fwrite(contents.data(), 1, contents.size(), file) == contents.size();
    ok = ok && std::fflush(file) == 0 && fsync(fileno(file)) == 0;
    ok = std::fclose(file) == 0 && ok; // NOLINT(cppcoreguidelines-owning-memory)
    if (ok && std::rename(temporary.c_str(), path) == 0)
    {
        return true;
    }
    static_cast<void>(std::remove(temporary.c_str()));
    return false;
}

} // namespace d6000
