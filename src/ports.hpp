// Copyright (c) d6000-dock-recovery contributors.
// SPDX-License-Identifier: Apache-2.0
//
// Read-only view of the Mac's USB-C ports: which ports exist, what state each is in, and whether
// its controller has the structure this tool was validated on.
#ifndef D6000_PORTS_HPP
#define D6000_PORTS_HPP

#include "iokit.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace d6000
{

// Blocking reason reported while a port carries USB, DisplayPort or Thunderbolt traffic. The
// watcher doesn't log it as "recovery blocked" because it is the normal state of a working port.
inline constexpr std::string_view kDataTransportActive = "data transport active";

// One port's state from cached registry properties. eligible() is true only when every guard
// passes; reason() names the first guard that fails.
struct Snapshot
{
    bool valid = false;       // the port, its controller and its transports were all readable
    bool isolated = false;    // the controller has the validated structure (see structureProblem)
    bool dock = false;        // a D6000 USB device is enumerated behind this port
    bool data = false;        // a transport other than CC is active
    bool fault = true;        // liquid detection or overcurrent is reported
    std::uint64_t count = 0;  // ConnectionCount: rises by one on every attach
    std::uint64_t portId = 0; // registry IDs, to detect a re-created port or controller
    std::uint64_t controllerId = 0;
    std::int64_t rid = -1;       // the controller's RID
    std::int64_t appLoaded = -1; // AppLoaded Count: rises when the controller restarts
    std::string portPath;
    std::string structureProblem;

    [[nodiscard]] std::string reason() const;
    [[nodiscard]] bool eligible() const
    {
        return reason().empty();
    }
};

// Reads port snapshots. The two costly parts of a poll are cached per port: the structure check
// (a recursive registry walk) and the scan of every USB device for a D6000. Neither cache can
// enable a restart on its own; see snapshot() in ports.cpp for why.
class PortReader
{
  public:
    // Cheap enough to call every 250 ms per port. freshDock bypasses the dock cache; it is used
    // right before a restart and while verifying one.
    [[nodiscard]] Snapshot snapshot(const std::string &portName, bool freshDock = false);

  private:
    struct StructureEntry
    {
        std::uint64_t controllerId = 0;
        double checked = -1e18;
        std::string issue;
    };
    struct DockEntry
    {
        std::uint64_t count = UINT64_MAX;
        std::string transports;
        bool dock = false;
        double checked = -1e18;
    };

    [[nodiscard]] std::string structureIssueCached(std::uint64_t portId, std::uint64_t controllerId,
                                                   io_registry_entry_t port, io_registry_entry_t controller);
    [[nodiscard]] bool dockPresentCached(std::uint64_t portId, std::uint64_t count, const std::string &transports,
                                         const std::string &portPath, bool fresh);

    std::map<std::uint64_t, StructureEntry> structureCache_;
    std::map<std::uint64_t, DockEntry> dockCache_;
};

// Every built-in USB-C port by name, sorted. MagSafe and other interface types are never watched.
[[nodiscard]] std::vector<std::string> usbCPorts();

// The AppleHPMInterface registry entry for a port name such as "Port-USB-C@3", or empty.
[[nodiscard]] iokit::Object findPort(const std::string &portName);

// The port's controller: the first ancestor in the IOService plane that is an AppleHPM.
[[nodiscard]] iokit::Object parentController(io_registry_entry_t port);

// True when both snapshots name the same port and controller registry entries, so the target
// can't have been swapped (replug, driver restart) between a check and a command.
[[nodiscard]] bool sameTarget(const Snapshot &first, const Snapshot &second) noexcept;

// Models where recovery has been confirmed end to end. Any other Apple-silicon Mac is allowed
// only on ports whose controller structure matches, and is logged as not yet validated.
inline constexpr std::array<std::string_view, 2> kValidatedModels{"Mac15,7", "Mac17,9"};

// The validated models as "Mac15,7 and Mac17,9", for messages.
[[nodiscard]] std::string validatedModelList();

[[nodiscard]] std::string machineModel();
[[nodiscard]] bool validatedModel(std::string_view model);
[[nodiscard]] bool appleSilicon();

} // namespace d6000

#endif // D6000_PORTS_HPP
