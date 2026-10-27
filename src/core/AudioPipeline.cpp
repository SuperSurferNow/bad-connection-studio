#include "core/AudioPipeline.h"

#include <QAudioFormat>
#include <QAudioSource>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QMediaDevices>

#include <algorithm>
#include <cmath>
#include <cstring>

Q_LOGGING_CATEGORY(lcAudio, "bcs.audio")

namespace bcs {

namespace {

AVSampleFormat toAv(QAudioFormat::SampleFormat f)
{
    switch (f) {
    case QAudioFormat::UInt8: return AV_SAMPLE_FMT_U8;
    case QAudioFormat::Int16: return AV_SAMPLE_FMT_S16;
    case QAudioFormat::Int32: return AV_SAMPLE_FMT_S32;
    case QAudioFormat::Float: return AV_SAMPLE_FMT_FLT;
    default: return AV_SAMPLE_FMT_NONE;
    }
}

float peakDb(const char* data, qint64 bytes, QAudioFormat::SampleFormat f)
{
    double peak = 0;
    switch (f) {
    case QAudioFormat::Int16: {
        auto* s = reinterpret_cast<const int16_t*>(data);
        for (qint64 i = 0; i < bytes / 2; ++i)
            peak = std::max(peak, std::abs(double(s[i])) / 32768.0);
        break;
    }
    case QAudioFormat::Int32: {
        auto* s = reinterpret_cast<const int32_t*>(data);
        for (qint64 i = 0; i < bytes / 4; ++i)
            peak = std::max(peak, std::abs(double(s[i])) / 2147483648.0);
        break;
    }
    case QAudioFormat::Float: {
        auto* s = reinterpret_cast<const float*>(data);
        for (qint64 i = 0; i < bytes / 4; ++i)
            peak = std::max(peak, double(std::fabs(s[i])));
        break;
    }
    case QAudioFormat::UInt8: {
        auto* s = reinterpret_cast<const uint8_t*>(data);
        for (qint64 i = 0; i < bytes; ++i)
            peak = std::max(peak, std::abs(double(s[i]) - 128.0) / 128.0);
        break;
    }
    default: break;
    }
    return peak > 1e-5 ? float(20.0 * std::log10(peak)) : -100.f;
}

float peakDb(const std::vector<int16_t>& s)
{
    int peak = 0;
    for (int16_t v : s)
        peak = std::max(peak, std::abs(int(v)));
    return peak > 0 ? float(20.0 * std::log10(peak / 32768.0)) : -100.f;
}

} // namespace

// Lives in AudioPipeline::m_thread. All QAudioSource and codec work happens
// here; it only touches the pipeline through atomics / mutex-guarded state.
class AudioWorker : public QObject {
public:
    explicit AudioWorker(AudioPipeline* p) : m_p(p) {}
    ~AudioWorker() override { closeInput(); }

