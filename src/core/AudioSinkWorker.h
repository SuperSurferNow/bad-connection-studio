#pragma once

#include "core/Errors.h"
#include "core/PcmRingBuffer.h"

#include <QAudioDevice>
#include <QObject>
#include <QThread>

#include <atomic>
#include <memory>
#include <mutex>

class QAudioSink;

namespace bcs {

class SinkPullDevice;

// Plays int16 PCM to a Qt audio output device from a dedicated thread, pulling
// from a bounded ring buffer (so UI stalls cannot cause underruns and a slow
// device cannot cause delay to build up). Used for:
//  - the "play into a virtual cable / null sink" virtual-mic fallback, and
//  - the optional local monitoring of the degraded audio.
class AudioSinkWorker : public QObject {
    Q_OBJECT
public:
    explicit AudioSinkWorker(QString threadName, QObject* parent = nullptr);
    ~AudioSinkWorker() override;

    // `channels` / `sampleRate` describe the data given to write(); the sink
    // converts mono<->stereo if the device needs it. Blocks until started.
    MediaError start(const QAudioDevice& device, int sampleRate, int channels);
    void stop();
    bool isRunning() const { return m_running.load(); }

    void write(const int16_t* interleaved, std::size_t frames); // any thread

    MediaError takeAsyncError();
    double bufferedMs() const;
    QString deviceDescription() const { return m_deviceDescription; }

private:
    void startInThread(const QAudioDevice& device, int sampleRate, int channels, MediaError* result);
    void stopInThread();

    QThread m_thread;
    QObject* m_ctx = nullptr; // lives in m_thread
    QAudioSink* m_sink = nullptr;
    std::unique_ptr<SinkPullDevice> m_pull;
    std::shared_ptr<PcmRingBuffer> m_ring;
    mutable std::mutex m_ringMutex;
    std::atomic<bool> m_running{false};
    QString m_deviceDescription;
    std::mutex m_errMutex;
    MediaError m_asyncError;
};

} // namespace bcs
