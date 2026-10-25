#pragma once

#include "backend/BackendRegistry.h"

#include <QString>

namespace bcs {

// First-run / Help diagnostics: what is installed, what is missing, and how
// to fix it. Read-only; it never changes system state.
QString buildDiagnosticsHtml(const CapabilityReport& report);
QString buildDiagnosticsText(const CapabilityReport& report); // for "Copy report"

bool runningFromAppImage();

} // namespace bcs
