#include "ui/LoopbackSetupDialog.h"

#include "backend/linux/V4L2Devices.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QLoggingCategory>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QVBoxLayout>

Q_LOGGING_CATEGORY(lcSetup, "bcs.setup")

namespace bcs {

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

// Only system locations: never a pkexec/systemd-run found through a PATH the
// AppImage or the user's shell may have changed.
QString systemTool(const QString& name)
{
    const QStringList dirs = {"/usr/bin", "/bin", "/usr/sbin", "/sbin"};
    return QStandardPaths::findExecutable(name, dirs);
}

QProcessEnvironment cleanEnvironment()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const char* k : {"LD_LIBRARY_PATH", "LD_PRELOAD", "APPIMAGE", "APPDIR", "ARGV0", "OWD", "QT_PLUGIN_PATH",
                          "QT_QPA_PLATFORM_PLUGIN_PATH", "PYTHONHOME", "PYTHONPATH", "PERLLIB", "GSETTINGS_SCHEMA_DIR"})
        env.remove(k);
    env.insert("PATH", "/usr/local/bin:/usr/bin:/bin");
    return env;
}

} // namespace

LoopbackSetupDialog::LoopbackSetupDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Set up the virtual camera"));
    resize(760, 620);
    auto* lay = new QVBoxLayout(this);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setTextFormat(Qt::RichText);
    lay->addWidget(m_status);

    m_planView = new QTextBrowser(this);
    m_planView->setOpenExternalLinks(true);
    lay->addWidget(m_planView, 3);

    m_persist = new QCheckBox(tr("Load the virtual camera automatically at every start"), this);
    m_persist->setChecked(true);
    lay->addWidget(m_persist);

    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setPlaceholderText(tr("Output of the setup appears here."));
    QFont mono("monospace");
    mono.setStyleHint(QFont::TypeWriter);
    m_log->setFont(mono);
    lay->addWidget(m_log, 2);

    auto* note = new QLabel(tr("Clicking Run asks for your password through the system's authentication dialog. "
                               "Only the steps listed above run with administrator rights, in an isolated "
                               "service with no access to your home folder. Bad Connection Studio itself never "
                               "runs as administrator."),
                            this);
    note->setWordWrap(true);
    note->setStyleSheet("color: palette(mid);");
    lay->addWidget(note);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    m_run = buttons->addButton(tr("Run"), QDialogButtonBox::AcceptRole);
    m_run->setDefault(true);
    m_terminal = buttons->addButton(tr("Run in terminal instead"), QDialogButtonBox::ActionRole);
    m_copy = buttons->addButton(tr("Copy commands"), QDialogButtonBox::ActionRole);
    m_recheck = buttons->addButton(tr("Re-check"), QDialogButtonBox::ActionRole);
    lay->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_run, &QPushButton::clicked, this, &LoopbackSetupDialog::run);
    connect(m_terminal, &QPushButton::clicked, this, &LoopbackSetupDialog::openInTerminal);
    connect(m_recheck, &QPushButton::clicked, this, &LoopbackSetupDialog::refresh);
    connect(m_persist, &QCheckBox::toggled, this, &LoopbackSetupDialog::refresh);
    connect(m_copy, &QPushButton::clicked, this, [this] {
        QStringList lines;
        for (const auto& st : m_plan.steps)
            for (const auto& c : st.commands)
                lines << "sudo " + qs(c);
        QApplication::clipboard()->setText(lines.join('\n'));
    });
    refresh();
}

LoopbackSetupDialog::~LoopbackSetupDialog()
{
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        // pkexec'd children run as root and cannot be killed from here; just
        // stop listening. The helper finishes on its own.
        m_proc->disconnect(this);
        m_proc->closeWriteChannel();
        m_proc->waitForFinished(100);
    }
}

loopback::State LoopbackSetupDialog::currentState(loopback::DistroInfo* d, loopback::KernelInfo* k)
{
    loopback::DistroInfo dist = loopback::detectDistro();
    loopback::KernelInfo kern = loopback::detectKernel();
    loopback::State s = loopback::evaluate(v4l2::loopbackModuleLoaded(), dist, kern);
    if (d)
        *d = dist;
    if (k)
        *k = kern;
    return s;
}

