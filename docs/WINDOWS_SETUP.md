# Windows 10/11: virtual-device investigation and setup

The Windows build is a **separate binary and installer** from the Linux
AppImage. The installer bundles the application, the Qt runtime (via
`windeployqt`) and the FFmpeg DLLs. It **does not install any driver or
virtual-device component**.

FFmpeg cannot create a Windows webcam or microphone. Call apps only see
cameras and microphones that are registered with the OS media stacks, so
something outside the app has to provide them.

## 1. Mechanisms investigated

### Virtual camera

| Option | Visible to | Kernel driver? | License / maintenance | Verdict |
|---|---|---|---|---|
| **Softcam** (DirectShow source filter DLL) | DirectShow apps: Zoom, Teams desktop, Discord, Slack, OBS, Chrome/Edge/Firefox (they enumerate DirectShow devices) | No, user-mode COM DLL registered with `regsvr32` | MIT; small, maintained, simple C API (`scCreateCamera` / `scSendFrame`) | **Chosen for milestone 5** (Windows 10 and 11) |
| Windows 11 `MFCreateVirtualCamera` + custom Media Foundation media source | All apps, including Frame Server-only ones (Windows Camera app, UWP) | No, but needs a COM media source DLL running in the Frame Server service, plus cross-session IPC | Our own code (Microsoft sample is MIT) | **Planned follow-up**, Windows 11 22000+ only |
| OBS Virtual Camera | DirectShow apps | No | GPL-2.0, part of OBS; no public producer API | Rejected: would mean driving another product's private shared memory |
| Kernel AVStream driver (e.g. from the WDK `avshws` sample) | All | **Yes**, needs EV/attestation signing via the Microsoft Hardware Dev Center | Our own | Rejected: disproportionate signing and maintenance cost |

### Virtual microphone

There is **no user-mode API** for creating a capture endpoint on Windows. A
virtual microphone always requires a kernel audio driver.

| Option | Notes | Verdict |
|---|---|---|
| **VB-Audio Virtual Cable** | Signed by its vendor, free for end users (donationware). May not be redistributed without a license, so the user installs it | **Chosen**: the app plays into "CABLE Input"; call apps select "CABLE Output" |
| Other virtual cable drivers (e.g. Voicemeeter, commercial SDKs) | Same mechanism; detected by name | Supported through the same backend |
| Own SysVAD-based driver | Kernel driver, attestation signing, large maintenance surface | Not planned |

## 2. What the app does on Windows

* **Camera capture:** FFmpeg `dshow` input. The device moniker is used as the
  stable id, so a replugged camera is found again.
* **Microphone capture / monitoring:** Qt Multimedia (WASAPI).
* **Virtual camera backend `softcam`:** reads the DLL path that the component
  installer recorded in `HKLM\Software\BadConnectionStudio\VirtualCamera\DllPath`,
  loads exactly that registered DLL, and sends top-down BGR24 frames. If the
  component is missing or not registered, the app explains this and keeps
  running in preview-only mode. Softcam allows one producer at a time; a
  second producer is reported as *device busy*.
* **Virtual microphone backend `audio-output`:** plays the decoded 48 kHz PCM
  into the selected virtual cable render endpoint through a bounded,
  drift-trimming ring buffer. "CABLE Output" is hidden from the physical
  microphone list, so the app cannot feed itself (feedback loop).
* Sleep/resume (`WM_POWERBROADCAST`) and device changes (`WM_DEVICECHANGE`)
  are handled: devices are released before sleep and reopened after resume.

## 3. Installation for users

1. Run `BadConnectionStudio-<version>-windows-x64-setup.exe` (app only).
2. **Virtual camera (optional):** run
   `BadConnectionStudio-VirtualCamera-<version>-Setup.exe`. It asks for
   administrator rights, installs and registers the signed `softcam.dll`, and
   adds its own entry to *Settings ▸ Apps* for uninstalling (which runs
   `regsvr32 /u`). Restart call apps afterwards. Select **DirectShow Softcam**
   in the call app.
3. **Virtual microphone (optional):** install VB-CABLE from
   <https://vb-audio.com/Cable/>. In the app choose *Play into: CABLE Input*;
   in the call app choose **CABLE Output** as the microphone.
4. Privacy: *Settings ▸ Privacy & security ▸ Camera / Microphone* → allow
   desktop apps. A refused permission is reported as *Permission denied*.

## 4. Signing policy

* The main installer and the virtual-camera component are Authenticode-signed
  in CI when a certificate is configured (`WINDOWS_CERT_PFX_BASE64`,
  `WINDOWS_CERT_PASSWORD` secrets).
* The **virtual-camera component is only built when a certificate is
  available**, and the build fails unless `softcam.dll` verifies as validly
  signed. An unsigned filter DLL is never produced, bundled or installed
  silently. Unsigned main-app installers from pull requests are development
  builds.

## 5. Status and validation still required (milestone 5)

The Windows sources are written against the documented APIs, but this
repository's CI is their first compile and test. Before release:

- [ ] CI build with MSYS2 UCRT64 succeeds; clean-VM install + `--self-test` passes.
- [ ] Confirm the pinned Softcam tag, its solution layout and the exported API signatures.
- [ ] Confirm Softcam's expected pixel orientation (top-down BGR24) with a test pattern.
- [ ] Verify the camera in Zoom, Teams, Chrome (Meet), Firefox, Discord, OBS.
- [ ] Verify VB-CABLE output in the same apps; confirm no feedback.
- [ ] Unplug/replug a USB camera and microphone while running; sleep/resume.
- [ ] Decide on the Windows 11 `MFCreateVirtualCamera` component for Frame-Server-only apps.
