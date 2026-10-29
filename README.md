> This was created using Claude Opus 5.5, and is mainly tested in Fedora Linux as that is what I use. Any feedback is appreciated!

# Bad Connection Studio

[![Latest release](https://img.shields.io/github/v/release/SuperSurferNow/bad-connection-studio?label=latest%20release)](https://github.com/SuperSurferNow/bad-connection-studio/releases/latest)

**Download:**
[![Linux x86_64](https://img.shields.io/badge/Linux-x86__64-FCC624?logo=linux&logoColor=black)](https://github.com/SuperSurferNow/bad-connection-studio/releases/latest/download/BadConnectionStudio-linux-x86_64.AppImage)
[![Linux ARM64](https://img.shields.io/badge/Linux-ARM64-FCC624?logo=linux&logoColor=black)](https://github.com/SuperSurferNow/bad-connection-studio/releases/latest/download/BadConnectionStudio-linux-aarch64.AppImage)
[![Windows](https://img.shields.io/badge/Windows-x64-0078D6?logo=windows&logoColor=white)](https://github.com/SuperSurferNow/bad-connection-studio/releases/latest/download/BadConnectionStudio-windows-x64-setup.exe)
[![macOS Apple Silicon](https://img.shields.io/badge/macOS-Apple%20Silicon-000000?logo=apple&logoColor=white)](https://github.com/SuperSurferNow/bad-connection-studio/releases/latest/download/BadConnectionStudio-macos-arm64.dmg)
[![macOS Intel](https://img.shields.io/badge/macOS-Intel-555555?logo=apple&logoColor=white)](https://github.com/SuperSurferNow/bad-connection-studio/releases/latest/download/BadConnectionStudio-macos-x86_64.dmg)

Optional Windows virtual camera:
[BadConnectionStudio-VirtualCamera-windows-setup.exe](https://github.com/SuperSurferNow/bad-connection-studio/releases/latest/download/BadConnectionStudio-VirtualCamera-windows-setup.exe).
All files and checksums: [latest release](https://github.com/SuperSurferNow/bad-connection-studio/releases/latest).

Make your webcam and microphone sound and look like a bad connection, for
real. Bad Connection Studio encodes your **live** camera and microphone at
a bitrate you choose, decodes them immediately, and publishes the degraded
result as a **virtual camera** and **virtual microphone** that any call app
can select. The artifacts are genuine codec artifacts: blocking, smearing,
frame skipping, narrowband robotic speech. They are not filters.

* C++20, Qt 6 Widgets, CMake, FFmpeg libraries (libavcodec/-format/-device/-util, libswscale, libswresample)
* Linux (Fedora 44 first): v4l2loopback camera with guided one-click setup, native PipeWire microphone
* Windows 10/11: Softcam DirectShow camera (separate installer), virtual audio cable microphone
* macOS 13+: camera through OBS (the "output window" bridge), BlackHole microphone
* **Separate builds:** a Linux AppImage per architecture, a Windows installer and a macOS DMG per architecture. There is no single cross-platform binary.

![Main window](docs/screenshot.png)

## Features

* **Record & loop:** record up to 2 minutes of camera and microphone (kept in
  memory only, never saved), then send it on repeat instead of the live feed.
  The quality sliders still apply to the loop, and audio stays in sync.
* **Guided virtual camera setup on Linux:** detects the distribution
  (Fedora, Fedora Atomic, Arch, Debian/Ubuntu, openSUSE) and kernel, and
  installs v4l2loopback after you confirm in a separate, contained admin
  window.
* **Output window (OBS bridge):** on any OS, show the degraded video in a
  window that OBS turns into a webcam. No driver needed.
* Separate **Video quality** (50 – 2,000 kbit/s, default 150) and
  **Microphone quality** (6 – 64 kbit/s, Opus VoIP mode) sliders with
  tier labels ("Very degraded — 50 kbit/s"), plus the measured bitrate.
* Live changes: bitrate applied in place (x264) or by restarting only the
  codec. Capture and virtual devices keep running and no delay builds up.
* Advanced: codec (x264 / VP8 / OpenH264 / VP9 / MPEG-4 / MPEG-2 and Opus /
  Speex / AAC, whatever the bundled FFmpeg provides), output resolution, frame
  rate, keyframe interval, encoder preset, low-latency mode, frame skipping,
  camera capture size/format, mono/stereo, codec sample rate.
* Preview of the actual degraded output; input/output level meters; 30 s
  local monitoring (headphones).
* Status: capture device, virtual device, codec, target/measured bitrate,
  fps/sample rate, dropped/skipped frames, measured (video) / estimated
  (audio) latency.
* Launch-time capability check and Diagnostics with setup instructions. A
  missing driver means preview-only mode with an explanation, never a crash.
* Hot-plug (camera and mic reconnect automatically), sleep/resume, clean
  shutdown with a "video paused" frame on the virtual camera.
* Feedback protection: virtual devices are hidden from the input lists by
  default, and the virtual mic is never the default source.
* Preferences are saved; camera/microphone content is never written to disk
  (a loop clip lives in RAM only). Text-only logs in
  `~/.local/share/BadConnectionStudio/logs` (`%LOCALAPPDATA%` on Windows,
  `~/Library/Application Support` on macOS).

## OS-level prerequisites (not bundled)

| | Linux | Windows | macOS |
|---|---|---|---|
| Virtual camera | `v4l2loopback` kernel module (the app offers to install it) | "Bad Connection Studio Virtual Camera" component (Softcam, separate installer) | OBS Studio, capturing the app's output window |
| Virtual microphone | PipeWire session (default on most distributions); PulseAudio null-sink fallback | VB-Audio Virtual Cable (user-installed) | BlackHole 2ch (user-installed) |
| Fallback camera | Output window + OBS | Output window + OBS | (the default) |

See [docs/LINUX_SETUP.md](docs/LINUX_SETUP.md),
[docs/WINDOWS_SETUP.md](docs/WINDOWS_SETUP.md) (including the investigation of
Windows mechanisms) and [docs/MACOS_SETUP.md](docs/MACOS_SETUP.md).

## Building

Linux (Fedora 44):

```bash
sudo dnf install cmake ninja-build gcc-c++ qt6-qtbase-devel qt6-qtmultimedia-devel \
  libavcodec-free-devel libavformat-free-devel libavdevice-free-devel libavutil-free-devel \
  libswscale-free-devel libswresample-free-devel pipewire-devel
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
./build/src/bad-connection-studio
```

(Debian/Ubuntu: `qt6-base-dev qt6-multimedia-dev libavcodec-dev libavformat-dev
libavdevice-dev libavutil-dev libswscale-dev libswresample-dev libpipewire-0.3-dev`.)

AppImage: `packaging/linux/build-appimage.sh dist/`. It uses linuxdeploy and
its Qt plugin, and refuses to bundle libpipewire or a non-redistributable
FFmpeg.

Windows (MSYS2 UCRT64): see `.github/workflows/windows.yml`. In short:
`cmake -G Ninja`, `cmake --build`, then `cpack -G NSIS` in the build directory.

macOS: see `.github/workflows/macos.yml`. In short: Qt 6.8 from qt.io,
`scripts/build-ffmpeg-macos.sh ~/ffmpeg`, then
`PKG_CONFIG_PATH=~/ffmpeg/lib/pkgconfig cmake -S . -B build -G Ninja` and
`cmake --install build --prefix stage` for the app bundle.

Useful extras:

* `bad-connection-studio --self-test`: headless codec + capability report (used by CI on clean machines)
* `bcs-roundtrip-bench`: milestone-1 prototype. It sweeps bitrates on synthetic material,
  `--camera /dev/video0` measures a real camera round-trip, and `--pipewire-mic` publishes
  degraded test speech on the PipeWire virtual microphone

## Repository layout

```
src/core/        codec round-trips, capture, pipelines, buffers (shared)
src/backend/     VirtualCameraBackend / VirtualMicBackend + linux/, windows/, common/ (incl. output window)
src/ui/          Qt Widgets UI, diagnostics
src/app/         logging, settings, power events
tests/           CTest executables (no framework needed)
tools/           roundtrip benchmark (milestone 1)
packaging/       AppImage script, desktop/metainfo, CPack NSIS, virtual-camera component installer
scripts/         dependency/license report generators
docs/            setup, architecture, licenses, testing
```

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for threads, queues and
failure handling.

## Milestone status

| # | Milestone | Status |
|---|---|---|
| 1 | Codec round-trip prototype, latency measured | **Done.** 720p x264 round-trip 3–6 ms/frame; Opus codec delay 26.5 ms (see docs/TESTING.md) |
| 2 | Qt UI and live quality sliders | **Done**, verified offscreen with the test pattern |
| 3 | Linux camera output + PipeWire microphone | **Done.** PipeWire node verified against a real daemon; v4l2loopback output written to the kernel API but **not yet run against the module** |
| 4 | Fedora 44 package test + driver docs | Docs done; AppImage build + clean-container tests in CI; **needs a run on real Fedora 44 hardware** |
| 5 | Windows virtual camera/mic + installer | Builds, installs and self-tests on clean Windows in CI; Softcam component built for x64 + x86 and registration checked in CI; **not yet run in call apps on real Windows** |
| 6 | Hot-plug, errors, diagnostics, packaging, CI | Implemented; hardware hot-plug and sleep/resume need manual verification |
| 7 | Call-app matrix | Checklist in docs/TESTING.md; to be run |
| 8 | Linux guided v4l2loopback setup | **Done**, verified on Fedora 44 (KDE) with the polkit prompt |
| 9 | Record & loop | **Done**; core and pipeline tested in CI, needs a call-app check |
| 10 | Output window (OBS bridge), all platforms | **Done**, tested offscreen in CI |
| 11 | macOS app (DMG, arm64 + x86_64) | Builds, bundles and self-tests on clean macOS in CI; **not yet run on a real Mac** |

## Licensing

The source code is MIT-licensed (see [LICENSE](LICENSE)). Bundled
third-party components keep their own licenses; see
[docs/LICENSES.md](docs/LICENSES.md). Each package includes a generated
`LICENSE-REPORT.txt`. Whether a binary package as a whole is LGPL or GPL
depends on the bundled FFmpeg's configuration.
