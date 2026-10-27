Bad Connection Studio encodes your live camera and microphone at a bitrate you choose, decodes them on the spot, and publishes the degraded result as a virtual camera and microphone that call apps can select.

> **Early release (0.x).** Codec round-trips, the UI, the PipeWire virtual microphone and every package are tested in CI on clean machines. The Linux camera and its guided setup are verified on Fedora 44. The Windows virtual devices and the macOS app have **not** been run on real hardware yet. See `docs/TESTING.md` and please report what you find.

## New in 0.2

* **Record & loop:** record up to 2 minutes of camera and microphone (in memory only, never saved) and send it on repeat instead of the live feed. The quality sliders still apply, and audio stays in sync with the video.
* **macOS app** (Apple Silicon and Intel, macOS 13+): camera through OBS, microphone through BlackHole.
* **Output window (OBS bridge)** on every platform: a window that OBS turns into a webcam, with no driver. The fallback for apps that don't list the Windows virtual camera (new Teams).
* **Linux: guided v4l2loopback setup.** If the virtual camera driver is missing, the app offers to install it, after you confirm, in a separate, contained admin window (Fedora, Fedora Atomic, Arch, Debian/Ubuntu, openSUSE). It warns when a kernel update needs a reboot first.
* **Windows virtual camera component** is now published with every release, for 64-bit and 32-bit call apps.

## Downloads

| Platform | File |
|---|---|
| Linux x86_64 (glibc ≥ Fedora 44's) | `BadConnectionStudio-linux-x86_64.AppImage` |
| Linux ARM64 | `BadConnectionStudio-linux-aarch64.AppImage` |
| Windows 10/11 x64 | `BadConnectionStudio-windows-x64-setup.exe` |
| Windows virtual camera (optional) | `BadConnectionStudio-VirtualCamera-windows-setup.exe` |
| macOS 13+ Apple Silicon | `BadConnectionStudio-macos-arm64.dmg` |
| macOS 13+ Intel | `BadConnectionStudio-macos-x86_64.dmg` |

There is no single binary for every operating system; download the one for yours. `SHA256SUMS.txt` lists checksums. Each package has a `LICENSE-REPORT.txt` describing its bundled libraries.

## What is NOT included

The packages bundle the app, Qt and FFmpeg only. Virtual devices need OS components:

* **Linux camera:** the `v4l2loopback` kernel module. The app offers to install it. See `docs/LINUX_SETUP.md`.
* **Linux microphone:** works with the PipeWire session most distributions already run; nothing to install.
* **Windows camera:** the separate Virtual Camera installer above, or OBS with the output window.
* **Windows microphone:** VB-Audio Virtual Cable from vb-audio.com.
* **macOS camera:** OBS Studio with the output window. **macOS microphone:** BlackHole 2ch. See `docs/MACOS_SETUP.md` (also in the DMG).

Without these the app still runs, shows the degraded video preview and meters the degraded audio. *Help ▸ Diagnostics* explains what is missing.

## Notes

* Linux: `chmod +x BadConnectionStudio-linux-*.AppImage`, then run it. Use `--self-test` for a quick headless check.
* Windows: the installers are **unsigned**, so SmartScreen warns: click **More info ▸ Run anyway**.
* macOS: the app is not notarized. On first start, allow it in **System Settings ▸ Privacy & Security ▸ Open Anyway** (macOS 15+), or right-click ▸ **Open** (macOS 13/14).
* Licensing: the source code is MIT. All packages bundle a GPL-licensed FFmpeg build (Fedora's on Linux, MSYS2's on Windows, built from source with x264 on macOS), so the binary packages as a whole are distributed under GPL terms; see `docs/LICENSES.md`.
* The AppImage needs a few standard desktop libraries from the host (OpenGL/EGL, X11, fontconfig, fribidi, libpipewire), which every desktop install already has. It runs on Fedora 44 and openSUSE Tumbleweed (tested in CI) and other distributions with glibc at least as new as Fedora 44's.
* H.264 is available on Windows and macOS (x264). The Linux AppImage offers VP8/VP9 and Opus (plus any other encoders Fedora's FFmpeg provides); it intentionally does not bundle Cisco's OpenH264 binary.
