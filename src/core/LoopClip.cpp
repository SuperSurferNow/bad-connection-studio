#include "core/LoopClip.h"

#include <QLoggingCategory>

#include <algorithm>
#include <cmath>

Q_LOGGING_CATEGORY(lcLoop, "bcs.loop")

namespace bcs {

namespace {

constexpr int kMaxRecordWidth = 1280;
constexpr int64_t kMinDurationUs = 500'000;
constexpr int kFadeSamples = LoopClip::kAudioRate / 200; // 5 ms at each end: no click at the seam

int64_t samplesToUs(int64_t samples) { return samples * 1'000'000 / LoopClip::kAudioRate; }

} // namespace

void LoopClip::beginRecording()
{
    std::lock_guard lk(m_mutex);
    m_video.clear();
    m_video.shrink_to_fit();
    m_audio.clear();
    m_audio.shrink_to_fit();
    m_audioChannels = 0;
    m_bytes = 0;
    m_durationUs = 0;
    m_enc.reset();
    m_dec.reset();
    m_sws.reset();
    m_full = false;
    m_recStartUs = nowUs();
    m_state = State::Recording;
    qCInfo(lcLoop) << "recording started (memory only)";
}

void LoopClip::clear()
{
    std::lock_guard lk(m_mutex);
    m_state = State::Empty;
    m_video.clear();
    m_video.shrink_to_fit();
    m_audio.clear();
    m_audio.shrink_to_fit();
    m_audioChannels = 0;
    m_bytes = 0;
    m_durationUs = 0;
    m_full = false;
    m_enc.reset();
    m_dec.reset();
    m_sws.reset();
    m_encFrame.reset();
}

bool LoopClip::finishRecording()
{
    {
        std::lock_guard lk(m_mutex);
        if (m_state != State::Recording)
            return m_state == State::Ready;

        int64_t videoUs = 0;
        if (!m_video.empty()) {
            const int64_t n = int64_t(m_video.size());
            const int64_t interval = n > 1 ? (m_video.back().ptsUs - m_video.front().ptsUs) / (n - 1) : 33'333;
            videoUs = m_video.back().ptsUs + interval;
        }
        const int64_t audioUs = m_audioChannels ? samplesToUs(int64_t(m_audio.size()) / m_audioChannels) : 0;
        int64_t d = (videoUs && audioUs) ? std::min(videoUs, audioUs) : std::max(videoUs, audioUs);

        if (d >= kMinDurationUs) {
            // Trim both streams to the common length.
            while (!m_video.empty() && m_video.back().ptsUs >= d) {
                m_bytes -= m_video.back().data.size();
                m_video.pop_back();
            }
            if (m_audioChannels) {
                const size_t keep = size_t(d * kAudioRate / 1'000'000) * size_t(m_audioChannels);
                if (m_audio.size() > keep) {
                    m_bytes -= (m_audio.size() - keep) * sizeof(int16_t);
                    m_audio.resize(keep);
                }
                const size_t frames = m_audio.size() / size_t(m_audioChannels);
                const size_t fade = std::min<size_t>(kFadeSamples, frames / 2);
                for (size_t i = 0; i < fade; ++i) {
                    const double g = double(i) / double(fade);
                    for (int c = 0; c < m_audioChannels; ++c) {
                        auto& head = m_audio[i * size_t(m_audioChannels) + size_t(c)];
                        auto& tail = m_audio[(frames - 1 - i) * size_t(m_audioChannels) + size_t(c)];
                        head = int16_t(std::lround(head * g));
                        tail = int16_t(std::lround(tail * g));
                    }
                }
            }
            m_durationUs = d;
            m_enc.reset();
            m_sws.reset();
            m_encFrame.reset();
            m_state = State::Ready;
            qCInfo(lcLoop) << "recording finished:" << d / 1000 << "ms," << m_video.size() << "video frames,"
                           << (m_audioChannels ? m_audio.size() / size_t(m_audioChannels) : 0) << "audio samples,"
                           << m_bytes / 1024 << "KiB";
            return true;
        }
    }
    qCInfo(lcLoop) << "recording too short, discarded";
    clear();
    return false;
}

int64_t LoopClip::recordedUs() const
{
    if (m_state == State::Recording)
        return std::min(nowUs() - m_recStartUs.load(), kMaxDurationUs);
    return durationUs();
}

int64_t LoopClip::durationUs() const
{
    std::lock_guard lk(m_mutex);
    return m_durationUs;
}

size_t LoopClip::bytes() const
{
    std::lock_guard lk(m_mutex);
    return m_bytes;
}

bool LoopClip::hasVideo() const
{
    std::lock_guard lk(m_mutex);
    return !m_video.empty();
}

bool LoopClip::hasAudio() const
{
    std::lock_guard lk(m_mutex);
    return m_audioChannels > 0 && !m_audio.empty();
}

int LoopClip::audioChannels() const
{
    std::lock_guard lk(m_mutex);
    return m_audioChannels;
}

bool LoopClip::roomFor(size_t more) const { return m_bytes + more <= kMaxBytes; }

// ------------------------------------------------------------------ recording

bool LoopClip::openEncoder(int w, int h)
{
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    if (!codec)
        return false;
    // Full-range YUV is what JPEG expects; older builds only accept the
    // deprecated YUVJ formats, newer ones also the regular format.
    for (AVPixelFormat fmt : {AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUV420P}) {
        CodecCtxPtr c(avcodec_alloc_context3(codec));
        c->width = w;
        c->height = h;
        c->pix_fmt = fmt;
        c->color_range = AVCOL_RANGE_JPEG;
        c->time_base = AVRational{1, 1000000};
        c->flags |= AV_CODEC_FLAG_QSCALE;
        c->global_quality = FF_QP2LAMBDA * 7; // clean enough to be degraded later, ~20x smaller than raw
        if (fmt == AV_PIX_FMT_YUV420P)
            c->strict_std_compliance = FF_COMPLIANCE_UNOFFICIAL;
        if (avcodec_open2(c.get(), codec, nullptr) == 0) {
            m_enc = std::move(c);
            m_encFrame = makeVideoFrame(w, h, fmt);
            if (m_encFrame)
                m_encFrame->color_range = AVCOL_RANGE_JPEG;
            m_pkt = makePacket();
            return m_encFrame != nullptr;
        }
    }
    return false;
}

void LoopClip::addVideo(const AVFrame* f)
{
    if (m_state != State::Recording || m_full || !f)
        return;
    std::lock_guard lk(m_mutex);
    const int64_t pts = nowUs() - m_recStartUs.load();
    if (pts >= kMaxDurationUs) {
        m_full = true;
        return;
    }
    if (!m_enc) {
        int w = f->width, h = f->height;
        if (w > kMaxRecordWidth) {
            h = int(int64_t(h) * kMaxRecordWidth / w);
            w = kMaxRecordWidth;
        }
        if (!openEncoder(w & ~1, h & ~1)) {
            qCWarning(lcLoop) << "MJPEG encoder unavailable: recording audio only";
            m_enc.reset();
            return;
        }
    }
    if (!m_sws || m_swsKey[0] != f->width || m_swsKey[1] != f->height || m_swsKey[2] != f->format) {
        m_sws.reset(sws_getContext(f->width, f->height, AVPixelFormat(f->format), m_enc->width, m_enc->height,
                                   m_enc->pix_fmt, SWS_BILINEAR, nullptr, nullptr, nullptr));
        m_swsKey[0] = f->width;
        m_swsKey[1] = f->height;
        m_swsKey[2] = f->format;
    }
    if (!m_sws || av_frame_make_writable(m_encFrame.get()) < 0)
        return;
    sws_scale(m_sws.get(), f->data, f->linesize, 0, f->height, m_encFrame->data, m_encFrame->linesize);
    m_encFrame->pts = pts;
    if (avcodec_send_frame(m_enc.get(), m_encFrame.get()) < 0)
        return;
    while (avcodec_receive_packet(m_enc.get(), m_pkt.get()) == 0) {
        if (!roomFor(size_t(m_pkt->size))) {
            m_full = true;
            av_packet_unref(m_pkt.get());
            break;
        }
        VideoPacket vp;
        vp.data.assign(m_pkt->data, m_pkt->data + m_pkt->size);
        vp.ptsUs = pts;
        m_bytes += vp.data.size();
        m_video.push_back(std::move(vp));
        av_packet_unref(m_pkt.get());
    }
}

void LoopClip::addAudio(const int16_t* pcm, size_t frames, int channels)
{
    if (m_state != State::Recording || m_full || !pcm || !frames || channels < 1)
        return;
    std::lock_guard lk(m_mutex);
    if (!m_audioChannels) {
        // Align the first chunk with the clip start (the video clock).
        m_audioChannels = std::min(channels, 2);
        const int64_t elapsed = nowUs() - m_recStartUs.load() - samplesToUs(int64_t(frames));
        if (elapsed > 0)
            m_audio.assign(size_t(elapsed * kAudioRate / 1'000'000) * size_t(m_audioChannels), 0);
    }
    const size_t addBytes = frames * size_t(m_audioChannels) * sizeof(int16_t);
    if (samplesToUs(int64_t(m_audio.size() / size_t(m_audioChannels) + frames)) > kMaxDurationUs ||
        !roomFor(addBytes)) {
        m_full = true;
        return;
    }
    m_audio.reserve(m_audio.size() + frames * size_t(m_audioChannels));
    for (size_t i = 0; i < frames; ++i) {
        if (channels == m_audioChannels) {
            for (int c = 0; c < channels; ++c)
                m_audio.push_back(pcm[i * size_t(channels) + size_t(c)]);
        } else if (m_audioChannels == 1) { // stereo (or more) -> mono
            int sum = 0;
            for (int c = 0; c < channels; ++c)
                sum += pcm[i * size_t(channels) + size_t(c)];
            m_audio.push_back(int16_t(sum / channels));
        } else { // mono -> stereo
            m_audio.push_back(pcm[i * size_t(channels)]);
            m_audio.push_back(pcm[i * size_t(channels) + (channels > 1 ? 1 : 0)]);
        }
    }
    m_bytes += addBytes;
}

// ------------------------------------------------------------------ playback

void LoopClip::startPlayback() { m_playStartUs = nowUs(); }

int64_t LoopClip::positionUs(int64_t now) const
{
    const int64_t d = durationUs();
    if (d <= 0)
        return 0;
    int64_t p = (now - m_playStartUs.load()) % d;
    return p < 0 ? p + d : p;
}

FramePtr LoopClip::videoFrameAt(int64_t posUs, int lastIndex, int* index)
{
    std::lock_guard lk(m_mutex);
    if (m_state != State::Ready || m_video.empty())
        return nullptr;
    auto it = std::upper_bound(m_video.begin(), m_video.end(), posUs,
                               [](int64_t p, const VideoPacket& v) { return p < v.ptsUs; });
    const int idx = it == m_video.begin() ? 0 : int(std::distance(m_video.begin(), it)) - 1;
    if (index)
        *index = idx;
    if (idx == lastIndex)
        return nullptr;

    if (!m_dec) {
        const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_MJPEG);
        if (!codec)
            return nullptr;
        CodecCtxPtr c(avcodec_alloc_context3(codec));
        if (avcodec_open2(c.get(), codec, nullptr) < 0)
            return nullptr;
        m_dec = std::move(c);
        if (!m_pkt)
            m_pkt = makePacket();
    }
    const auto& vp = m_video[size_t(idx)];
    if (av_new_packet(m_pkt.get(), int(vp.data.size())) < 0)
        return nullptr;
    std::copy(vp.data.begin(), vp.data.end(), m_pkt->data);
    const int sent = avcodec_send_packet(m_dec.get(), m_pkt.get());
    av_packet_unref(m_pkt.get());
    if (sent < 0)
        return nullptr;
    FramePtr out = makeFrame();
    if (avcodec_receive_frame(m_dec.get(), out.get()) < 0)
        return nullptr;
    return out;
}

int LoopClip::readAudio(int64_t start, size_t frames, std::vector<int16_t>& out) const
{
    std::lock_guard lk(m_mutex);
    if (m_state != State::Ready || !m_audioChannels || m_audio.empty())
        return 0;
    const int64_t len = int64_t(m_audio.size()) / m_audioChannels;
    out.resize(frames * size_t(m_audioChannels));
    int64_t pos = start % len;
    if (pos < 0)
        pos += len;
    for (size_t i = 0; i < frames; ++i) {
        for (int c = 0; c < m_audioChannels; ++c)
            out[i * size_t(m_audioChannels) + size_t(c)] = m_audio[size_t(pos) * size_t(m_audioChannels) + size_t(c)];
        if (++pos == len)
            pos = 0;
    }
    return m_audioChannels;
}

} // namespace bcs
