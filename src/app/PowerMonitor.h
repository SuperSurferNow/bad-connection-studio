#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>

namespace bcs {

// Emits aboutToSleep()/resumed() around system suspend so pipelines can
// release devices cleanly and reopen them afterwards.
//   Linux:   systemd-logind PrepareForSleep(bool) on the system D-Bus
//   Windows: WM_POWERBROADCAST (PBT_APMSUSPEND / PBT_APMRESUMEAUTOMATIC)
// Device-change notifications on Windows (WM_DEVICECHANGE) are forwarded too.
class PowerMonitor : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    explicit PowerMonitor(QObject* parent = nullptr);
    ~PowerMonitor() override;

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

Q_SIGNALS:
    void aboutToSleep();
    void resumed();
    void devicesChanged();

private Q_SLOTS:
    void onPrepareForSleep(bool sleeping);
};

} // namespace bcs
