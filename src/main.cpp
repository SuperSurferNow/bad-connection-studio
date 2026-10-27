#include "app/Logging.h"
#include "core/SelfTest.h"
#include "ui/MainWindow.h"
#if defined(BCS_PLATFORM_LINUX)
#include "backend/linux/LoopbackSetup.h"
#include "backend/linux/V4L2Devices.h"
#endif

extern "C" {
#include <libavdevice/avdevice.h>
}

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QLoggingCategory>
#if defined(BCS_PLATFORM_MACOS)
#include <QPermissions>
#endif

#include <cstdio>
#include <cstring>
#include <memory>

#if defined(_WIN32)
#include <windows.h>
#endif

#if defined(__unix__) || defined(__APPLE__)
#include <QSocketNotifier>
#include <csignal>
#include <sys/socket.h>
#include <unistd.h>

namespace {
int g_sigFd[2] = {-1, -1};
void onSignal(int) { [[maybe_unused]] auto n = ::write(g_sigFd[0], "x", 1); }

// SIGINT/SIGTERM (Ctrl+C, logout, systemd stop) -> normal Qt shutdown, so
// pipelines stop and virtual devices are released cleanly.
void installSignalHandlers(QCoreApplication* app)
{
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, g_sigFd) != 0)
        return;
    auto* n = new QSocketNotifier(g_sigFd[1], QSocketNotifier::Read, app);
    QObject::connect(n, &QSocketNotifier::activated, app, [] {
        char c;
        [[maybe_unused]] auto r = ::read(g_sigFd[1], &c, 1);
        QCoreApplication::quit();
    });
    struct sigaction sa {};
    sa.sa_handler = onSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGHUP, &sa, nullptr);
}
} // namespace
#else
namespace {
void installSignalHandlers(QCoreApplication*) {}
} // namespace
#endif

int main(int argc, char** argv)
{
    QCoreApplication::setOrganizationName("BadConnectionStudio");
    QCoreApplication::setApplicationName("BadConnectionStudio");
    QCoreApplication::setApplicationVersion(BCS_VERSION);

    // --self-test and --loopback-plan run headless (CI on clean machines), without a display.
    bool headless = false;
    for (int i = 1; i < argc; ++i)
        headless |= std::strcmp(argv[i], "--self-test") == 0 || std::strcmp(argv[i], "--loopback-plan") == 0;

    std::unique_ptr<QCoreApplication> app;
    if (headless)
        app = std::make_unique<QCoreApplication>(argc, argv);
    else
        app = std::make_unique<QApplication>(argc, argv);
    QGuiApplication::setDesktopFileName("bad-connection-studio");

    QCommandLineParser parser;
    parser.setApplicationDescription("Bad Connection Studio: live low-bitrate camera/microphone degradation");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"self-test", "Run codec round-trip and capability checks, print a report, and exit."});
    parser.addOption({"loopback-plan", "Linux: print the detected distribution, kernel and virtual camera setup "
                                       "plan (read-only), and exit."});
    QCommandLineOption screenshot("screenshot", "Run the test pattern and save a PNG of the window (docs/CI).", "file");
    parser.addOption(screenshot);
    parser.process(*app);

    bcs::installLogging();
    avdevice_register_all();
    installSignalHandlers(app.get());
    qInfo() << "Bad Connection Studio" << BCS_VERSION << "starting";

    if (parser.isSet("loopback-plan")) {
#if defined(BCS_PLATFORM_LINUX)
        namespace lb = bcs::loopback;
        const auto d = lb::detectDistro();
        const auto k = lb::detectKernel();
        const auto s = lb::evaluate(bcs::v4l2::loopbackModuleLoaded(), d, k);
        std::fputs(lb::planReport(d, k, s, lb::buildPlan(d, k, s, true)).c_str(), stdout);
        return 0;
#else
        std::fputs("--loopback-plan is only available on Linux\n", stderr);
        return 2;
#endif
    }

    if (parser.isSet("self-test")) {
#if defined(_WIN32)
        // GUI-subsystem binary: when started from a console without
        // redirection, attach to it so the report is visible.
        HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
        if ((out == nullptr || out == INVALID_HANDLE_VALUE) && AttachConsole(ATTACH_PARENT_PROCESS))
            std::freopen("CONOUT$", "w", stdout);
#endif
        bcs::SelfTestResult r = bcs::runSelfTest([](const std::string& chunk) {
            std::fputs(chunk.c_str(), stdout);
            std::fflush(stdout);
        });
        return r.passed ? 0 : 1;
    }

    bcs::MainWindow w;
    w.show();
#if defined(BCS_PLATFORM_MACOS) && QT_CONFIG(permissions)
    // Ask up front (one system prompt each) instead of failing on first use.
    // A denial is reported when the device is opened, with a hint.
    app->requestPermission(QCameraPermission{}, &w, [&w](const QPermission& cam) {
        qInfo() << "camera permission:" << (cam.status() == Qt::PermissionStatus::Granted ? "granted" : "denied");
        qApp->requestPermission(QMicrophonePermission{}, &w, [](const QPermission& mic) {
            qInfo() << "microphone permission:"
                    << (mic.status() == Qt::PermissionStatus::Granted ? "granted" : "denied");
        });
    });
#endif
    if (parser.isSet(screenshot))
        w.captureScreenshot(parser.value(screenshot), 4000);
    else
        w.runStartupCheck();
    QObject::connect(app.get(), &QCoreApplication::aboutToQuit, &w, [&w] { w.close(); });
    int rc = app->exec();
    qInfo() << "exiting cleanly";
    return rc;
}