    MediaError openInput(const QAudioDevice& dev, const AudioCodecConfig& codec)
    {
        closeInput();
        if (auto e = m_codec.open(codec))
            return e;

        QAudioFormat want;
        want.setSampleRate(48000);
        want.setChannelCount(codec.channels);
        want.setSampleFormat(QAudioFormat::Int16);
        QAudioFormat fmt = dev.isFormatSupported(want) ? want : dev.preferredFormat();
        if (toAv(fmt.sampleFormat()) == AV_SAMPLE_FMT_NONE || fmt.channelCount() < 1 || fmt.sampleRate() < 8000)
            return MediaError::make(ErrorKind::UnsupportedFormat,
                                    "Microphone '" + dev.description().toStdString() + "' uses an unsupported format.");
        m_fmt = fmt;
        m_inFmt = PcmFormat{toAv(fmt.sampleFormat()), fmt.sampleRate(), fmt.channelCount()};

        m_src = new QAudioSource(dev, fmt, this);
        // 20 ms capture buffer: low latency, still robust on a loaded system.
        m_src->setBufferSize(fmt.bytesForDuration(20000));
        connect(m_src, &QAudioSource::stateChanged, this, [this](QAudio::State s) {
            if (s == QAudio::StoppedState && m_src && m_src->error() != QAudio::NoError) {
                auto err = m_src->error();
                QMetaObject::invokeMethod(m_p, [p = m_p, err] {
                    p->report(MediaError::make(err == QAudio::OpenError ? ErrorKind::DeviceBusy : ErrorKind::DeviceLost,
                                               "The microphone stopped delivering audio.",
                                               "QAudio error " + std::to_string(int(err))));
                    {
                        std::lock_guard lk(p->m_statsMutex);
                        p->m_stats.micConnected = false;
                    }
                    if (p->m_cfg.autoReconnect && p->m_running) {
                        p->setState(AudioPipeline::State::Reconnecting, "Microphone disconnected — waiting for it");
                        p->m_reconnectTimer.start();
                    }
                });
            }
        });
        m_io = m_src->start();
        if (!m_io || m_src->error() != QAudio::NoError) {
            auto err = m_src->error();
            closeInput();
#if defined(_WIN32)
            const std::string hint = " Check Windows privacy settings for microphone access.";
#elif defined(__APPLE__)
            const std::string hint = " Allow microphone access in System Settings > Privacy & Security.";
#else
            const std::string hint;
#endif
            return MediaError::make(err == QAudio::OpenError ? ErrorKind::PermissionDenied : ErrorKind::Other,
                                    "Could not open microphone '" + dev.description().toStdString() + "'." + hint);
        }
        connect(m_io, &QIODevice::readyRead, this, [this] { onReadyRead(); });
        m_captureBufferMs = 20;
        std::lock_guard lk(m_p->m_statsMutex);
        m_p->m_stats.micConnected = true;
        m_p->m_stats.captureDescription = dev.description().toStdString() + " — " +
                                          std::to_string(fmt.sampleRate()) + " Hz, " +
                                          std::to_string(fmt.channelCount()) + " ch";
        updateCodecStats();
        return {};
    }

    void closeInput()
    {
        if (m_src) {
            m_src->disconnect(this);
            m_src->stop();
            delete m_src;
            m_src = nullptr;
            m_io = nullptr;
        }
    }

private:
    void updateCodecStats()
    {
        m_p->m_stats.effective = m_codec.effectiveConfig();
        m_p->m_stats.adjustments = m_codec.adjustments();
    }

    void applyPending()
    {
        std::optional<AudioCodecConfig> next;
        {
            std::lock_guard lk(m_p->m_pendingMutex);
            next.swap(m_p->m_pending);
        }
        if (!next || *next == m_codec.effectiveConfig())
            return;
        // libopus has no in-place reconfiguration through FFmpeg; reopening
        // the codec pair costs well under a millisecond and keeps capture and
        // the virtual microphone running.
        const AudioCodecConfig previous = m_codec.effectiveConfig();
        if (auto e = m_codec.open(*next)) {
            // Keep the microphone running with the last working settings.
            m_codec.open(previous);
            QMetaObject::invokeMethod(m_p, [p = m_p, e] { p->report(e); });
            return;
        }
        qCInfo(lcAudio).noquote() << "audio codec" << QString::fromStdString(next->encoder) << next->bitrateKbps
                                  << "kbit/s";
        std::lock_guard lk(m_p->m_statsMutex);
        updateCodecStats();
    }

public:
    void setLooping(bool on)
    {
        if (!on) {
            if (m_loopTimer)
                m_loopTimer->stop();
            return;
        }
        if (!m_loopTimer) {
            m_loopTimer = new QTimer(this);
            m_loopTimer->setTimerType(Qt::PreciseTimer);
            m_loopTimer->setInterval(10);
            connect(m_loopTimer, &QTimer::timeout, this, [this] { feedLoop(); });
        }
        m_loopSample = -1;
        m_loopTimer->start();
    }

private:
    // Clip audio for the time elapsed since the last tick, positioned by the
    // shared clip clock (so it cannot drift from the looping video).
    void feedLoop()
    {
        LoopClip* clip = m_p->m_clip;
        if (!m_p->m_running || !clip || !clip->isReady() || !clip->hasAudio())
            return;
        const int64_t target = (nowUs() - clip->playbackStartUs()) * LoopClip::kAudioRate / 1'000'000;
        if (m_loopSample < 0 || target < m_loopSample || target - m_loopSample > LoopClip::kAudioRate / 2)
            m_loopSample = target - LoopClip::kAudioRate / 50; // (re)start 20 ms behind
        const int64_t n = target - m_loopSample;
        if (n <= 0)
            return;
        const int ch = clip->readAudio(m_loopSample, size_t(n), m_loopPcm);
        m_loopSample = target;
        if (!ch)
            return;
        applyPending();
        m_out.clear();
        if (auto e = m_codec.process(reinterpret_cast<const uint8_t*>(m_loopPcm.data()), int(n),
                                     PcmFormat{AV_SAMPLE_FMT_S16, LoopClip::kAudioRate, ch}, m_out)) {
            QMetaObject::invokeMethod(m_p, [p = m_p, e] { p->report(e); });
            return;
        }
        deliver(-100.f);
    }

