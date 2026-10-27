#pragma once

#include "backend/linux/LoopbackSetup.h"

#include <QDialog>
#include <QProcess>

class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTextBrowser;

namespace bcs {

// Separate window for the one privileged operation in the app: installing and
// loading v4l2loopback. It previews every command, runs nothing until the user
// clicks Run, and then pipes the fixed helper script to
// `pkexec systemd-run ... bash -s` with only allow-listed action names.
class LoopbackSetupDialog : public QDialog {
    Q_OBJECT
public:
    explicit LoopbackSetupDialog(QWidget* parent = nullptr);
    ~LoopbackSetupDialog() override;

    // Current state without a dialog (startup check).
    static loopback::State currentState(loopback::DistroInfo* d = nullptr, loopback::KernelInfo* k = nullptr);
    static QString helperScript();

private:
    void refresh();
    void run();
    void openInTerminal();
    void appendOutput(const QByteArray& chunk);
    void onFinished(int exitCode, QProcess::ExitStatus status);
    QStringList actions() const;

    loopback::DistroInfo m_distro;
    loopback::KernelInfo m_kernel;
    loopback::State m_state = loopback::State::NotInstalled;
    loopback::InstallPlan m_plan;

    QLabel* m_status = nullptr;
    QTextBrowser* m_planView = nullptr;
    QPlainTextEdit* m_log = nullptr;
    QCheckBox* m_persist = nullptr;
    QPushButton* m_run = nullptr;
    QPushButton* m_terminal = nullptr;
    QPushButton* m_recheck = nullptr;
    QPushButton* m_copy = nullptr;
    QProcess* m_proc = nullptr;
    QByteArray m_pending;
    QString m_output;
};

} // namespace bcs
