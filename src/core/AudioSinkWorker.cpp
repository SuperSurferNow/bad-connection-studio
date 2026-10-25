#include "core/AudioSinkWorker.h"

#include <QAudioFormat>
#include <QAudioSink>
#include <QIODevice>

#include <algorithm>
#include <vector>

namespace bcs {

// QIODevice the QAudioSink pulls from. Always returns the full request
// (silence on underrun) so the sink never drops into IdleState.
class SinkPullDevice : public QIODevice {
public:
    SinkPullDevice(std::shared_ptr<PcmRingBuffer> ring, int sinkChannels)
        : m_ring(std::move(ring)), m_sinkCh(sinkChannels)
    {
    }

protected:
    qint64 readData(char* data, qint64 maxlen) override
    {
        const int dataCh = m_ring->channels();
        const qint64 frames = maxlen / qint64(sizeof(int16_t) * m_sinkCh);
        if (frames <= 0)
            return 0;
        auto* out = reinterpret_cast<int16_t*>(data);
        if (dataCh == m_sinkCh) {
            m_ring->read(out, std::size_t(frames));
        } else {
            m_tmp.resize(std::size_t(frames) * std::size_t(dataCh));
            m_ring->read(m_tmp.data(), std::size_t(frames));
            for (qint64 i = 0; i < frames; ++i) {
                if (dataCh == 1) { // mono -> stereo
                    out[i * 2] = out[i * 2 + 1] = m_tmp[std::size_t(i)];
                } else { // stereo -> mono
                    out[i] = int16_t((int(m_tmp[std::size_t(i * 2)]) + int(m_tmp[std::size_t(i * 2 + 1)])) / 2);
                }
            }
        }
        return frames * qint64(sizeof(int16_t)) * m_sinkCh;
    }
    qint64 writeData(const char*, qint64) override { return -1; }
    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return 1 << 20; }

private:
    std::shared_ptr<PcmRingBuffer> m_ring;
    int m_sinkCh;
    std::vector<int16_t> m_tmp;
};

AudioSinkWorker::AudioSinkWorker(QString threadName, QObject* parent) : QObject(parent)
{
    m_thread.setObjectName(threadName);
    m_ctx = new QObject;
    m_ctx->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_ctx, &QObject::deleteLater);
    m_thread.start();
}

AudioSinkWorker::~AudioSinkWorker()
{
    stop();
    m_thread.quit();
    m_thread.wait();
}

MediaError AudioSinkWorker::start(const QAudioDevice& device, int sampleRate, int channels)
{
    stop();
    MediaError result;
    QMetaObject::invokeMethod(
        m_ctx, [&] { startInThread(device, sampleRate, channels, &result); }, Qt::BlockingQueuedConnection);
    return result;
}

void AudioSinkWorker::startInThread(const QAudioDevice& device, int sampleRate, int channels, MediaError* result)
{
    if (device.isNull()) {
        *result = MediaError::make(ErrorKind::DeviceNotFound, "Audio output device not found.");
        return;
    }
    QAudioFormat fmt;
    fmt.setSampleRate(sampleRate);
    fmt.setSampleFormat(QAudioFormat::Int16);
    fmt.setChannelCount(channels);
    if (!device.isFormatSupported(fmt)) {
        fmt.setChannelCount(channels == 1 ? 2 : 1);
        if (!device.isFormatSupported(fmt)) {
            *result = MediaError::make(ErrorKind::UnsupportedFormat,
                                       "Audio device '" + device.description().toStdString() +
                                           "' does not accept 16-bit " + std::to_string(sampleRate) + " Hz audio.");
            return;
        }
    }
    {
        std::lock_guard lk(m_ringMutex);
        m_ring = std::make_shared<PcmRingBuffer>(channels, sampleRate, 30, 90);
    }
    m_pull = std::make_unique<SinkPullDevice>(m_ring, fmt.channelCount());
    m_pull->open(QIODevice::ReadOnly);

    m_sink = new QAudioSink(device, fmt, m_ctx);
    // ~40 ms device buffer: small enough for live use, large enough not to
    // crackle when the system is busy.
    m_sink->setBufferSize(qsizetype(sampleRate) * fmt.channelCount() * 2 * 40 / 1000);
    QObject::connect(m_sink, &QAudioSink::stateChanged, m_ctx, [this](QAudio::State s) {
        if (s == QAudio::StoppedState && m_sink && m_sink->error() != QAudio::NoError && m_running) {
            std::lock_guard lk(m_errMutex);
            m_asyncError = MediaError::make(ErrorKind::DeviceLost,
                                            "Audio output '" + m_deviceDescription.toStdString() + "' stopped.",
                                            "QAudio error " + std::to_string(int(m_sink->error())));
        }
    });
    m_sink->start(m_pull.get());
    if (m_sink->error() != QAudio::NoError) {
        *result = MediaError::make(m_sink->error() == QAudio::OpenError ? ErrorKind::DeviceBusy : ErrorKind::Other,
                                   "Could not open audio output '" + device.description().toStdString() + "'.");
        stopInThread();
        return;
    }
    m_deviceDescription = device.description();
    m_running = true;
}

void AudioSinkWorker::stop()
{
    if (!m_sink)
        return;
    QMetaObject::invokeMethod(m_ctx, [this] { stopInThread(); }, Qt::BlockingQueuedConnection);
}

void AudioSinkWorker::stopInThread()
{
    m_running = false;
    if (m_sink) {
        m_sink->stop();
        delete m_sink;
        m_sink = nullptr;
    }
    m_pull.reset();
    std::lock_guard lk(m_ringMutex);
    m_ring.reset();
}

void AudioSinkWorker::write(const int16_t* data, std::size_t frames)
{
    std::lock_guard lk(m_ringMutex);
    if (m_ring)
        m_ring->write(data, frames);
}

MediaError AudioSinkWorker::takeAsyncError()
{
    std::lock_guard lk(m_errMutex);
    MediaError e = m_asyncError;
    m_asyncError = {};
    return e;
}

double AudioSinkWorker::bufferedMs() const
{
    std::lock_guard lk(m_ringMutex);
    return m_ring ? m_ring->bufferedMs() : 0;
}

} // namespace bcs
