#pragma once

#include <QString>

namespace bcs {

// Technical log (text only, never audio/video content) written to
//   Linux:   ~/.local/share/BadConnectionStudio/logs/bad-connection-studio.log
//   Windows: %LOCALAPPDATA%/BadConnectionStudio/logs/bad-connection-studio.log
// Rotated at 1 MiB, three generations kept. Also echoed to stderr.
void installLogging();
QString logDirectory();

} // namespace bcs
