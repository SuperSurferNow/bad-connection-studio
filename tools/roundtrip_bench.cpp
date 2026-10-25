// Milestone 1 prototype: measure the codec round-trip without the UI.
//
//   bcs-roundtrip-bench                       synthetic video + audio sweep
//   bcs-roundtrip-bench --camera /dev/video0  real camera, capture->decode latency
//       [--encoder libx264] [--kbps 150] [--seconds 10] [--size 1280x720]
//   bcs-roundtrip-bench --kbps 8 --seconds 10 --pipewire-mic
//                                             degraded test speech on the PipeWire virtual mic
#include "core/AudioCodecRoundTrip.h"
#include "core/CameraEnumerator.h"
#include "core/FfmpegUtil.h"
#include "core/SelfTest.h"
#include "core/VideoCapture.h"
#include "core/VideoCodecRoundTrip.h"

#if defined(BCS_WITH_PIPEWIRE)
#include "backend/linux/PipeWireMicBackend.h"
#include "core/SyntheticSource.h"
#endif

extern "C" {
#include <libavdevice/avdevice.h>
}

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace bcs;

static int runCamera(const std::string& url, const std::string& encoder, int kbps, int seconds, int w, int h)
{
    CameraDevice dev;
    dev.url = url;
    dev.name = url;
#if defined(_WIN32)
    dev.format = "dshow";
#else
    dev.format = "video4linux2";
#endif
    VideoCapture cap;
    CaptureRequest req;
    req.width = w;
    req.height = h;
    if (auto e = cap.open(dev, req)) {
        std::fprintf(stderr, "camera: %s (%s)\n", e.message.c_str(), e.detail.c_str());
        return 2;
    }
    std::printf("camera: %s\n", cap.description().c_str());
    VideoCodecConfig cfg;
    cfg.encoder = encoder;
    cfg.width = w;
    cfg.height = h;
    cfg.bitrateKbps = kbps;
    VideoCodecRoundTrip rt;
    if (auto e = rt.open(cfg)) {
        std::fprintf(stderr, "codec: %s (%s)\n", e.message.c_str(), e.detail.c_str());
        return 2;
    }
    SwsContext* sws = nullptr;
    std::vector<double> lat;
    int64_t end = nowUs() + int64_t(seconds) * 1000000;
    while (nowUs() < end) {
        FramePtr f;
        MediaError err;
        int r = cap.read(f, err);
        if (r < 0) {
            std::fprintf(stderr, "read: %s\n", err.message.c_str());
            return 3;
        }
        if (r == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        int64_t t0 = nowUs();
        sws = sws_getCachedContext(sws, f->width, f->height, AVPixelFormat(f->format), w, h, rt.inputPixelFormat(),
                                   SWS_BILINEAR, nullptr, nullptr, nullptr);
        FramePtr in = makeVideoFrame(w, h, rt.inputPixelFormat());
        sws_scale(sws, f->data, f->linesize, 0, f->height, in->data, in->linesize);
        std::vector<FramePtr> out;
        rt.process(in.get(), out);
        if (!out.empty())
            lat.push_back(double(nowUs() - t0) / 1000.0);
    }
    sws_freeContext(sws);
    std::sort(lat.begin(), lat.end());
    if (lat.empty())
        return 4;
    std::printf("%zu frames, processing latency (convert+encode+decode) median %.1f ms, p95 %.1f ms, max %.1f ms\n",
                lat.size(), lat[lat.size() / 2], lat[lat.size() * 95 / 100], lat.back());
    std::printf("actual bitrate %.0f kbit/s (target %d)\n", double(rt.encodedBytes()) * 8 / 1000 / seconds, kbps);
    return 0;
}

#if defined(BCS_WITH_PIPEWIRE)
// Publishes the degraded synthetic speech on the PipeWire virtual microphone
// in real time, so it can be checked with pw-record or a call app.
static int runPipeWireMic(int kbps, int seconds)
{
    PipeWireMicBackend mic;
    BackendStatus st = mic.probe();
    std::printf("%s\n", st.summary.c_str());
    if (auto e = mic.open(PipeWireMicBackend::kDeviceId, 48000, 1)) {
        std::fprintf(stderr, "open: %s (%s)\n", e.message.c_str(), e.detail.c_str());
        return 2;
    }
    AudioCodecRoundTrip rt;
    AudioCodecConfig cfg;
    cfg.bitrateKbps = kbps;
    if (auto e = rt.open(cfg)) {
        std::fprintf(stderr, "codec: %s\n", e.message.c_str());
        return 2;
    }
    auto speech = syntheticSpeech(48000, seconds);
    PcmFormat in{AV_SAMPLE_FMT_S16, 48000, 1};
    std::vector<int16_t> out;
    int64_t t0 = nowUs();
    for (std::size_t pos = 0; pos + 480 <= speech.size(); pos += 480) {
        out.clear();
        rt.process(reinterpret_cast<const uint8_t*>(speech.data() + pos), 480, in, out);
        mic.write(out.data(), out.size());
        int64_t due = t0 + int64_t(pos + 480) * 1000000 / 48000; // real-time pacing, like a mic
        std::this_thread::sleep_for(std::chrono::microseconds(std::max<int64_t>(0, due - nowUs())));
        if (auto e = mic.takeAsyncError()) {
            std::fprintf(stderr, "async: %s\n", e.message.c_str());
            return 3;
        }
    }
    std::printf("streamed %d s at %d kbit/s; output buffer %.1f ms; %s\n", seconds, kbps, mic.bufferedMs(),
                mic.describe().c_str());
    mic.close();
    return 0;
}
#endif

int main(int argc, char** argv)
{
    avdevice_register_all();
    std::string camera, encoder;
    int kbps = 150, seconds = 10, w = 1280, h = 720;
    for (int i = 1; i < argc; ++i) {
        auto arg = [&](const char* n) { return std::strcmp(argv[i], n) == 0 && i + 1 < argc; };
        if (arg("--camera"))
            camera = argv[++i];
        else if (arg("--encoder"))
            encoder = argv[++i];
        else if (arg("--kbps"))
            kbps = std::atoi(argv[++i]);
        else if (arg("--seconds"))
            seconds = std::atoi(argv[++i]);
        else if (arg("--size"))
            std::sscanf(argv[++i], "%dx%d", &w, &h);
#if defined(BCS_WITH_PIPEWIRE)
        else if (std::strcmp(argv[i], "--pipewire-mic") == 0)
            return runPipeWireMic(kbps, seconds);
#endif
        else if (std::strcmp(argv[i], "--list") == 0) {
            for (const auto& c : enumerateCameras(true))
                std::printf("%s\t%s\t%s\n", c.url.c_str(), c.name.c_str(), c.problem.c_str());
            return 0;
        }
    }
    if (encoder.empty())
        for (const auto& c : videoCodecChoices())
            if (c.available && encoder.empty())
                encoder = c.encoder;
    if (!camera.empty())
        return runCamera(camera, encoder, kbps, seconds, w, h);

    std::printf("Synthetic video, %dx%d @ 30 fps, encoder %s\n", w, h, encoder.c_str());
    for (const auto& p : measureVideoRoundTrip(encoder, w, h, 30, {50, 100, 150, 300, 600, 1000, 2000}, 120))
        std::printf("  target %5d  actual %7.1f kbit/s  PSNR %5.2f dB  codec %5.2f ms/frame  frames %d/%d\n",
                    p.targetKbps, p.actualKbps, p.psnrDb, p.avgCodecMs, p.framesOut, p.framesIn);
    std::printf("Synthetic speech, libopus mono\n");
    for (const auto& p : measureAudioRoundTrip("libopus", 1, {6, 8, 12, 16, 24, 32, 48, 64}, 5.0))
        std::printf("  target %3d  actual %5.1f kbit/s  spectral distance %5.2f dB  codec delay %4.1f ms\n",
                    p.targetKbps, p.actualKbps, p.spectralDistanceDb, p.codecDelayMs);
    return 0;
}
