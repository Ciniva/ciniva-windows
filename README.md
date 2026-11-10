# Ciniva for Windows

Native Win32 client for [Ciniva](https://github.com/Ciniva/ciniva), the self-hosted media server. C++20, Direct2D/DirectWrite UI, libmpv playback with HDR passthrough. Feature parity target: [ciniva-macos](https://github.com/Ciniva/ciniva-macos).

## Install

Download `Ciniva-<version>-win64.zip` from the latest release, unpack it anywhere and run:

```powershell
ciniva.exe --install
```

That copies Ciniva to `%LOCALAPPDATA%\Programs\Ciniva`, adds a Start menu shortcut and starts it. The build isn't code-signed yet, so SmartScreen warns on first run. Ciniva then updates itself from GitHub releases (Settings → Updates), checking every update's Ed25519 signature before installing it.

## Build

Requires Visual Studio 2022 or newer (Desktop development with C++) and the Windows 10/11 SDK. CMake downloads pinned, SHA-256 checked dependencies (libmpv from shinchiro/mpv-winbuild-cmake, nlohmann/json, Monocypher) into `build/_deps`.

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release
```

The app is `build/Release/ciniva.exe`, with `libmpv-2.dll` next to it.

## Run

```powershell
ciniva.exe [<media url or file>] [--start=<seconds>] [--exit-after-first-frame]
```

Library: arrow keys and Tab move between items, Enter opens, Esc/Backspace/mouse back go back, Ctrl+F searches, Ctrl+, opens Settings, F5 reloads.

Player: Space play/pause, ←/→ seek (Shift and Ctrl for the longer steps set in Settings), ↑/↓ volume, M mute, F, F11 or double-click fullscreen, S cycle subtitles, Shift+N / Shift+P next and previous episode, E episodes, I info, Shift+I mpv statistics, Esc back.

Startup timings go to `%LOCALAPPDATA%\Ciniva\ciniva.log`. Set `CINIVA_DEBUG=1` for a verbose `mpv.log` in the same folder.

## HDR

Playback uses `vo=gpu-next`, `gpu-api=d3d11` and `target-colorspace-hint=yes`, so with Windows HDR on, mpv switches its swapchain to HDR10 (PQ / BT.2020) and passes HDR10 and Dolby Vision content through. Player controls are drawn in a separate DirectComposition window above the video, so they never touch the HDR swapchain.

## Releases

Pull requests and manual runs on any branch build and test (the zip is a workflow artifact). Pushing tag `v<VERSION>` makes CI build, test, zip `ciniva.exe` and `libmpv-2.dll`, sign the zip with the `UPDATE_SIGNING_KEY` secret (base64 raw Ed25519 private key, same key as the Mac app) and publish release `v<VERSION>`.
