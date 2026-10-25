#pragma once

#include "core/AudioCodecRoundTrip.h"
#include "core/VideoCodecRoundTrip.h"

#include <QString>

namespace bcs {

// User preferences (device choices and codec settings only). Camera and
// microphone content is never persisted.
struct AppSettings {
    QString cameraId;
    QString micId;
    QString virtualCameraKey; // "backend|device"
    QString virtualMicKey;
    bool showVirtualInputs = false;
    bool showAllAudioOutputs = false;
    bool v4l2Yuyv = false;

    VideoCodecConfig video;
    int captureWidth = 1280;
    int captureHeight = 720;
    QString captureInputFormat; // "", "mjpeg", "yuyv422"

    AudioCodecConfig audio;
    bool autoReconnect = true;
    bool skipStartupDiagnostics = false;

    static AppSettings load();
    void save() const;
};

} // namespace bcs
