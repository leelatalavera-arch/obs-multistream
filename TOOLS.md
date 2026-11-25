# Tools installed for this project

Everything came from official sources. Only the Visual Studio Build Tools needed admin rights.

| Tool | Version | Where | Why | How to remove |
|---|---|---|---|---|
| Visual Studio 2022 Build Tools (C++ workload, Windows 11 SDK 10.0.22621, CMake) | MSVC 14.44, CMake 3.31.6 | `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools` (winget `Microsoft.VisualStudio.2022.BuildTools`) | Compile the Windows plugin | Settings → Apps → *Visual Studio Build Tools 2022* → Uninstall (or Visual Studio Installer → Uninstall) |
| Inno Setup | 6.7.3 | `%LOCALAPPDATA%\Programs\Inno Setup 6` (per-user, winget `JRSoftware.InnoSetup --scope user`) | Build the Windows .exe installer | Settings → Apps → *Inno Setup* → Uninstall |
| OBS sources 31.1.1 + obs-deps 2025-07-11 + Qt6 | as in `buildspec.json` | `.deps/` in this folder (downloaded by CMake from github.com/obsproject) | Headers/libs to build against | Delete the `.deps` folder |
| Portable OBS copies (test only) | 32.2.1 (copy of the installed OBS) | `tools/obs-portable`, `tools/obs-installer-test` | Run tests without touching your real OBS or settings | Delete the folders |
| MediaMTX | 1.21.4 | `tools/mediamtx` (github.com/bluenviron/mediamtx releases) | Local RTMP server for the end-to-end test | Delete the folder |
| clang-format + gersemi (PyPI) | 19.1.1 / 0.29.2 | `tools/fmt-venv` (Python virtual env) | Match OBS's code-format rules so CI's format check passes | Delete the folder |
| GitHub CLI (gh), portable | 2.105.0 | `tools/gh` (github.com/cli/cli releases zip) | Create the private repo, watch builds, manage the draft release. Signed in with the device code; the sign-in is saved in Windows Credential Manager | Run `tools/gh/bin/gh auth logout`, then delete the folder. Also revoke at github.com/settings/applications |
| FFmpeg/ffprobe (BtbN build, linked from ffmpeg.org) | N-129241 (2026-11-23) | `tools/ffmpeg` | Check the test streams | Delete the folder |

Mac and Linux are built and tested on GitHub's machines (free, private repo). See `.github/workflows/smoke-test.yaml`. It installs the real packages on clean Ubuntu 24.04 (OBS from the official PPA) and macOS (OBS from Homebrew), then streams to a local MediaMTX server.

Not installed: Docker, valgrind/ASan, WSL.

Clean-up of build output only: delete `build_x64/` and `release/`.
