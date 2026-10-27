# How it works

## The failure

On some plug-ins, the Mac's USB-C port controller briefly takes the power-source role. The dock
then tries to swap roles, the Mac sees that as a disconnect, and the Mac keeps the port idle for
about half a second before trying again. That is longer than the dock is willing to wait, so both
sides start over at the same moment, about once a second, and never get out of it. The dock's
light stays amber.

Restarting the Mac's controller for that one port while the dock stays plugged in breaks the loop:
the next attach negotiates normally. The restart is the controller's "Gaid" warm-restart command,
sent through Apple's `AppleHPMLib` user client.

This is a workaround. The behaviour lives in the controller firmware, which this tool can't and
doesn't change.

## Detect

The watcher reads every watched USB-C port every 250 ms, using only properties macOS already
publishes in the IORegistry. It doesn't talk to the controller while watching.

Each port's `ConnectionCount` rises by one on every attach. A port is treated as stuck when the
count rises at least **3 times** within a **2.5 second** window that the watcher observed in full,
while no D6000 enumerates on it and every guard below passes. Only rises seen between two polls
inside the window count, so an attach just before the window never does. A healthy plug-in
attaches once and a fumbled replug twice, so neither can trigger it.

Polls are 250 ms apart plus the time each pass takes. With polls up to 300 ms apart, the rule
always catches a loop whose attaches are up to (window − 0.3 s) / (attaches − 1) apart, and never
catches one whose attaches are window / (attaches − 1) or more apart:

| Settings | Always caught | Never caught |
|---|---|---|
| Default: 3 attaches in 2.5 s | attaches up to 1.1 s apart | 1.25 s apart or more |
| `--detect-window 3` | up to 1.35 s apart | 1.5 s apart or more |

On the Macs tested so far, a stuck loop re-attached every 0.65 to 0.92 s. If your log shows
a slower rhythm that isn't caught, widen the window and please report it.

## Act

Before sending anything, the watcher checks every guard again with fresh readings, confirms the
port and controller are the same registry entries it saw a moment ago, opens the controller,
confirms its firmware mode register reads `APP `, checks once more, and only then sends exactly one
restart. Any change in between cancels it, and the log says why.

## Verify

After the restart it waits up to a second for the controller to report `APP ` again, logs whether
the controller really restarted (its `AppLoaded Count` rises), and then waits up to 10 seconds for
the D6000 to enumerate. A restart counts as failed if the dock doesn't appear in that time.

## Guards

Every one of these must pass before a restart:

- **Apple silicon**, and the port's controller has the tested structure: a USB-C
  `AppleHPMInterfaceType10` port under an `AppleHPMDeviceHALType3` for an ACE3 controller (vendor
  `0x28`, device `0x02012024`–`0x02012027`) that booted into its `APP ` firmware, on an
  `AppleHPMARMSPMI` controller with exactly one chip that serves only this port. The restart resets
  the whole controller, so a controller shared with another port is never touched. `--list` shows
  this as `structure=ok`.
- **No D6000 enumerated** on the port.
- **No data transport active**: no USB, DisplayPort or Thunderbolt connection on the port.
- **No liquid detection or overcurrent** reported for the port.
- **Settled**: not within 10 seconds of the watcher starting, the Mac waking, or the watcher stalling.

## Limits

- At most one restart every 30 seconds per port, and the same spacing across all ports.
- At most 6 restarts an hour per port, and 6 across all ports.
- After 2 restarts in a row that don't bring the dock back, the port is left alone for 30 minutes.
- The limits survive restarts of the watcher: they are saved to `/Library/Logs/D6000Recovery/state`
  (atomically, with `fsync`) around every restart.

Every number here can be changed within the limits in [Configuration](configuration.md).

## Logging

The service writes `/Library/Logs/D6000Recovery/events.log`. At 5 MB it moves the contents to
`events.1.log` (replacing any older copy) and starts a fresh file. Each line is
`<UTC time> <level> <message>`:

| Level | What it adds |
|---|---|
| `error` | the command failed or can't run |
| `warning` | a restart was refused or didn't bring the dock back, or a limit was reached |
| `info` (default) | every attach with its timing, enumerations, restarts and their outcome, blocked reasons |
| `debug` | every guard change on every port and each save of the state file |

A port that logs more than 10 attaches in a minute is summarized ("N more attaches in the last
minute") instead of logged line by line.

## What it never does

It never writes firmware or controller registers, never unlocks debug modes, never touches a port
whose structure doesn't match, and never changes security, boot or network settings. The only
command it sends is the one warm restart.
