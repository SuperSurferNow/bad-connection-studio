# macOS 13+: setup

The macOS app is a normal app bundle in a DMG
(`BadConnectionStudio-macos-arm64.dmg` for Apple Silicon,
`BadConnectionStudio-macos-x86_64.dmg` for Intel Macs). It bundles Qt and
FFmpeg. It **does not install any driver**.

## 1. Install and first start

1. Open the DMG and drag **Bad Connection Studio** into **Applications**.
2. Start it. The app is not notarized by Apple (that needs a paid developer
   account), so macOS refuses the first start:
   * macOS 15 and newer: open **System Settings ▸ Privacy & Security**, scroll
     to *"Bad Connection Studio" was blocked*, click **Open Anyway** and
     confirm with your password.
   * macOS 13 and 14: right-click (Control-click) the app in Applications,
     choose **Open**, then **Open** again.

   This is needed once. If macOS says the app *"is damaged"*, the download
   was quarantined; run
   `xattr -dr com.apple.quarantine /Applications/BadConnectionStudio.app`
   in Terminal and start it again.
3. Allow **Camera** and **Microphone** when asked. If you clicked *Don't
   Allow*, turn the app on in **System Settings ▸ Privacy & Security ▸ Camera**
   (and **Microphone**), then restart the app.

## 2. Virtual camera: the OBS bridge

A macOS app can only create a webcam through a *Camera Extension*, which needs
a paid Apple developer entitlement. Instead the app shows the degraded video
in an **output window**, and the free **OBS Studio** turns that window into a
webcam:

1. Install OBS Studio from <https://obsproject.com>. Start it once and allow
   **Screen Recording** when asked (needed to capture a window).
2. In Bad Connection Studio, set the virtual camera to
   **Output window (for OBS)** and start the video. A borderless window titled
   *Bad Connection Studio — Output* opens (drag it to move it).
3. In OBS: **Sources ▸ + ▸ macOS Screen Capture**, method **Window Capture**,
   pick *Bad Connection Studio — Output*. Right-click the source ▸
   **Transform ▸ Fit to screen**.
4. Click **Start Virtual Camera** in OBS.
5. In the call app (Zoom, Meet in the browser, FaceTime, …) choose
   **OBS Virtual Camera**.

Keep the output window open and not minimised. It may sit behind other
windows. It closes when the video stops.

The OBS Virtual Camera is hidden from the app's own camera list, so the app
cannot capture its own output by accident.

## 3. Virtual microphone: BlackHole

macOS has no API for an app to create a microphone either; a virtual audio
driver is needed. Use **BlackHole 2ch** (free, GPL-3.0):

1. Install it from <https://existential.audio/blackhole/>, or with Homebrew:
   `brew install blackhole-2ch`. It asks for your password and may need a
   restart of the call app.
2. In Bad Connection Studio, set the virtual microphone to
   **Play into: BlackHole 2ch** and start the microphone.
3. In the call app choose **BlackHole 2ch** as the microphone.

BlackHole is hidden from the app's microphone list to prevent a feedback loop.
To uninstall BlackHole, see its README (or `brew uninstall blackhole-2ch`).

## 4. What the app does on macOS

* **Camera capture:** FFmpeg `avfoundation` input, opened by device name, in
  NV12 at the requested frame rate.
* **Microphone capture / monitoring:** Qt Multimedia (Core Audio).
* **Virtual camera:** the output window above (`window` backend). It is also
  available on Linux and Windows as a fallback.
* **Virtual microphone:** plays the decoded 48 kHz audio into the selected
  virtual audio output (`audio-output` backend).
* Sleep/resume handling is not implemented on macOS: if a camera stops after
  waking the Mac, stop and start the video.

## 5. Possible later: a built-in camera

If an Apple developer account is ever available, a **Camera Extension**
(CoreMediaIO, macOS 12.3+) would give a real "Bad Connection Camera" without
OBS. It needs the `com.apple.developer.system-extension.install` entitlement,
a signed and notarized app, and the user's approval in System Settings.
