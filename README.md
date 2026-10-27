# D6000 dock recovery for Apple-silicon Macs

[![CI](https://github.com/dillonm197/d6000-dock-recovery/actions/workflows/ci.yml/badge.svg)](https://github.com/dillonm197/d6000-dock-recovery/actions/workflows/ci.yml)
[![License: Apache-2.0](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)

Some Dell D6000 and D6000S docks get stuck on Apple-silicon Macs: the light stays amber, the Mac
doesn't charge, and Ethernet, USB and displays never come up. Replugging only works sometimes.

`d6000-recovery` runs in the background, notices a stuck dock within about 2 seconds and restarts
that USB-C port's controller, which brings the dock back a few seconds later. You don't need Xcode
or any developer tools to install it.

> [!NOTE]
> This is an independent workaround, not a Dell or Apple product, and it is still experimental.
> It runs as root and sends one controller restart command, guarded by the checks described in
> [How it works](docs/how-it-works.md).

## Does it apply to my Mac?

- **Mac:** Apple silicon. Tested end to end on a MacBook Pro with M3 Pro (`Mac15,7`) and on a
  `Mac17,9`, both on macOS 26. On any other model the tool checks each USB-C port and only acts on
  ports whose controller layout matches the tested one.
- **Symptom:** the dock's light stays amber and nothing on the dock works. While it's stuck, the
  port re-attaches about once a second, which you can see in the log below.

## Install

```bash
curl -fsSL https://raw.githubusercontent.com/dillonm197/d6000-dock-recovery/main/scripts/get.sh | bash
```

This downloads the latest release, checks its SHA-256 checksum and installs it for every USB-C
port. `sudo` asks for your password. The tool then starts automatically at every boot.

To look before you install, do the same steps by hand:

```bash
base=https://github.com/dillonm197/d6000-dock-recovery/releases/latest/download
curl -fsSLO "$base/d6000-recovery-macos-arm64.tar.gz"
curl -fsSLO "$base/d6000-recovery-macos-arm64.tar.gz.sha256"
shasum -a 256 -c d6000-recovery-macos-arm64.tar.gz.sha256
tar -xzf d6000-recovery-macos-arm64.tar.gz
cd d6000-recovery
sudo ./install.sh --all-ports
```

Download with `curl`, not a browser. Browsers mark downloads as quarantined, and macOS then blocks
the helper because it isn't notarized by Apple. The installer stops and tells you if that happened.

Each release is built by GitHub Actions and has a signed build provenance attestation. With the
[GitHub CLI](https://cli.github.com) you can check that the archive came from this repository's
release workflow:

```bash
gh attestation verify d6000-recovery-macos-arm64.tar.gz --repo dillonm197/d6000-dock-recovery
```

## Check that it works

```bash
tail -f /Library/Logs/D6000Recovery/events.log
```

Every plug-in is logged with its timing. When a dock gets stuck, a recovery looks like this:

```text
2026-10-27T21:06:09Z info Port-USB-C@1: attach #448, 0.91 s after previous; dock=no, recovery eligible
2026-10-27T21:06:10Z info Port-USB-C@1: attach #449, 0.91 s after previous; dock=no, recovery eligible
2026-10-27T21:06:10Z info sending one Gaid warm restart on Port-USB-C@1 RID 0 (AppLoaded Count 5, ConnectionCount 449)
2026-10-27T21:06:10Z info controller mode APP 0.3 s after Gaid
2026-10-27T21:06:10Z info Port-USB-C@1: warm-restart command return=0x00000000
2026-10-27T21:06:11Z info controller restart confirmed: AppLoaded Count 5 -> 6
2026-10-27T21:06:12Z info recovered: D6000 enumerated 2.0 s after the reset
```

To see each port's state right now:

```bash
/Library/PrivilegedHelperTools/local.d6000-recovery --list
```

## What it does, and what it won't do

- It reads each USB-C port's state from macOS 4 times a second. While it is only watching it
  doesn't talk to any hardware, and it uses well under 1% of one CPU core.
- It acts only when a port re-attaches 3 times within 2 seconds with no dock showing up and no USB,
  display or Thunderbolt connection active on it, and only if that port's controller serves that
  one port. A normal plug-in or a fumbled replug never triggers it.
- It then sends one restart to that port's controller. It never writes firmware, never changes
  security, boot or network settings, and never touches another port.
- It restarts a port at most once every 30 seconds and 6 times an hour. If 2 restarts in a row
  don't bring the dock back, it leaves that port alone for 30 minutes.

All of these numbers can be changed within safe limits; see [Configuration](docs/configuration.md).

## Uninstall

```bash
curl -fsSL https://raw.githubusercontent.com/dillonm197/d6000-dock-recovery/main/scripts/uninstall.sh | sudo bash
```

This removes the background service and the helper. The logs in `/Library/Logs/D6000Recovery` are
kept.

## Troubleshooting

- **A port doesn't show `structure=ok` in `--list`.** That port's controller layout is different
  from the tested one, so the tool won't act on it. Please
  [open a compatibility report](https://github.com/dillonm197/d6000-dock-recovery/issues/new?template=compatibility_report.yml)
  with the `--list` output.
- **The dock stayed amber and the log shows no restart.** Look at the attach lines for that port.
  If they are more than about 1 second apart, the loop is slower than the default detection window
  catches; try `--detect-window 2.5` (see [Configuration](docs/configuration.md)) and please report
  it.
- **It restarted a port when nothing was wrong.** Please
  [report the false positive](https://github.com/dillonm197/d6000-dock-recovery/issues/new?template=false_positive.yml)
  with the log lines around it.
- **The installer printed `Killed: 9`.** Security software on the Mac stopped the helper. Send the
  installer output in a bug report.
- **To fix a stuck dock once by hand:** `sudo /Library/PrivilegedHelperTools/local.d6000-recovery --recover --all-ports`
- **To upgrade:** run the install command again.

## Help improve it: share your logs

The tool has only been tested on a few Macs, so logs from other models and docks are the most
useful contribution, whether it worked or not. Use one of the
[issue forms](https://github.com/dillonm197/d6000-dock-recovery/issues/new/choose); they ask for:

```bash
sysctl -n hw.model
sw_vers -productVersion
/Library/PrivilegedHelperTools/local.d6000-recovery --version
/Library/PrivilegedHelperTools/local.d6000-recovery --list
tail -n 200 /Library/Logs/D6000Recovery/events.log
```

The log holds port names, counters, controller IDs and timings. It contains no names, serial
numbers or network details, but please read it before posting.

## Build from source

You need the Xcode Command Line Tools (`xcode-select --install`) and CMake 3.28 or later
(`brew install cmake`).

```bash
cmake --workflow --preset release
sudo scripts/install.sh --all-ports
```

[CONTRIBUTING.md](CONTRIBUTING.md) covers the debug build, tests and lint.

## More

- [How it works](docs/how-it-works.md): the failure, the detection rule, every guard and the log format
- [Configuration](docs/configuration.md): every setting, its limits and how to change it
- [Contributing](CONTRIBUTING.md) · [Support](SUPPORT.md) · [Security](SECURITY.md) ·
  [Code of conduct](CODE_OF_CONDUCT.md) · [Changelog](CHANGELOG.md)

## Trademarks

Dell and D6000 are trademarks of Dell Inc. Apple, Mac, MacBook Pro and macOS are trademarks of
Apple Inc. This project isn't affiliated with or endorsed by Dell or Apple.

## License

[Apache License 2.0](LICENSE). See [NOTICE](NOTICE) and
[THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt) for the code this project includes from others.
