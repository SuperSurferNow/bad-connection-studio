#!/usr/bin/env bash
# Build a Bad Connection Studio AppImage for the host architecture.
#
#   packaging/linux/build-appimage.sh [output-dir]
#
# The AppImage bundles the app, Qt (incl. platform/multimedia plugins), FFmpeg
# and codec libraries. It deliberately does NOT bundle or install:
#   - kernel modules (v4l2loopback),
#   - PipeWire (libpipewire is dlopen()ed from the host at runtime),
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
# Wayland + X11 platform plugins; multimedia plugins follow from Qt6::Multimedia.
# offscreen is used by the headless smoke test in CI.
export EXTRA_PLATFORM_PLUGINS="libqwayland-generic.so;libqwayland-egl.so;libqxcb.so;libqoffscreen.so"
export DEPLOY_PLATFORM_THEMES=1

VERSION="$(grep -m1 -oE '^ +VERSION [0-9.]+' "$SRC_DIR/CMakeLists.txt" | awk '{print $2}')"
export LINUXDEPLOY_OUTPUT_VERSION="$VERSION"

linuxdeploy --appdir "$APPDIR" \
    --desktop-file "$APPDIR/usr/share/applications/bad-connection-studio.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/scalable/apps/bad-connection-studio.svg" \
    --plugin qt

# Never ship a PipeWire client library (see PipeWireApi.h).
if find "$APPDIR" -name 'libpipewire-0.3.so*' | grep -q .; then
    echo "error: libpipewire was bundled; it must come from the host" >&2
    exit 1
fi

# Dependency / license report, inside the AppImage and next to it.
"$SRC_DIR/scripts/license-report-linux.sh" "$APPDIR" > "$WORK/LICENSE-REPORT.txt"
install -Dm644 "$WORK/LICENSE-REPORT.txt" "$APPDIR/usr/share/doc/BadConnectionStudio/LICENSE-REPORT.txt"
if grep -q "unredistributable" "$WORK/LICENSE-REPORT.txt"; then
    echo "error: the FFmpeg build is 'nonfree and unredistributable'; refusing to package" >&2
    exit 1
fi

linuxdeploy --appdir "$APPDIR" --output appimage
mv Bad_Connection_Studio-*.AppImage "$OUT_DIR/BadConnectionStudio-${VERSION}-${ARCH}.AppImage"
cp "$WORK/LICENSE-REPORT.txt" "$OUT_DIR/BadConnectionStudio-${VERSION}-${ARCH}.LICENSE-REPORT.txt"
echo "Built $OUT_DIR/BadConnectionStudio-${VERSION}-${ARCH}.AppImage"
