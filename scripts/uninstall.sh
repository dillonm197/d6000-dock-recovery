#!/bin/bash
# Copyright (c) d6000-dock-recovery contributors.
# SPDX-License-Identifier: Apache-2.0
#
# Removes the d6000-recovery watcher: unloads the LaunchDaemon and deletes its plist, the helper
# and its rate-limit state. The logs in /Library/Logs/D6000Recovery are kept.
#
#   sudo ./uninstall.sh
#   curl -fsSL https://raw.githubusercontent.com/dillonm197/d6000-dock-recovery/main/scripts/uninstall.sh | sudo bash
set -euo pipefail

readonly label=local.d6000-recovery
if ((EUID != 0)); then
    echo "Run with sudo to remove the privileged helper." >&2
    exit 1
fi
/bin/launchctl bootout "system/$label" 2>/dev/null || true
/bin/rm -f "/Library/LaunchDaemons/$label.plist"
/bin/rm -f "/Library/PrivilegedHelperTools/$label"
/bin/rm -f /Library/Logs/D6000Recovery/state /Library/Logs/D6000Recovery/state.tmp
echo "Automatic recovery removed. Logs are kept in /Library/Logs/D6000Recovery."
