// Copyright (c) d6000-dock-recovery contributors.
// SPDX-License-Identifier: Apache-2.0
#include "controller.hpp"

#include "AppleHPMLib.h"
#include "clock.hpp"
#include "log.hpp"
#include "stop.hpp"

#include <IOKit/IOCFPlugIn.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <format>
#include <map>
#include <optional>
#include <span>
#include <thread>
#include <utility>
#include <vector>

namespace d6000
{
namespace
{

using namespace std::chrono_literals;

// The TI-style "Gaid" warm-restart command as a FourCC.
constexpr std::uint32_t kGaidCommand = 0x47616964;
// Controller registers. 0x03 is the firmware mode ("APP " when running normally). 0x28 (port
// information) and 0x69 (attach state) are read only in evidence mode; their layout is partly
// inferred, so they are recorded raw.
constexpr std::uint8_t kModeRegister = 0x03;
constexpr std::uint8_t kPortInfoRegister = 0x28;
constexpr std::uint8_t kAttachStateRegister = 0x69;
constexpr std::string_view kAppMode = "APP ";

// The HPM user client rejects reads longer than its 64-byte register window.
using RegisterBytes = std::array<unsigned char, 64>;

constexpr auto kAppModePoll = 100ms;
constexpr int kAppModePolls = 10;
constexpr auto kVerifyPoll = 250ms;
constexpr double kVerifyPollSeconds = 0.25;
constexpr auto kEvidencePrePoll = 20ms;
constexpr auto kEvidencePostPoll = 25ms;
constexpr double kEvidencePreSeconds = 3;
constexpr double kEvidencePostSeconds = 6;
constexpr auto kLoopProbe = 3s;
constexpr std::uint64_t kLoopProbeAttaches = 2;
constexpr double kMillisecondsPerSecond = 1000;

std::string hex(std::span<const unsigned char> bytes)
{
    std::string text;
    text.reserve(bytes.size() * 2);
    for (const unsigned char byte : bytes)
    {
        text += std::format("{:02x}", byte);
    }
    return text;
}

std::string returnCode(IOReturn result)
{
    return std::format("0x{:08x}", static_cast<std::uint32_t>(result));
}

// The controller's AppleHPMLib user client (an IOKit CFPlugIn), released on scope exit.
class HpmClient
{
  public:
    // Opens the user client for a controller service. On failure, returns nullopt and sets error.
    static std::optional<HpmClient> open(io_service_t controller, IOReturn &error)
    {
        IOCFPlugInInterface **plugin = nullptr;
        SInt32 score = 0;
        error =
            IOCreatePlugInInterfaceForService(controller, kAppleHPMLibType, kIOCFPlugInInterfaceID, &plugin, &score);
        if (error != kIOReturnSuccess || plugin == nullptr)
        {
            if (error == kIOReturnSuccess)
            {
                error = kIOReturnError;
            }
            return std::nullopt;
        }
        AppleHPMLib **device = nullptr;
        const HRESULT queried =
            (*plugin)->QueryInterface(static_cast<void *>(plugin), CFUUIDGetUUIDBytes(kAppleHPMLibInterface),
                                      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): COM out-parameter
                                      reinterpret_cast<LPVOID *>(&device));
        if (queried != S_OK || device == nullptr)
        {
            IODestroyPlugInInterface(plugin);
            error = kIOReturnUnsupported;
            return std::nullopt;
        }
        error = kIOReturnSuccess;
        return HpmClient(plugin, device);
    }

    ~HpmClient()
    {
        release();
    }
    HpmClient(const HpmClient &) = delete;
    HpmClient &operator=(const HpmClient &) = delete;
    HpmClient(HpmClient &&other) noexcept
        : plugin_(std::exchange(other.plugin_, nullptr)), device_(std::exchange(other.device_, nullptr))
    {
    }
    HpmClient &operator=(HpmClient &&other) noexcept
    {
        if (this != &other)
        {
            release();
            plugin_ = std::exchange(other.plugin_, nullptr);
            device_ = std::exchange(other.device_, nullptr);
        }
        return *this;
    }

    // Reads one register of chip 0. length receives the number of bytes the controller returned.
    IOReturn read(std::uint8_t reg, RegisterBytes &bytes, std::uint64_t &length)
    {
        length = 0;
        return (*device_)->Read(static_cast<void *>(device_), 0, reg, bytes.data(), bytes.size(), 0, &length);
    }

