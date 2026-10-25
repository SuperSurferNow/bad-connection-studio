# Linux setup (Fedora 44 first)

Bad Connection Studio ships as an **AppImage** per architecture
(`x86_64`, `aarch64`). The AppImage bundles the application, Qt 6 (with its
platform and multimedia plugins), FFmpeg and the codec libraries.

> **The AppImage does not include or install kernel modules, PipeWire,
> system services, udev rules or drivers.** It never runs `sudo` and never
> changes kernel state. The virtual camera and microphone rely on OS
> components described below. Help ▸ *Diagnostics and setup* shows what is
> present and what is missing on your machine, with these same instructions.

| Feature | OS component | Bundled? | Needed for |
|---|---|---|---|
| Virtual camera | `v4l2loopback` kernel module | No | Call apps seeing "Bad Connection Camera" |
| Virtual microphone (native) | PipeWire user session (default on Fedora) | No (the host's libpipewire is used; it is present on every PipeWire or Qt 6.10+ desktop) | Call apps seeing "Bad Connection Microphone" |
| Virtual microphone (fallback) | PulseAudio-compatible null sink + remap source | No | Systems without PipeWire |
| Preview, meters, local monitoring | none | Yes | Always works |

## 1. Run the AppImage

```bash
chmod +x BadConnectionStudio-0.1.0-x86_64.AppImage
./BadConnectionStudio-0.1.0-x86_64.AppImage
```

On Fedora 44, FUSE 3 is present by default. On minimal systems without FUSE
use `--appimage-extract-and-run`.

`./BadConnectionStudio-*.AppImage --self-test` prints a codec round-trip
report and the virtual-device status, without opening a window.

**glibc:** the AppImage uses the host's glibc and runs on distributions
whose glibc is at least as new as the build machine's (Fedora 44 for the CI
builds). Distributions with an older glibc need a build made on an older base
(see *Building* in the README).

## 2. Virtual camera: v4l2loopback

The module is packaged for Fedora in **RPM Fusion (free)**.

```bash
# RPM Fusion free repository
sudo dnf install https://mirrors.rpmfusion.org/free/fedora/rpmfusion-free-release-$(rpm -E %fedora).noarch.rpm
# kmod built for your kernel by akmods + the userspace helper
sudo dnf install akmod-v4l2loopback v4l2loopback
sudo akmods --force

# Create one virtual camera (until reboot)
sudo modprobe v4l2loopback devices=1 video_nr=10 \
     card_label="Bad Connection Camera" exclusive_caps=1
```

* `exclusive_caps=1` is required for Chrome/Chromium/Electron call apps
  (Google Meet, Teams web, Discord, Slack) to list the device.
* `video_nr=10` keeps the virtual camera away from your physical
  `/dev/video0..N` numbers.

**Load at boot**

```bash
echo v4l2loopback | sudo tee /etc/modules-load.d/v4l2loopback.conf
echo 'options v4l2loopback devices=1 video_nr=10 card_label="Bad Connection Camera" exclusive_caps=1' \
  | sudo tee /etc/modprobe.d/v4l2loopback.conf
```

**Secure Boot.** With Secure Boot enabled, modules built by akmods must be
signed with a key enrolled in the firmware, otherwise `modprobe` fails with
`Key was rejected by service`. Follow RPM Fusion's *Secure Boot* how-to
(`kmodgenca`, `mokutil --import`, reboot and enrol).

**Permissions.** On a local desktop session systemd-logind grants your user
access to `/dev/video*` ("uaccess"). If Diagnostics reports *no write
permission*, add your user to the `video` group and log in again:

```bash
sudo usermod -aG video "$USER"
```

**Remove it again:** `sudo modprobe -r v4l2loopback` (stop the app and any
call app using the device first). Your physical camera is never touched:
the app refuses to write to any device whose driver is not `v4l2 loopback`.

Other distributions: `sudo apt install v4l2loopback-dkms` (Debian/Ubuntu),
`sudo pacman -S v4l2loopback-dkms` (Arch), then the same `modprobe` line.

## 3. Virtual microphone: PipeWire (native, default)

Fedora uses PipeWire by default; nothing needs to be installed. When you
start the microphone with destination *Bad Connection Microphone (new
PipeWire source)*, the app creates a node with `media.class=Audio/Source`
in your session. It exists only while the microphone is running; nothing is
written to PipeWire/WirePlumber configuration.

The node is visible to PipeWire-native apps and to PulseAudio clients through
`pipewire-pulse`, which covers browsers, Zoom, Teams, Discord, Slack, OBS.

Check it yourself while the microphone is running:

```bash
pactl list short sources | grep bad_connection_mic
pw-record --target bad_connection_mic /tmp/check.wav   # Ctrl+C, then play it
```

The status panel says *"an app is listening"* when any program records from
the node, which is a quick way to confirm a call app picked it up.

## 4. Virtual microphone fallback (PulseAudio-compatible layer)

For systems without a PipeWire daemon (plain PulseAudio). Run these yourself.
They are temporary and last until you log out:

```bash
pactl load-module module-null-sink sink_name=bcs_sink \
      sink_properties=device.description=BadConnectionSink
pactl load-module module-remap-source master=bcs_sink.monitor source_name=bcs_mic \
      source_properties=device.description=BadConnectionMic
```

Then choose **Play into: BadConnectionSink** as the virtual microphone and
select **BadConnectionMic** in the call app. Remove with
`pactl unload-module module-remap-source; pactl unload-module module-null-sink`.

## 5. Feedback protection

* Virtual inputs (`Bad Connection Microphone`, `BadConnectionMic`, monitor
  sources, `CABLE Output`) are hidden from the *Physical microphone* list
  unless you tick *List virtual devices as inputs*, and starting with one
  asks for confirmation.
* The PipeWire node is created with the lowest session priority, so
  WirePlumber prefers your real microphone as the default source.
* Local monitoring of the degraded audio switches itself off after 30 s.

## 6. Troubleshooting

| Symptom | Fix |
|---|---|
| "v4l2loopback kernel module is not loaded" | Section 2; check `lsmod | grep v4l2loopback` |
| "Virtual camera … is busy" | Another producer (OBS virtual camera) is feeding it, or a call app holds it at another resolution. Close it or use the same output resolution. |
| Call app does not list the camera | `exclusive_caps=1` missing; restart the call app after loading the module |
| "no PipeWire daemon is running" | `systemctl --user status pipewire wireplumber` |
| Camera shows "Reconnecting" | Camera unplugged or in use; the app reconnects automatically when it returns |
| Choppy video at the lowest bitrates with VP8 | Expected: VP8 skips frames to stay on budget. Untick *Allow encoder to skip frames*, or use H.264 (x264) if your build has it |

Logs (text only, never media) are in
`~/.local/share/BadConnectionStudio/logs/` (Help ▸ *Open log folder*).
