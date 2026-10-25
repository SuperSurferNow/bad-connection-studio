#include "check.h"
#include "core/VideoCapture.h"
#include "core/VideoPipeline.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTimer>

#include <cstdio>

using namespace bcs;

// Runs the threaded pipeline on the generated test pattern (no camera or
// virtual device needed) and moves the "slider" while it runs.
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
    VideoPipeline p;
    int previews = 0;
    // &app as context: slots run on the main thread (queued), like in the UI.
    QObject::connect(&p, &VideoPipeline::previewFrame, &app, [&](const QImage& img) {
        CHECK(img.width() == 640 && img.height() == 360);
        ++previews;
        p.previewConsumed();
    });
    int problems = 0;
    QObject::connect(&p, &VideoPipeline::problem, &app, [&](int, const QString& m, const QString& d) {
        std::fprintf(stderr, "problem: %s %s\n", qPrintable(m), qPrintable(d));
        ++problems;
    });

    VideoPipelineConfig cfg;
    cfg.camera = testPatternCamera();
    cfg.codec.encoder = encoderAvailable("libx264") ? "libx264" : "libvpx";
    cfg.codec.width = 640;
    cfg.codec.height = 360;
    cfg.codec.bitrateKbps = 150;
    cfg.capture.width = 640;
    cfg.capture.height = 360;

    for (int round = 0; round < 2; ++round) { // stop/restart must work
        p.start(cfg);
        runFor(1000);
        for (int kbps : {50, 2000, 300, 80, 1200}) { // rapid slider moves
            p.setBitrate(kbps);
            runFor(300);
        }
        if (round == 1) { // a rejected live codec change must not drop the feed
            VideoCodecConfig bad = cfg.codec;
            bad.encoder = "no-such-encoder";
            p.setCodecConfig(bad);
            runFor(500);
            CHECK(p.state() == VideoPipeline::State::Running);
            --problems; // that one error report is expected
            p.setBitrate(1200);
            runFor(300);
        }
        VideoStats s = p.stats();
        std::printf("round %d: captured %llu output %llu late-drops %llu latency %.1f ms\n", round,
                    (unsigned long long)s.captured, (unsigned long long)s.output, (unsigned long long)s.droppedLate,
                    s.latencyMs);
        CHECK(p.state() == VideoPipeline::State::Running);
        CHECK(s.output > 50);
        CHECK(s.latencyMs < 150); // no accumulated delay after slider moves
        CHECK(s.bitrateKbps == 1200);
        p.stop();
        CHECK(p.state() == VideoPipeline::State::Stopped);
    }
    CHECK(previews > 20);
    CHECK(problems == 0);
    return TEST_RESULT();
}
