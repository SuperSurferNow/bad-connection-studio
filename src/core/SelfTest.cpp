#include "core/SelfTest.h"

#include "backend/BackendRegistry.h"
#include "core/AudioCodecRoundTrip.h"
#include "core/FfmpegUtil.h"
#include "core/SyntheticSource.h"
#include "core/VideoCodecRoundTrip.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <map>
#include <sstream>

namespace bcs {

std::vector<VideoRoundTripPoint> measureVideoRoundTrip(const std::string& encoder, int width, int height, int fps,
                                                       const std::vector<int>& kbps, int frames)
{
    std::vector<VideoRoundTripPoint> out;
    for (int target : kbps) {
        VideoRoundTripPoint pt;
        pt.targetKbps = target;
        VideoCodecConfig cfg;
        cfg.encoder = encoder;
        cfg.width = width;
        cfg.height = height;
        cfg.fps = fps;
        cfg.bitrateKbps = target;
        VideoCodecRoundTrip rt;
        if (rt.open(cfg)) {
            out.push_back(pt);
            continue;
        }
        SyntheticVideo src(width, height);
        std::map<int64_t, FramePtr> sources; // pts -> original, for PSNR
        double psnrSum = 0;
        int psnrN = 0;
        int64_t codecUs = 0;
        for (int i = 0; i < frames; ++i) {
            FramePtr f = src.frame(i);
            FramePtr keep(av_frame_clone(f.get()));
            std::vector<FramePtr> dec;
            int64_t t0 = nowUs();
            if (rt.process(f.get(), dec))
                break;
            codecUs += nowUs() - t0;
            sources[f->pts] = std::move(keep);
            ++pt.framesIn;
            for (auto& d : dec) {
                ++pt.framesOut;
                pt.outWidth = d->width;
                pt.outHeight = d->height;
                auto it = sources.find(d->pts);
                // Skip the first second: rate control is still converging.
                if (it != sources.end() && i >= fps) {
                    psnrSum += lumaPsnr(it->second.get(), d.get());
                    ++psnrN;
                }
            }
            while (sources.size() > 8)
                sources.erase(sources.begin());
        }
        pt.actualKbps = double(rt.encodedBytes()) * 8.0 / 1000.0 / (double(pt.framesIn) / fps);
        pt.psnrDb = psnrN ? psnrSum / psnrN : 0;
        pt.avgCodecMs = pt.framesIn ? double(codecUs) / 1000.0 / pt.framesIn : 0;
        out.push_back(pt);
    }
    return out;
}

namespace {

void fft(std::vector<std::complex<double>>& a)
{
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2 * 3.14159265358979323846 / double(len);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (std::size_t i = 0; i < n; i += len) {
            std::complex<double> w(1);
            for (std::size_t k = 0; k < len / 2; ++k) {
                auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// Mean log-spectral distance (dB) over 1024-sample Hann frames, 0..20 kHz,
// after aligning the decoded signal by cross-correlation.
double logSpectralDistance(const std::vector<int16_t>& ref, const std::vector<int16_t>& deg)
{
    const std::size_t start = 9600, maxLag = 2400, n = 1024;
    if (ref.size() < start * 2 || deg.size() < start * 2)
        return 99;
    const std::size_t end = std::min(ref.size(), deg.size()) - maxLag - n;
    std::size_t bestLag = 0;
    double best = -1e300;
    for (std::size_t lag = 0; lag <= maxLag; ++lag) {
        double c = 0;
        for (std::size_t i = start; i < start + 24000; i += 3)
            c += double(ref[i]) * deg[i + lag];
        if (c > best) {
            best = c;
            bestLag = lag;
        }
    }
    const std::size_t maxBin = n * 20000 / 48000;
    double total = 0;
    int frames = 0;
    std::vector<std::complex<double>> a(n), b(n);
    for (std::size_t pos = start; pos < end; pos += n / 2) {
        for (std::size_t i = 0; i < n; ++i) {
            double w = 0.5 - 0.5 * std::cos(2 * 3.14159265358979323846 * double(i) / double(n - 1));
            a[i] = ref[pos + i] * w;
            b[i] = deg[pos + bestLag + i] * w;
        }
        fft(a);
        fft(b);
        double d = 0;
        for (std::size_t k = 1; k < maxBin; ++k) {
            // 1e2 floor (~ -110 dBFS per bin) keeps silent bins from dominating.
            double pa = 10 * std::log10(std::norm(a[k]) + 1e2), pb = 10 * std::log10(std::norm(b[k]) + 1e2);
            d += std::abs(pa - pb);
        }
        total += d / double(maxBin - 1);
        ++frames;
    }
    return frames ? total / frames : 99;
}

} // namespace

std::vector<AudioRoundTripPoint> measureAudioRoundTrip(const std::string& encoder, int channels,
                                                       const std::vector<int>& kbps, double seconds)
{
    std::vector<AudioRoundTripPoint> out;
    const std::vector<int16_t> mono = syntheticSpeech(48000, seconds);
    for (int target : kbps) {
        AudioRoundTripPoint pt;
        pt.targetKbps = target;
        AudioCodecRoundTrip rt;
        AudioCodecConfig cfg;
        cfg.encoder = encoder;
        cfg.channels = 1; // quality metric is computed on mono speech
        cfg.bitrateKbps = target;
        (void)channels;
        if (rt.open(cfg)) {
            out.push_back(pt);
            continue;
        }
        std::vector<int16_t> decoded;
        PcmFormat in{AV_SAMPLE_FMT_S16, 48000, 1};
        for (std::size_t pos = 0; pos + 480 <= mono.size(); pos += 480) // 10 ms chunks, like a live mic
            if (rt.process(reinterpret_cast<const uint8_t*>(mono.data() + pos), 480, in, decoded))
                break;
        pt.codecDelayMs = rt.codecDelayMs();
        pt.actualKbps = rt.encodedSeconds() > 0 ? double(rt.encodedBytes()) * 8.0 / 1000.0 / rt.encodedSeconds() : 0;
        pt.spectralDistanceDb = logSpectralDistance(mono, decoded);
        out.push_back(pt);
    }
    return out;
}

SelfTestResult runSelfTest(const std::function<void(const std::string&)>& emit)
{
    SelfTestResult res;
    std::ostringstream r;
    std::size_t emitted = 0;
    // Stream finished sections immediately, so a crash in a later stage
    // (e.g. a host library during backend probing) still shows how far it got.
    auto flush = [&] {
        if (!emit)
            return;
        const std::string all = r.str();
        if (all.size() > emitted)
            emit(all.substr(emitted));
        emitted = all.size();
    };
    bool ok = true;
    auto check = [&](bool cond, const std::string& what) {
        r << (cond ? "  PASS  " : "  FAIL  ") << what << "\n";
        ok &= cond;
    };

    const FfmpegBuildInfo ff = ffmpegBuildInfo();
    r << "Bad Connection Studio " << BCS_VERSION << " self-test\n";
    r << "FFmpeg libavcodec " << ff.avcodecVersion << ", license: " << ff.license << "\n\n";
    flush();

    // ---- video
    std::string venc;
    for (const auto& c : videoCodecChoices())
        if (c.available && venc.empty())
            venc = c.encoder;
    check(!venc.empty(), "a video encoder is available");
    if (!venc.empty()) {
        r << "Video round-trip (" << venc << ", 1280x720 @ 30 fps, synthetic):\n";
        auto pts = measureVideoRoundTrip(venc, 1280, 720, 30, {50, 150, 2000}, 75);
        char line[200];
        for (const auto& p : pts) {
            std::snprintf(line, sizeof line,
                          "    target %5d kbit/s  actual %7.1f kbit/s  PSNR %5.1f dB  %5.1f ms/frame  out %dx%d  %d/%d frames\n",
                          p.targetKbps, p.actualKbps, p.psnrDb, p.avgCodecMs, p.outWidth, p.outHeight, p.framesOut,
                          p.framesIn);
            r << line;
        }
        bool sizes = std::all_of(pts.begin(), pts.end(), [](auto& p) { return p.outWidth == 1280 && p.outHeight == 720; });
        check(sizes, "output resolution stays 1280x720 at every bitrate");
        check(pts.size() == 3 && pts[2].psnrDb > pts[0].psnrDb + 3.0, "quality (PSNR) rises clearly with bitrate");
        check(pts.size() == 3 && pts[0].actualKbps < pts[2].actualKbps, "encoded bitrate follows the target");
    }
    flush();

    // ---- audio
    std::string aenc;
    for (const auto& c : audioCodecChoices())
        if (c.available && aenc.empty())
            aenc = c.encoder;
    check(!aenc.empty(), "an audio encoder is available");
    if (!aenc.empty()) {
        r << "Audio round-trip (" << aenc << ", mono speech-like signal):\n";
        auto pts = measureAudioRoundTrip(aenc, 1, {6, 16, 64}, 4.0);
        char line[200];
        for (const auto& p : pts) {
            std::snprintf(line, sizeof line,
                          "    target %3d kbit/s  actual %5.1f kbit/s  spectral distance %5.2f dB  codec delay %4.1f ms\n",
                          p.targetKbps, p.actualKbps, p.spectralDistanceDb, p.codecDelayMs);
            r << line;
        }
        check(pts.size() == 3 && pts[2].spectralDistanceDb < pts[0].spectralDistanceDb - 1.0,
              "audio fidelity rises with bitrate (spectral distance falls)");
        check(pts.size() == 3 && pts[0].actualKbps < pts[2].actualKbps, "audio bitrate follows the target");
    }
    flush();

    // ---- OS integration (informational)
    r << "\nVirtual device backends (OS components are external; not a failure if missing):\n";
    r << "  platform: " << probeBackends({}).platform << "\n";
    flush();
    // Probe one backend at a time (they load host libraries) so a failure
    // is attributable.
    for (const auto& id : cameraBackendIds()) {
        if (auto b = createCameraBackend(id, {})) {
            BackendStatus s = b->probe();
            r << "  camera  " << (s.available ? "[ready]   " : "[missing] ") << s.displayName << ": " << s.summary << "\n";
            flush();
        }
    }
    for (const auto& id : micBackendIds()) {
        if (auto b = createMicBackend(id, {})) {
            BackendStatus s = b->probe();
            r << "  mic     " << (s.available ? "[ready]   " : "[missing] ") << s.displayName << ": " << s.summary << "\n";
            flush();
        }
    }

    r << "\nResult: " << (ok ? "PASS" : "FAIL") << "\n";
    flush();
    res.passed = ok;
    res.report = r.str();
    return res;
}

} // namespace bcs
