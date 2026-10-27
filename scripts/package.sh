#!/bin/bash
# Copyright (c) d6000-dock-recovery contributors.
# SPDX-License-Identifier: Apache-2.0
#
# Bundles a release build into dist/d6000-recovery-macos-arm64.tar.gz plus a .sha256 file:
#
#   cmake --workflow --preset release && scripts/package.sh
#
# The archive holds d6000-recovery/ with bin/d6000-recovery, install.sh, uninstall.sh and the
# README, LICENSE, NOTICE and THIRD-PARTY-NOTICES.txt files.
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${1:-$root/build/release}"
readonly asset=d6000-recovery-macos-arm64.tar.gz
dist="$root/dist"
stage="$dist/d6000-recovery"

if [[ ! -x "$build_dir/d6000-recovery" ]]; then
    echo "No release build in $build_dir. Run: cmake --workflow --preset release" >&2
    exit 1
fi
rm -rf "$dist"
mkdir -p "$stage/bin"
install -m 755 "$build_dir/d6000-recovery" "$stage/bin/d6000-recovery"
install -m 755 "$root/scripts/install.sh" "$root/scripts/uninstall.sh" "$stage/"
install -m 644 "$root/README.md" "$root/LICENSE" "$root/NOTICE" "$root/THIRD-PARTY-NOTICES.txt" "$stage/"
tar -C "$dist" --uid 0 --gid 0 --uname root --gname wheel -czf "$dist/$asset" d6000-recovery
(cd "$dist" && shasum -a 256 "$asset" >"$asset.sha256")
cat "$dist/$asset.sha256"
