#pragma once

#include "core/Errors.h"
#include "core/FfmpegUtil.h"

extern "C" {
#include <libavutil/audio_fifo.h>
}

#include <string>
#include <vector>

namespace bcs {

struct AudioCodecConfig {
    std::string encoder = "libopus";
    int sampleRate = 48000; // codec sample rate (may be snapped per codec)
    int channels = 1;
    int bitrateKbps = 12;
    bool speechMode = true; // Opus "voip" application

    bool operator==(const AudioCodecConfig&) const = default;
};

// Interleaved (packed) PCM description.
struct PcmFormat {
    AVSampleFormat fmt = AV_SAMPLE_FMT_S16;
    int rate = 48000;
    int channels = 1;
    bool operator==(const PcmFormat&) const = default;
};

// In-process audio encode -> decode. Input: any packed PCM format. Output:
// signed 16-bit interleaved PCM at kOutputRate with the configured channel
// count, ready for a virtual-microphone backend. Not thread-safe.
class AudioCodecRoundTrip {
public:
    static constexpr int kOutputRate = 48000;

    AudioCodecRoundTrip() = default;
    ~AudioCodecRoundTrip() { close(); }
    AudioCodecRoundTrip(const AudioCodecRoundTrip&) = delete;
    AudioCodecRoundTrip& operator=(const AudioCodecRoundTrip&) = delete;

    MediaError open(const AudioCodecConfig& requested);
    void close();
    bool isOpen() const { return m_enc != nullptr; }

    MediaError process(const uint8_t* interleaved, int frames, const PcmFormat& in, std::vector<int16_t>& out);

    // The configuration actually in use after snapping to what the codec
    // supports, and a human-readable list of what was adjusted.
    const AudioCodecConfig& effectiveConfig() const { return m_cfg; }
    const std::string& adjustments() const { return m_adjustments; }
    int outputChannels() const { return m_cfg.channels; }

    // Algorithmic delay: encoder look-ahead + one codec frame + FIFO fill.
    double codecDelayMs() const;
    uint64_t encodedBytes() const { return m_encodedBytes; }
    double encodedSeconds() const { return double(m_encodedSamples) / double(m_cfg.sampleRate); }

private:
    AudioCodecConfig snapToCodec(const AVCodec* codec, AudioCodecConfig cfg);
    MediaError ensureInputResampler(const PcmFormat& in);
    MediaError encodeAvailable(std::vector<int16_t>& out);
    MediaError convertDecoded(const AVFrame* f, std::vector<int16_t>& out);

    AudioCodecConfig m_cfg;
    std::string m_adjustments;
    CodecCtxPtr m_enc;
    CodecCtxPtr m_dec;
    PacketPtr m_pkt;
    AVAudioFifo* m_fifo = nullptr;
    SwrPtr m_inSwr;
    PcmFormat m_inFmt{AV_SAMPLE_FMT_NONE, 0, 0};
    SwrPtr m_outSwr;
    AVSampleFormat m_outSwrFmt = AV_SAMPLE_FMT_NONE;
    int m_outSwrRate = 0;
    int m_outSwrChannels = 0;
    int m_frameSize = 960;
    int64_t m_pts = 0;
    uint64_t m_encodedBytes = 0;
    uint64_t m_encodedSamples = 0;
};

} // namespace bcs
