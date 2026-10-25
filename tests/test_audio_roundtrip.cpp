#include "check.h"
#include "core/AudioCodecRoundTrip.h"
#include "core/FfmpegUtil.h"
#include "core/SelfTest.h"
#include "core/SyntheticSource.h"

#include <cstdio>

using namespace bcs;

int main()
{
    CHECK(encoderAvailable("libopus"));
    auto pts = measureAudioRoundTrip("libopus", 1, {6, 12, 24, 64}, 4.0);
    CHECK(pts.size() == 4);
    for (const auto& p : pts)
        std::printf("opus %2d kbit/s: actual %.1f kbit/s, spectral distance %.2f dB, codec delay %.1f ms\n",
                    p.targetKbps, p.actualKbps, p.spectralDistanceDb, p.codecDelayMs);
    if (pts.size() == 4) {
        CHECK(pts[3].spectralDistanceDb < pts[0].spectralDistanceDb - 1.0);
        for (std::size_t i = 1; i < pts.size(); ++i) // monotone: more bits, closer to the original
            CHECK(pts[i].spectralDistanceDb < pts[i - 1].spectralDistanceDb + 0.25);
        CHECK(pts[0].actualKbps < 9.0);  // really low bitrate at the bottom
        CHECK(pts[3].actualKbps > 40.0 && pts[3].actualKbps < 70.0); // constrained VBR: on budget
        for (const auto& p : pts)
            CHECK(p.codecDelayMs < 40.0); // 20 ms frames + look-ahead
    }

    // Reopen mid-stream (what a slider move does) and keep the output rate.
    AudioCodecRoundTrip rt;
    AudioCodecConfig cfg;
    cfg.bitrateKbps = 32;
    CHECK(!rt.open(cfg));
    auto speech = syntheticSpeech(48000, 2.0);
    std::vector<int16_t> out;
    PcmFormat in{AV_SAMPLE_FMT_S16, 48000, 1};
    for (std::size_t pos = 0; pos + 480 <= speech.size(); pos += 480) {
        if (pos == 48000) {
            cfg.bitrateKbps = 6;
            CHECK(!rt.open(cfg));
        }
        CHECK(!rt.process(reinterpret_cast<const uint8_t*>(speech.data() + pos), 480, in, out));
    }
    // ~2 s in -> ~2 s out (minus codec delay at each restart).
    CHECK(out.size() > 48000 * 2 - 4000 && out.size() <= 48000 * 2);

    // Stereo float input from a 44.1 kHz device is resampled correctly.
    cfg.channels = 2;
    CHECK(!rt.open(cfg));
    std::vector<float> f(2 * 441, 0.25f);
    PcmFormat fin{AV_SAMPLE_FMT_FLT, 44100, 2};
    out.clear();
    for (int i = 0; i < 100; ++i)
        CHECK(!rt.process(reinterpret_cast<const uint8_t*>(f.data()), 441, fin, out));
    CHECK(out.size() > 2 * 40000); // ~1 s of 48 kHz stereo
    return TEST_RESULT();
}
