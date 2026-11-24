# Multistream for OBS

An OBS Studio plugin that adds a **Multistream** dock: stream to Twitch, YouTube, Kick, TikTok, Instagram, Facebook, X or any custom RTMP/RTMPS server at the same time. Destinations with the same quality settings share one encoder ("encode once, send many").

- **Install guide for everyone (with screenshots):** [INSTALL.md](INSTALL.md)
- License: GPL-2.0-or-later ([LICENSE](LICENSE)). OBS plugins link to libobs, which is GPL-2.0-or-later.
- Needs OBS Studio **31.0 or newer** (Windows x64, macOS 12+ universal, Ubuntu 24.04 x86_64).

## Features

- Dock with per-platform Start/Stop, **Start all / Stop all**, live status and upload estimate
- First-run guide with links to where each platform's stream key lives. The dock opens by itself the first time.
- Per-destination or shared encoder settings, Auto encoder (NVENC → AMF → QuickSync → Apple VT → x264)
- **Export / import setups without keys.** Exports are built from a whitelist: no keys, and server URLs only when they're the platform's public default.
- Optional: start/stop together with OBS's own Start Streaming
- Hotkeys: *Multistream: Start all* / *Stop all* (Settings → Hotkeys)
- Friendly message instead of a crash when OBS is too old

## Privacy

The plugin ships with **no** personal data. Each user's destinations, keys included, are saved only in their own OBS profile folder as `obs-multistream.json`, with owner-only permissions on macOS/Linux. OBS stores its own stream key the same way. Nothing is sent anywhere except to the RTMP servers the user enters.

## Packages

| OS | File | Notes |
|---|---|---|
| Windows | `obs-multistream-X-windows-x64-installer.exe` | Finds the OBS folder (registry, then `C:\Program Files\obs-studio`), checks the OBS version, refuses to run while OBS is open, adds an uninstaller |
| Windows | `obs-multistream-X-windows-x64.zip` | Manual install, OBS-folder layout |
| macOS | `obs-multistream-X-macos-universal.pkg` | Unsigned: right-click → Open (see INSTALL.md) |
| Linux | `obs-multistream-X-x86_64-linux-gnu.deb` | Ubuntu/Debian |
| Linux | `obs-multistream-X-linux-x86_64.tar.gz` | Per-user install script, no sudo |

## Uninstall

See the "Remove it" section of [INSTALL.md](INSTALL.md). The Windows uninstaller asks before deleting saved setups. Leftover settings are only `obs-multistream.json` files in OBS profile folders.

## Building

Based on the official [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate). Builds against OBS 31.1.1 headers and the obs-deps Qt6 (both downloaded automatically by CMake).

**Windows** (Visual Studio 2022 Build Tools + CMake ≥ 3.28):

```bash
cmake --preset windows-x64 -DMULTISTREAM_TESTS=ON
cmake --build --preset windows-x64
cmake --install build_x64 --prefix release/RelWithDebInfo --config RelWithDebInfo
powershell -File installer/windows/make-packages.ps1      # .zip + .exe (needs Inno Setup 6)
```

**macOS / Linux:** use `.github/scripts/build-macos` / `build-ubuntu`, or the GitHub Actions workflow. Pushing a tag like `1.0.0` builds all platforms and creates a **draft** GitHub Release with every package, checksums and an all-platforms zip. Nothing is public until you press Publish.

## Tests

- `multistream-model-tests` (build with `-DMULTISTREAM_TESTS=ON`): checks that exports never contain keys or private URLs, that hand-edited keys in imports are ignored, input validation, and the config round trip.
- `tests/e2e/run-e2e.sh` (Windows): starts a portable OBS copy with the plugin and a local [MediaMTX](https://github.com/bluenviron/mediamtx) RTMP server, then checks:
  - shared and separate encoders both publish H.264 + AAC
  - disabled destinations stay off
  - a dead server reports an error
  - a missing key is skipped
  - OBS shuts down cleanly and the config persists

  See [TOOLS.md](TOOLS.md) for setup.

## Project layout

```
src/                    plugin code (C++/Qt)
tests/                  unit test + end-to-end test
installer/windows       Inno Setup script, packaging script, manual-install notes
installer/linux         per-user tarball scripts
docs/screenshots        images used by INSTALL.md
.github/                CI (from obs-plugintemplate, extended for .exe/.tar.gz/all-in-one zip)
```
