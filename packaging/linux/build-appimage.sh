#!/usr/bin/env bash
# Build a Bad Connection Studio AppImage for the host architecture.
#
#   packaging/linux/build-appimage.sh [output-dir]
#
# The AppImage bundles the app, Qt (incl. platform/multimedia plugins), FFmpeg
# and codec libraries. It deliberately does NOT bundle or install:
#   - kernel modules (v4l2loopback),
#   - PipeWire (libpipewire is always taken from the host),
#   - system services, udev rules or drivers.
# glibc is taken from the host, so the AppImage runs on distributions with a
# glibc at least as new as the build machine's. Build on the oldest target.
set -euo pipefail

SRC_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
OUT_DIR="$(realpath -m "${1:-$SRC_DIR/dist}")"
ARCH="$(uname -m)"
WORK="$(mktemp -d)"
BUILD="$WORK/build"
APPDIR="$WORK/AppDir"
TOOLS="${BCS_TOOLS_DIR:-$WORK/tools}"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$OUT_DIR" "$TOOLS"

# Tool versions are pinned by URL; override for mirrors or newer pins.
LINUXDEPLOY_URL="${LINUXDEPLOY_URL:-https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${ARCH}.AppImage}"
LINUXDEPLOY_QT_URL="${LINUXDEPLOY_QT_URL:-https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-${ARCH}.AppImage}"
fetch() { [ -x "$2" ] || { curl -fsSL "$1" -o "$2"; chmod +x "$2"; }; }
fetch "$LINUXDEPLOY_URL" "$TOOLS/linuxdeploy"
fetch "$LINUXDEPLOY_QT_URL" "$TOOLS/linuxdeploy-plugin-qt"
export PATH="$TOOLS:$PATH"
export APPIMAGE_EXTRACT_AND_RUN=1 # works in containers without FUSE

cmake -S "$SRC_DIR" -B "$BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
    -DBCS_BUILD_TESTS=OFF -DBCS_BUILD_TOOLS=OFF
cmake --build "$BUILD"
DESTDIR="$APPDIR" cmake --install "$BUILD"

# Qt: find qmake6 for the plugin.
export QMAKE="${QMAKE:-$(command -v qmake6 || command -v qmake-qt6 || echo /usr/lib64/qt6/bin/qmake)}"
# Multimedia plugins follow from Qt6::Multimedia.
# Wayland + X11 + offscreen (headless CI smoke test). Plugin file names
# differ between Qt versions (6.11 merged the Wayland plugins into
# libqwayland.so), so deploy whichever of them this Qt actually has.
PLATFORMS_DIR="$("$QMAKE" -query QT_INSTALL_PLUGINS)/platforms"
EXTRA_PLATFORM_PLUGINS=""
for p in libqwayland.so libqwayland-generic.so libqwayland-egl.so libqxcb.so libqoffscreen.so; do
    if [ -f "$PLATFORMS_DIR/$p" ]; then
        EXTRA_PLATFORM_PLUGINS="${EXTRA_PLATFORM_PLUGINS:+$EXTRA_PLATFORM_PLUGINS;}$p"
    fi
done
export EXTRA_PLATFORM_PLUGINS
echo "platform plugins: $EXTRA_PLATFORM_PLUGINS"
export DEPLOY_PLATFORM_THEMES=1
# linuxdeploy's bundled strip predates the RELR relocation sections that
# current toolchains emit (Fedora 44) and fails on them. Distribution
# libraries are already stripped, so skip it.
export NO_STRIP=1
# Same age problem with linuxdeploy's bundled patchelf: rewriting the rpath
# of libraries that use DT_RELR relocations leaves init_array entries
# unrelocated, and the app segfaults while loading them (seen with Qt's
# FFmpeg multimedia plugin on x86_64). Use the distribution's patchelf
# (>= 0.18 handles DT_RELR) instead.
if command -v patchelf >/dev/null; then
    export PATCHELF="$(command -v patchelf)"
    echo "using $PATCHELF ($(patchelf --version))"
else
    echo "warning: no system patchelf; linuxdeploy's bundled one may corrupt DT_RELR libraries" >&2
fi

VERSION="$(grep -m1 -oE '^ +VERSION [0-9.]+' "$SRC_DIR/CMakeLists.txt" | awk '{print $2}')"
export LINUXDEPLOY_OUTPUT_VERSION="$VERSION"

# libpipewire must come from the host: Qt >= 6.10 Multimedia links it, and a
# bundled copy would load the host's PipeWire modules with a mismatched
# library. Every distribution this AppImage targets ships it.
linuxdeploy --appdir "$APPDIR" \
    --exclude-library 'libpipewire-0.3.so*' \
    --desktop-file "$APPDIR/usr/share/applications/bad-connection-studio.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/scalable/apps/bad-connection-studio.svg" \
    --plugin qt

# libjack: linked by the distribution's libavdevice for its JACK input,
# which this app never opens. linuxdeploy's excludelist skips it (a JACK
# client must normally match the host server), but without it the binary
# does not even load on hosts without JACK, so copy it in ourselves. Fedora
# keeps PipeWire's libjack in a private directory (/usr/lib64/pipewire-0.3/jack).
if [ ! -e "$APPDIR/usr/lib/libjack.so.0" ]; then
    JACK_LIB="$( (ldconfig -p 2>/dev/null || /usr/sbin/ldconfig -p) | awk '$1 == "libjack.so.0" {print $NF; exit}' || true)"
    if [ -z "$JACK_LIB" ]; then
        JACK_LIB="$(find /usr/lib64 /usr/lib -name libjack.so.0 2>/dev/null | head -n1 || true)"
    fi
    if [ -n "$JACK_LIB" ]; then
        cp -L "$JACK_LIB" "$APPDIR/usr/lib/libjack.so.0"
        echo "::notice title=AppImage::bundled libjack from $JACK_LIB"
    else
        echo "::warning title=AppImage::libjack.so.0 not found on the build host"
    fi
fi

# Never ship a PipeWire client library (see PipeWireApi.h).
if find "$APPDIR" -name 'libpipewire-0.3.so*' | grep -q .; then
    echo "error: libpipewire was bundled; it must come from the host" >&2
    exit 1
fi

# Dependency / license report, inside the AppImage and next to it.
echo "Generating license report..."
"$SRC_DIR/scripts/license-report-linux.sh" "$APPDIR" > "$WORK/LICENSE-REPORT.txt"
tail -n 5 "$WORK/LICENSE-REPORT.txt"
install -Dm644 "$WORK/LICENSE-REPORT.txt" "$APPDIR/usr/share/doc/BadConnectionStudio/LICENSE-REPORT.txt"
if grep -q "unredistributable" "$WORK/LICENSE-REPORT.txt"; then
    echo "error: the FFmpeg build is 'nonfree and unredistributable'; refusing to package" >&2
    exit 1
fi

linuxdeploy --appdir "$APPDIR" --exclude-library 'libpipewire-0.3.so*' --output appimage
mv Bad_Connection_Studio-*.AppImage "$OUT_DIR/BadConnectionStudio-${VERSION}-${ARCH}.AppImage"
cp "$WORK/LICENSE-REPORT.txt" "$OUT_DIR/BadConnectionStudio-${VERSION}-${ARCH}.LICENSE-REPORT.txt"
echo "Built $OUT_DIR/BadConnectionStudio-${VERSION}-${ARCH}.AppImage"
