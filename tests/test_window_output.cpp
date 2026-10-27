#include "check.h"
#include "backend/BackendRegistry.h"
#include "backend/common/WindowOutputBackend.h"
#include "core/SyntheticSource.h"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QWindow>

#include <thread>

using namespace bcs;

namespace {

QWindow* outputWindow()
{
    for (QWindow* w : QGuiApplication::topLevelWindows())
        if (w->title() == QString::fromUtf8(WindowOutputBackend::kWindowTitle))
            return w;
    return nullptr;
}

void pump(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

} // namespace

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);

    // ---- the window opens, follows the frame size and closes
    {
        WindowOutputBackend b;
        BackendStatus st = b.probe();
        CHECK(st.available);
        CHECK(st.devices.size() == 1);

        CHECK(!b.open(st.devices[0].id, 320, 180, 30));
        pump(50);
        QWindow* w = outputWindow();
        CHECK(w && w->isVisible());
        CHECK(w && w->size() == QSize(320, 180));

        // Frames come from another thread, as from the video output thread.
        SyntheticVideo src(320, 180), big(640, 360);
        std::thread out([&] {
            for (int i = 0; i < 30; ++i)
                CHECK(!b.writeFrame(src.frame(i).get()));
            CHECK(!b.writeFrame(big.frame(0).get()));
        });
        out.join();
        pump(100);
        w = outputWindow();
        CHECK(w && w->size() == QSize(640, 360));
        std::printf("window: %s\n", b.describe().c_str());

        b.close(nullptr);
        CHECK(!b.isOpen());
        CHECK(b.writeFrame(src.frame(0).get())); // error once closed
        pump(50);
        CHECK(outputWindow() == nullptr);

        // Reopen, then destroy the backend while open: the window goes too.
        CHECK(!b.open({}, 320, 180, 30));
    }
    pump(50);
    CHECK(outputWindow() == nullptr);

    // ---- it never hides a missing native camera (except as the only backend)
    {
        CapabilityReport rep;
        BackendStatus native, window;
        native.backendId = "native";
        window.backendId = WindowOutputBackend::kId;
        window.available = true;
        rep.cameras = {native, window};
        CHECK(rep.activeCamera() == nullptr);
        rep.cameras = {window};
        CHECK(rep.activeCamera() != nullptr);
    }

    return TEST_RESULT();
}
