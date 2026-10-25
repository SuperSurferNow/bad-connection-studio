#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace bcs {

std::string averr(int err);

struct AVFrameDeleter {
    void operator()(AVFrame* f) const { av_frame_free(&f); }
};
struct AVPacketDeleter {
    void operator()(AVPacket* p) const { av_packet_free(&p); }
};
struct AVCodecContextDeleter {
    void operator()(AVCodecContext* c) const { avcodec_free_context(&c); }
};
struct SwsContextDeleter {
    void operator()(SwsContext* c) const { sws_freeContext(c); }
};
struct SwrContextDeleter {
    void operator()(SwrContext* c) const { swr_free(&c); }
};

using FramePtr = std::unique_ptr<AVFrame, AVFrameDeleter>;
using PacketPtr = std::unique_ptr<AVPacket, AVPacketDeleter>;
using CodecCtxPtr = std::unique_ptr<AVCodecContext, AVCodecContextDeleter>;
using SwsPtr = std::unique_ptr<SwsContext, SwsContextDeleter>;
using SwrPtr = std::unique_ptr<SwrContext, SwrContextDeleter>;

FramePtr makeFrame();
PacketPtr makePacket();
// Allocates a frame with buffers of the given geometry.
FramePtr makeVideoFrame(int width, int height, AVPixelFormat fmt);

// Monotonic microseconds, for latency measurements only.
inline int64_t nowUs()
{
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

// Supported-config queries that work across FFmpeg 6.x .. 8.x.
std::vector<AVPixelFormat> supportedPixFmts(const AVCodec* codec);
std::vector<AVSampleFormat> supportedSampleFmts(const AVCodec* codec);
std::vector<int> supportedSampleRates(const AVCodec* codec);

// Encoder exists in this FFmpeg build AND can actually be opened (cached).
bool encoderAvailable(const std::string& name);

struct CodecChoice {
    std::string encoder;     // FFmpeg encoder name
    std::string displayName; // UI name
    bool available = false;
    std::string note;        // licensing / behaviour note
};
// Candidate codecs in preference order, with runtime availability.
std::vector<CodecChoice> videoCodecChoices();
std::vector<CodecChoice> audioCodecChoices();

// Runtime FFmpeg build information for the About box and license report.
struct FfmpegBuildInfo {
    std::string avcodecVersion;
    std::string license;       // as reported by avcodec_license()
    std::string configuration; // ./configure flags
    bool gpl = false;
    bool nonfree = false;
};
FfmpegBuildInfo ffmpegBuildInfo();

// Route FFmpeg's av_log into a callback (warnings and errors only), with
// de-duplication of repeated lines. Never receives media content.
void installFfmpegLogBridge(std::function<void(int level, const std::string& line)> sink);

} // namespace bcs
