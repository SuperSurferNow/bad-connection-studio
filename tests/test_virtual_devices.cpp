#include "check.h"
#include "backend/BackendRegistry.h"
#include "core/SyntheticSource.h"
#include "core/VideoCapture.h"

extern "C" {
#include <libavdevice/avdevice.h>
}

#include <QAudioSource>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QMediaDevices>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

using namespace bcs;
using namespace std::chrono_literals;

// End-to-end check of the real virtual devices, from the consumer's side:
// frames written to the virtual camera must be readable from it like from a
// webcam, and audio written to the virtual microphone must be recordable from
// it like from a microphone. Each half is skipped when its device is absent;
// exit 77 (skipped) if neither is.
static void runFor(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

static const BackendStatus* usable(const std::vector<BackendStatus>& list, const char* id)
{
    for (const auto& s : list)
        if (s.backendId == id && s.available && !s.devices.empty())
            return &s;
    return nullptr;
}

static bool testCamera(const BackendStatus& st)
{
    const std::string path = st.devices.front().id;
    std::printf("virtual camera: %s (%s)\n", st.devices.front().name.c_str(), path.c_str());
    auto cam = createCameraBackend(st.backendId, {});
    if (auto e = cam->open(path, 640, 360, 30)) {
        std::printf("open failed (busy?): %s\n", e.message.c_str());
        return false;
    }
    SyntheticVideo src(640, 360);
    FramePtr ref = src.frame(7);
    std::atomic<bool> run{true};
    std::thread writer([&] {
        while (run) {
            cam->writeFrame(ref.get());
            std::this_thread::sleep_for(33ms);
        }
    });
    std::this_thread::sleep_for(200ms); // exclusive_caps=1: CAPTURE appears once a producer writes

    CameraDevice dev{path, "loopback", path, "video4linux2", true};
    VideoCapture cap;
    MediaError err = cap.open(dev, CaptureRequest{640, 360, 30, ""});
    if (err)
        std::printf("capture open failed: %s %s\n", err.message.c_str(), err.detail.c_str());
    CHECK(!err);
    int frames = 0;
    double psnr = 0;
    QElapsedTimer t;
    t.start();
    while (!err && t.elapsed() < 2000) {
        FramePtr f;
        int r = cap.read(f, err);
        if (r == 0) {
            std::this_thread::sleep_for(2ms);
            continue;
        }
        if (r < 0)
            break;
        ++frames;
        if (f->width == 640 && f->height == 360 && f->format == AV_PIX_FMT_YUV420P)
            psnr = lumaPsnr(ref.get(), f.get());
    }
    cap.close();
    run = false;
    writer.join();
    cam->close(nullptr);
    std::printf("camera: read %d frames (%s), luma PSNR vs written %.1f dB\n", frames, cap.description().c_str(), psnr);
    CHECK(frames > 30);
    CHECK(psnr > 40); // raw transfer: what we write is what call apps see
    return true;
}

static bool testMic(const BackendStatus& st, QCoreApplication&)
{
    auto mic = createMicBackend(st.backendId, {});
    if (auto e = mic->open(st.devices.front().id, 48000, 1)) {
        std::printf("virtual mic open failed: %s\n", e.message.c_str());
        CHECK(false);
        return true;
    }
    const std::vector<int16_t> speech = syntheticSpeech(48000, 2.0);
    std::atomic<bool> run{true};
    std::thread feeder([&] {
        size_t pos = 0;
        auto next = std::chrono::steady_clock::now();
        while (run) {
            mic->write(speech.data() + pos, 480);
            pos = (pos + 480) % (speech.size() - 480);
            next += 10ms;
            std::this_thread::sleep_until(next);
        }
    });

    // The node must show up as a microphone for other programs.
    QAudioDevice input;
    QElapsedTimer t;
    t.start();
    while (input.isNull() && t.elapsed() < 4000) {
        runFor(100);
        for (const auto& d : QMediaDevices::audioInputs())
            if (d.description().contains("Bad Connection"))
                input = d;
    }
    std::printf("virtual mic as input device: %s\n", input.isNull() ? "NOT FOUND" : qPrintable(input.description()));
    CHECK(!input.isNull());

    double peak = 0;
    if (!input.isNull()) {
        QAudioFormat fmt;
        fmt.setSampleRate(48000);
        fmt.setChannelCount(1);
        fmt.setSampleFormat(QAudioFormat::Int16);
        QAudioSource src(input, fmt);
        QIODevice* io = src.start();
        CHECK(io != nullptr);
        t.restart();
        while (io && t.elapsed() < 1500) {
            runFor(50);
            const QByteArray b = io->readAll();
            auto* s = reinterpret_cast<const int16_t*>(b.constData());
            for (qsizetype i = 0; i < b.size() / 2; ++i)
                peak = std::max(peak, std::abs(double(s[i])) / 32768.0);
        }
        src.stop();
    }
    run = false;
    feeder.join();
    mic->close();
    const double db = peak > 0 ? 20 * std::log10(peak) : -100;
    std::printf("mic: recorded peak %.1f dBFS from the virtual microphone\n", db);
    CHECK(db > -30); // our test speech, not silence
    return true;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    avdevice_register_all();
    CapabilityReport rep = probeBackends({});
    bool ran = false;
    if (const BackendStatus* cam = usable(rep.cameras, "v4l2loopback"))
        ran |= testCamera(*cam);
    else
        std::printf("no usable v4l2loopback device: camera part skipped\n");
    if (const BackendStatus* mic = usable(rep.mics, "pipewire"))
        ran |= testMic(*mic, app);
    else
        std::printf("no PipeWire session: microphone part skipped\n");
    if (!ran)
        return 77;
    return TEST_RESULT();
}
