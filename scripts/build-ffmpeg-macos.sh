#!/usr/bin/env bash
# Builds the FFmpeg that the macOS app bundles, for the current architecture,
# into <prefix>. Homebrew's FFmpeg only runs on the macOS version it was built
# for and pulls in dozens of libraries; this one targets macOS 13 and contains
# only what the app uses: x264, libvpx (VP8/VP9) and libopus as static
# libraries inside shared FFmpeg libraries, plus FFmpeg's own codecs (MJPEG,
# MPEG-4, native Opus) and the avfoundation camera input.
#
# GPL because of x264, like the Linux and Windows packages.
#   scripts/build-ffmpeg-macos.sh <prefix>
# CI caches <prefix>, keyed on this file: change a version here to rebuild.
set -euo pipefail

PREFIX="$1"
FFMPEG_VERSION=7.1.1
X264_COMMIT=b35605ace3ddf7c1a5d67a2eb553f034aef41d55 # "stable" branch
LIBVPX_VERSION=1.15.0
OPUS_VERSION=1.5.2
export MACOSX_DEPLOYMENT_TARGET=13.0

JOBS="$(sysctl -n hw.ncpu)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$PREFIX"
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig"
export CFLAGS="-mmacosx-version-min=$MACOSX_DEPLOYMENT_TARGET"
export LDFLAGS="-mmacosx-version-min=$MACOSX_DEPLOYMENT_TARGET"
cd "$WORK"

fetch() { curl -fsSL --retry 3 -o "$2" "$1"; }

echo "::group::opus $OPUS_VERSION"
fetch "https://downloads.xiph.org/releases/opus/opus-$OPUS_VERSION.tar.gz" opus.tar.gz
tar xf opus.tar.gz
(cd "opus-$OPUS_VERSION" &&
    ./configure --prefix="$PREFIX" --disable-shared --enable-static --disable-doc --disable-extra-programs &&
    make -j"$JOBS" && make install)
echo "::endgroup::"

echo "::group::libvpx $LIBVPX_VERSION"
fetch "https://github.com/webmproject/libvpx/archive/refs/tags/v$LIBVPX_VERSION.tar.gz" vpx.tar.gz
tar xf vpx.tar.gz
(cd "libvpx-$LIBVPX_VERSION" &&
    ./configure --prefix="$PREFIX" --disable-shared --enable-static --enable-pic --disable-examples \
        --disable-tools --disable-docs --disable-unit-tests --enable-vp8 --enable-vp9 \
        --extra-cflags="$CFLAGS" &&
    make -j"$JOBS" && make install)
echo "::endgroup::"

echo "::group::x264"
fetch "https://code.videolan.org/videolan/x264/-/archive/$X264_COMMIT/x264-$X264_COMMIT.tar.gz" x264.tar.gz
tar xf x264.tar.gz
(cd "x264-$X264_COMMIT" &&
    ./configure --prefix="$PREFIX" --enable-static --enable-pic --disable-cli &&
    make -j"$JOBS" && make install)
echo "::endgroup::"

echo "::group::FFmpeg $FFMPEG_VERSION"
fetch "https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VERSION.tar.xz" ffmpeg.tar.xz
tar xf ffmpeg.tar.xz
(cd "ffmpeg-$FFMPEG_VERSION" &&
    ./configure --prefix="$PREFIX" --enable-shared --disable-static --enable-gpl \
        --enable-libx264 --enable-libvpx --enable-libopus \
        --disable-programs --disable-doc --disable-debug --disable-network \
        --disable-sdl2 --disable-xlib --disable-libxcb --disable-securetransport \
        --extra-cflags="$CFLAGS" --extra-ldflags="$LDFLAGS" \
        --install-name-dir="$PREFIX/lib" &&
    make -j"$JOBS" && make install)
echo "::endgroup::"

# Only the FFmpeg libraries are needed at run time (the codecs are static).
ls -la "$PREFIX/lib"/libav*.dylib "$PREFIX/lib"/libsw*.dylib
