# Testing and acceptance

## Automated (CTest, run in CI on every push)

| Test | What it proves |
|---|---|
| `test_quality_mapping` | Slider ↔ bitrate mapping and labels ("Very degraded — 50 kbit/s" … "Cleaner — 2,000 kbit/s"; 6 … 64 kbit/s) |
| `test_buffers` | Bounded queues drop the oldest frame; the PCM ring never holds more than its latency cap under clock drift; underruns give silence |
| `test_video_roundtrip` | For **every** video encoder in the bundled FFmpeg: output resolution is identical at 50 and 2,000 kbit/s while PSNR rises; live bitrate change / encoder restart keeps the size |
| `test_audio_roundtrip` | Opus: spectral distance falls monotonically with bitrate; the actual bitrate stays on budget (constrained VBR); codec delay < 40 ms; codec reopen mid-stream; 44.1 kHz stereo float input is resampled |
| `test_video_pipeline` | The threaded pipeline (test pattern, no hardware): slider moved 5× while running, latency stays < 150 ms, no late drops, stop/restart twice |
| `test_audio_pipeline` | Microphone → codec → virtual microphone end to end, slider moves, stop/restart. Skipped (exit 77) without an audio server |
| `--self-test` | Run from the **packaged** app on clean containers/VMs without Qt or FFmpeg: codec checks plus a capability report in which missing drivers are explained |
| `--screenshot` | GUI smoke test of the packaged app (offscreen), generated test pattern |

## Results measured during development

Ubuntu 24.04 container, FFmpeg 6.1, Qt 6.4, x86_64. Synthetic 1280×720 content
with sensor-like noise (`bcs-roundtrip-bench`):

```
libx264  target    50  actual    47.8 kbit/s  PSNR 23.55 dB  codec 3.10 ms/frame
libx264  target   150  actual   121.4 kbit/s  PSNR 29.54 dB  codec 2.79 ms/frame
libx264  target   600  actual   510.8 kbit/s  PSNR 33.52 dB  codec 3.95 ms/frame
libx264  target  2000  actual  1609.2 kbit/s  PSNR 39.97 dB  codec 4.77 ms/frame
opus     target     6  actual     5.5 kbit/s  spectral distance 23.12 dB  codec delay 26.5 ms
opus     target    16  actual    13.0 kbit/s  spectral distance  4.25 dB  codec delay 26.5 ms
opus     target    64  actual    64.5 kbit/s  spectral distance  1.78 dB  codec delay 26.5 ms
```

Pipeline capture→output latency with the test pattern: 2–4 ms (720p, x264,
low-latency mode), and it does not grow after repeated slider moves.

Measured floors on this noisy synthetic content: VP8 cannot go below roughly
1.6 Mbit/s at 720p **without** frame skipping; with skipping enabled (the
default) it holds the target by dropping frames. MPEG-4 Part 2 floors around
2.7 Mbit/s here. Real webcam content compresses much better. The UI always
shows the **measured** bitrate next to the target.

PipeWire (headless PipeWire 1.0.5 + WirePlumber in the dev container):

* the node `bad_connection_mic` appears with `media.class = Audio/Source` and
  is listed by `pactl list short sources` (PulseAudio clients see it);
* `pw-record --target bad_connection_mic` records the degraded audio;
* the fallback backend (null sink `BadConnectionSink` + remap source
  `BadConnectionMic`) carries a test tone end to end.

**Not testable in the development container:** v4l2loopback (no kernel
module loading), real USB camera/microphone hot-plug, sleep/resume, the
Windows build. These are the manual checks below.

## Manual acceptance checklist (Fedora 44, then Windows 10/11)

Prerequisites: v4l2loopback loaded as in LINUX_SETUP.md, a USB webcam and
a USB headset.

1. **Missing drivers.** Unload v4l2loopback (`sudo modprobe -r v4l2loopback`)
   and start the app. Diagnostics opens and explains the setup; *Start video*
   runs in preview-only mode with a "Virtual device driver missing" message.
   No crash.
2. **Video bitrate.** Load the module, pick *Bad Connection Camera*, start
   video. At 50 kbit/s blocking/smearing is obvious in the preview **and** in
   the call app. At 2,000 kbit/s it is clean. `v4l2-ctl -d /dev/video10 --get-fmt-video`
   shows the same resolution throughout.
3. **Slider under load.** Drag the video slider end to end for 30 s. The
   status latency stays flat (tens of ms) and the call app's picture does not
   lag behind your movements.
4. **Mic bitrate.** Start the microphone with *Bad Connection Microphone*.
   In the call app's mic test (or `pw-record`), 6 kbit/s sounds narrowband and
   robotic, 64 kbit/s sounds clean. No echo or howl. The physical mic list
   does not contain the virtual mic.
5. **Call apps see the devices** (record pass/fail per app):

   | App | Camera listed | Video degraded | Mic listed | Audio degraded |
   |---|---|---|---|---|
   | Google Meet (Chrome) | | | | |
   | Google Meet (Firefox) | | | | |
   | Zoom desktop | | | | |
   | Microsoft Teams (web/desktop) | | | | |
   | Discord desktop | | | | |
   | OBS Studio (as source) | | | | |

6. **Stop/restart.** Stop and start video and mic 10 times in a row; then
   *Restore / Stop all*. `lsof /dev/video*` shows no handles held by the app,
   and the PipeWire node disappears (`pactl list short sources`).
7. **Unplug/replug.** While running, unplug the webcam: the status shows
   *Reconnecting* and the preview says so. Replug it: video resumes without
   user action (the `/dev/v4l/by-id` path is used). Repeat with the USB mic.
8. **Sleep/resume.** Suspend with both running. After resume, both restart
   within a few seconds.
9. **Clean machine.** On a fresh Fedora 44 VM without Qt/FFmpeg packages, the
   AppImage starts; Diagnostics lists only the OS-level prerequisites.
10. **Windows.** Repeat 1–8 with the Softcam component and VB-CABLE, then
    uninstall both components and confirm the app reports them missing again.
