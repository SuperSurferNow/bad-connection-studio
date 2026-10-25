#pragma once

#include "core/Errors.h"
#include "core/FfmpegUtil.h"

#include <algorithm>
#include <string>
#include <vector>

namespace bcs {

enum class EncoderPreset { Fastest, Fast, Balanced, Quality };

struct VideoCodecConfig {
    std::string encoder = "libvpx";
    int width = 1280;
    int height = 720;
    int fps = 30;
    int bitrateKbps = 150;
    double keyframeIntervalSec = 2.0;
    EncoderPreset preset = EncoderPreset::Fast;
    bool lowLatency = true;
    bool allowFrameDrop = true; // encoder may skip frames to stay on budget

    bool sameStreamShape(const VideoCodecConfig& o) const
    {
        return encoder == o.encoder && width == o.width && height == o.height && fps == o.fps &&
               keyframeIntervalSec == o.keyframeIntervalSec && preset == o.preset && lowLatency == o.lowLatency &&
               allowFrameDrop == o.allowFrameDrop;
    }
};

// In-process encode -> decode of raw frames. Not thread-safe; owned by the
// codec worker thread. The resolution of the output always equals the
// configured width/height: bitrate is the only thing the quality slider
// changes.
class VideoCodecRoundTrip {
public:
    VideoCodecRoundTrip() = default;
    ~VideoCodecRoundTrip() { close(); }
    VideoCodecRoundTrip(const VideoCodecRoundTrip&) = delete;
    VideoCodecRoundTrip& operator=(const VideoCodecRoundTrip&) = delete;

    MediaError open(const VideoCodecConfig& cfg);
    void close();
    bool isOpen() const { return m_enc != nullptr; }

    // True if the encoder accepts bitrate changes mid-stream (x264).
    bool supportsLiveBitrate() const;
    // Applies the new bitrate in place; returns false if a reopen is needed.
    bool setBitrateLive(int kbps);

    // Pixel format the input frames must have (frame size = cfg width/height).
    AVPixelFormat inputPixelFormat() const { return m_inputFmt; }

    // Encode one frame and decode whatever packets result. Decoded frames
    // (any pixel format the decoder produces) are appended to `out`.
    MediaError process(AVFrame* input, std::vector<FramePtr>& out);
    void requestKeyframe() { m_forceKey = true; }

    const VideoCodecConfig& config() const { return m_cfg; }
    uint64_t encodedBytes() const { return m_encodedBytes; }
    uint64_t encodedPackets() const { return m_encodedPackets; }
    double lastEncodeMs() const { return m_lastEncodeMs; }
    double lastDecodeMs() const { return m_lastDecodeMs; }
    // Input frames that produced no output: frames the encoder skipped to
    // meet the bitrate (low-latency encoders have no other reason to hold one).
    int64_t skippedFrames() const { return std::max<int64_t>(0, m_sent - m_decoded); }

private:
    MediaError openEncoder();
    MediaError openDecoder();

    VideoCodecConfig m_cfg;
    CodecCtxPtr m_enc;
    CodecCtxPtr m_dec;
    PacketPtr m_pkt;
    AVPixelFormat m_inputFmt = AV_PIX_FMT_YUV420P;
    int64_t m_pts = 0;
    bool m_forceKey = true;
    uint64_t m_encodedBytes = 0;
    uint64_t m_encodedPackets = 0;
    double m_lastEncodeMs = 0;
    double m_lastDecodeMs = 0;
    int64_t m_sent = 0;
    int64_t m_decoded = 0;
};

} // namespace bcs