    void recordInput(const QByteArray& data, int frames)
    {
        const int ch = std::clamp(m_codec.effectiveConfig().channels, 1, 2);
        if (!m_recSwr || m_recIn != m_inFmt || m_recChannels != ch) {
            m_recSwr.reset();
            AVChannelLayout inLayout{}, outLayout{};
            av_channel_layout_default(&inLayout, m_inFmt.channels);
            av_channel_layout_default(&outLayout, ch);
            SwrContext* s = nullptr;
            if (swr_alloc_set_opts2(&s, &outLayout, AV_SAMPLE_FMT_S16, LoopClip::kAudioRate, &inLayout, m_inFmt.fmt,
                                    m_inFmt.rate, 0, nullptr) < 0 ||
                swr_init(s) < 0) {
                swr_free(&s);
                return;
            }
            m_recSwr.reset(s);
            m_recIn = m_inFmt;
            m_recChannels = ch;
        }
        const int maxOut = swr_get_out_samples(m_recSwr.get(), frames);
        if (maxOut <= 0)
            return;
        m_recPcm.resize(size_t(maxOut) * size_t(ch));
        uint8_t* dst[1] = {reinterpret_cast<uint8_t*>(m_recPcm.data())};
        const uint8_t* src[1] = {reinterpret_cast<const uint8_t*>(data.constData())};
        const int got = swr_convert(m_recSwr.get(), dst, maxOut, src, frames);
        if (got > 0)
            m_p->m_clip->addAudio(m_recPcm.data(), size_t(got), ch);
    }

    void onReadyRead()
    {
        if (!m_io)
            return;
        applyPending();
        QByteArray data = m_io->readAll();
        const int bpf = m_fmt.bytesPerFrame();
        if (data.isEmpty() || bpf <= 0)
            return;
        const int frames = int(data.size() / bpf);
        const float inPeak = peakDb(data.constData(), data.size(), m_fmt.sampleFormat());

        if (m_p->m_looping && m_p->m_clip && m_p->m_clip->hasAudio()) {
            // The loop timer produces the output; the microphone only
            // drives the input meter.
            std::lock_guard lk(m_p->m_statsMutex);
            m_p->m_stats.inputPeakDb = std::max(inPeak, m_p->m_stats.inputPeakDb);
            return;
        }
        if (m_p->m_clip && m_p->m_clip->isRecording())
            recordInput(data, frames);

        m_out.clear();
        if (auto e = m_codec.process(reinterpret_cast<const uint8_t*>(data.constData()), frames, m_inFmt, m_out)) {
            QMetaObject::invokeMethod(m_p, [p = m_p, e] { p->report(e); });
            return;
        }
        deliver(inPeak);
    }

    // Degraded audio in m_out -> virtual microphone, monitor, stats.
    void deliver(float inPeak)
    {
        const std::size_t outFrames = m_out.size() / std::size_t(std::max(1, m_codec.outputChannels()));
        if (outFrames && m_p->m_vmicOpen)
            m_p->m_vmic->write(m_out.data(), outFrames);
        if (outFrames && m_p->m_monitorOn && m_p->m_monitor)
            m_p->m_monitor->write(m_out.data(), outFrames);

        checkBackend();

        std::lock_guard lk(m_p->m_statsMutex);
        auto& st = m_p->m_stats;
        st.inputPeakDb = std::max(inPeak, st.inputPeakDb);
        st.outputPeakDb = std::max(peakDb(m_out), st.outputPeakDb);
        st.codecDelayMs = m_codec.codecDelayMs();
        st.outputBufferMs = m_p->m_vmicOpen ? m_p->m_vmic->bufferedMs() : 0;
        if (m_p->m_vmicOpen)
            st.outputDescription = m_p->m_vmic->describe();
        st.latencyMs = m_captureBufferMs + st.codecDelayMs + st.outputBufferMs;
        st.encodedBytes = m_codec.encodedBytes();
        st.encodedSeconds = m_codec.encodedSeconds();
    }