    // Sends one 4CC command to chip 0.
    IOReturn command(std::uint32_t fourcc)
    {
        return (*device_)->Command(static_cast<void *>(device_), 0, fourcc, 0);
    }

    // True when the mode register reads back "APP ".
    bool inAppMode()
    {
        RegisterBytes mode{};
        std::uint64_t length = 0;
        return read(kModeRegister, mode, length) == kIOReturnSuccess && isAppMode(mode, length);
    }

    static bool isAppMode(const RegisterBytes &mode, std::uint64_t length)
    {
        return length == kAppMode.size() && std::equal(kAppMode.begin(), kAppMode.end(), mode.begin());
    }

  private:
    HpmClient(IOCFPlugInInterface **plugin, AppleHPMLib **device) noexcept : plugin_(plugin), device_(device)
    {
    }
    void release() noexcept
    {
        if (device_ != nullptr)
        {
            (*device_)->Release(static_cast<void *>(device_));
            device_ = nullptr;
        }
        if (plugin_ != nullptr)
        {
            IODestroyPlugInInterface(plugin_);
            plugin_ = nullptr;
        }
    }

    IOCFPlugInInterface **plugin_ = nullptr;
    AppleHPMLib **device_ = nullptr;
};

// One JSON line per evidence read; times are seconds relative to origin.
void evidenceRead(HpmClient &client, std::uint8_t reg, std::string_view phase, double origin)
{
    RegisterBytes bytes{};
    std::uint64_t length = 0;
    const double start = monotonicSeconds();
    const IOReturn status = client.read(reg, bytes, length);
    const std::size_t shown = std::min<std::size_t>(length, bytes.size());
    const std::string line =
        std::format(R"({{"phase":"{}","t":{:.6f},"read_ms":{:.3f},"reg":"{:02x}","status":"{}","hex":"{}"}})", phase,
                    start - origin, (monotonicSeconds() - start) * kMillisecondsPerSecond, reg, returnCode(status),
                    status == kIOReturnSuccess ? hex(std::span(bytes).first(shown)) : std::string());
    static_cast<void>(std::puts(line.c_str()));
    static_cast<void>(std::fflush(stdout));
}

// Evidence mode: documents the attach loop for 3 s first, and refuses if no attach happens.
bool recordLoopEvidence(PortReader &reader, HpmClient &client, const std::string &portName, const Snapshot &state)
{
    const double origin = monotonicSeconds();
    evidenceRead(client, kPortInfoRegister, "pre", origin);
    while (!stopRequested() && monotonicSeconds() - origin < kEvidencePreSeconds)
    {
        evidenceRead(client, kAttachStateRegister, "pre", origin);
        std::this_thread::sleep_for(kEvidencePrePoll);
    }
    if (reader.snapshot(portName).count <= state.count)
    {
        logWarning("reset refused: no attach loop observed in 3 seconds");
        return false;
    }
    return true;
}

void recordRestartEvidence(HpmClient &client, double origin)
{
    while (monotonicSeconds() - origin < kEvidencePostSeconds)
    {
        evidenceRead(client, kAttachStateRegister, "post", origin);
        evidenceRead(client, kModeRegister, "post", origin);
        evidenceRead(client, kPortInfoRegister, "post", origin);
        std::this_thread::sleep_for(kEvidencePostPoll);
    }
}

// Like Apple's own Gaid handling: wait up to a second for the controller to return to APP mode.
void waitForAppMode(HpmClient &client, double origin)
{
    bool app = false;
    for (int poll = 0; poll < kAppModePolls && !app; ++poll)
    {
        std::this_thread::sleep_for(kAppModePoll);
        app = client.inAppMode();
    }
    logInfo(
        std::format("controller mode {} {:.1f} s after Gaid", app ? "APP" : "not APP", monotonicSeconds() - origin));
}

} // namespace

