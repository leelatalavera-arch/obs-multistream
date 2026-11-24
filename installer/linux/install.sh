#!/usr/bin/env bash
# Installs Multistream for OBS for the current user only (no sudo needed).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
NAME="obs-multistream"
DEST="${XDG_CONFIG_HOME:-$HOME/.config}/obs-studio/plugins"

if pgrep -x obs > /dev/null 2>&1; then
  echo "Please close OBS first, then run this again."
  exit 1
fi
mkdir -p "$DEST"
rm -rf "${DEST:?}/$NAME"
cp -r "$HERE/plugin/$NAME" "$DEST/"
echo "Installed to $DEST/$NAME"
echo "Start OBS. The Multistream dock opens the first time (later: Docks > Multistream)."
echo "Using the Flatpak version of OBS? This package won't load there; see HOW-TO-INSTALL.txt."
