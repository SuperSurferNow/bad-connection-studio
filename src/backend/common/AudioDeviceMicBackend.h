#pragma once

#include "backend/VirtualMicBackend.h"
#include "core/AudioSinkWorker.h"

#include <QAudioDevice>

#include <memory>

namespace bcs {

// FALLBACK virtual microphone: plays the degraded audio into an existing
// audio OUTPUT device whose other end appears as a microphone:
//  - Windows: VB-Audio Virtual Cable ("CABLE Input" -> call app selects
//    "CABLE Output"), installed separately by the user;
//  - Linux without native PipeWire: a PulseAudio/pipewire-pulse null sink +
//    remap-source the user created (see Diagnostics).
// By default only outputs that look virtual are offered, so the degraded mic
// is not accidentally sent to the speakers (feedback).
class AudioDeviceMicBackend final : public VirtualMicBackend {
public:
    static constexpr const char* kIdPrefix = "audiodev:";

    AudioDeviceMicBackend();
    ~AudioDeviceMicBackend() override;

    void setShowAllOutputs(bool on) { m_showAll = on; }

    BackendStatus probe() override;
    MediaError open(const std::string& deviceId, int sampleRate, int channels) override;
    void write(const int16_t* interleaved, std::size_t frames) override;
    void close() override;
    bool isOpen() const override;
    MediaError takeAsyncError() override;
    double bufferedMs() const override;
    std::string describe() const override;

    static bool looksVirtual(const QString& description);
    static std::string setupHelpHtml();

private:
    std::unique_ptr<AudioSinkWorker> m_worker;
    bool m_showAll = false;
};

} // namespace bcs
