# Configuration

The defaults are the tuning tested on real docks, and most people should keep them. Each setting
can be changed within hard limits that keep any combination safe to run unattended. The values in
use are logged when the service starts:

```text
info settings: 3 attaches within 2.5 s, dock wait 10 s, cooldown 30 s, at most 6 restarts per hour, 1800 s pause after 2 failed restarts in a row
```

## Settings

| Flag | Range | Default | What it changes |
|---|---|---|---|
| `--detect-window SECONDS` | 1.5–5 | 2.5 | How long a window must contain the attaches. Wider catches slower loops but reacts later. |
| `--detect-attaches N` | 3–6 | 3 | Attaches without the dock needed to act. Higher is more cautious but slower. |
| `--verify-timeout SECONDS` | 5–60 | 10 | How long to wait for the dock after a restart before counting it as failed. |
| `--cooldown SECONDS` | 10–600 | 30 | Minimum time between restarts, per port and across all ports. |
| `--max-resets-per-hour N` | 1–20 | 6 | Restarts allowed in any hour, per port and across all ports. |
| `--failure-pause SECONDS` | 60–7200 | 1800 | How long a port is left alone after 2 restarts in a row fail. |
| `--log-level LEVEL` | `error`, `warning`, `info`, `debug` | `info` | How much the log records; see [Logging](how-it-works.md#logging). |

[How it works](how-it-works.md#detect) shows which loop speeds a given window and attach count
catch.

## Changing them

Settings go after the target when you install, and they are written into the service:

```bash
sudo ./install.sh --all-ports --detect-window 3 --log-level debug
```

With the one-line installer, pass them after `bash -s --`:

```bash
curl -fsSL https://raw.githubusercontent.com/dillonm197/d6000-dock-recovery/main/scripts/get.sh | bash -s -- --all-ports --detect-window 3
```

To change them later, run the install command again with the new values. Leaving a setting out
puts it back to its default.

The installer checks the values before installing anything:

```bash
d6000-recovery --check --all-ports --detect-window 3
```

`d6000-recovery --help` lists every flag with its range and default.
