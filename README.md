# Ciniva for Windows

Native Win32 client for [Ciniva](https://github.com/Ciniva/ciniva), the self-hosted media server. C++20, Direct2D/DirectWrite UI, libmpv playback with HDR passthrough.

## Build

Requires Visual Studio 2022 (Desktop development with C++) and the Windows 10/11 SDK. CMake downloads pinned, SHA-256 checked dependencies (libmpv from shinchiro/mpv-winbuild-cmake, nlohmann/json, Monocypher) into `build/_deps`.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release
```

The app is `build/Release/ciniva.exe`, with `libmpv-2.dll` next to it.

## Run

```powershell
build\Release\ciniva.exe <media url or file> [--start=<seconds>] [--exit-after-first-frame]
```

Keys: Space play/pause, ←/→ seek 10 s (Shift 30 s, Ctrl 60 s), F or double-click fullscreen, Esc leave fullscreen, I mpv stats.

Startup timings go to `%LOCALAPPDATA%\Ciniva\ciniva.log`. Set `CINIVA_DEBUG=1` for a verbose `mpv.log` in the same folder.

## HDR

Playback uses `vo=gpu-next`, `gpu-api=d3d11` and `target-colorspace-hint=yes`, so with Windows HDR on, mpv switches its swapchain to HDR10 (PQ / BT.2020) and passes HDR10 and Dolby Vision content through.
