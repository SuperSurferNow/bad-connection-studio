#include "core/FfmpegUtil.h"

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/log.h>
}

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>

namespace bcs {

std::string averr(int err)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(err, buf, sizeof(buf));
    return buf;
}

FramePtr makeFrame() { return FramePtr(av_frame_alloc()); }
PacketPtr makePacket() { return PacketPtr(av_packet_alloc()); }

FramePtr makeVideoFrame(int width, int height, AVPixelFormat fmt)
{
    FramePtr f = makeFrame();
    if (!f)
        return nullptr;
    f->width = width;
    f->height = height;
    f->format = fmt;
    if (av_frame_get_buffer(f.get(), 0) < 0)
        return nullptr;
    return f;
}

// FFmpeg 7.1 (libavcodec 61.13) added avcodec_get_supported_config() and
// deprecated the AVCodec arrays, which are removed in later majors.
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(61, 13, 100)
#define BCS_HAVE_SUPPORTED_CONFIG 1
#endif

template <typename T, typename Sentinel>
static std::vector<T> collect(const T* arr, Sentinel end, int count = -1)
{
    std::vector<T> out;
    if (!arr)
        return out;
    for (int i = 0; count < 0 ? arr[i] != end : i < count; ++i)
        out.push_back(arr[i]);
    return out;
}

std::vector<AVPixelFormat> supportedPixFmts(const AVCodec* codec)
{
#ifdef BCS_HAVE_SUPPORTED_CONFIG
    const void* cfg = nullptr;
    int n = 0;
    if (avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_PIX_FORMAT, 0, &cfg, &n) < 0)
        return {};
    return collect(static_cast<const AVPixelFormat*>(cfg), AV_PIX_FMT_NONE, n);
#else
    return collect(codec->pix_fmts, AV_PIX_FMT_NONE);
#endif
}

std::vector<AVSampleFormat> supportedSampleFmts(const AVCodec* codec)
{
#ifdef BCS_HAVE_SUPPORTED_CONFIG
    const void* cfg = nullptr;
    int n = 0;
    if (avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0, &cfg, &n) < 0)
        return {};
    return collect(static_cast<const AVSampleFormat*>(cfg), AV_SAMPLE_FMT_NONE, n);
#else
    return collect(codec->sample_fmts, AV_SAMPLE_FMT_NONE);
#endif
}

std::vector<int> supportedSampleRates(const AVCodec* codec)
{
#ifdef BCS_HAVE_SUPPORTED_CONFIG
    const void* cfg = nullptr;
    int n = 0;
    if (avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_SAMPLE_RATE, 0, &cfg, &n) < 0)
        return {};
    return collect(static_cast<const int*>(cfg), 0, n);
#else
    return collect(codec->supported_samplerates, 0);
#endif
}

namespace {

// Opening a tiny encoder catches registered-but-unusable encoders, e.g.
// libopenh264 built against Fedora's "noopenh264" stub library.
bool tryOpen(const AVCodec* codec)
{
    AVCodecContext* c = avcodec_alloc_context3(codec);
    if (!c)
        return false;
    if (codec->type == AVMEDIA_TYPE_VIDEO) {
        c->width = 320;
        c->height = 240;
        c->time_base = AVRational{1, 30};
        c->framerate = AVRational{30, 1};
        auto fmts = supportedPixFmts(codec);
        c->pix_fmt = fmts.empty() ? AV_PIX_FMT_YUV420P : fmts.front();
        c->bit_rate = 300000;
    } else {
        auto rates = supportedSampleRates(codec);
        c->sample_rate = rates.empty() ? 48000 : rates.front();
        for (int r : rates)
            if (r == 48000)
                c->sample_rate = r;
        auto fmts = supportedSampleFmts(codec);
        c->sample_fmt = fmts.empty() ? AV_SAMPLE_FMT_S16 : fmts.front();
        av_channel_layout_default(&c->ch_layout, 1);
        c->bit_rate = 32000;
        c->time_base = AVRational{1, c->sample_rate};
        c->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
    }
    int saved = av_log_get_level();
    av_log_set_level(AV_LOG_QUIET); // expected failures are not errors
    bool ok = avcodec_open2(c, codec, nullptr) >= 0;
    av_log_set_level(saved);
    avcodec_free_context(&c);
    return ok;
}

} // namespace

