#include "check.h"
#include "core/FfmpegUtil.h"
#include "core/SelfTest.h"
#include "core/SyntheticSource.h"
#include "core/VideoCodecRoundTrip.h"

#include <cstdio>

using namespace bcs;

int main()
{
    int tested = 0;
    for (const auto& c : videoCodecChoices()) {
        if (!c.available)
            continue;
        ++tested;
        auto pts = measureVideoRoundTrip(c.encoder, 640, 360, 30, {50, 2000}, 60);
        CHECK(pts.size() == 2);
        if (pts.size() != 2)
            continue;
        std::printf("%-12s 50 kbit/s: %.1f dB (actual %.0f)   2000 kbit/s: %.1f dB (actual %.0f)\n", c.encoder.c_str(),
                    pts[0].psnrDb, pts[0].actualKbps, pts[1].psnrDb, pts[1].actualKbps);
        // Acceptance: resolution never changes with bitrate...
        CHECK(pts[0].outWidth == 640 && pts[0].outHeight == 360);
        CHECK(pts[1].outWidth == 640 && pts[1].outHeight == 360);
        // ...but quality does.
        CHECK(pts[1].psnrDb > pts[0].psnrDb + 2.0);
    }
    CHECK(tested > 0);

    // Live bitrate change (x264) or encoder restart keeps the size and keeps
    // producing frames with no growing backlog.
    VideoCodecConfig cfg;
    cfg.encoder = encoderAvailable("libx264") ? "libx264" : "libvpx";
    cfg.width = 640;
    cfg.height = 360;
    cfg.bitrateKbps = 1500;
    VideoCodecRoundTrip rt;
    CHECK(!rt.open(cfg));
    SyntheticVideo src(640, 360);
    int outFrames = 0;
    for (int i = 0; i < 90; ++i) {
        if (i == 30 && !rt.setBitrateLive(60)) {
            cfg.bitrateKbps = 60;
            CHECK(!rt.open(cfg));
        }
        auto f = src.frame(i);
        std::vector<FramePtr> out;
        CHECK(!rt.process(f.get(), out));
        for (auto& o : out) {
            CHECK(o->width == 640 && o->height == 360);
            ++outFrames;
        }
    }
    CHECK(outFrames > 40);
    return TEST_RESULT();
}
