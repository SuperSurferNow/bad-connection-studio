Bad Connection Studio encodes your live camera and microphone at a bitrate you choose, decodes them on the spot, and publishes the degraded result as a virtual camera and microphone that call apps can select.

> **Early release (0.x).** Codec round-trips, the UI, the PipeWire virtual microphone and the packages are tested in CI on clean machines. The v4l2loopback camera output, real USB hot-plug, sleep/resume and the Windows virtual devices have **not** been validated on real hardware yet. See `docs/TESTING.md` and please report what you find.

## Downloads

| Platform | File |
|---|---|
| Linux x86_64 (Fedora 44 and distributions with glibc ≥ Fedora 44's) | `BadConnectionStudio-*-x86_64.AppImage` |
| Linux aarch64 | `BadConnectionStudio-*-aarch64.AppImage` |
| Windows 10/11 x64 | `BadConnectionStudio-*-windows-x64-setup.exe` |

There is no single binary for both operating systems; download the one for yours. `SHA256SUMS.txt` lists checksums. Each package has a `LICENSE-REPORT.txt` describing its bundled libraries.

## What is NOT included

The packages bundle the app, Qt and FFmpeg only. Virtual devices need OS components that you install yourself:

* **Linux camera:** the `v4l2loopback` kernel module (RPM Fusion on Fedora). See `docs/LINUX_SETUP.md`.
* **Linux microphone:** works with the PipeWire session Fedora already runs; nothing to install.
* **Windows camera:** the separate, signed Virtual Camera component. It is only published when a code-signing certificate is configured, so it may be absent from this release.
* **Windows microphone:** VB-Audio Virtual Cable from vb-audio.com.

Without these the app still runs, shows the degraded video preview and meters the degraded audio. *Help ▸ Diagnostics* explains what is missing.

## Notes

* Linux: `chmod +x BadConnectionStudio-*.AppImage`, then run it. Use `--self-test` for a quick headless check.
* Windows: the installer is **unsigned** unless a certificate was configured, so SmartScreen may warn ("More info ▸ Run anyway").
* Licensing: the source code is MIT. Both packages bundle a GPL-licensed FFmpeg build (Fedora's on Linux, MSYS2's on Windows), so the binary packages as a whole are distributed under GPL terms; see `docs/LICENSES.md`.
* The AppImage needs a few standard desktop libraries from the host (OpenGL/EGL, X11, fontconfig, fribidi, libpipewire), which every desktop install already has. It runs on Fedora 44 and openSUSE Tumbleweed (tested in CI) and other distributions with glibc at least as new as Fedora 44's.
* H.264 is available on Windows (x264). The Linux AppImage offers VP8/VP9/MPEG-4 and Opus; it intentionally does not bundle Cisco's OpenH264 binary.
