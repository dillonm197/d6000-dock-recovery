// Copyright (c) d6000-dock-recovery contributors.
// SPDX-License-Identifier: Apache-2.0
//
// d6000-recovery: automatic recovery for a Dell D6000/D6000S dock that gets stuck in a USB-C attach
// loop on an Apple-silicon Mac (the dock's light stays amber; no charging, USB or Ethernet).
//
// On some plug-ins the Mac's port controller briefly becomes the power source, the dock's role swap
// makes the Mac see a disconnect, and the Mac then keeps the port idle for about half a second.
// That is longer than the dock waits, so both sides restart in lockstep about once a second and
// never get out of it. Restarting that one port's controller while the dock stays plugged in breaks
// the loop. This is a workaround: the behaviour is in the controller's firmware and can't be
// patched here. docs/how-it-works.md has the full description, the guards and the log format.
//
// Only --recover and a live --watch talk to a controller, and both need root. The tool never
// writes firmware or controller registers, unlocks debug modes, or changes security, boot or
// network settings.
#include "cli.hpp"
#include "clock.hpp"
#include "constants.hpp"
#include "controller.hpp"
#include "log.hpp"
#include "ports.hpp"
#include "version.hpp"
#include "watcher.hpp"

#include <IOKit/IOReturn.h>

#include <cstdint>
#include <cstdio>
#include <exception>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

namespace
{

using namespace d6000;

void print(const std::string &text)
{
    static_cast<void>(std::fputs(text.c_str(), stdout));
}

int listPorts()
{
    PortReader reader;
    for (const std::string &name : usbCPorts())
    {
        const Snapshot state = reader.snapshot(name);
        if (!state.valid)
        {
            continue;
        }
        print(std::format("{} controller_rid={} structure={} dock={} data={} count={} app_loaded={}\n", name, state.rid,
                          state.isolated ? std::string("ok") : "\"" + state.structureProblem + "\"",
                          state.dock ? "yes" : "no", state.data ? "active" : "none", state.count, state.appLoaded));
    }
    return kExitOk;
}

int checkPorts(const Options &options)
{
    PortReader reader;
    const std::vector<std::string> targets =
        options.allPorts ? usbCPorts() : std::vector<std::string>{options.portName};
    std::string usable;
    bool dock = false;
    for (const std::string &name : targets)
    {
        const Snapshot state = reader.snapshot(name);
        if (!state.valid || !state.isolated)
        {
            logWarning(name + " not usable: " + state.reason());
            continue;
        }
        usable += std::format(" {} (RID {}{})", name, state.rid, state.dock ? ", D6000 present" : "");
        dock = dock || state.dock;
    }
    if (usable.empty() || (options.requireDock && !dock))
    {
        logError(usable.empty() ? "preflight failed: no usable USB-C port"
                                : "preflight failed: no D6000 connected to a usable port");
        return kExitFailed;
    }
    logInfo(describe(options.tuning));
    logInfo("preflight passed for" + usable + "; no controller commands sent");
    return kExitOk;
}

int recoverOnce(Options options)
{
    PortReader reader;
    if (options.allPorts)
    {
        options.portName = findLoopingPort(reader);
        if (options.portName.empty())
        {
            return kExitFailed;
        }
    }
    const Snapshot initial = reader.snapshot(options.portName);
    if (!initial.valid || !initial.isolated)
    {
        logError("preflight failed: " + initial.reason());
        return kExitFailed;
    }
    const double resetStarted = monotonicSeconds();
    const IOReturn result = warmReset(reader, options.portName, Snapshot{}, options.evidence);
    logInfo(std::format("command return=0x{:08x}", static_cast<std::uint32_t>(result)));
    if (result != kIOReturnSuccess)
    {
        return kExitFailed;
    }
    return verifyRecovery(reader, options.portName, initial.appLoaded, resetStarted, options.tuning.verifySeconds)
               ? kExitOk
               : kExitFailed;
}

int run(const Options &options)
{
    if (options.action == Action::List)
    {
        return listPorts();
    }
    if (!appleSilicon())
    {
        logError("unsupported Mac: Apple silicon required");
        return options.action == Action::Watch ? kExitOk : kExitFailed;
    }
    if (const std::string model = machineModel(); !validatedModel(model))
    {
        logWarning("model " + model +
                   " is not yet validated end to end; acting only on ports whose controller structure matches");
    }
    if (options.action == Action::Check)
    {
        return checkPorts(options);
    }
    if (!options.dryRun && geteuid() != 0)
    {
        logError("administrator access required; no changes made");
        return kExitFailed;
    }
    if (options.action == Action::Recover)
    {
        return recoverOnce(options);
    }
    return runWatch(options);
}

} // namespace

int main(int argc, char **argv)
{
    try
    {
        const ParseResult parsed = parseArguments(std::span<const char *const>(argv, static_cast<std::size_t>(argc)));
        if (!parsed.options)
        {
            static_cast<void>(std::fputs(parsed.output.c_str(), parsed.error ? stderr : stdout));
            return parsed.exitCode;
        }
        try
        {
            configureLogging(parsed.options->logLevel, parsed.options->logFile);
        }
        catch (const std::exception &error)
        {
            const std::string message =
                std::format("cannot open log file {}: {}\n", parsed.options->logFile, error.what());
            static_cast<void>(std::fputs(message.c_str(), stderr));
            return kExitFailed;
        }
        return run(*parsed.options);
    }
    catch (const std::exception &error)
    {
        static_cast<void>(std::fputs("d6000-recovery: unexpected error: ", stderr));
        static_cast<void>(std::fputs(error.what(), stderr));
        static_cast<void>(std::fputs("\n", stderr));
        return kExitFailed;
    }
    catch (...)
    {
        static_cast<void>(std::fputs("d6000-recovery: unexpected error\n", stderr));
        return kExitFailed;
    }
}
