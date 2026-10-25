#include "app/Logging.h"
#include "core/SelfTest.h"
#include "ui/MainWindow.h"

extern "C" {
#include <libavdevice/avdevice.h>
}

#include <QApplication>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QLoggingCategory>

#include <cstdio>
#include <cstring>
#include <memory>

#if defined(_WIN32)
#include <windows.h>
#endif

#if defined(__unix__)
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

    // --self-test runs headless (CI on clean machines), without a display.
    bool selfTest = false;
    for (int i = 1; i < argc; ++i)
        selfTest |= std::strcmp(argv[i], "--self-test") == 0;

    std::unique_ptr<QCoreApplication> app;
    if (selfTest)
        app = std::make_unique<QCoreApplication>(argc, argv);
    else
        app = std::make_unique<QApplication>(argc, argv);
    QGuiApplication::setDesktopFileName("bad-connection-studio");

    QCommandLineParser parser;
    parser.setApplicationDescription("Bad Connection Studio: live low-bitrate camera/microphone degradation");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"self-test", "Run codec round-trip and capability checks, print a report, and exit."});
    QCommandLineOption screenshot("screenshot", "Run the test pattern and save a PNG of the window (docs/CI).", "file");
    parser.addOption(screenshot);
    parser.process(*app);

    bcs::installLogging();
    avdevice_register_all();
    installSignalHandlers(app.get());
    qInfo() << "Bad Connection Studio" << BCS_VERSION << "starting";

    if (selfTest) {
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
    if (parser.isSet(screenshot))
        w.captureScreenshot(parser.value(screenshot), 4000);
    else
        w.runStartupCheck();
    QObject::connect(app.get(), &QCoreApplication::aboutToQuit, &w, [&w] { w.close(); });
    int rc = app->exec();
    qInfo() << "exiting cleanly";
    return rc;
}