QString LoopbackSetupDialog::helperScript()
{
    QFile f(":/linux/bcs-loopback-helper.sh");
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll());
}

QStringList LoopbackSetupDialog::actions() const
{
    QStringList out;
    for (const auto& a : m_plan.actions())
        if (loopback::isAllowedAction(a)) // defence in depth; buildPlan only emits these
            out << qs(a);
    return out;
}

void LoopbackSetupDialog::refresh()
{
    m_state = currentState(&m_distro, &m_kernel);
    m_plan = loopback::buildPlan(m_distro, m_kernel, m_state, m_persist->isChecked());
    const bool busy = m_proc && m_proc->state() != QProcess::NotRunning;

    QString head = tr("<b>%1</b> &mdash; kernel %2%3")
                       .arg(qs(m_distro.prettyName).toHtmlEscaped(), qs(m_kernel.running).toHtmlEscaped(),
                            m_kernel.secureBoot ? tr(", Secure Boot on") : QString());
    m_status->setText(head + "<br>" + qs(loopback::describeState(m_state, m_distro, m_kernel)).toHtmlEscaped());

    QString html;
    if (!m_plan.blocker.empty())
        html += "<p><b>" + qs(m_plan.blocker).toHtmlEscaped() + "</b></p>";
    if (!m_plan.steps.empty()) {
        html += tr("<p>These steps will run with administrator rights:</p><ol>");
        for (const auto& st : m_plan.steps) {
            html += "<li>" + qs(st.title).toHtmlEscaped() + "<pre>";
            for (const auto& c : st.commands)
                html += qs(c).toHtmlEscaped() + "\n";
            html += "</pre></li>";
        }
        html += "</ol>";
    }
    for (const auto& n : m_plan.notes)
        html += "<p>&#9888; " + qs(n).toHtmlEscaped() + "</p>";
    m_planView->setHtml(html);

    const bool runnable = m_plan.runnable && !m_plan.steps.empty();
    m_run->setEnabled(runnable && !busy);
    m_run->setText(m_state == loopback::State::KernelOutdated ? tr("Run anyway") : tr("Run"));
    m_terminal->setEnabled(runnable && !busy);
    m_copy->setEnabled(!m_plan.steps.empty());
    m_recheck->setEnabled(!busy);
    m_persist->setEnabled(!busy && m_state != loopback::State::NeedsReboot &&
                          m_state != loopback::State::Unsupported);
}

void LoopbackSetupDialog::run()
{
    const QString script = helperScript();
    const QString pkexec = systemTool("pkexec");
    if (script.isEmpty() || pkexec.isEmpty()) {
        m_log->appendPlainText(tr("pkexec is not available on this system. Use \"Run in terminal instead\"."));
        return;
    }
    QStringList args;
    const QString systemdRun = systemTool("systemd-run");
    if (!systemdRun.isEmpty())
        args << systemdRun << "--pipe" << "--wait" << "--collect" << "--quiet"
             << "--description=Bad Connection Studio virtual camera setup"
             << "-p" << "ProtectHome=yes" << "-p" << "PrivateTmp=yes";
    args << "/bin/bash" << "-s" << "--" << actions();

    m_log->clear();
    m_output.clear();
    m_pending.clear();
    qCInfo(lcSetup).noquote() << "running helper actions:" << actions().join(' ');

    delete m_proc;
    m_proc = new QProcess(this);
    m_proc->setProcessEnvironment(cleanEnvironment());
    m_proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_proc, &QProcess::readyReadStandardOutput, this,
            [this] { appendOutput(m_proc->readAllStandardOutput()); });
    connect(m_proc, &QProcess::finished, this, &LoopbackSetupDialog::onFinished);
    m_proc->start(pkexec, args);
    if (!m_proc->waitForStarted(5000)) {
        m_log->appendPlainText(tr("Could not start pkexec: %1").arg(m_proc->errorString()));
        return;
    }
    m_proc->write(script.toUtf8());
    m_proc->closeWriteChannel();
    m_log->appendPlainText(tr("Waiting for authentication..."));
    refresh();
}

