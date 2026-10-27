// Copyright (c) d6000-dock-recovery contributors.
// SPDX-License-Identifier: Apache-2.0
#include "log.hpp"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_sinks.h>
#include <spdlog/spdlog.h>

#include <cstddef>
#include <format>
#include <memory>

namespace d6000
{
namespace
{

constexpr std::size_t kMaxLogBytes = std::size_t{5} * 1024 * 1024;
constexpr std::size_t kRotatedFiles = 1;

spdlog::level::level_enum toSpdlog(LogLevel level)
{
    switch (level)
    {
    case LogLevel::Error:
        return spdlog::level::err;
    case LogLevel::Warning:
        return spdlog::level::warn;
    case LogLevel::Info:
        return spdlog::level::info;
    case LogLevel::Debug:
        return spdlog::level::debug;
    }
    return spdlog::level::info;
}

} // namespace

void configureLogging(LogLevel level, const std::string &path)
{
    spdlog::sink_ptr sink;
    if (path.empty())
    {
        sink = std::make_shared<spdlog::sinks::stdout_sink_st>();
    }
    else
    {
        sink = std::make_shared<spdlog::sinks::rotating_file_sink_st>(path, kMaxLogBytes, kRotatedFiles);
    }
    auto logger = std::make_shared<spdlog::logger>("d6000-recovery", std::move(sink));
    logger->set_pattern("%Y-%m-%dT%H:%M:%SZ %l %v", spdlog::pattern_time_type::utc);
    logger->set_level(toSpdlog(level));
    logger->flush_on(spdlog::level::trace);
    spdlog::set_default_logger(std::move(logger));
}

void logError(std::string_view text)
{
    spdlog::error("{}", text);
}

void logWarning(std::string_view text)
{
    spdlog::warn("{}", text);
}

void logInfo(std::string_view text)
{
    spdlog::info("{}", text);
}

void logDebug(std::string_view text)
{
    spdlog::debug("{}", text);
}

std::string fixed2(double value)
{
    return std::format("{:.2f}", value);
}

} // namespace d6000
