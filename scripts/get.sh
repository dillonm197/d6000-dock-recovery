#!/bin/bash
# Copyright (c) d6000-dock-recovery contributors.
# SPDX-License-Identifier: Apache-2.0
#
# Downloads a release, checks its SHA-256 checksum and runs its installer:
#
#   curl -fsSL https://raw.githubusercontent.com/dillonm197/d6000-dock-recovery/main/scripts/get.sh | bash -s -- --all-ports
#
# Arguments go to install.sh (target and settings). D6000_VERSION=v0.7.0 picks a release; the
# default is the latest. curl doesn't mark downloads as quarantined, so Gatekeeper doesn't block
# the helper.
set -euo pipefail

readonly repo=dillonm197/d6000-dock-recovery
readonly asset=d6000-recovery-macos-arm64.tar.gz
version=${D6000_VERSION:-latest}
if [[ "$version" == latest ]]; then
    base="https://github.com/$repo/releases/latest/download"
else
    base="https://github.com/$repo/releases/download/$version"
fi

if [[ "$(uname -s)" != Darwin || "$(uname -m)" != arm64 ]]; then
    echo "d6000-recovery needs an Apple-silicon Mac." >&2
    exit 1
fi
if (($# == 0)); then
    set -- --all-ports
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
echo "Downloading $asset ($version)..."
curl -fsSL --proto '=https' --tlsv1.2 -o "$work/$asset" "$base/$asset"
curl -fsSL --proto '=https' --tlsv1.2 -o "$work/$asset.sha256" "$base/$asset.sha256"
(cd "$work" && shasum -a 256 -c "$asset.sha256")
tar -xzf "$work/$asset" -C "$work"
echo "Installing; sudo asks for your password."
sudo "$work/d6000-recovery/install.sh" "$@"
