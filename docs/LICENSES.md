# Dependencies and licensing

Every package build generates `LICENSE-REPORT.txt`
(`scripts/license-report-linux.sh`, `scripts/license-report-msys2.sh`). It
lists each bundled library, the distribution package it came from and that
package's license, plus the exact FFmpeg `configure` line. The report is
shipped inside the package (`share/doc/BadConnectionStudio/`) and as a CI
artifact. **Review it for every release.** The app also shows the FFmpeg
license it was built against at runtime (Help ▸ About and `--self-test`).

## Bundled components

| Component | License | Notes |
|---|---|---|
| Qt 6 (Core, Gui, Widgets, Multimedia, DBus, platform plugins) | LGPL-3.0 (open-source edition) | Dynamically linked. Provide Qt's license texts and source offer, and allow users to replace the Qt libraries (satisfied by shipping them as separate shared libraries). |
| FFmpeg (libavcodec, libavformat, libavdevice, libavutil, libswscale, libswresample) | **LGPL-2.1+ or GPL-2+/3+, depending on build options** | See below. Dynamically linked. |
| libvpx (VP8/VP9) | BSD-3-Clause | |
| libopus | BSD-3-Clause | Preferred speech codec |
| libspeex | BSD-3-Clause | Optional |
| libx264 | **GPL-2.0+** | Optional; its presence makes the FFmpeg build GPL |
| OpenH264 | BSD-2-Clause | Patent coverage only applies to Cisco's prebuilt binary, which must be downloaded by the user, not bundled |
| libpulse (pulled in by Qt Multimedia on Linux) | LGPL-2.1+ | |
| MinGW-w64 runtime (Windows) | ZPL / MIT-style with GCC runtime exception | |

## Not bundled (OS-level, installed by the user)

| Component | License | Why not bundled |
|---|---|---|
| v4l2loopback | GPL-2.0 | Kernel module; must match the running kernel |
| PipeWire / libpipewire | MIT | System service; libpipewire is `dlopen()`ed from the host to avoid version skew |
| Softcam (Windows virtual camera component) | MIT | Shipped as a **separate, signed** installer; never inside the app installer |
| VB-Audio Virtual Cable | Proprietary donationware | Redistribution needs a license from VB-Audio; the user installs it |

## FFmpeg build options decide the distribution license

* An FFmpeg built **without** `--enable-gpl` is LGPL-2.1+ (LGPL-3+ with
  `--enable-version3`). The application can then use any license compatible
  with dynamically linking LGPL libraries.
* An FFmpeg built **with** `--enable-gpl` (for example to include libx264) is
  GPL. Distributing the AppImage/installer then requires the whole package,
  including Bad Connection Studio's own code, to be distributed under
  GPL-compatible terms with corresponding source.
* `--enable-nonfree` builds (e.g. with libfdk-aac) are **not redistributable**.
  Both packaging scripts refuse to produce a package from such a build.

What the CI builds use:

| Build | FFmpeg source | Expected license |
|---|---|---|
| Linux AppImage (Fedora 44 container) | Fedora `ffmpeg-free` libraries | LGPL (Fedora's build has no x264/x265; check the report) |
| Windows installer (MSYS2 UCRT64) | MSYS2 `mingw-w64-ucrt-x86_64-ffmpeg` | **GPL** (MSYS2 enables x264/x265) |

For an LGPL Windows build, replace the MSYS2 FFmpeg with a custom build such
as `vcpkg install ffmpeg[avcodec,avformat,avdevice,swscale,swresample,opus,vpx]`
without the `gpl` feature, and point `PKG_CONFIG_PATH` at it.

**Project license:** the project owner must choose one before publishing.
If release builds bundle a GPL FFmpeg (as the default Windows build does), it
must be GPL-compatible; GPL-3.0-or-later avoids any ambiguity.

## Codec patents

H.264 is patent-encumbered in some jurisdictions. Bundling libx264 or an
H.264-enabled FFmpeg may need a patent license there. VP8 and Opus are
royalty-free. The default codec order prefers whatever the bundled build
provides, so an LGPL VP8/Opus-only build is fully functional.

## Privacy

No camera or microphone content is ever written to disk or sent anywhere.
Preferences (`QSettings`) hold only device ids and codec settings. Logs hold
only technical text.
