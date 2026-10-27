#!/usr/bin/env bash
# Writes a dependency/license report for the macOS app bundle to stdout: every
# bundled library and framework, and the FFmpeg build configuration (which
# decides LGPL vs GPL).
#   scripts/license-report-macos.sh <BadConnectionStudio.app>
set -eu
APP="$1"
echo "Bad Connection Studio - bundled dependency and license report"
echo "Generated: $(date -u +%Y-%m-%dT%H:%MZ) on macOS $(sw_vers -productVersion) $(uname -m)"
echo
echo "NOT bundled (OS-level prerequisites): a virtual audio driver for the virtual microphone"
echo "(BlackHole 2ch, GPL-3.0, installed by you), OBS Studio for the virtual camera (OBS bridge)."
echo
echo "Bundled:"
echo "  Qt 6 (LGPL-3.0, dynamically linked frameworks):"
find "$APP/Contents/Frameworks" -maxdepth 1 -name '*.framework' -exec basename {} \; | sort | sed 's/^/    /'
echo "  Qt plugins:"
find "$APP/Contents/PlugIns" -name '*.dylib' | sed "s|^$APP/Contents/PlugIns/|    |" | sort
echo "  Other libraries:"
find "$APP/Contents/Frameworks" -maxdepth 1 -name '*.dylib' -exec basename {} \; | sort | sed 's/^/    /'
echo
echo "  FFmpeg (GPL-2.0-or-later as configured, built from source by scripts/build-ffmpeg-macos.sh)"
echo "  statically containing x264 (GPL-2.0-or-later), libvpx (BSD-3-Clause), libopus (BSD-3-Clause)."
echo
echo "FFmpeg build (decides the distribution license):"
for lib in "$APP"/Contents/Frameworks/libavcodec.*.dylib; do
    if [ -f "$lib" ]; then
        strings "$lib" | grep -m1 -- '--prefix=' | sed 's/^/  configure: /' || true
    fi
done
