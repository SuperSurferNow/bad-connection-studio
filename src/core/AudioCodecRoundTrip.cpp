#include "core/AudioCodecRoundTrip.h"

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace bcs {

namespace {

AVChannelLayout layoutFor(int channels)
{
    AVChannelLayout l{};
    av_channel_layout_default(&l, channels);
    return l;
}

int nearest(const std::vector<int>& options, int want)
{
    return *std::min_element(options.begin(), options.end(),
                             [&](int a, int b) { return std::abs(a - want) < std::abs(b - want); });
}

AVSampleFormat pickSampleFmt(const AVCodec* codec)
{
    auto fmts = supportedSampleFmts(codec);
    if (fmts.empty())
        return AV_SAMPLE_FMT_S16;
    for (AVSampleFormat pref : {AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_S16P})
        if (std::find(fmts.begin(), fmts.end(), pref) != fmts.end())
            return pref;
    return fmts.front();
}

} // namespace

AudioCodecConfig AudioCodecRoundTrip::snapToCodec(const AVCodec* codec, AudioCodecConfig cfg)
{
    m_adjustments.clear();
    auto note = [&](const std::string& s) { m_adjustments += (m_adjustments.empty() ? "" : "; ") + s; };

    cfg.channels = std::clamp(cfg.channels, 1, 2);
    const std::string& name = cfg.encoder;
    std::vector<int> rates = supportedSampleRates(codec);
    if (name == "opus") {
        rates = {48000};
    } else if (name == "libspeex" && rates.empty()) {
        rates = {8000, 16000, 32000};
    }
    if (!rates.empty() && std::find(rates.begin(), rates.end(), cfg.sampleRate) == rates.end()) {
        int r = nearest(rates, cfg.sampleRate);
        note("sample rate " + std::to_string(cfg.sampleRate) + " Hz not supported, using " + std::to_string(r) + " Hz");
        cfg.sampleRate = r;
    }
    return cfg;
}

MediaError AudioCodecRoundTrip::open(const AudioCodecConfig& requested)
{
    close();
    const AVCodec* codec = avcodec_find_encoder_by_name(requested.encoder.c_str());
    if (!codec)
        return MediaError::make(ErrorKind::CodecInit,
                                "Audio encoder '" + requested.encoder + "' is not available in this FFmpeg build.");
    m_cfg = snapToCodec(codec, requested);

    CodecCtxPtr c(avcodec_alloc_context3(codec));
    c->sample_rate = m_cfg.sampleRate;
    c->sample_fmt = pickSampleFmt(codec);
    c->ch_layout = layoutFor(m_cfg.channels);
    c->bit_rate = int64_t(m_cfg.bitrateKbps) * 1000;
    c->time_base = AVRational{1, m_cfg.sampleRate};

    AVDictionary* opts = nullptr;
    if (m_cfg.encoder == "libopus") {
        av_dict_set(&opts, "application", m_cfg.speechMode ? "voip" : "audio", 0);
        av_dict_set(&opts, "frame_duration", "20", 0);
        // Constrained VBR: like a real VoIP call, never exceed the budget
        // (unconstrained VBR overshoots by up to 2x on tonal input).
        av_dict_set(&opts, "vbr", "constrained", 0);
    } else if (m_cfg.encoder == "opus") {
        c->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
    } else if (m_cfg.encoder == "libspeex") {
        av_dict_set(&opts, "frames_per_packet", "1", 0);
    }
    int r = avcodec_open2(c.get(), codec, &opts);
    av_dict_free(&opts);
    if (r < 0)
        return MediaError::make(ErrorKind::CodecInit,
                                "Could not open audio encoder '" + m_cfg.encoder + "' (" +
                                    std::to_string(m_cfg.sampleRate) + " Hz, " + std::to_string(m_cfg.channels) +
                                    " ch, " + std::to_string(m_cfg.bitrateKbps) + " kbit/s).",
                                averr(r));
    m_enc = std::move(c);

    m_frameSize = m_enc->frame_size;
    if (m_frameSize <= 0 || (codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE))
        m_frameSize = m_cfg.sampleRate / 50; // 20 ms

    const AVCodec* dcodec = avcodec_find_decoder(m_enc->codec_id);
    if (!dcodec) {
        close();
        return MediaError::make(ErrorKind::CodecInit, "No decoder available for the selected audio codec.");
    }
    CodecCtxPtr d(avcodec_alloc_context3(dcodec));
    d->sample_rate = m_enc->sample_rate;
    av_channel_layout_copy(&d->ch_layout, &m_enc->ch_layout);
    d->bits_per_coded_sample = m_enc->bits_per_coded_sample;
    d->pkt_timebase = m_enc->time_base; // lets the Opus decoder trim pre-skip by timestamp
    d->bit_rate = m_enc->bit_rate;
    if (m_enc->extradata_size > 0) {
        d->extradata = static_cast<uint8_t*>(av_mallocz(size_t(m_enc->extradata_size) + AV_INPUT_BUFFER_PADDING_SIZE));
        std::copy_n(m_enc->extradata, m_enc->extradata_size, d->extradata);
        d->extradata_size = m_enc->extradata_size;
    }
    r = avcodec_open2(d.get(), dcodec, nullptr);
    if (r < 0) {
        close();
        return MediaError::make(ErrorKind::CodecInit, "Could not open the audio decoder.", averr(r));
    }
    m_dec = std::move(d);

    m_fifo = av_audio_fifo_alloc(m_enc->sample_fmt, m_cfg.channels, m_frameSize * 4);
    m_pkt = makePacket();
    m_pts = 0;
    return {};
}

void AudioCodecRoundTrip::close()
{
    if (m_fifo) {
        av_audio_fifo_free(m_fifo);
        m_fifo = nullptr;
    }
    m_enc.reset();
    m_dec.reset();
    m_pkt.reset();
    m_inSwr.reset();
    m_outSwr.reset();
    m_inFmt = PcmFormat{AV_SAMPLE_FMT_NONE, 0, 0};
    m_outSwrFmt = AV_SAMPLE_FMT_NONE;
}

double AudioCodecRoundTrip::codecDelayMs() const
{
    if (!m_enc)
        return 0;
    int fifo = m_fifo ? av_audio_fifo_size(m_fifo) : 0;
    return 1000.0 * double(m_enc->initial_padding + m_frameSize + fifo) / double(m_cfg.sampleRate);
}

MediaError AudioCodecRoundTrip::ensureInputResampler(const PcmFormat& in)
{
    if (m_inSwr && in == m_inFmt)
        return {};
    m_inSwr.reset();
    AVChannelLayout inLayout = layoutFor(in.channels);
    SwrContext* s = nullptr;
    int r = swr_alloc_set_opts2(&s, &m_enc->ch_layout, m_enc->sample_fmt, m_enc->sample_rate, &inLayout, in.fmt,
                                in.rate, 0, nullptr);
    if (r < 0 || swr_init(s) < 0) {
        swr_free(&s);
        return MediaError::make(ErrorKind::UnsupportedFormat, "Unsupported microphone sample format.", averr(r));
    }
    m_inSwr.reset(s);
    m_inFmt = in;
    return {};
}

MediaError AudioCodecRoundTrip::process(const uint8_t* data, int frames, const PcmFormat& in, std::vector<int16_t>& out)
{
    if (!m_enc)
        return MediaError::make(ErrorKind::CodecInit, "Audio codec is not open.");
    if (auto e = ensureInputResampler(in))
        return e;

    // Resample straight into the encoder's sample format.
    int maxOut = swr_get_out_samples(m_inSwr.get(), frames);
    if (maxOut > 0) {
        uint8_t** buf = nullptr;
        if (av_samples_alloc_array_and_samples(&buf, nullptr, m_cfg.channels, maxOut, m_enc->sample_fmt, 0) < 0)
            return MediaError::make(ErrorKind::Other, "Out of memory.");
        const uint8_t* src[1] = {data};
        int got = swr_convert(m_inSwr.get(), buf, maxOut, src, frames);
        if (got > 0)
            av_audio_fifo_write(m_fifo, reinterpret_cast<void**>(buf), got);
        av_freep(&buf[0]);
        av_freep(&buf);
    }
    // Bound the FIFO: if the codec ever falls behind, drop old input.
    int excess = av_audio_fifo_size(m_fifo) - m_frameSize * 8;
    if (excess > 0)
        av_audio_fifo_drain(m_fifo, excess);
    return encodeAvailable(out);
}

MediaError AudioCodecRoundTrip::encodeAvailable(std::vector<int16_t>& out)
{
    while (av_audio_fifo_size(m_fifo) >= m_frameSize) {
        FramePtr f = makeFrame();
        f->nb_samples = m_frameSize;
        f->format = m_enc->sample_fmt;
        f->sample_rate = m_enc->sample_rate;
        av_channel_layout_copy(&f->ch_layout, &m_enc->ch_layout);
        if (av_frame_get_buffer(f.get(), 0) < 0)
            return MediaError::make(ErrorKind::Other, "Out of memory.");
        av_audio_fifo_read(m_fifo, reinterpret_cast<void**>(f->data), m_frameSize);
        f->pts = m_pts;
        m_pts += m_frameSize;
        m_encodedSamples += uint64_t(m_frameSize);

        int r = avcodec_send_frame(m_enc.get(), f.get());
        if (r < 0)
            return MediaError::make(ErrorKind::Other, "Audio encoder rejected a frame.", averr(r));
        for (;;) {
            r = avcodec_receive_packet(m_enc.get(), m_pkt.get());
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF)
                break;
            if (r < 0)
                return MediaError::make(ErrorKind::Other, "Audio encoding failed.", averr(r));
            m_encodedBytes += uint64_t(m_pkt->size);
            r = avcodec_send_packet(m_dec.get(), m_pkt.get());
            av_packet_unref(m_pkt.get());
            if (r < 0 && r != AVERROR(EAGAIN) && r != AVERROR_INVALIDDATA)
                return MediaError::make(ErrorKind::Other, "Audio decoding failed.", averr(r));
            for (;;) {
                FramePtr df = makeFrame();
                r = avcodec_receive_frame(m_dec.get(), df.get());
                if (r == AVERROR(EAGAIN) || r == AVERROR_EOF)
                    break;
                if (r < 0)
                    return MediaError::make(ErrorKind::Other, "Audio decoding failed.", averr(r));
                if (auto e = convertDecoded(df.get(), out))
                    return e;
            }
        }
    }
    return {};
}

