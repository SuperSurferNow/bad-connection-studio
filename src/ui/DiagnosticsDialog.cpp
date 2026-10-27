#include "ui/DiagnosticsDialog.h"

#include "app/Logging.h"
#include "ui/Diagnostics.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

namespace bcs {

DiagnosticsDialog::DiagnosticsDialog(std::function<CapabilityReport()> probe, bool skipAtStartup, QWidget* parent)
    : QDialog(parent), m_probe(std::move(probe))
{
    setWindowTitle(tr("Diagnostics and setup"));
    resize(760, 640);
    auto* lay = new QVBoxLayout(this);
    m_text = new QTextBrowser(this);
    m_text->setOpenExternalLinks(true);
    lay->addWidget(m_text);

    m_skip = new QCheckBox(tr("Do not show this at startup when something is missing"), this);
    m_skip->setChecked(skipAtStartup);
    lay->addWidget(m_skip);

    auto* buttons = m_buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* recheck = buttons->addButton(tr("Re-check"), QDialogButtonBox::ActionRole);
    auto* copy = buttons->addButton(tr("Copy report"), QDialogButtonBox::ActionRole);
    auto* logs = buttons->addButton(tr("Open log folder"), QDialogButtonBox::ActionRole);
    lay->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(recheck, &QPushButton::clicked, this, &DiagnosticsDialog::refresh);
    connect(copy, &QPushButton::clicked, this,
            [this] { QApplication::clipboard()->setText(buildDiagnosticsText(m_report)); });
    connect(logs, &QPushButton::clicked, this,
            [] { QDesktopServices::openUrl(QUrl::fromLocalFile(logDirectory())); });
    refresh();
}

bool DiagnosticsDialog::skipAtStartup() const { return m_skip->isChecked(); }

void DiagnosticsDialog::setSetupAction(const QString& label, std::function<void()> action)
{
    auto* b = m_buttons->addButton(label, QDialogButtonBox::ActionRole);
    connect(b, &QPushButton::clicked, this, [this, action = std::move(action)] {
        action();
        refresh();
    });
}

void DiagnosticsDialog::refresh()
{
    m_report = m_probe();
    m_text->setHtml(buildDiagnosticsHtml(m_report));
}

} // namespace bcs
