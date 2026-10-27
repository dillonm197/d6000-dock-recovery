// Copyright (c) d6000-dock-recovery contributors.
// SPDX-License-Identifier: Apache-2.0
#include "cli.hpp"

#include "constants.hpp"
#include "ports.hpp"
#include "version.hpp"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <locale>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace d6000
{
namespace
{

constexpr int kMaxPort = 32;
constexpr int kMaxDurationSeconds = 86400;

// Bounds for every setting. They keep any combination safe to run unattended; see
// docs/configuration.md for what each one trades off.
constexpr double kMinWindow = 1.5;
constexpr double kMaxWindow = 5;
constexpr std::uint64_t kMinAttaches = 3;
constexpr std::uint64_t kMaxAttaches = 6;
constexpr double kMinVerify = 5;
constexpr double kMaxVerify = 60;
constexpr double kMinCooldown = 10;
constexpr double kMaxCooldown = 600;
constexpr std::size_t kMinResetsPerHour = 1;
constexpr std::size_t kMaxResetsPerHour = 20;
constexpr double kMinFailurePause = 60;
constexpr double kMaxFailurePause = 7200;

struct NamedLevel
{
    std::string_view name;
    LogLevel level;
};
constexpr std::array kLogLevels{
    NamedLevel{.name = "error", .level = LogLevel::Error}, NamedLevel{.name = "warning", .level = LogLevel::Warning},
    NamedLevel{.name = "info", .level = LogLevel::Info}, NamedLevel{.name = "debug", .level = LogLevel::Debug}};

LogLevel levelNamed(std::string_view name)
{
    const auto *const found = std::ranges::find(kLogLevels, name, &NamedLevel::name);
    return found == kLogLevels.end() ? LogLevel::Info : found->level;
}

// A finite decimal number within [minimum, maximum]. CLI::Range alone would accept "nan".
CLI::Validator finiteRange(double minimum, double maximum)
{
    return {[minimum, maximum](std::string &text) {
                std::istringstream input(text);
                input.imbue(std::locale::classic());
                double value = 0;
                input >> value;
                if (!input || input.peek() != std::char_traits<char>::eof() || !std::isfinite(value) ||
                    value < minimum || value > maximum)
                {
                    return std::format("must be a number from {:g} to {:g}", minimum, maximum);
                }
                return std::string();
            },
            std::format("[{:g} - {:g}]", minimum, maximum)};
}

// Parser state that doesn't map straight onto Options.
struct Choices
{
    bool list = false;
    bool check = false;
    bool recover = false;
    bool watch = false;
    int port = 0;
    std::string logLevel = "info";
    // Options that only fit some actions, checked after parsing (see actionError).
    CLI::Option *portOption = nullptr;
    CLI::Option *allPortsOption = nullptr;
    CLI::Option *dryRunOption = nullptr;
    CLI::Option *durationOption = nullptr;
    CLI::Option *requireDockOption = nullptr;
    CLI::Option *evidenceOption = nullptr;
    std::vector<CLI::Option *> notForList; // settings and logging
};

std::unique_ptr<CLI::App> buildApp(Options &options, Choices &choices)
{
    auto app = std::make_unique<CLI::App>(
        "Automatic recovery for Dell D6000 docks stuck in a USB-C attach loop on Apple-silicon Macs.",
        "d6000-recovery");
    // Help and version are re-added last so the actions come first in --help.
    app->set_help_flag();
    app->footer(std::format("Requires Apple silicon and the controller structure shown by --list (structure=ok).\n"
                            "Validated end to end on {}. --recover and a live --watch need root.\n"
                            "Exit codes: 0 success, 1 refused or failed, 2 invalid arguments.\n"
                            "Settings are explained in docs/configuration.md.",
                            validatedModelList()));

    app->add_flag("--list", choices.list, "Print every USB-C port and its state")->group("Actions (choose one)");
    app->add_flag("--check", choices.check, "Confirm usable ports and validate the settings (read-only)")
        ->group("Actions (choose one)");
    app->add_flag("--recover", choices.recover, "Restart one looping port's controller once (root)")
        ->group("Actions (choose one)");
    app->add_flag("--watch", choices.watch, "Run the background watcher (root unless --dry-run)")
        ->group("Actions (choose one)");

    choices.portOption = app->add_option("--port", choices.port, "Act on Port-USB-C@N only (numbers from --list)")
                             ->check(CLI::Range(1, kMaxPort))
                             ->group("Target");
    choices.allPortsOption =
        app->add_flag("--all-ports", options.allPorts, "Every built-in USB-C port; --recover picks the one that loops")
            ->group("Target");

    choices.dryRunOption =
        app->add_flag("--dry-run", options.dryRun, "--watch: log what it would do, never send a command")
            ->group("Action options");
    choices.durationOption = app->add_option("--duration", options.durationSeconds, "--watch: stop after SECONDS")
                                 ->check(CLI::Range(1, kMaxDurationSeconds))
                                 ->group("Action options");
    choices.requireDockOption =
        app->add_flag("--require-dock", options.requireDock, "--check: fail unless a working D6000 is connected")
            ->group("Action options");
    choices.evidenceOption =
        app->add_flag("--evidence", options.evidence, "--recover: record controller registers as JSON lines")
            ->group("Action options");

    Tuning &tuning = options.tuning;
    const auto setting = [&](const std::string &flag, auto &value, const CLI::Validator &validator,
                             const std::string &help) {
        choices.notForList.push_back(
            app->add_option(flag, value, help)->check(validator)->capture_default_str()->group("Settings"));
    };
    setting("--detect-window", tuning.windowSeconds, finiteRange(kMinWindow, kMaxWindow),
            "Seconds that must contain the attaches");
    setting("--detect-attaches", tuning.minAttaches, CLI::Range(kMinAttaches, kMaxAttaches),
            "Attaches without the dock needed to act");
    setting("--verify-timeout", tuning.verifySeconds, finiteRange(kMinVerify, kMaxVerify),
            "Seconds to wait for the dock after a restart");
    setting("--cooldown", tuning.cooldownSeconds, finiteRange(kMinCooldown, kMaxCooldown),
            "Minimum seconds between restarts, per port and machine-wide");
    setting("--max-resets-per-hour", tuning.maxResetsPerHour, CLI::Range(kMinResetsPerHour, kMaxResetsPerHour),
            "Restarts allowed in any hour, per port and machine-wide");
    setting("--failure-pause", tuning.failurePauseSeconds, finiteRange(kMinFailurePause, kMaxFailurePause),
            "Seconds a port is left alone after 2 restarts in a row fail");

    choices.notForList.push_back(app->add_option("--log-level", choices.logLevel, "Least severe level written")
                                     ->check(CLI::IsMember({"error", "warning", "info", "debug"}))
                                     ->capture_default_str()
                                     ->group("Logging"));
    choices.notForList.push_back(
        app->add_option("--log-file", options.logFile, "Write the log to FILE, rotating at 5 MB (default: stdout)")
            ->group("Logging"));

    app->set_help_flag("-h,--help", "Print this help and exit")->group("Other");
    app->set_version_flag("--version", versionText())->group("Other");
    return app;
}

ParseResult usageError(std::string_view message)
{
    return {.options = std::nullopt,
            .exitCode = kExitUsage,
            .output = std::format("{}\nRun with --help for more information.\n", message),
            .error = true};
}

bool given(const CLI::Option *option)
{
    return option->count() > 0;
}

// Returns why the flags don't fit the chosen action, or empty when they do.
std::string_view actionError(const Options &options, const Choices &choices)
{
    if (static_cast<int>(choices.list) + static_cast<int>(choices.check) + static_cast<int>(choices.recover) +
            static_cast<int>(choices.watch) !=
        1)
    {
        return "choose exactly one of --list, --check, --recover or --watch";
    }
    if (given(choices.allPortsOption) && given(choices.portOption))
    {
        return "use --port or --all-ports, not both";
    }
    const bool watch = options.action == Action::Watch;
    if ((given(choices.dryRunOption) || given(choices.durationOption)) && !watch)
    {
        return "--dry-run and --duration only work with --watch";
    }
    if (given(choices.requireDockOption) && options.action != Action::Check)
    {
        return "--require-dock only works with --check";
    }
    if (given(choices.evidenceOption) && options.action != Action::Recover)
    {
        return "--evidence only works with --recover";
    }
    if (options.action == Action::List &&
        (given(choices.allPortsOption) || std::ranges::any_of(choices.notForList, given)))
    {
        return "--list takes no other options";
    }
    if (options.action != Action::List && options.portName.empty() && !options.allPorts)
    {
        return "--port or --all-ports is required";
    }
    return {};
}

} // namespace

ParseResult parseArguments(std::span<const char *const> argv)
{
    Options options;
    Choices choices;
    const std::unique_ptr<CLI::App> app = buildApp(options, choices);
    try
    {
        app->parse(static_cast<int>(argv.size()), argv.data());
    }
    catch (const CLI::ParseError &error)
    {
        std::ostringstream out;
        std::ostringstream err;
        const int code = app->exit(error, out, err);
        return {.options = std::nullopt,
                .exitCode = code == 0 ? kExitOk : kExitUsage,
                .output = out.str() + err.str(),
                .error = code != 0};
    }
    if (choices.list)
    {
        options.action = Action::List;
    }
    else if (choices.check)
    {
        options.action = Action::Check;
    }
    else if (choices.recover)
    {
        options.action = Action::Recover;
    }
    else
    {
        options.action = Action::Watch;
    }
    if (choices.port != 0)
    {
        options.portName = std::format("{}{}", kUsbCPortPrefix, choices.port);
    }
    if (const std::string_view error = actionError(options, choices); !error.empty())
    {
        return usageError(error);
    }
    options.logLevel = levelNamed(choices.logLevel);
    return {.options = options, .exitCode = kExitOk, .output = {}, .error = false};
}

std::string usage()
{
    Options options;
    Choices choices;
    return buildApp(options, choices)->help();
}

std::string versionText()
{
    return std::format("d6000-recovery {} (experimental; validated {})", kVersion, validatedModelList());
}

std::string describe(const Tuning &tuning)
{
    return std::format("settings: {} attaches within {:g} s, dock wait {:g} s, cooldown {:g} s, at most {} restarts "
                       "per hour, {:g} s pause after {} failed restarts in a row",
                       tuning.minAttaches, tuning.windowSeconds, tuning.verifySeconds, tuning.cooldownSeconds,
                       tuning.maxResetsPerHour, tuning.failurePauseSeconds, kFailuresBeforePause);
}

} // namespace d6000
