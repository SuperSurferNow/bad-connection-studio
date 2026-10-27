#include "check.h"
#include "core/LoopClip.h"
#include "core/SyntheticSource.h"
#include "core/VideoCapture.h"
#include "core/VideoPipeline.h"

#include <cstdlib>
#include <QCoreApplication>
#include <QElapsedTimer>

#include <chrono>
#include <cstdio>
#include <thread>

using namespace bcs;
using namespace std::chrono_literals;

static void runFor(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    // ---- record ~0.8 s of synthetic video + audio, in real time
    {
        LoopClip clip;
        CHECK(clip.state() == LoopClip::State::Empty);
        SyntheticVideo src(640, 360);
        clip.beginRecording();
        CHECK(clip.isRecording());
        FramePtr first;
        std::vector<int16_t> chunk(480 * 2); // 10 ms stereo
        for (int i = 0; i < 80; ++i) {
            FramePtr f = src.frame(i);
            if (i == 0)
                first.reset(av_frame_clone(f.get()));
            clip.addVideo(f.get());
            for (size_t s = 0; s < chunk.size(); ++s)
                chunk[s] = int16_t((i * 480 + int(s / 2)) % 30000); // ramp: sample position is checkable
            clip.addAudio(chunk.data(), 480, 2);
            std::this_thread::sleep_for(10ms);
        }
        CHECK(clip.finishRecording());
        CHECK(clip.isReady());
        CHECK(clip.hasVideo() && clip.hasAudio());
        CHECK(clip.audioChannels() == 2);
        const int64_t d = clip.durationUs();
        std::printf("clip: %.0f ms, %zu KiB\n", d / 1000.0, clip.bytes() / 1024);
        CHECK(d > 600'000 && d < 1'200'000);
        CHECK(clip.bytes() < size_t(80) * 640 * 360 * 3 / 2 / 4); // much smaller than raw

        // Frame 0 decodes with high fidelity (MJPEG at high quality).
        int idx = -2;
        FramePtr dec = clip.videoFrameAt(0, -1, &idx);
        CHECK(dec != nullptr);
        CHECK(idx == 0);
        if (dec) {
            CHECK(dec->width == 640 && dec->height == 360);
            // Compare in the same pixel format (MJPEG decodes to full-range YUV).
            SwsPtr s(sws_getContext(dec->width, dec->height, AVPixelFormat(dec->format), 640, 360, AV_PIX_FMT_YUV420P,
                                    SWS_BILINEAR, nullptr, nullptr, nullptr));
            FramePtr conv = makeVideoFrame(640, 360, AV_PIX_FMT_YUV420P);
            sws_scale(s.get(), dec->data, dec->linesize, 0, dec->height, conv->data, conv->linesize);
            const double psnr = lumaPsnr(first.get(), conv.get());
            std::printf("loop frame PSNR %.1f dB\n", psnr);
            CHECK(psnr > 32);
        }
        // Same index again: no new frame.
        CHECK(clip.videoFrameAt(0, 0, &idx) == nullptr);
        // Later positions give later frames. Frames are timed by the wall
        // clock, so how many of the 80 fit in the trimmed clip depends on how
        // long the sleeps above really took (Windows timers, busy CI runners).
        int mid = -1, last = -1;
        CHECK(clip.videoFrameAt(d / 2, -1, &mid) != nullptr);
        CHECK(clip.videoFrameAt(d - 1, -1, &last) != nullptr);
        std::printf("frame index at 50%%: %d, at end: %d\n", mid, last);
        CHECK(mid > 0 && last > mid);

        // Loop clock wraps.
        clip.startPlayback();
        const int64_t t0 = clip.playbackStartUs();
        CHECK(clip.positionUs(t0) == 0);
        CHECK(clip.positionUs(t0 + d + 1234) == 1234);
        CHECK(clip.positionUs(t0 + 7 * d + 99) == 99);

        // Audio reads wrap around the loop end, sample-exactly.
        const int64_t len = d * LoopClip::kAudioRate / 1'000'000;
        std::vector<int16_t> a, b;
        CHECK(clip.readAudio(len - 100, 300, a) == 2);
        CHECK(clip.readAudio(0, 200, b) == 2);
        bool same = a.size() == 600 && b.size() == 400;
        for (size_t i = 0; same && i < 400; ++i)
            same = a[200 + i] == b[i];
        CHECK(same);
        // 100 loops later the same absolute position maps to the same sample.
        CHECK(clip.readAudio(100 * len + 4800, 1, a) == 2);
        CHECK(clip.readAudio(4800, 1, b) == 2);
        CHECK(a == b);

        clip.clear();
        CHECK(clip.state() == LoopClip::State::Empty && clip.bytes() == 0);
    }

    // ---- too short a recording is discarded
    {
        LoopClip clip;
        clip.beginRecording();
        SyntheticVideo src(320, 180);
        clip.addVideo(src.frame(0).get());
        CHECK(!clip.finishRecording());
        CHECK(clip.state() == LoopClip::State::Empty);
    }

    // ---- video-only clip through the threaded pipeline: record, loop, back to live
    {
        LoopClip clip;
        VideoPipeline p;
        p.setLoopClip(&clip);
        QObject::connect(&p, &VideoPipeline::previewFrame, &app, [&](const QImage&) { p.previewConsumed(); });
        int problems = 0;
        QObject::connect(&p, &VideoPipeline::problem, &app, [&](int, const QString& m, const QString&) {
            std::fprintf(stderr, "problem: %s\n", qPrintable(m));
            ++problems;
        });
        VideoPipelineConfig cfg;
        cfg.camera = testPatternCamera();
        cfg.codec.encoder = std::getenv("BCS_TEST_ENCODER") ? std::getenv("BCS_TEST_ENCODER") : encoderAvailable("libx264") ? "libx264" : "libvpx";
        cfg.codec.width = 640;
        cfg.codec.height = 360;
        cfg.codec.bitrateKbps = 300;
        cfg.capture.width = 640;
        cfg.capture.height = 360;
        p.start(cfg);
        runFor(500);
        clip.beginRecording();
        runFor(1500);
        CHECK(clip.finishRecording());
        CHECK(clip.hasVideo() && !clip.hasAudio());
        std::printf("pipeline clip: %.0f ms\n", clip.durationUs() / 1000.0);

        clip.startPlayback();
        p.setLooping(true);
        const uint64_t before = p.stats().output;
        runFor(1500);
        p.setBitrate(80); // the slider still applies to the loop
        runFor(1500);
        VideoStats s = p.stats();
        std::printf("looping: output %llu (+%llu), bitrate %d\n", (unsigned long long)s.output,
                    (unsigned long long)(s.output - before), s.bitrateKbps);
        CHECK(s.output - before > 60);
        CHECK(s.bitrateKbps == 80);
        CHECK(p.state() == VideoPipeline::State::Running);

        // Back to live: the camera is read at full rate again, and frames
        // reach the output. How many do is up to the codec and the machine:
        // VP8 (CI's Fedora FFmpeg has no x264) skips frames at 80 kbit/s after
        // the content changes, and slow runners drop late frames by design.
        p.setLooping(false);
        const VideoStats l0 = p.stats();
        runFor(800);
        const VideoStats l1 = p.stats();
        std::printf("live again (%s): captured +%llu, output +%llu, skipped +%llu\n", l1.codec.c_str(),
                    (unsigned long long)(l1.captured - l0.captured), (unsigned long long)(l1.output - l0.output),
                    (unsigned long long)(l1.skippedByEncoder - l0.skippedByEncoder));
        CHECK(l1.captured - l0.captured > 15);
        CHECK(l1.output - l0.output > 5);
        CHECK(p.stats().latencyMs < 150);
        p.stop();
        CHECK(problems == 0);
    }
    return TEST_RESULT();
}