void LoopbackSetupDialog::appendOutput(const QByteArray& chunk)
{
    m_pending += chunk;
    int nl;
    while ((nl = m_pending.indexOf('\n')) >= 0) {
        QString line = QString::fromUtf8(m_pending.left(nl));
        m_pending.remove(0, nl + 1);
        m_output += line + '\n';
        qCInfo(lcSetup).noquote() << line;
        if (line.startsWith("BCS-STEP: "))
            m_log->appendHtml("<b>&#9654; " + line.mid(10).toHtmlEscaped() + "</b>");
        else if (line.startsWith("BCS-FAIL: "))
            m_log->appendHtml("<b style='color:#c0392b'>&#10007; " + line.mid(10).toHtmlEscaped() + "</b>");
        else if (line.startsWith("BCS-RESTART: "))
            m_log->appendHtml("<b>&#8635; " + line.mid(13).toHtmlEscaped() + "</b>");
        else if (line == "BCS-DONE")
            m_log->appendHtml(tr("<b style='color:#27ae60'>&#10003; Done.</b>"));
        else
            m_log->appendPlainText(line);
    }
}

void LoopbackSetupDialog::onFinished(int exitCode, QProcess::ExitStatus status)
{
    if (!m_pending.isEmpty())
        appendOutput("\n");
    qCInfo(lcSetup) << "helper finished, exit code" << exitCode;
    if (status != QProcess::NormalExit) {
        m_log->appendPlainText(tr("The setup was interrupted."));
    } else if (exitCode == 10) {
        m_log->appendHtml(tr("<b>Restart your computer to finish.</b> After the restart, open Bad Connection "
                             "Studio again; it will offer to load the driver."));
    } else if (exitCode == 126) {
        m_log->appendPlainText(tr("Authentication was cancelled. Nothing was changed."));
    } else if (exitCode == 127 && m_output.contains("authentication agent")) {
        m_log->appendPlainText(tr("No password dialog is available in this desktop session. "
                                  "Use \"Run in terminal instead\"."));
    } else if (exitCode != 0) {
        m_log->appendPlainText(tr("The setup failed (exit code %1). The log above shows why.").arg(exitCode));
        if (m_output.contains("Key was rejected"))
            m_log->appendPlainText(tr("Secure Boot blocked the driver; see the Secure Boot note above."));
    }
    refresh();
}

void LoopbackSetupDialog::openInTerminal()
{
    // Fallback when no polkit agent runs: the same script, via sudo in a
    // visible terminal. It has to be a file here; it is private to the user
    // and deleted after the run.
    QTemporaryDir dir(QDir::tempPath() + "/bcs-setup-XXXXXX");
    dir.setAutoRemove(false);
    if (!dir.isValid())
        return;
    const QString path = dir.filePath("bcs-loopback-helper.sh");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(helperScript().toUtf8()) < 0)
        return;
    f.close();
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);

    const QString cmd = QString("sudo /bin/bash '%1' %2; echo; read -r -p 'Press Enter to close this window. '; "
                                "rm -rf '%3'")
                            .arg(path, actions().join(' '), dir.path());
    struct Term { const char* exe; QStringList pre; };
    const Term terms[] = {
        {"konsole", {"-e"}},         {"gnome-terminal", {"--"}}, {"kgx", {"--"}},
        {"xfce4-terminal", {"-x"}},  {"x-terminal-emulator", {"-e"}}, {"xterm", {"-e"}},
    };
    for (const auto& t : terms) {
        const QString exe = QStandardPaths::findExecutable(t.exe);
        if (exe.isEmpty())
            continue;
        QProcess p;
        p.setProcessEnvironment(cleanEnvironment());
        p.setProgram(exe);
        p.setArguments(t.pre + QStringList{"/bin/bash", "-c", cmd});
        if (p.startDetached()) {
            m_log->appendPlainText(tr("Opened %1. Follow the steps there, then click Re-check.").arg(t.exe));
            return;
        }
    }
    m_log->appendPlainText(tr("No terminal emulator found. Use \"Copy commands\" and run them yourself."));
    dir.remove();
}

} // namespace bcs
