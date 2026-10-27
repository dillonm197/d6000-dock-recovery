#!/bin/bash
# Copyright (c) d6000-dock-recovery contributors.
# SPDX-License-Identifier: Apache-2.0
#
# Installs the d6000-recovery watcher as a system LaunchDaemon. Re-running it upgrades in place.
#
#   sudo ./install.sh --all-ports [settings]   watch every built-in USB-C port (recommended)
#   sudo ./install.sh --port N [settings]      watch only Port-USB-C@N (numbers from --list)
#
# Settings are the helper's own flags, each with a value: --detect-window, --detect-attaches,
# --verify-timeout, --cooldown, --max-resets-per-hour, --failure-pause and --log-level (see
# docs/configuration.md). The preflight validates them before anything is installed.
#
# It uses the helper next to this script (bin/d6000-recovery in a release download) or, in a
# source checkout, build/release/d6000-recovery. With --port N the D6000 must be connected and
# working on that port; --all-ports only needs one compatible USB-C port.
#
# Steps: a read-only preflight (--check) runs as you; the helper is copied to
# /Library/PrivilegedHelperTools/local.d6000-recovery (root-owned, never run by this script); the
# job is written to /Library/LaunchDaemons/local.d6000-recovery.plist and loaded. The log is
# /Library/Logs/D6000Recovery/events.log. Nothing else is installed: no kernel extension, driver,
# firmware or security change.
set -euo pipefail

readonly label=local.d6000-recovery
readonly helper_dir=/Library/PrivilegedHelperTools
readonly helper_path=$helper_dir/$label
readonly plist_path=/Library/LaunchDaemons/$label.plist
readonly log_dir=/Library/Logs/D6000Recovery
readonly uninstall_url=https://raw.githubusercontent.com/dillonm197/d6000-dock-recovery/main/scripts/uninstall.sh

usage() {
    echo "Usage: sudo $0 --all-ports | --port N [--detect-window S] [--detect-attaches N] [--verify-timeout S]" >&2
    echo "       [--cooldown S] [--max-resets-per-hour N] [--failure-pause S] [--log-level LEVEL]" >&2
    exit 2
}

case "${1:-}" in
--all-ports)
    target_args=(--all-ports)
    check_args=()
    target_text="every USB-C port"
    shift
    ;;
--port)
    if [[ $# -lt 2 || ! "$2" =~ ^[1-9][0-9]?$ ]] || (($2 > 32)); then usage; fi
    target_args=(--port "$2")
    check_args=(--require-dock)
    target_text="USB-C port $2"
    shift 2
    ;;
*) usage ;;
esac

# Only settings may follow the target; anything else would make the service refuse to start.
settings=()
while (($# > 0)); do
    case "$1" in
    --detect-window | --detect-attaches | --verify-timeout | --cooldown | --max-resets-per-hour | --failure-pause | --log-level)
        if (($# < 2)); then usage; fi
        settings+=("$1" "$2")
        shift 2
        ;;
    *) usage ;;
    esac
done

if ((EUID != 0)); then
    echo "Run with sudo to install the privileged recovery helper." >&2
    exit 1
fi

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if [[ -x "$script_dir/bin/d6000-recovery" ]]; then
    helper="$script_dir/bin/d6000-recovery"
elif [[ -x "$script_dir/../build/release/d6000-recovery" ]]; then
    helper="$(cd -- "$script_dir/../build/release" && pwd)/d6000-recovery"
else
    echo "No d6000-recovery helper found. Download a release, or build one with: cmake --workflow --preset release" >&2
    exit 1
fi

# A browser download marks files as quarantined, and Gatekeeper then blocks the helper, which is
# not notarized. curl downloads (as in the README) aren't marked.
if /usr/bin/xattr -p com.apple.quarantine "$helper" >/dev/null 2>&1; then
    echo "$helper is quarantined because it was downloaded with a browser." >&2
    echo "Download the release with curl as shown in the README instead." >&2
    exit 1
fi

if [[ -L "$helper_path" || -L "$plist_path" || -L "$log_dir" || -L "$helper_dir" ]]; then
    echo "Refusing to install over a symbolic link." >&2
    exit 1
fi

# The preflight runs as the invoking user from where the helper already is. --check is read-only
# (no controller commands), and it validates the settings with the same rules the service uses.
preflight=("$helper" --check "${target_args[@]}" ${check_args[@]+"${check_args[@]}"} ${settings[@]+"${settings[@]}"})
if ((${SUDO_UID:-0} != 0)); then
    /usr/bin/sudo -u "#${SUDO_UID}" "${preflight[@]}"
else
    "${preflight[@]}"
fi

[[ -d "$helper_dir" ]] || /usr/bin/install -d -o root -g wheel -m 755 "$helper_dir"
/bin/launchctl bootout "system/$label" 2>/dev/null || true
# The helper is copied into place, never run from there by this script; launchd is the only thing
# that executes it.
/usr/bin/install -o root -g wheel -m 755 "$helper" "$helper_path"
/usr/bin/install -d -o root -g wheel -m 755 "$log_dir"

program_arguments=("$helper_path" --watch "${target_args[@]}" --log-file "$log_dir/events.log" ${settings[@]+"${settings[@]}"})
umask 022
{
    echo '<?xml version="1.0" encoding="UTF-8"?>'
    echo '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">'
    echo '<plist version="1.0"><dict>'
    echo "<key>Label</key><string>$label</string>"
    echo '<key>ProgramArguments</key><array>'
    printf '<string>%s</string>\n' "${program_arguments[@]}"
    echo '</array>'
    echo '<key>RunAtLoad</key><true/>'
    # Relaunch only after a failure: permanent refusals exit 0 and are not retried every 30 s.
    echo '<key>KeepAlive</key><dict><key>SuccessfulExit</key><false/></dict>'
    echo '<key>ThrottleInterval</key><integer>30</integer>'
    # The helper writes events.log itself; this only catches output from outside the logger.
    echo "<key>StandardOutPath</key><string>$log_dir/launchd.log</string>"
    echo "<key>StandardErrorPath</key><string>$log_dir/launchd.log</string>"
    echo '</dict></plist>'
} >"$plist_path"
/usr/sbin/chown root:wheel "$plist_path"
/bin/chmod 644 "$plist_path"
/usr/bin/plutil -lint "$plist_path" >/dev/null

# launchd can briefly refuse to load a job it is still tearing down ("Bootstrap failed: 5").
loaded=false
for _ in 1 2 3 4 5; do
    if /bin/launchctl bootstrap system "$plist_path" 2>/dev/null; then
        loaded=true
        break
    fi
    sleep 1
done
if ! $loaded; then
    echo "Installed, but launchd would not load the job. Run: sudo launchctl bootstrap system $plist_path" >&2
    exit 1
fi

echo "Automatic recovery installed for $target_text ($("$helper_path" --version))."
echo "Log: $log_dir/events.log"
echo "Remove it with: curl -fsSL $uninstall_url | sudo bash"