    // Virtual-mic backends fail asynchronously (PipeWire restarted, cable
    // device removed). Reopen with back-off; capture keeps running.
    void checkBackend()
    {
        auto* p = m_p;
        if (!p->m_vmic)
            return;
        if (p->m_vmicOpen) {
            if (MediaError e = p->m_vmic->takeAsyncError()) {
                p->m_vmicOpen = false;
                p->m_vmic->close();
                m_retry.start();
                QMetaObject::invokeMethod(p, [p, e] { p->report(e); });
            }
            return;
        }
        if (m_retry.isValid() && m_retry.elapsed() < 3000)
            return;
        m_retry.start();
        if (!p->m_vmic->open(p->m_cfg.micDeviceId, AudioCodecRoundTrip::kOutputRate, m_codec.outputChannels())) {
            p->m_vmicOpen = true;
            std::lock_guard lk(p->m_statsMutex);
            p->m_stats.outputDescription = p->m_vmic->describe();
        }
    }

    AudioPipeline* m_p;
    QAudioSource* m_src = nullptr;
    QIODevice* m_io = nullptr;
    QAudioFormat m_fmt;
    PcmFormat m_inFmt;
    AudioCodecRoundTrip m_codec;
    std::vector<int16_t> m_out;
    double m_captureBufferMs = 20;
    QElapsedTimer m_retry;
    // record / loop
    QTimer* m_loopTimer = nullptr;
    int64_t m_loopSample = -1;
    std::vector<int16_t> m_loopPcm;
    SwrPtr m_recSwr;
    PcmFormat m_recIn{AV_SAMPLE_FMT_NONE, 0, 0};
    int m_recChannels = 0;
    std::vector<int16_t> m_recPcm;
};

AudioPipeline::AudioPipeline(QObject* parent) : QObject(parent)
{
    qRegisterMetaType<bcs::AudioPipeline::State>();
    m_thread.setObjectName("bcs-audio");
    m_worker = new AudioWorker(this);
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.start(QThread::HighPriority);

    m_monitor = std::make_unique<AudioSinkWorker>("bcs-monitor");
    m_reconnectTimer.setInterval(1500);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &AudioPipeline::tryReconnect);
}

AudioPipeline::~AudioPipeline()
{
    stop();
    m_monitor.reset();
    m_thread.quit();
    m_thread.wait();
}

void AudioPipeline::setState(State s, const QString& detail)
{
    m_state = s;
    Q_EMIT stateChanged(s, detail);
}

void AudioPipeline::report(const MediaError& e)
{
    if (!e)
        return;
    qCWarning(lcAudio).noquote() << errorKindTitle(e.kind) << ":" << QString::fromStdString(e.message)
                                 << QString::fromStdString(e.detail);
    Q_EMIT problem(int(e.kind), QString::fromStdString(e.message), QString::fromStdString(e.detail));
}

void AudioPipeline::start(const AudioPipelineConfig& cfg)
{
    stop();
    m_cfg = cfg;
    {
        std::lock_guard lk(m_pendingMutex);
        m_pending.reset();
    }
    {
        std::lock_guard lk(m_statsMutex);
        m_stats = AudioStats{};
        m_stats.outputDescription = "monitor/meter only (no virtual microphone selected)";
    }

    m_vmic.reset();
    m_vmicOpen = false;
    if (!cfg.micBackendId.empty()) {
        m_vmic = createMicBackend(cfg.micBackendId, cfg.backendOptions);
        if (!m_vmic) {
            report(MediaError::make(ErrorKind::VirtualDeviceMissing,
                                    "Virtual microphone backend '" + cfg.micBackendId + "' is not built into this version."));
        } else if (auto e = m_vmic->open(cfg.micDeviceId, AudioCodecRoundTrip::kOutputRate, std::clamp(cfg.codec.channels, 1, 2))) {
            report(e);
            std::lock_guard lk(m_statsMutex);
            m_stats.outputDescription = "unavailable — meter/monitor only";
        } else {
            m_vmicOpen = true;
            std::lock_guard lk(m_statsMutex);
            m_stats.outputDescription = m_vmic->describe();
        }
    }

    if (m_monitorOn) // channel count may have changed
        setMonitoring(true, m_monitorDevice);

    MediaError err;
    QMetaObject::invokeMethod(
        m_worker, [&] { err = m_worker->openInput(cfg.input, cfg.codec); }, Qt::BlockingQueuedConnection);
    m_running = true;
    if (err) {
        report(err);
        const bool transient = err.kind == ErrorKind::DeviceBusy || err.kind == ErrorKind::DeviceLost ||
                               err.kind == ErrorKind::DeviceNotFound;
        if (transient && cfg.autoReconnect) {
            setState(State::Reconnecting, QString::fromStdString(err.message));
            m_reconnectTimer.start();
        } else {
            setState(State::Failed, QString::fromStdString(err.message));
        }
        return;
    }
    setState(State::Running);
}

