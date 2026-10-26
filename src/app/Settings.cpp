#include "app/Settings.h"

#include "core/FfmpegUtil.h"
#include "core/QualityMapping.h"

#include <QSettings>

#include <algorithm>

namespace bcs {

namespace {

std::string firstAvailable(const std::vector<CodecChoice>& choices, const std::string& fallback)
{
    for (const auto& c : choices)
        if (c.available)
            return c.encoder;
    return fallback;
}

} // namespace

AppSettings AppSettings::load()
{
    QSettings s;
    AppSettings a;
    a.cameraId = s.value("devices/camera").toString();
    a.micId = s.value("devices/mic").toString();
    a.virtualCameraKey = s.value("devices/virtualCamera").toString();
    a.virtualMicKey = s.value("devices/virtualMic").toString();
    a.showVirtualInputs = s.value("devices/showVirtualInputs", false).toBool();
    a.showAllAudioOutputs = s.value("devices/showAllAudioOutputs", false).toBool();
    a.v4l2Yuyv = s.value("devices/v4l2Yuyv", false).toBool();

    a.video.encoder = s.value("video/codec", QString::fromStdString(firstAvailable(videoCodecChoices(), "libvpx")))
                          .toString()
                          .toStdString();
    a.video.bitrateKbps = s.value("video/bitrateKbps", videoBitrateScale().defaultKbps).toInt();
    a.video.width = s.value("video/width", 1280).toInt();
    a.video.height = s.value("video/height", 720).toInt();
    a.video.fps = s.value("video/fps", 30).toInt();
    a.video.keyframeIntervalSec = s.value("video/keyframeSec", 2.0).toDouble();
    a.video.preset = EncoderPreset(std::clamp(s.value("video/preset", int(EncoderPreset::Fast)).toInt(), 0, 3));
    a.video.lowLatency = s.value("video/lowLatency", true).toBool();
    a.video.allowFrameDrop = s.value("video/allowFrameDrop", true).toBool();
    a.captureWidth = s.value("video/captureWidth", 1280).toInt();
    a.captureHeight = s.value("video/captureHeight", 720).toInt();
    a.captureInputFormat = s.value("video/captureInputFormat").toString();

    a.audio.encoder = s.value("audio/codec", QString::fromStdString(firstAvailable(audioCodecChoices(), "libopus")))
                          .toString()
                          .toStdString();
    a.audio.bitrateKbps = s.value("audio/bitrateKbps", audioBitrateScale().defaultKbps).toInt();
    a.audio.channels = std::clamp(s.value("audio/channels", 1).toInt(), 1, 2);
    a.audio.sampleRate = s.value("audio/sampleRate", 48000).toInt();
    a.audio.speechMode = s.value("audio/speechMode", true).toBool();

    a.autoReconnect = s.value("general/autoReconnect", true).toBool();
    a.skipStartupDiagnostics = s.value("general/skipStartupDiagnostics", false).toBool();
    a.skipLoopbackPrompt = s.value("general/skipLoopbackPrompt", false).toBool();
    return a;
}

void AppSettings::save() const
{
    QSettings s;
    s.setValue("devices/camera", cameraId);
    s.setValue("devices/mic", micId);
    s.setValue("devices/virtualCamera", virtualCameraKey);
    s.setValue("devices/virtualMic", virtualMicKey);
    s.setValue("devices/showVirtualInputs", showVirtualInputs);
    s.setValue("devices/showAllAudioOutputs", showAllAudioOutputs);
    s.setValue("devices/v4l2Yuyv", v4l2Yuyv);

    s.setValue("video/codec", QString::fromStdString(video.encoder));
    s.setValue("video/bitrateKbps", video.bitrateKbps);
    s.setValue("video/width", video.width);
    s.setValue("video/height", video.height);
    s.setValue("video/fps", video.fps);
    s.setValue("video/keyframeSec", video.keyframeIntervalSec);
    s.setValue("video/preset", int(video.preset));
    s.setValue("video/lowLatency", video.lowLatency);
    s.setValue("video/allowFrameDrop", video.allowFrameDrop);
    s.setValue("video/captureWidth", captureWidth);
    s.setValue("video/captureHeight", captureHeight);
    s.setValue("video/captureInputFormat", captureInputFormat);

    s.setValue("audio/codec", QString::fromStdString(audio.encoder));
    s.setValue("audio/bitrateKbps", audio.bitrateKbps);
    s.setValue("audio/channels", audio.channels);
    s.setValue("audio/sampleRate", audio.sampleRate);
    s.setValue("audio/speechMode", audio.speechMode);

    s.setValue("general/autoReconnect", autoReconnect);
    s.setValue("general/skipStartupDiagnostics", skipStartupDiagnostics);
    s.setValue("general/skipLoopbackPrompt", skipLoopbackPrompt);
}

} // namespace bcs
