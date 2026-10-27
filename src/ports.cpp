// Copyright (c) d6000-dock-recovery contributors.
// SPDX-License-Identifier: Apache-2.0
#include "ports.hpp"

#include "clock.hpp"
#include "constants.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <sys/sysctl.h>

namespace d6000
{
namespace
{

// The validated controller: an ACE3 (vendor 0x28, device 0x02012024-27) booted into "APP ".
constexpr std::int64_t kAce3VendorId = 0x28;
constexpr std::int64_t kAce3FirstDeviceId = 0x02012024;
constexpr std::int64_t kAce3LastDeviceId = 0x02012027;
constexpr std::string_view kAppMode = "APP ";

// How far up the IOService plane to look for a port's controller and for a USB device's port.
constexpr int kMaxControllerDepth = 24;
constexpr int kMaxUsbDepth = 32;

// Cache lifetimes in seconds. A passing structure check is redone every minute and a failing one
// after 5 s, so a transient read (for example mid-restart) can't block a port for long. A present
// dock is re-confirmed every 30 s and an absent one every 2 s, because the dock's USB devices
// register a moment after its transports come up.
constexpr double kStructureRecheckSeconds = 60;
constexpr double kStructureRetrySeconds = 5;
constexpr double kDockPresentRecheckSeconds = 30;
constexpr double kDockAbsentRecheckSeconds = 2;
// Entries for ports that no longer exist are dropped once a cache grows past this.
constexpr std::size_t kMaxCacheEntries = 64;

constexpr std::size_t kModelBufferSize = 64;

// Counts the controller's descendants of a class (port interfaces, controller-chip HALs), or -1.
int descendantCount(io_registry_entry_t controller, const char *className)
{
    iokit::Object iterator;
    if (IORegistryEntryCreateIterator(controller, kIOServicePlane, kIORegistryIterateRecursively, iterator.put()) !=
        KERN_SUCCESS)
    {
        return -1;
    }
    int count = 0;
    while (const iokit::Object entry{IOIteratorNext(iterator.get())})
    {
        if (IOObjectConformsTo(entry.get(), className) != 0)
        {
            ++count;
        }
    }
    return count;
}

// The UsbIOPort path that a USB device hangs off, found by walking up from the device.
std::string owningPort(io_registry_entry_t device)
{
    iokit::Object current = iokit::Object::retain(device);
    for (int depth = 0; current && depth < kMaxUsbDepth; ++depth)
    {
        std::string path = iokit::stringProperty(current.get(), "UsbIOPort");
        if (!path.empty())
        {
            return path;
        }
        current = iokit::parent(current.get());
    }
    return {};
}

// True when a USB device whose product name contains "D6000" is enumerated behind this port.
bool dockOnPort(const std::string &portPath)
{
    bool found = false;
    iokit::forEachService("IOUSBHostDevice", [&](io_service_t device) {
        found = iokit::stringProperty(device, "USB Product Name").find("D6000") != std::string::npos &&
                owningPort(device) == portPath;
        return !found;
    });
    return found;
}

// Returns "" when the port has the structure this tool was validated on, else the first
// difference. Required: a USB-C AppleHPMInterfaceType10 port under an AppleHPMDeviceHALType3 for an
// ACE3 controller that booted into its "APP " firmware, on an AppleHPMARMSPMI controller with
// exactly one chip and serving only this port. A warm restart restarts the whole controller, so a
// controller shared with another port is refused.
std::string_view structureIssue(io_registry_entry_t port, io_registry_entry_t controller)
{
    if (IOObjectConformsTo(port, "AppleHPMInterfaceType10") == 0)
    {
        return "port is not an AppleHPMInterfaceType10 USB-C interface";
    }
    const iokit::Object hal = iokit::parent(port);
    if (!hal)
    {
        return "port has no controller HAL";
    }
    const std::int64_t device = iokit::numberProperty(hal.get(), "Device ID");
    if (IOObjectConformsTo(hal.get(), "AppleHPMDeviceHALType3") == 0)
    {
        return "controller HAL is not AppleHPMDeviceHALType3";
    }
    if (iokit::numberProperty(hal.get(), "Vendor ID") != kAce3VendorId || device < kAce3FirstDeviceId ||
        device > kAce3LastDeviceId)
    {
        return "controller is not an ACE3 (vendor 0x28, device 0x02012024-0x02012027)";
    }
    if (iokit::dataProperty(hal.get(), "HPM Mode Register on Boot") != kAppMode)
    {
        return "controller did not boot into APP firmware";
    }
    if (controller == IO_OBJECT_NULL || IOObjectConformsTo(controller, "AppleHPMARMSPMI") == 0)
    {
        return "controller is not AppleHPMARMSPMI";
    }
    // Register reads and commands address chip 0, which is only unambiguous with a single HAL.
    if (descendantCount(controller, "AppleHPMDeviceHAL") != 1)
    {
        return "controller has more than one controller chip";
    }
    if (descendantCount(controller, "AppleHPMInterface") != 1)
    {
        return "controller serves more than one port";
    }
    return {};
}

// The port's active transports. Unknown transports count as data, so they block recovery.
struct Transports
{
    bool known = false;
    bool data = true;
    bool usbLike = true; // could carry the dock's USB devices (anything but CC or DisplayPort)
    std::string list;    // comma-terminated names, used as a cache key
};

Transports readTransports(io_registry_entry_t port)
{
    Transports result;
    const iokit::CFObject active = iokit::property(port, "TransportsActive");
    if (!active || CFGetTypeID(active.get()) != CFArrayGetTypeID())
    {
        return result;
    }
    result.known = true;
    result.data = false;
    result.usbLike = false;
    const auto *const array = static_cast<CFArrayRef>(active.get());
    for (CFIndex index = 0; index < CFArrayGetCount(array); ++index)
    {
        const CFTypeRef transport = CFArrayGetValueAtIndex(array, index);
        result.list += iokit::toString(transport) + ",";
        if (CFEqual(transport, CFSTR("CC")) != 0)
        {
            continue;
        }
        result.data = true;
        if (CFEqual(transport, CFSTR("DisplayPort")) == 0)
        {
            result.usbLike = true;
        }
    }
    return result;
}

} // namespace

std::string Snapshot::reason() const
{
    if (!valid)
    {
        return "port or controller mapping unknown";
    }
    if (!isolated)
    {
        return "unsupported structure: " + structureProblem;
    }
    if (dock)
    {
        return "D6000 enumerated";
    }
    if (data)
    {
        return std::string(kDataTransportActive);
    }
    if (fault)
    {
        return "liquid or overcurrent reported";
    }
    return {};
}

Snapshot PortReader::snapshot(const std::string &portName, bool freshDock)
{
    Snapshot state;
    const iokit::Object port = findPort(portName);
    if (!port)
    {
        return state;
    }
    state.portPath = iokit::registryPath(port.get());
    state.portId = iokit::registryEntryId(port.get());
    const std::int64_t count = iokit::numberProperty(port.get(), "ConnectionCount");
    state.appLoaded = iokit::numberProperty(port.get(), "AppLoaded Count");
    const Transports transports = readTransports(port.get());
    state.data = transports.data;
    // A missing counter is unknown rather than a fault; a reported nonzero count blocks.
    state.fault = iokit::boolProperty(port.get(), "LDCM_LiquidDetected") ||
                  iokit::numberProperty(port.get(), "Overcurrent Count") > 0;
    const iokit::Object controller = parentController(port.get());
    if (controller)
    {
        state.rid = iokit::numberProperty(controller.get(), "RID");
        state.controllerId = iokit::registryEntryId(controller.get());
    }
    state.structureProblem = structureIssueCached(state.portId, state.controllerId, port.get(), controller.get());
    state.isolated = state.structureProblem.empty();
    state.valid = count >= 0 && state.rid >= 0 && transports.known && !state.portPath.empty();
    if (count >= 0)
    {
        state.count = static_cast<std::uint64_t>(count);
    }
    // An enumerated D6000 means a USB transport is active, so CC-only and display-only ports skip
    // the USB device scan. The cache can't enable a restart: any USB transport already blocks it.
    if (transports.usbLike)
    {
        state.dock = dockPresentCached(state.portId, state.count, transports.list, state.portPath, freshDock);
    }
    return state;
}

// A port's structure can't change while its registry entries exist, so the result is cached per
// port and controller entry. The restart path still reads the live controller mode before acting.
std::string PortReader::structureIssueCached(std::uint64_t portId, std::uint64_t controllerId, io_registry_entry_t port,
                                             io_registry_entry_t controller)
{
    if (structureCache_.size() > kMaxCacheEntries)
    {
        structureCache_.clear();
    }
    StructureEntry &cached = structureCache_[portId];
    const double now = monotonicSeconds();
    const double lifetime = cached.issue.empty() ? kStructureRecheckSeconds : kStructureRetrySeconds;
    if (cached.controllerId != controllerId || now - cached.checked >= lifetime)
    {
        cached = {.controllerId = controllerId, .checked = now, .issue = std::string(structureIssue(port, controller))};
    }
    return cached.issue;
}

// The dock scan result is reused while the port's ConnectionCount and active transports are
// unchanged: a present dock can only leave through one of those.
bool PortReader::dockPresentCached(std::uint64_t portId, std::uint64_t count, const std::string &transports,
                                   const std::string &portPath, bool fresh)
{
    if (dockCache_.size() > kMaxCacheEntries)
    {
        dockCache_.clear();
    }
    DockEntry &cached = dockCache_[portId];
    const double now = monotonicSeconds();
    const double lifetime = cached.dock ? kDockPresentRecheckSeconds : kDockAbsentRecheckSeconds;
    if (fresh || cached.count != count || cached.transports != transports || now - cached.checked >= lifetime)
    {
        cached = {.count = count, .transports = transports, .dock = dockOnPort(portPath), .checked = now};
    }
    return cached.dock;
}

std::vector<std::string> usbCPorts()
{
    std::vector<std::string> names;
    iokit::forEachService("AppleHPMInterface", [&](io_service_t entry) {
        std::string name = iokit::stringProperty(entry, "PortDescription");
        if (name.starts_with(kUsbCPortPrefix))
        {
            names.push_back(std::move(name));
        }
        return true;
    });
    std::ranges::sort(names);
    return names;
}

iokit::Object findPort(const std::string &portName)
{
    iokit::Object found;
    iokit::forEachService("AppleHPMInterface", [&](io_service_t entry) {
        if (iokit::stringProperty(entry, "PortDescription") != portName)
        {
            return true;
        }
        found = iokit::Object::retain(entry);
        return false;
    });
    return found;
}

iokit::Object parentController(io_registry_entry_t port)
{
    iokit::Object current = iokit::Object::retain(port);
    for (int depth = 0; current && depth < kMaxControllerDepth; ++depth)
    {
        if (IOObjectConformsTo(current.get(), "AppleHPM") != 0)
        {
            return current;
        }
        current = iokit::parent(current.get());
    }
    return {};
}

bool sameTarget(const Snapshot &first, const Snapshot &second) noexcept
{
    return first.portId == second.portId && first.controllerId == second.controllerId && first.rid == second.rid;
}

std::string machineModel()
{
    std::array<char, kModelBufferSize> model{};
    std::size_t size = model.size();
    if (sysctlbyname("hw.model", model.data(), &size, nullptr, 0) != 0)
    {
        return {};
    }
    return model.data();
}

std::string validatedModelList()
{
    std::string list;
    for (std::size_t index = 0; index < kValidatedModels.size(); ++index)
    {
        if (index != 0)
        {
            list += index + 1 == kValidatedModels.size() ? " and " : ", ";
        }
        list += kValidatedModels.at(index);
    }
    return list;
}

bool validatedModel(std::string_view model)
{
    return std::ranges::find(kValidatedModels, model) != kValidatedModels.end();
}

bool appleSilicon()
{
    int arm64 = 0;
    std::size_t size = sizeof(arm64);
    return sysctlbyname("hw.optional.arm64", &arm64, &size, nullptr, 0) == 0 && arm64 == 1;
}

} // namespace d6000
