#include "app/PowerMonitor.h"

#include <QCoreApplication>
#include <QLoggingCategory>

#if defined(BCS_HAVE_DBUS)
#include <QDBusConnection>
#endif

#if defined(_WIN32)
#include <windows.h>
#endif

Q_LOGGING_CATEGORY(lcPower, "bcs.power")

namespace bcs {

PowerMonitor::PowerMonitor(QObject* parent) : QObject(parent)
{
#if defined(BCS_HAVE_DBUS)
    bool ok = QDBusConnection::systemBus().connect("org.freedesktop.login1", "/org/freedesktop/login1",
                                                   "org.freedesktop.login1.Manager", "PrepareForSleep", this,
                                                   SLOT(onPrepareForSleep(bool)));
    if (!ok)
        qCInfo(lcPower) << "logind not reachable; sleep/resume handled by device-error recovery only";
#endif
    QCoreApplication::instance()->installNativeEventFilter(this);
}

PowerMonitor::~PowerMonitor()
{
    if (auto* app = QCoreApplication::instance())
        app->removeNativeEventFilter(this);
}

void PowerMonitor::onPrepareForSleep(bool sleeping)
{
    qCInfo(lcPower) << (sleeping ? "system going to sleep" : "system resumed");
    if (sleeping)
        Q_EMIT aboutToSleep();
    else
        Q_EMIT resumed();
}

bool PowerMonitor::nativeEventFilter(const QByteArray& eventType, void* message, qintptr*)
{
#if defined(_WIN32)
    if (eventType == "windows_generic_MSG") {
        const MSG* msg = static_cast<const MSG*>(message);
        if (msg->message == WM_POWERBROADCAST) {
            if (msg->wParam == PBT_APMSUSPEND)
                onPrepareForSleep(true);
            else if (msg->wParam == PBT_APMRESUMEAUTOMATIC)
                onPrepareForSleep(false);
        } else if (msg->message == WM_DEVICECHANGE) {
            Q_EMIT devicesChanged();
        }
    }
#else
    (void)eventType;
    (void)message;
#endif
    return false;
}

} // namespace bcs