void AudioPipeline::stop()
{
    m_reconnectTimer.stop();
    if (!m_running) {
        if (m_state != State::Stopped)
            setState(State::Stopped);
        return;
    }
    m_running = false;
    QMetaObject::invokeMethod(m_worker, [this] { m_worker->closeInput(); }, Qt::BlockingQueuedConnection);
    m_vmicOpen = false;
    if (m_vmic) {
        m_vmic->close();
        m_vmic.reset();
    }
    {
        std::lock_guard lk(m_statsMutex);
        m_stats.micConnected = false;
    }
    setState(State::Stopped);
}

void AudioPipeline::tryReconnect()
{
    if (!m_running) {
        m_reconnectTimer.stop();
        return;
    }
    const QByteArray id = m_cfg.input.id();
    for (const QAudioDevice& d : QMediaDevices::audioInputs()) {
        if (d.id() != id)
            continue;
        MediaError err;
        QMetaObject::invokeMethod(
            m_worker, [&] { err = m_worker->openInput(d, m_cfg.codec); }, Qt::BlockingQueuedConnection);
        if (!err) {
            m_reconnectTimer.stop();
            qCInfo(lcAudio) << "microphone reconnected";
            setState(State::Running);
        }
        return;
    }
}

void AudioPipeline::setLooping(bool on)
{
    m_looping = on;
    QMetaObject::invokeMethod(m_worker, [w = m_worker, on] { w->setLooping(on); }, Qt::QueuedConnection);
}

void AudioPipeline::onAudioInputsChanged()
{
    if (m_running && m_state == State::Reconnecting)
        tryReconnect();
}

void AudioPipeline::setBitrate(int kbps)
{
    std::lock_guard lk(m_pendingMutex);
    AudioCodecConfig c = m_pending ? *m_pending : m_cfg.codec;
    c.bitrateKbps = kbps;
    m_cfg.codec = c;
    m_pending = c;
}

void AudioPipeline::setCodecConfig(const AudioCodecConfig& cfg)
{
    std::lock_guard lk(m_pendingMutex);
    if (cfg.channels != m_cfg.codec.channels && m_running) {
        // The virtual microphone's channel count is fixed when it opens.
        m_cfg.codec = cfg;
        m_pending.reset();
        QMetaObject::invokeMethod(this, [this] { start(m_cfg); }, Qt::QueuedConnection);
        return;
    }
    m_cfg.codec = cfg;
    m_pending = cfg;
}

MediaError AudioPipeline::setMonitoring(bool on, const QAudioDevice& output)
{
    if (!on) {
        m_monitorOn = false;
        m_monitor->stop();
        return {};
    }
    m_monitorOn = false;
    MediaError e = m_monitor->start(output, AudioCodecRoundTrip::kOutputRate, std::clamp(m_cfg.codec.channels, 1, 2));
    m_monitorDevice = output;
    m_monitorOn = !e;
    return e;
}

AudioStats AudioPipeline::stats()
{
    std::lock_guard lk(m_statsMutex);
    AudioStats s = m_stats;
    s.virtualMicOpen = m_vmicOpen;
    m_stats.inputPeakDb = -100;
    m_stats.outputPeakDb = -100;
    return s;
}

} // namespace bcs
