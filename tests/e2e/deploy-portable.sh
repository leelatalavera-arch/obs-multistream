#!/usr/bin/env bash
# Build (Windows, RelWithDebInfo) and copy the plugin into tools/obs-portable.
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
CMAKE="/c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
cd "$ROOT"
"$CMAKE" --build --preset windows-x64 | grep -E "error|warning C|->" || true
"$CMAKE" --install build_x64 --prefix release/RelWithDebInfo --config RelWithDebInfo > /dev/null
P="$ROOT/tools/obs-portable"
cp release/RelWithDebInfo/obs-multistream/bin/64bit/obs-multistream.dll "$P/obs-plugins/64bit/"
mkdir -p "$P/data/obs-plugins/obs-multistream"
cp -r release/RelWithDebInfo/obs-multistream/data/. "$P/data/obs-plugins/obs-multistream/"
echo deployed
