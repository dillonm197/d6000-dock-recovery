// Copyright (c) d6000-dock-recovery contributors.
// SPDX-License-Identifier: Apache-2.0
//
// The only code that talks to a USB-C controller: one warm restart ("Gaid") after every guard has
// been re-checked, and the read-only checks around it.
#ifndef D6000_CONTROLLER_HPP
#define D6000_CONTROLLER_HPP

#include "ports.hpp"

#include <IOKit/IOReturn.h>

#include <cstdint>
#include <string>

namespace d6000
{

// Sends one Gaid warm restart to the port's controller after re-checking every guard. When
// expected has registry IDs, the current port and controller must be the same entries. It first
// confirms the controller runs its "APP " firmware, then afterwards waits briefly for APP mode to
// return. In evidence mode it instead records 3 s of the attach loop before the restart (and refuses
// if ConnectionCount doesn't rise) and 6 s of registers after it, as JSON lines on stdout.
// Returns the command result, or kIOReturnNotPermitted after logging why it refused.
[[nodiscard]] IOReturn warmReset(PortReader &reader, const std::string &portName, const Snapshot &expected,
                                 bool evidence);

// Polls every 250 ms for up to verifySeconds and returns true once a D6000 enumerates on the port.
// It also logs whether the controller restarted (its AppLoaded Count rose).
[[nodiscard]] bool verifyRecovery(PortReader &reader, const std::string &portName, std::int64_t appLoadedBefore,
                                  double resetStarted, double verifySeconds);

// The one eligible USB-C port that attaches at least twice in three seconds, or empty (logged).
[[nodiscard]] std::string findLoopingPort(PortReader &reader);

} // namespace d6000

#endif // D6000_CONTROLLER_HPP