MediaError AudioCodecRoundTrip::convertDecoded(const AVFrame* f, std::vector<int16_t>& out)
{
    const int fch = f->ch_layout.nb_channels;
    if (!m_outSwr || m_outSwrFmt != f->format || m_outSwrRate != f->sample_rate || m_outSwrChannels != fch) {
        m_outSwr.reset();
        AVChannelLayout outLayout = layoutFor(m_cfg.channels);
        SwrContext* s = nullptr;
        int r = swr_alloc_set_opts2(&s, &outLayout, AV_SAMPLE_FMT_S16, kOutputRate, &f->ch_layout,
                                    AVSampleFormat(f->format), f->sample_rate, 0, nullptr);
        if (r < 0 || swr_init(s) < 0) {
            swr_free(&s);
            return MediaError::make(ErrorKind::Other, "Could not convert decoded audio.", averr(r));
        }
        m_outSwr.reset(s);
        m_outSwrFmt = AVSampleFormat(f->format);
        m_outSwrRate = f->sample_rate;
        m_outSwrChannels = fch;
    }
    int maxOut = swr_get_out_samples(m_outSwr.get(), f->nb_samples);
    if (maxOut <= 0)
        return {};
    std::size_t base = out.size();
    out.resize(base + std::size_t(maxOut) * std::size_t(m_cfg.channels));
    uint8_t* dst[1] = {reinterpret_cast<uint8_t*>(out.data() + base)};
    int got = swr_convert(m_outSwr.get(), dst, maxOut, const_cast<const uint8_t**>(f->extended_data), f->nb_samples);
    out.resize(base + std::size_t(std::max(got, 0)) * std::size_t(m_cfg.channels));
    return {};
}

} // namespace bcs
