#include "check.h"
#include "backend/BackendRegistry.h"
#include "core/AudioPipeline.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QMediaDevices>

#include <cstdio>

using namespace bcs;

// End-to-end audio pipeline on whatever input the session offers. Skipped
// (exit 77) on machines without an audio server, e.g. bare CI containers.
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
    const auto inputs = QMediaDevices::audioInputs();
    if (inputs.isEmpty()) {
        std::printf("no audio input devices: skipped\n");
        return 77;
    }
    CapabilityReport rep = probeBackends({});
    const BackendStatus* mic = rep.activeMic();
    // BCS_TEST_MIC_BACKEND=audio-output exercises the fallback backend.
    if (const QByteArray want = qgetenv("BCS_TEST_MIC_BACKEND"); !want.isEmpty()) {
        mic = nullptr;
        for (const auto& s : rep.mics)
            if (s.backendId == want.toStdString() && s.available)
                mic = &s;
        CHECK(mic != nullptr);
    }

    AudioPipeline p;
    int problems = 0;
    QObject::connect(&p, &AudioPipeline::problem, [&](int, const QString& m, const QString& d) {
        std::fprintf(stderr, "problem: %s %s\n", qPrintable(m), qPrintable(d));
        ++problems;
    });

    AudioPipelineConfig cfg;
    cfg.input = inputs.front();
    cfg.codec.bitrateKbps = 24;
    if (mic && !mic->devices.empty()) {
        cfg.micBackendId = mic->backendId;
        cfg.micDeviceId = mic->devices.front().id;
    }
    std::printf("input: %s, virtual mic: %s\n", qPrintable(cfg.input.description()),
                mic ? mic->displayName.c_str() : "none");

    for (int round = 0; round < 2; ++round) {
        p.start(cfg);
        runFor(800);
        for (int kbps : {6, 64, 12, 40}) { // slider moves while running
            p.setBitrate(kbps);
            runFor(400);
        }
        AudioStats s = p.stats();
        std::printf("round %d: state %d, encoded %.2f s, codec %s %d kbit/s, latency est %.1f ms, out %s\n", round,
                    int(p.state()), s.encodedSeconds, s.effective.encoder.c_str(), s.effective.bitrateKbps,
                    s.latencyMs, s.outputDescription.c_str());
        CHECK(p.state() == AudioPipeline::State::Running);
        CHECK(s.encodedSeconds > 1.5);
        CHECK(s.effective.bitrateKbps == 40);
        CHECK(s.latencyMs < 150);
        if (mic)
            CHECK(s.virtualMicOpen);
        p.stop();
        CHECK(p.state() == AudioPipeline::State::Stopped);
    }
    CHECK(problems == 0);
    return TEST_RESULT();
}
