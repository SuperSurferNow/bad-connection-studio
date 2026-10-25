#pragma once

#include "backend/BackendRegistry.h"
#include "core/AudioCodecRoundTrip.h"
#include "core/AudioSinkWorker.h"
#include "core/Errors.h"

#include <QAudioDevice>
#include <QObject>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>

namespace bcs {

class AudioWorker;

struct AudioPipelineConfig {
    QAudioDevice input;
    AudioCodecConfig codec;
    std::string micBackendId; // empty: monitor/meter only
    std::string micDeviceId;
    BackendOptions backendOptions;
    bool autoReconnect = true;
};

struct AudioStats {
    float inputPeakDb = -100;
    float outputPeakDb = -100;
    double latencyMs = 0;      // estimate: capture buffer + codec delay + output buffer
    double codecDelayMs = 0;
    double outputBufferMs = 0;
    uint64_t encodedBytes = 0;
    double encodedSeconds = 0;
    uint64_t droppedMs = 0;    // input discarded to keep latency bounded
    AudioCodecConfig effective;
    std::string adjustments;
    std::string captureDescription;
    std::string outputDescription;
    bool micConnected = false;
    bool virtualMicOpen = false;
};

// Microphone -> (resample) -> encoder -> decoder -> virtual microphone.
// Capture and codec run on a dedicated thread; the virtual microphone pulls
// from a bounded ring buffer on its own real-time thread.
class AudioPipeline : public QObject {
    Q_OBJECT
public:
    enum class State { Stopped, Running, Reconnecting, Failed };
    Q_ENUM(State)

    explicit AudioPipeline(QObject* parent = nullptr);
    ~AudioPipeline() override;

    void start(const AudioPipelineConfig& cfg);
    void stop();
    bool isRunning() const { return m_running; }
    State state() const { return m_state; }

    void setBitrate(int kbps);
    void setCodecConfig(const AudioCodecConfig& cfg);

    // Local monitoring of the degraded audio (headphones recommended).
    MediaError setMonitoring(bool on, const QAudioDevice& output);
    bool monitoring() const { return m_monitorOn; }

    // Snapshot; peak levels are reset on every call (the meter decays them).
    AudioStats stats();

    // Called by the UI when QMediaDevices reports input changes.
    void onAudioInputsChanged();

Q_SIGNALS:
    void problem(int kind, const QString& message, const QString& detail);
    void stateChanged(bcs::AudioPipeline::State state, const QString& detail);

private:
    friend class AudioWorker;
    void setState(State s, const QString& detail = {});
    void report(const MediaError& e);
    void tryReconnect();

    AudioPipelineConfig m_cfg;
    QThread m_thread;
    AudioWorker* m_worker = nullptr; // lives in m_thread
    std::unique_ptr<VirtualMicBackend> m_vmic;
    std::unique_ptr<AudioSinkWorker> m_monitor;
    std::atomic<bool> m_monitorOn{false};
    QAudioDevice m_monitorDevice;
    std::atomic<bool> m_vmicOpen{false};
    bool m_running = false;
    State m_state = State::Stopped;
    QTimer m_reconnectTimer;

    std::mutex m_pendingMutex;
    std::optional<AudioCodecConfig> m_pending;

    mutable std::mutex m_statsMutex;
    AudioStats m_stats;
};

} // namespace bcs

Q_DECLARE_METATYPE(bcs::AudioPipeline::State)