IOReturn warmReset(PortReader &reader, const std::string &portName, const Snapshot &expected, bool evidence)
{
    // Recheck every guard immediately before opening a controller.
    const Snapshot state = reader.snapshot(portName, true);
    if (!state.eligible())
    {
        logWarning("reset refused: " + state.reason());
        return kIOReturnNotPermitted;
    }
    if (expected.portId != 0 && !sameTarget(state, expected))
    {
        logWarning("reset refused: port/controller registry entry changed");
        return kIOReturnNotPermitted;
    }
    const iokit::Object port = findPort(portName);
    if (!port)
    {
        return kIOReturnNoDevice;
    }
    const iokit::Object controller = parentController(port.get());
    if (!controller)
    {
        return kIOReturnNoDevice;
    }
    if (iokit::registryEntryId(controller.get()) != state.controllerId ||
        iokit::numberProperty(controller.get(), "RID") != state.rid)
    {
        logWarning("reset refused: controller changed");
        return kIOReturnNotPermitted;
    }
    IOReturn result = kIOReturnSuccess;
    std::optional<HpmClient> client = HpmClient::open(controller.get(), result);
    if (!client)
    {
        return result;
    }
    RegisterBytes mode{};
    std::uint64_t length = 0;
    result = client->read(kModeRegister, mode, length);
    if (result != kIOReturnSuccess || !HpmClient::isAppMode(mode, length))
    {
        const std::size_t shown = std::min<std::size_t>(length, mode.size());
        logWarning(std::format("reset refused: controller mode read status={} length={} mode={}", returnCode(result),
                               length, hex(std::span(mode).first(shown))));
        return kIOReturnNotPermitted;
    }
    if (evidence && !recordLoopEvidence(reader, *client, portName, state))
    {
        return kIOReturnNotPermitted;
    }
    const Snapshot last = reader.snapshot(portName, true);
    if (!last.eligible() || !sameTarget(last, state))
    {
        logWarning("reset refused: state changed before command (" +
                   (last.eligible() ? std::string("target moved") : last.reason()) + ")");
        return kIOReturnNotPermitted;
    }
    logInfo(std::format("sending one Gaid warm restart on {} RID {} (AppLoaded Count {}, ConnectionCount {})", portName,
                        state.rid, state.appLoaded, last.count));
    result = client->command(kGaidCommand);
    const double origin = monotonicSeconds();
    if (result != kIOReturnSuccess)
    {
        return result;
    }
    if (evidence)
    {
        recordRestartEvidence(*client, origin);
    }
    else
    {
        waitForAppMode(*client, origin);
    }
    return result;
}

bool verifyRecovery(PortReader &reader, const std::string &portName, std::int64_t appLoadedBefore, double resetStarted,
                    double verifySeconds)
{
    const auto polls = static_cast<int>(std::lround(verifySeconds / kVerifyPollSeconds));
    bool restarted = false;
    for (int poll = 0; !stopRequested() && poll < polls; ++poll)
    {
        std::this_thread::sleep_for(kVerifyPoll);
        const Snapshot after = reader.snapshot(portName, true);
        if (!restarted && appLoadedBefore >= 0 && after.appLoaded > appLoadedBefore)
        {
            restarted = true;
            logInfo(std::format("controller restart confirmed: AppLoaded Count {} -> {}", appLoadedBefore,
                                after.appLoaded));
        }
        if (after.dock)
        {
            logInfo(
                std::format("recovered: D6000 enumerated {:.1f} s after the reset", monotonicSeconds() - resetStarted));
            return true;
        }
    }
    if (!restarted)
    {
        logWarning("no AppLoaded Count increment observed");
    }
    logWarning(std::format("D6000 still absent after {:g} seconds", verifySeconds));
    return false;
}

std::string findLoopingPort(PortReader &reader)
{
    const std::vector<std::string> names = usbCPorts();
    std::map<std::string, Snapshot> before;
    for (const std::string &name : names)
    {
        before[name] = reader.snapshot(name);
    }
    std::this_thread::sleep_for(kLoopProbe);
    std::vector<std::string> looping;
    for (const std::string &name : names)
    {
        const Snapshot after = reader.snapshot(name);
        const Snapshot &first = before[name];
        if (first.eligible() && after.eligible() && sameTarget(first, after) &&
            after.count >= first.count + kLoopProbeAttaches)
        {
            looping.push_back(name);
        }
    }
    if (looping.size() == 1)
    {
        return looping.front();
    }
    logError(looping.empty() ? "no eligible USB-C port is in an attach loop"
                             : "more than one USB-C port is looping; choose one with --port");
    return {};
}

} // namespace d6000
