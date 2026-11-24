#!/usr/bin/env bash
# Removes Multistream for OBS installed with install.sh (current user).
set -euo pipefail
NAME="obs-multistream"
CFG="${XDG_CONFIG_HOME:-$HOME/.config}/obs-studio"

rm -rf "${CFG:?}/plugins/$NAME"
echo "Removed the plugin from $CFG/plugins/$NAME"

read -r -p "Also delete your saved platforms and stream keys? [y/N] " answer
if [[ "${answer:-N}" =~ ^[Yy]$ ]]; then
  find "$CFG/basic/profiles" -maxdepth 2 -name "$NAME.json" -print -delete 2>/dev/null || true
  echo "Saved setups deleted."
else
  echo "Saved setups kept (in each OBS profile folder as $NAME.json)."
fi
