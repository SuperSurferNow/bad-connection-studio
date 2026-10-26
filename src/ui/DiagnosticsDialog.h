#pragma once

#include "backend/BackendRegistry.h"

#include <QDialog>

#include <functional>

class QTextBrowser;
class QCheckBox;
class QDialogButtonBox;

namespace bcs {

class DiagnosticsDialog : public QDialog {
    Q_OBJECT
public:
    // `probe` re-runs the capability check when the user clicks "Re-check".
    DiagnosticsDialog(std::function<CapabilityReport()> probe, bool skipAtStartup, QWidget* parent = nullptr);
    bool skipAtStartup() const;
    // Extra button (Linux: driver setup). The report refreshes after it runs.
    void setSetupAction(const QString& label, std::function<void()> action);

private:
    void refresh();

    std::function<CapabilityReport()> m_probe;
    CapabilityReport m_report;
    QTextBrowser* m_text = nullptr;
    QCheckBox* m_skip = nullptr;
    QDialogButtonBox* m_buttons = nullptr;
};

} // namespace bcs
