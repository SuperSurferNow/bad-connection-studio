#pragma once

#include "backend/BackendRegistry.h"
#include "core/BoundedQueue.h"
#include "core/CameraEnumerator.h"
#include "core/Errors.h"
#include "core/FfmpegUtil.h"
#include "core/LoopClip.h"
#include "core/VideoCapture.h"
#include "core/VideoCodecRoundTrip.h"

#include <QImage>
#include <QObject>
#include <QString>

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace bcs {

struct VideoPipelineConfig {
    CameraDevice camera;
    CaptureRequest capture;
    VideoCodecConfig codec;
    std::string cameraBackendId; // empty: preview only
    std::string cameraDeviceId;
    BackendOptions backendOptions;
    bool autoReconnect = true;
    QImage placeholder; // shown on the virtual camera after stop
};

struct VideoStats {
    uint64_t captured = 0;
    uint64_t droppedLate = 0;      // discarded by bounded queues (processing could not keep up)
    uint64_t skippedByEncoder = 0; // encoder chose not to code a frame (rate control)
    uint64_t output = 0;
    uint64_t encodedBytes = 0;
    double latencyMs = 0; // capture -> virtual device write, measured
    double encodeMs = 0;
    double decodeMs = 0;
    int bitrateKbps = 0;
    std::string codec;
    std::string captureDescription;
    std::string outputDescription;
    bool cameraConnected = false;
    bool virtualCameraOpen = false;
};

// Capture thread -> [queue 2] -> codec thread -> [queue 2] -> output thread.
// Queues drop the oldest frame when full, so latency stays bounded even when
// the encoder is briefly slow (e.g. while it restarts after a slider move).
class VideoPipeline : public QObject {
    Q_OBJECT
public:
    enum class State { Stopped, Starting, Running, Reconnecting, Failed };
    Q_ENUM(State)

    explicit VideoPipeline(QObject* parent = nullptr);
    ~VideoPipeline() override;

    // Opens the virtual camera synchronously (so a missing driver is reported
    // immediately; the pipeline then runs in preview-only mode) and starts
    // the worker threads. Camera errors are reported asynchronously.
    void start(const VideoPipelineConfig& cfg);
    void stop();
    bool isRunning() const { return m_running.load(); }
    State state() const { return m_state.load(); }

    // Live controls (any thread). Bitrate is applied in place if the encoder
    // supports it, otherwise only the codec is restarted; capture and the
    // virtual device keep running.
    void setBitrate(int kbps);
    void setCodecConfig(const VideoCodecConfig& cfg);

    VideoStats stats() const;
    void previewConsumed() { m_previewBusy = false; }

    // Record / loop (see LoopClip). The clip is owned by the caller and must
    // outlive the pipeline; set it once, before start(). While recording,
    // clean camera frames are copied into the clip. While looping, clip
    // frames replace the camera feed (the camera stays open, so going back
    // to live is instant) and still go through the codec.
    void setLoopClip(LoopClip* clip) { m_clip = clip; }
    void setLooping(bool on) { m_looping = on; }
    bool isLooping() const { return m_looping.load(); }

Q_SIGNALS:
    void previewFrame(const QImage& image);
    void problem(int kind, const QString& message, const QString& detail);
    void stateChanged(bcs::VideoPipeline::State state, const QString& detail);

private:
    struct TimedFrame {
        FramePtr frame;
        int64_t captureUs = 0;
    };

    void captureLoop();
    void codecLoop();
    void outputLoop();
    void setState(State s, const QString& detail = {});
    void report(const MediaError& e);
    FramePtr scaleForEncoder(const AVFrame* src);
    QImage toPreview(const AVFrame* f);
    FramePtr placeholderFrame(int w, int h);

    VideoPipelineConfig m_cfg;
    LoopClip* m_clip = nullptr;
    std::atomic<bool> m_looping{false};
    std::atomic<bool> m_running{false};
    std::atomic<State> m_state{State::Stopped};
    std::thread m_captureThread, m_codecThread, m_outputThread;
    BoundedQueue<TimedFrame> m_rawQueue{2};
    BoundedQueue<TimedFrame> m_decodedQueue{2};
    std::unique_ptr<VirtualCameraBackend> m_vcam;
    std::atomic<bool> m_vcamOpen{false};

    // Pending live changes, consumed by the codec thread.
    std::mutex m_pendingMutex;
    std::optional<VideoCodecConfig> m_pendingCodec;
    std::atomic<int> m_pendingBitrate{0};

    // codec-thread state
    VideoCodecRoundTrip m_codec;
    SwsPtr m_scaler;
    int m_scalerKey[3] = {0, 0, -1};
    // output-thread state
    SwsPtr m_previewScaler;
    int m_previewKey[3] = {0, 0, -1};
    std::atomic<bool> m_previewBusy{false};

    // stats
    std::atomic<uint64_t> m_captured{0}, m_droppedLate{0}, m_skipped{0}, m_output{0}, m_encodedBytes{0};
    std::atomic<double> m_latencyMs{0}, m_encodeMs{0}, m_decodeMs{0};
    std::atomic<int> m_bitrate{0};
    std::atomic<bool> m_cameraConnected{false};
    mutable std::mutex m_descMutex;
    std::string m_captureDesc, m_outputDesc, m_codecName;
};

} // namespace bcs

Q_DECLARE_METATYPE(bcs::VideoPipeline::State)
