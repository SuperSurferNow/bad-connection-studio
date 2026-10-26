#include "core/VideoPipeline.h"

#include <QLoggingCategory>

#include <algorithm>
#include <chrono>

Q_LOGGING_CATEGORY(lcVideo, "bcs.video")

namespace bcs {

using namespace std::chrono_literals;

namespace {

double ewma(double prev, double sample, double alpha = 0.1)
{
    return prev <= 0 ? sample : prev + alpha * (sample - prev);
}

// Interruptible sleep for retry back-off.
void sleepWhile(const std::atomic<bool>& running, std::chrono::milliseconds total)
{
    auto end = std::chrono::steady_clock::now() + total;
    while (running && std::chrono::steady_clock::now() < end)
        std::this_thread::sleep_for(20ms);
}

} // namespace

VideoPipeline::VideoPipeline(QObject* parent) : QObject(parent)
{
    qRegisterMetaType<bcs::VideoPipeline::State>();
}

VideoPipeline::~VideoPipeline() { stop(); }

void VideoPipeline::setState(State s, const QString& detail)
{
    m_state = s;
    Q_EMIT stateChanged(s, detail);
}

void VideoPipeline::report(const MediaError& e)
{
    if (!e)
        return;
    qCWarning(lcVideo).noquote() << errorKindTitle(e.kind) << ":" << QString::fromStdString(e.message)
                                 << QString::fromStdString(e.detail);
    Q_EMIT problem(int(e.kind), QString::fromStdString(e.message), QString::fromStdString(e.detail));
}

void VideoPipeline::start(const VideoPipelineConfig& cfg)
{
    stop();
    m_cfg = cfg;
    m_captured = m_droppedLate = m_skipped = m_output = m_encodedBytes = 0;
    m_latencyMs = m_encodeMs = m_decodeMs = 0;
    m_bitrate = cfg.codec.bitrateKbps;
    m_pendingBitrate = 0;
    {
        std::lock_guard lk(m_pendingMutex);
        m_pendingCodec.reset();
    }
    m_rawQueue.reset();
    m_decodedQueue.reset();
    m_previewBusy = false;
    setState(State::Starting);

    // Virtual camera first, synchronously: a missing driver is the most
    // common setup problem and must be reported right away. The feed keeps
    // running (preview only) so the user can still judge the quality.
    m_vcam.reset();
    m_vcamOpen = false;
    {
        std::lock_guard lk(m_descMutex);
        m_outputDesc = "preview only (no virtual camera selected)";
    }
    if (!cfg.cameraBackendId.empty()) {
        m_vcam = createCameraBackend(cfg.cameraBackendId, cfg.backendOptions);
        if (!m_vcam) {
            report(MediaError::make(ErrorKind::VirtualDeviceMissing,
                                    "Virtual camera backend '" + cfg.cameraBackendId + "' is not built into this version."));
        } else if (auto e = m_vcam->open(cfg.cameraDeviceId, cfg.codec.width, cfg.codec.height, cfg.codec.fps)) {
            report(e);
            std::lock_guard lk(m_descMutex);
            m_outputDesc = "unavailable — preview only";
        } else {
            m_vcamOpen = true;
            std::lock_guard lk(m_descMutex);
            m_outputDesc = m_vcam->describe();
        }
    }

    m_running = true;
    m_captureThread = std::thread([this] { captureLoop(); });
    m_codecThread = std::thread([this] { codecLoop(); });
    m_outputThread = std::thread([this] { outputLoop(); });
}

void VideoPipeline::stop()
{
    if (!m_captureThread.joinable() && !m_codecThread.joinable() && !m_outputThread.joinable()) {
        if (m_state != State::Stopped)
            setState(State::Stopped);
        return;
    }
    m_running = false;
    m_rawQueue.close();
    m_decodedQueue.close();
    for (auto* t : {&m_captureThread, &m_codecThread, &m_outputThread})
        if (t->joinable())
            t->join();
    if (m_vcam) {
        // Leave a neutral frame instead of a frozen last image of the user.
        FramePtr ph = m_vcamOpen ? placeholderFrame(m_cfg.codec.width, m_cfg.codec.height) : nullptr;
        m_vcam->close(ph.get());
        m_vcam.reset();
    }
    m_vcamOpen = false;
    m_codec.close();
    m_scaler.reset();
    m_previewScaler.reset();
    m_cameraConnected = false;
    setState(State::Stopped);
}

void VideoPipeline::setBitrate(int kbps) { m_pendingBitrate = kbps; }

void VideoPipeline::setCodecConfig(const VideoCodecConfig& cfg)
{
    std::lock_guard lk(m_pendingMutex);
    m_pendingCodec = cfg;
}

VideoStats VideoPipeline::stats() const
{
    VideoStats s;
    s.captured = m_captured;
    s.droppedLate = m_droppedLate;
    s.skippedByEncoder = m_skipped;
    s.output = m_output;
    s.encodedBytes = m_encodedBytes;
    s.latencyMs = m_latencyMs;
    s.encodeMs = m_encodeMs;
    s.decodeMs = m_decodeMs;
    s.bitrateKbps = m_bitrate;
    s.cameraConnected = m_cameraConnected;
    s.virtualCameraOpen = m_vcamOpen;
    std::lock_guard lk(m_descMutex);
    s.codec = m_codecName;
    s.captureDescription = m_captureDesc;
    s.outputDescription = m_outputDesc;
    return s;
}

// ---------------------------------------------------------------- capture

void VideoPipeline::captureLoop()
{
    VideoCapture cap;
    bool reportedWaiting = false;
    int loopIndex = -1;
    while (m_running) {
        if (m_looping && m_clip && m_clip->isReady() && m_clip->hasVideo()) {
            // Keep draining the camera so no stale backlog builds up in the
            // capture layer; a failing camera is simply closed until live.
            if (cap.isOpen()) {
                FramePtr discard;
                MediaError err;
                if (cap.read(discard, err) < 0) {
                    cap.close();
                    m_cameraConnected = false;
                }
            }
            int idx = loopIndex;
            FramePtr lf = m_clip->videoFrameAt(m_clip->positionUs(nowUs()), loopIndex, &idx);
            if (!lf) {
                std::this_thread::sleep_for(2ms);
                continue;
            }
            loopIndex = idx;
            ++m_captured;
            m_droppedLate += m_rawQueue.push(TimedFrame{std::move(lf), nowUs()});
            continue;
        }
        loopIndex = -1;

        if (!cap.isOpen()) {
            CameraDevice dev = m_cfg.camera;
            if (dev.format != "synthetic") {
                // The /dev node or moniker may have changed after a replug.
                if (auto found = findCamera(dev.id))
                    dev = *found;
            }
            MediaError e = cap.open(dev, m_cfg.capture);
            if (e) {
                m_cameraConnected = false;
                const bool transient = e.kind == ErrorKind::DeviceNotFound || e.kind == ErrorKind::DeviceLost ||
                                       e.kind == ErrorKind::DeviceBusy;
                if (!reportedWaiting)
                    report(e);
                if (transient && m_cfg.autoReconnect) {
                    if (!reportedWaiting)
                        setState(State::Reconnecting, QString::fromStdString(e.message));
                    reportedWaiting = true;
                    sleepWhile(m_running, 1500ms);
                    continue;
                }
                setState(State::Failed, QString::fromStdString(e.message));
                m_running = false;
                m_rawQueue.close();
                m_decodedQueue.close();
                return;
            }
            reportedWaiting = false;
            m_cameraConnected = true;
            {
                std::lock_guard lk(m_descMutex);
                m_captureDesc = m_cfg.camera.name + " — " + cap.description();
            }
            qCInfo(lcVideo).noquote() << "camera opened:" << QString::fromStdString(cap.description());
            setState(State::Running);
        }

        FramePtr f;
        MediaError err;
        int r = cap.read(f, err);
        if (r == 0) {
            std::this_thread::sleep_for(2ms);
            continue;
        }
        if (r < 0) {
            report(err);
            cap.close();
            m_cameraConnected = false;
            if (!m_cfg.autoReconnect) {
                setState(State::Failed, QString::fromStdString(err.message));
                m_running = false;
                m_rawQueue.close();
                m_decodedQueue.close();
                return;
            }
            setState(State::Reconnecting, "Camera disconnected — waiting for it to come back");
            reportedWaiting = true;
            sleepWhile(m_running, 1000ms);
            continue;
        }
        ++m_captured;
        if (m_clip && m_clip->isRecording())
            m_clip->addVideo(f.get());
        m_droppedLate += m_rawQueue.push(TimedFrame{std::move(f), nowUs()});
    }
    cap.close();
}

// ---------------------------------------------------------------- codec

FramePtr VideoPipeline::scaleForEncoder(const AVFrame* src)
{
    const int dw = m_codec.config().width, dh = m_codec.config().height;
    const AVPixelFormat dfmt = m_codec.inputPixelFormat();

    // Centre-crop to the output aspect ratio, so a 4:3 camera is not
    // stretched into a 16:9 virtual camera.
    FramePtr view(av_frame_clone(src));
    if (!view)
        return nullptr;
    const double sa = double(src->width) / src->height, da = double(dw) / dh;
    if (sa > da + 0.01) {
        int keep = int(src->height * da) & ~1;
        view->crop_left = size_t((src->width - keep) / 2) & ~size_t(1);
        view->crop_right = size_t(src->width - keep) - view->crop_left;
    } else if (sa < da - 0.01) {
        int keep = int(src->width / da) & ~1;
        view->crop_top = size_t((src->height - keep) / 2) & ~size_t(1);
        view->crop_bottom = size_t(src->height - keep) - view->crop_top;
    }
    av_frame_apply_cropping(view.get(), AV_FRAME_CROP_UNALIGNED);

    if (!m_scaler || m_scalerKey[0] != view->width || m_scalerKey[1] != view->height || m_scalerKey[2] != view->format) {
        m_scaler.reset(sws_getContext(view->width, view->height, AVPixelFormat(view->format), dw, dh, dfmt,
                                      SWS_BILINEAR, nullptr, nullptr, nullptr));
        m_scalerKey[0] = view->width;
        m_scalerKey[1] = view->height;
        m_scalerKey[2] = view->format;
    }
    if (!m_scaler)
        return nullptr;
    FramePtr out = makeVideoFrame(dw, dh, dfmt);
    if (!out)
        return nullptr;
    sws_scale(m_scaler.get(), view->data, view->linesize, 0, view->height, out->data, out->linesize);
    return out;
}

void VideoPipeline::codecLoop()
{
    VideoCodecConfig active = m_cfg.codec;
    std::optional<VideoCodecConfig> lastGood; // to fall back after a bad live change
    bool needOpen = true;
    int64_t nextDueUs = 0;

    while (m_running) {
        // Apply pending live changes between frames.
        {
            std::lock_guard lk(m_pendingMutex);
            if (m_pendingCodec) {
                active = *m_pendingCodec;
                m_pendingCodec.reset();
                needOpen = true;
            }
        }
        if (int br = m_pendingBitrate.exchange(0); br > 0 && br != active.bitrateKbps) {
            active.bitrateKbps = br;
            if (!needOpen && m_codec.setBitrateLive(br)) {
                m_bitrate = br;
                lastGood = active;
                qCInfo(lcVideo) << "bitrate changed live to" << br << "kbit/s";
            }
            else
                needOpen = true; // restart only the codec, not capture/output
        }
        if (needOpen) {
            needOpen = false;
            m_scaler.reset();
            if (auto e = m_codec.open(active)) {
                report(e);
                if (lastGood && !(lastGood->sameStreamShape(active) && lastGood->bitrateKbps == active.bitrateKbps)) {
                    // A live change was rejected: keep streaming with the
                    // previous working settings instead of dropping the feed.
                    active = *lastGood;
                    needOpen = true;
                    continue;
                }
                setState(State::Failed, QString::fromStdString(e.message));
                m_running = false;
                m_rawQueue.close();
                m_decodedQueue.close();
                return;
            }
            m_bitrate = active.bitrateKbps;
            lastGood = active;
            std::lock_guard lk(m_descMutex);
            m_codecName = active.encoder;
            qCInfo(lcVideo).noquote() << "video codec" << QString::fromStdString(active.encoder) << active.width << "x"
                                      << active.height << "@" << active.bitrateKbps << "kbit/s";
        }

        auto item = m_rawQueue.pop(50ms);
        if (!item)
            continue;

        // Frame pacing to the configured output frame rate.
        const int64_t interval = 1000000 / std::max(1, active.fps);
        if (item->captureUs < nextDueUs - interval / 4)
            continue;
        nextDueUs = std::max(nextDueUs + interval, item->captureUs);

        FramePtr in = scaleForEncoder(item->frame.get());
        if (!in)
            continue;
        std::vector<FramePtr> decoded;
        uint64_t bytesBefore = m_codec.encodedBytes();
        int64_t skippedBefore = m_codec.skippedFrames();
        if (auto e = m_codec.process(in.get(), decoded)) {
            report(e);
            needOpen = true;
            continue;
        }
        m_encodedBytes += m_codec.encodedBytes() - bytesBefore;
        if (m_codec.skippedFrames() > skippedBefore)
            m_skipped += uint64_t(m_codec.skippedFrames() - skippedBefore);
        m_encodeMs = ewma(m_encodeMs, m_codec.lastEncodeMs());
        m_decodeMs = ewma(m_decodeMs, m_codec.lastDecodeMs());
        for (auto& d : decoded)
            m_droppedLate += m_decodedQueue.push(TimedFrame{std::move(d), item->captureUs});
    }
}

// ---------------------------------------------------------------- output

QImage VideoPipeline::toPreview(const AVFrame* f)
{
    const int pw = std::min(f->width, 640);
    const int ph = std::max(2, int(int64_t(f->height) * pw / f->width));
    if (!m_previewScaler || m_previewKey[0] != f->width || m_previewKey[1] != f->height || m_previewKey[2] != f->format) {
        m_previewScaler.reset(sws_getContext(f->width, f->height, AVPixelFormat(f->format), pw, ph, AV_PIX_FMT_RGB24,
                                             SWS_FAST_BILINEAR, nullptr, nullptr, nullptr));
        m_previewKey[0] = f->width;
        m_previewKey[1] = f->height;
        m_previewKey[2] = f->format;
    }
    if (!m_previewScaler)
        return {};
    QImage img(pw, ph, QImage::Format_RGB888);
    uint8_t* dst[4] = {img.bits(), nullptr, nullptr, nullptr};
    int stride[4] = {int(img.bytesPerLine()), 0, 0, 0};
    sws_scale(m_previewScaler.get(), f->data, f->linesize, 0, f->height, dst, stride);
    return img;
}

FramePtr VideoPipeline::placeholderFrame(int w, int h)
{
    if (m_cfg.placeholder.isNull())
        return nullptr;
    QImage img = m_cfg.placeholder.convertToFormat(QImage::Format_RGB888);
    SwsPtr s(sws_getContext(img.width(), img.height(), AV_PIX_FMT_RGB24, w, h, AV_PIX_FMT_YUV420P, SWS_BILINEAR,
                            nullptr, nullptr, nullptr));
    FramePtr out = makeVideoFrame(w, h, AV_PIX_FMT_YUV420P);
    if (!s || !out)
        return nullptr;
    const uint8_t* src[4] = {img.constBits(), nullptr, nullptr, nullptr};
    int stride[4] = {int(img.bytesPerLine()), 0, 0, 0};
    sws_scale(s.get(), src, stride, 0, img.height(), out->data, out->linesize);
    return out;
}

void VideoPipeline::outputLoop()
{
    int64_t lastPreviewUs = 0;
    int64_t nextVcamRetryUs = 0;
    while (m_running) {
        auto item = m_decodedQueue.pop(50ms);
        if (!item)
            continue;
        AVFrame* f = item->frame.get();

        if (m_vcam && !m_vcamOpen && nowUs() >= nextVcamRetryUs) {
            // Re-open after an error (e.g. module reloaded, consumer released
            // a conflicting format). Rate-limited.
            nextVcamRetryUs = nowUs() + 3000000;
            if (!m_vcam->open(m_cfg.cameraDeviceId, f->width, f->height, m_cfg.codec.fps)) {
                m_vcamOpen = true;
                std::lock_guard lk(m_descMutex);
                m_outputDesc = m_vcam->describe();
            }
        }
        if (m_vcam && m_vcamOpen) {
            if (auto e = m_vcam->writeFrame(f)) {
                report(e);
                m_vcam->close(nullptr);
                m_vcamOpen = false;
                nextVcamRetryUs = nowUs() + 3000000;
                std::lock_guard lk(m_descMutex);
                m_outputDesc = "error — retrying";
            } else {
                std::lock_guard lk(m_descMutex);
                m_outputDesc = m_vcam->describe();
            }
        }
        ++m_output;
        m_latencyMs = ewma(m_latencyMs, double(nowUs() - item->captureUs) / 1000.0);

        // Preview: at most ~30 fps and only when the UI consumed the last one,
        // so a busy UI thread cannot build up a backlog of images.
        const int64_t now = nowUs();
        if (now - lastPreviewUs >= 33000 && !m_previewBusy.exchange(true)) {
            lastPreviewUs = now;
            QImage img = toPreview(f);
            if (img.isNull())
                m_previewBusy = false;
            else
                Q_EMIT previewFrame(img);
        }
    }
}

} // namespace bcs
