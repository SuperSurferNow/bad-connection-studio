# Architecture

```
                         ┌───────────────── shared core (bcs_core) ─────────────────┐
 physical camera ──► VideoCapture ─[q=2]─► codec thread ─[q=2]─► output thread ──► VirtualCameraBackend
 (libavdevice           (capture thread)    crop+scale            write frame          ├ V4L2LoopbackCamera (Linux)
  v4l2 / dshow)                             encode→decode         preview (≤30 fps)    └ SoftcamCameraBackend (Windows)
                                            (VideoCodecRoundTrip)

 physical mic ──► QAudioSource ──► audio thread: resample → encode → decode ──► PcmRingBuffer ──► VirtualMicBackend
 (Qt Multimedia)   (20 ms buffer)   (AudioCodecRoundTrip, Opus 20 ms frames)     (bounded)        ├ PipeWireMicBackend (Linux, native)
                                                                                                  └ AudioDeviceMicBackend (fallback:
                                                                                     ▲               null sink / VB-CABLE)
                                                                     monitor (AudioSinkWorker, 30 s auto-off)
                         └──────────────────────────────────────────────────────────────────────────┘
  UI (Qt Widgets): MainWindow, VideoPreview, LevelMeter, DiagnosticsDialog — polls stats at 4 Hz
```

## Principles

* **The codec round-trip is in-process.** `VideoCodecRoundTrip` and
  `AudioCodecRoundTrip` use libavcodec directly: frame → encoder → packet →
  decoder → frame. No files, pipes or `ffmpeg` processes are involved.
* **Bounded everything.** Video queues hold 2 frames and drop the oldest.
  The PCM ring trims back to its target when the writer runs ahead (clock
  drift between capture device and output graph) and plays silence on
  underrun. Encoder FIFOs are capped. Latency therefore cannot accumulate.
* **Low-delay codec settings.** No B-frames, slice threads only (frame
  threads add N frames of delay), x264 `zerolatency`, libvpx `realtime` with
  `lag-in-frames=0`, a VBV of 0.5 s, single-threaded decoders with
  `LOW_DELAY`. Opus uses 20 ms frames, VoIP mode and constrained VBR.
* **The quality slider changes bitrate only.** Resolution is a separate
  setting, and the round-trip always outputs the configured size.
* **Live reconfiguration.** Bitrate changes are applied between frames: in
  place for x264 (`x264_encoder_reconfig` through FFmpeg), otherwise by
  reopening just the encoder/decoder pair (sub-millisecond for Opus, a
  keyframe for video). Capture and the virtual device keep running. Slider
  input is debounced (120–150 ms).
* **Platform code behind interfaces.** `VirtualCameraBackend` and
  `VirtualMicBackend` (in `src/backend/`) are the only OS-specific seams,
  besides camera enumeration (`CameraEnumerator`) and power events
  (`PowerMonitor`). `BackendRegistry` lists backends per platform. Each
  backend has a read-only `probe()` used by the launch-time capability check
  and Diagnostics.
* **External OS components are never installed silently.** Backends
  detect, explain and fall back (preview-only / meter-only). The one
  exception is opt-in: on Linux, *Set up virtual camera*
  (`LoopbackSetupDialog`) installs and loads v4l2loopback after showing every
  step. The app itself never runs as root; the dialog pipes a fixed helper
  script (`packaging/linux/bcs-loopback-helper.sh`, embedded as a resource)
  to `pkexec systemd-run --pipe -p ProtectHome=yes -p PrivateTmp=yes bash -s`
  and passes only allow-listed action names (`loopback::allowedActions()`).
  Distribution, kernel and Secure Boot detection (`LoopbackSetup.cpp`) is
  unprivileged and read-only. The v4l2
  backend refuses any device whose driver is not `v4l2 loopback`, so a
  physical camera is never written to.

## Failure handling

| Event | Behaviour |
|---|---|
| Camera unplugged / read error | Capture thread closes it, state *Reconnecting*, re-resolves the stable id (`/dev/v4l/by-id/...`, dshow moniker) every 1–1.5 s and resumes |
| Camera busy / not present at start | Same retry loop; permission or format errors stop with a clear message |
| Microphone lost | `QAudioSource` error → *Reconnecting*; retried on a timer and on `QMediaDevices::audioInputsChanged` |
| Virtual camera write fails | Message shown, the device is closed and reopened after 3 s; the preview keeps running |
| Virtual mic async failure (PipeWire restart) | Detected via stream state; reopened with back-off |
| Codec init fails | Video: pipeline stops with *Codec initialization failed* and a hint. Audio: previous codec stays active |
| Sleep | logind `PrepareForSleep` / `WM_POWERBROADCAST`: both pipelines stop (devices released); restarted 3 s after resume |
| Quit / SIGTERM / SIGINT | Normal Qt shutdown → *Restore / Stop all*: threads joined, the virtual camera gets a neutral "video paused" frame and is closed, the PipeWire node is destroyed |

## Threads

| Thread | Owner | Work |
|---|---|---|
| GUI | Qt | UI, device lists, 4 Hz stats polling |
| `capture` | VideoPipeline | non-blocking `av_read_frame`, MJPEG/raw decode |
| `codec` | VideoPipeline | pacing to output fps, crop/scale, encode→decode, live reconfig |
| `output` | VideoPipeline | virtual camera writes, preview conversion (only when the UI consumed the previous image) |
| `bcs-audio` | AudioPipeline | QAudioSource, resample, Opus round-trip, writes to backend/monitor rings |
| PipeWire data thread | libpipewire | `process` callback pulls from the ring |
| `bcs-vmic-out`, `bcs-monitor` | AudioSinkWorker | QAudioSink pull from rings (fallback mic, monitoring) |
