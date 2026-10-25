#include "core/VideoCodecRoundTrip.h"

extern "C" {
#include <libavutil/opt.h>
}

#include <algorithm>
#include <thread>

namespace bcs {

namespace {

struct DictGuard {
    AVDictionary* d = nullptr;
    ~DictGuard() { av_dict_free(&d); }
    void set(const char* k, const char* v) { av_dict_set(&d, k, v, 0); }
    void set(const char* k, int v) { av_dict_set_int(&d, k, v, 0); }
};

const char* x264Preset(EncoderPreset p)
{
    switch (p) {
    case EncoderPreset::Fastest: return "ultrafast";
    case EncoderPreset::Fast: return "superfast";
    case EncoderPreset::Balanced: return "veryfast";
    case EncoderPreset::Quality: return "faster";
    }
    return "superfast";
}

int vp8CpuUsed(EncoderPreset p)
{
    switch (p) {
    case EncoderPreset::Fastest: return 16;
    case EncoderPreset::Fast: return 10;
    case EncoderPreset::Balanced: return 6;
    case EncoderPreset::Quality: return 4;
    }
    return 10;
}

int vp9CpuUsed(EncoderPreset p)
{
    switch (p) {
    case EncoderPreset::Fastest: return 8;
    case EncoderPreset::Fast: return 7;
    case EncoderPreset::Balanced: return 6;
    case EncoderPreset::Quality: return 5;
    }
    return 7;
}

double msSince(int64_t t0) { return double(nowUs() - t0) / 1000.0; }

// Rate-control fields shared by most encoders. A small VBV buffer is what
// keeps the encoder from "saving up" bits, which would add latency and
// hide the bitrate starvation we want to show.
void applyRateControl(AVCodecContext* c, const VideoCodecConfig& cfg)
{
    const int64_t br = int64_t(cfg.bitrateKbps) * 1000;
    c->bit_rate = br;
    c->rc_max_rate = br;
    c->rc_buffer_size = int(cfg.lowLatency ? br / 2 : br * 2);
    c->rc_initial_buffer_occupancy = c->rc_buffer_size * 3 / 4;
    c->bit_rate_tolerance = int(std::max<int64_t>(br / 4, 1000));
}

} // namespace

MediaError VideoCodecRoundTrip::open(const VideoCodecConfig& cfg)
{
    close();
    m_cfg = cfg;
    m_cfg.width &= ~1; // 4:2:0 needs even dimensions
    m_cfg.height &= ~1;
    m_cfg.fps = std::clamp(m_cfg.fps, 1, 60);
    m_cfg.bitrateKbps = std::clamp(m_cfg.bitrateKbps, 10, 20000);
    m_pkt = makePacket();
    if (auto e = openEncoder()) {
        close();
        return e;
    }
    if (auto e = openDecoder()) {
        close();
        return e;
    }
    m_pts = 0;
    m_forceKey = true;
    m_sent = m_decoded = 0;
    return {};
}

void VideoCodecRoundTrip::close()
{
    m_enc.reset();
    m_dec.reset();
    m_pkt.reset();
}

MediaError VideoCodecRoundTrip::openEncoder()
{
    const AVCodec* codec = avcodec_find_encoder_by_name(m_cfg.encoder.c_str());
    if (!codec)
        return MediaError::make(ErrorKind::CodecInit,
                                "Video encoder '" + m_cfg.encoder + "' is not available in this FFmpeg build.");

    CodecCtxPtr c(avcodec_alloc_context3(codec));
    if (!c)
        return MediaError::make(ErrorKind::CodecInit, "Out of memory creating the video encoder.");

    auto fmts = supportedPixFmts(codec);
    m_inputFmt = AV_PIX_FMT_YUV420P;
    if (!fmts.empty() && std::find(fmts.begin(), fmts.end(), AV_PIX_FMT_YUV420P) == fmts.end())
        m_inputFmt = fmts.front();

    c->width = m_cfg.width;
    c->height = m_cfg.height;
    c->pix_fmt = m_inputFmt;
    c->time_base = AVRational{1, m_cfg.fps};
    c->framerate = AVRational{m_cfg.fps, 1};
    c->gop_size = std::max(1, int(m_cfg.keyframeIntervalSec * m_cfg.fps));
    c->max_b_frames = 0; // B-frames add a frame of reordering delay
    c->color_range = AVCOL_RANGE_MPEG;
    applyRateControl(c.get(), m_cfg);

    const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    const std::string& name = m_cfg.encoder;
    const bool mpegVideo = name == "mpeg4" || name == "mpeg2video";
    if (m_cfg.lowLatency) {
        // mpeg4 rejects the low-delay flag; it has no B-frames here anyway.
        if (name != "mpeg4")
            c->flags |= AV_CODEC_FLAG_LOW_DELAY;
        c->thread_type = FF_THREAD_SLICE; // frame threading delays output by N frames
    }
    c->thread_count = int(std::min(4u, hw));

    DictGuard opts;
    if (name == "libx264") {
        opts.set("preset", x264Preset(m_cfg.preset));
        if (m_cfg.lowLatency)
            opts.set("tune", "zerolatency");
        c->thread_count = 0; // zerolatency selects sliced threads
    } else if (name == "libvpx") {
        opts.set("deadline", "realtime");
        opts.set("cpu-used", vp8CpuUsed(m_cfg.preset));
        opts.set("lag-in-frames", 0);
        opts.set("auto-alt-ref", 0);
        // Below its quality floor VP8 can only meet the budget by skipping
        // frames (stutter), which is what a starved real call looks like.
        if (m_cfg.allowFrameDrop)
            opts.set("drop-threshold", 25);
        c->qmin = 2;
        c->qmax = 63; // allow real starvation at the bottom of the slider
        if (m_cfg.lowLatency)
            c->rc_min_rate = c->bit_rate; // min == max -> CBR in libvpxenc
    } else if (name == "libvpx-vp9") {
        opts.set("deadline", "realtime");
        opts.set("cpu-used", vp9CpuUsed(m_cfg.preset));
        opts.set("lag-in-frames", 0);
        opts.set("row-mt", 1);
        c->qmin = 2;
        c->qmax = 63;
        if (m_cfg.lowLatency)
            c->rc_min_rate = c->bit_rate;
    } else if (name == "libopenh264") {
        opts.set("rc_mode", "bitrate");
        opts.set("allow_skip_frames", 1);
    } else if (mpegVideo) {
        // The mpegvideo rate controller cannot honour max_rate == bit_rate
        // with a tiny VBV; run it as plain ABR. It floors at qscale 31.
        c->rc_max_rate = 0;
        c->rc_min_rate = 0;
        c->rc_buffer_size = 0;
        c->rc_initial_buffer_occupancy = 0;
        c->qmin = 2;
        c->qmax = 31;
        c->thread_count = 1;
    }

    int r = avcodec_open2(c.get(), codec, &opts.d);
    if (r < 0)
        return MediaError::make(ErrorKind::CodecInit,
                                "Could not open video encoder '" + name + "' at " + std::to_string(m_cfg.width) +
                                    "x" + std::to_string(m_cfg.height) + ".",
                                averr(r));
    m_enc = std::move(c);
    return {};
}

MediaError VideoCodecRoundTrip::openDecoder()
{
    const AVCodec* codec = avcodec_find_decoder(m_enc->codec_id);
    if (!codec)
        return MediaError::make(ErrorKind::CodecInit, "No decoder available for the selected video codec.");
    CodecCtxPtr d(avcodec_alloc_context3(codec));
    d->width = m_enc->width;
    d->height = m_enc->height;
    d->flags |= AV_CODEC_FLAG_LOW_DELAY;
    d->thread_count = 1; // decoder frame threads would add latency
    if (m_enc->extradata_size > 0) {
        d->extradata = static_cast<uint8_t*>(av_mallocz(size_t(m_enc->extradata_size) + AV_INPUT_BUFFER_PADDING_SIZE));
        std::copy_n(m_enc->extradata, m_enc->extradata_size, d->extradata);
        d->extradata_size = m_enc->extradata_size;
    }
    int r = avcodec_open2(d.get(), codec, nullptr);
    if (r < 0)
        return MediaError::make(ErrorKind::CodecInit, "Could not open the video decoder.", averr(r));
    m_dec = std::move(d);
    return {};
}

bool VideoCodecRoundTrip::supportsLiveBitrate() const
{
    // libx264's wrapper re-reads bit_rate/rc_max_rate/rc_buffer_size on
    // every frame and calls x264_encoder_reconfig(). The other wrappers read
    // rate control only at init, so they are reopened instead.
    return m_cfg.encoder == "libx264";
}

bool VideoCodecRoundTrip::setBitrateLive(int kbps)
{
    if (!m_enc || !supportsLiveBitrate())
        return false;
    m_cfg.bitrateKbps = kbps;
    applyRateControl(m_enc.get(), m_cfg);
    return true;
}

MediaError VideoCodecRoundTrip::process(AVFrame* in, std::vector<FramePtr>& out)
{
    if (!m_enc)
        return MediaError::make(ErrorKind::CodecInit, "Video codec is not open.");

    in->pts = m_pts++;
    in->pict_type = m_forceKey ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;
    m_forceKey = false;

    int64_t t0 = nowUs();
    int r = avcodec_send_frame(m_enc.get(), in);
    if (r < 0)
        return MediaError::make(ErrorKind::Other, "Video encoder rejected a frame.", averr(r));
    ++m_sent;

    double decodeMs = 0;
    for (;;) {
        r = avcodec_receive_packet(m_enc.get(), m_pkt.get());
        if (r == AVERROR(EAGAIN) || r == AVERROR_EOF)
            break;
        if (r < 0)
            return MediaError::make(ErrorKind::Other, "Video encoding failed.", averr(r));
        m_encodedBytes += uint64_t(m_pkt->size);
        ++m_encodedPackets;

        int64_t d0 = nowUs();
        r = avcodec_send_packet(m_dec.get(), m_pkt.get());
        av_packet_unref(m_pkt.get());
        if (r < 0 && r != AVERROR(EAGAIN))
            return MediaError::make(ErrorKind::Other, "Video decoding failed.", averr(r));
        for (;;) {
            FramePtr f = makeFrame();
            r = avcodec_receive_frame(m_dec.get(), f.get());
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF)
                break;
            if (r < 0)
                return MediaError::make(ErrorKind::Other, "Video decoding failed.", averr(r));
            ++m_decoded;
            out.push_back(std::move(f));
        }
        decodeMs += msSince(d0);
    }
    m_lastDecodeMs = decodeMs;
    m_lastEncodeMs = msSince(t0) - decodeMs;
    return {};
}

} // namespace bcs