bool encoderAvailable(const std::string& name)
{
    static std::mutex m;
    static std::map<std::string, bool> cache;
    std::lock_guard lk(m);
    if (auto it = cache.find(name); it != cache.end())
        return it->second;
    const AVCodec* codec = avcodec_find_encoder_by_name(name.c_str());
    bool ok = codec && tryOpen(codec);
    cache[name] = ok;
    return ok;
}

std::vector<CodecChoice> videoCodecChoices()
{
    std::vector<CodecChoice> v = {
        {"libx264", "H.264 (x264, zerolatency)", false, "Tracks the target bitrate closely; live bitrate changes. GPL"},
        {"libvpx", "VP8 (libvpx, realtime)", false, "BSD; skips frames when starved (stutter + blur)"},
        {"libopenh264", "H.264 (OpenH264)", false, "BSD; Cisco binary for patent coverage"},
        {"libvpx-vp9", "VP9 (libvpx, realtime)", false, "BSD; higher CPU, higher bitrate floor"},
        {"mpeg4", "MPEG-4 Part 2 (built-in)", false, "LGPL; classic blocky artifacts, floor ~100 kbit/s at 720p"},
        {"mpeg2video", "MPEG-2 (built-in)", false, "LGPL; very blocky"},
    };
    for (auto& c : v)
        c.available = encoderAvailable(c.encoder);
    return v;
}

std::vector<CodecChoice> audioCodecChoices()
{
    std::vector<CodecChoice> v = {
        {"libopus", "Opus (libopus, VoIP mode)", false, "BSD; best speech codec, 6-64 kbit/s"},
        {"opus", "Opus (FFmpeg native, experimental)", false, "CELT-only, 48 kHz"},
        {"libspeex", "Speex (libspeex)", false, "BSD; legacy narrowband/wideband speech"},
        {"aac", "AAC-LC (built-in)", false, "Poor below ~16 kbit/s"},
    };
    for (auto& c : v)
        c.available = encoderAvailable(c.encoder);
    return v;
}

FfmpegBuildInfo ffmpegBuildInfo()
{
    FfmpegBuildInfo info;
    unsigned v = avcodec_version();
    info.avcodecVersion = std::to_string(AV_VERSION_MAJOR(v)) + "." + std::to_string(AV_VERSION_MINOR(v)) +
                          "." + std::to_string(AV_VERSION_MICRO(v));
    info.license = avcodec_license();
    info.configuration = avcodec_configuration();
    info.gpl = info.configuration.find("--enable-gpl") != std::string::npos;
    info.nonfree = info.configuration.find("--enable-nonfree") != std::string::npos;
    return info;
}

namespace {
std::mutex g_logMutex;
std::function<void(int, const std::string&)> g_logSink;
std::string g_lastLine;
int g_repeat = 0;

void avLogCallback(void* avcl, int level, const char* fmt, va_list vl)
{
    if (level > AV_LOG_WARNING)
        return;
    char line[1024];
    int prefix = 1;
    va_list copy;
    va_copy(copy, vl);
    av_log_format_line(avcl, level, fmt, copy, line, sizeof(line), &prefix);
    va_end(copy);
    std::string s(line);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
        s.pop_back();
    if (s.empty())
        return;

    std::lock_guard lk(g_logMutex);
    if (!g_logSink)
        return;
    // Encoders at extreme bitrates repeat the same rate-control warning per
    // frame; collapse those so the log stays useful.
    if (s == g_lastLine) {
        if (++g_repeat % 500 != 0)
            return;
        s += " (repeated 500x)";
    } else {
        g_lastLine = s;
        g_repeat = 0;
    }
    g_logSink(level, s);
}
} // namespace

void installFfmpegLogBridge(std::function<void(int, const std::string&)> sink)
{
    {
        std::lock_guard lk(g_logMutex);
        g_logSink = std::move(sink);
    }
    av_log_set_callback(avLogCallback);
}

} // namespace bcs
