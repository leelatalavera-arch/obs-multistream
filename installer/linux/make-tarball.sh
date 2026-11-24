#!/usr/bin/env bash
# Builds the per-user Linux .tar.gz (no root needed to install).
# Run after the CMake build, from the project root:
#   installer/linux/make-tarball.sh <build-dir> <config>
# e.g. installer/linux/make-tarball.sh build_x86_64 RelWithDebInfo
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="${1:-build_x86_64}"
CONFIG="${2:-RelWithDebInfo}"
NAME="$(jq -r .name "$ROOT/buildspec.json")"
VERSION="$(jq -r .version "$ROOT/buildspec.json")"
ARCH="$(uname -m)"

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

# Install into a throwaway prefix, then rearrange into OBS's per-user layout:
#   ~/.config/obs-studio/plugins/<name>/bin/64bit/<name>.so
#   ~/.config/obs-studio/plugins/<name>/data/...
cmake --install "$ROOT/$BUILD_DIR" --config "$CONFIG" --prefix "$STAGE/prefix" > /dev/null
SO="$(find "$STAGE/prefix" -name "$NAME.so" | head -1)"
DATA="$(find "$STAGE/prefix" -type d -path "*/obs-plugins/$NAME" | grep share | head -1)"
[ -n "$SO" ] || { echo "plugin .so not found" >&2; exit 1; }

PKG="$STAGE/$NAME"
mkdir -p "$PKG/plugin/$NAME/bin/64bit" "$PKG/plugin/$NAME/data"
cp "$SO" "$PKG/plugin/$NAME/bin/64bit/"
[ -n "$DATA" ] && cp -r "$DATA/." "$PKG/plugin/$NAME/data/"
cp "$ROOT/LICENSE" "$PKG/LICENSE.txt"
cp "$ROOT/installer/linux/install.sh" "$ROOT/installer/linux/uninstall.sh" "$PKG/"
cp "$ROOT/installer/linux/HOW-TO-INSTALL.txt" "$PKG/"
chmod +x "$PKG/install.sh" "$PKG/uninstall.sh"

mkdir -p "$ROOT/release"
OUT="$ROOT/release/$NAME-$VERSION-linux-$ARCH.tar.gz"
tar -C "$STAGE" -czf "$OUT" "$NAME"
echo "tarball: $OUT"
