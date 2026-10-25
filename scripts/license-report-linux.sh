#!/usr/bin/env bash
# Writes a dependency/license report for an AppDir to stdout: every bundled
# shared library with the distribution package it came from, that package's
# license, and the FFmpeg build configuration (which decides LGPL vs GPL).
#   scripts/license-report-linux.sh <AppDir>
# No pipefail: lookups below pipe into early-exiting readers (awk exit,
# head, grep -m1), which would turn SIGPIPE in the writer into a failure.
set -eu
APPDIR="$1"
LDCACHE="$(ldconfig -p)"
echo "Bad Connection Studio - bundled dependency and license report"
echo "Generated: $(date -u +%Y-%m-%dT%H:%MZ) on $(. /etc/os-release; echo "$PRETTY_NAME") $(uname -m)"
echo
echo "NOT bundled (OS-level prerequisites): v4l2loopback kernel module, PipeWire"
echo "(libpipewire is loaded from the host), PulseAudio/PipeWire server, udev, glibc, GPU drivers."
echo

owner() { # path-on-host -> "package|version|license"
    local p="$1"
    if command -v rpm >/dev/null; then
        local r
        r="$(rpm -qf --qf '%{NAME}|%{VERSION}-%{RELEASE}|%{LICENSE}\n' "$p" 2>/dev/null | head -1 || true)"
        case "$r" in *"|"*) echo "$r"; return ;; esac
    fi
    if command -v dpkg-query >/dev/null; then
        local pkg="" c
        # merged-/usr: dpkg may know the file under /lib or /usr/lib.
        for c in "$p" "$(realpath -m "$p")" "/usr$p" "${p#/usr}"; do
            pkg="$(dpkg-query -S "$c" 2>/dev/null | head -1 | cut -d: -f1)" || true
            [ -n "$pkg" ] && break
        done
        if [ -n "$pkg" ]; then
            echo "$pkg|$(dpkg-query -W -f='${Version}' "$pkg")|see /usr/share/doc/$pkg/copyright"
            return
        fi
    fi
    echo "unknown|unknown|unknown"
}

printf '%-44s %-28s %-24s %s\n' "LIBRARY" "PACKAGE" "VERSION" "LICENSE"
find "$APPDIR" -name '*.so*' -type f | sort | while read -r lib; do
    name="$(basename "$lib")"
    host="$(printf '%s\n' "$LDCACHE" | awk -v n="$name" '$1==n {print $NF; exit}')"
    if [ -z "$host" ]; then
        host="$(find /usr/lib64 /usr/lib /usr/lib/*-linux-gnu -maxdepth 4 -name "$name" 2>/dev/null | head -1 || true)"
    fi
    IFS='|' read -r pkg ver lic <<<"$(owner "${host:-/nonexistent}")"
    printf '%-44s %-28s %-24s %s\n' "${lib#$APPDIR/}" "$pkg" "$ver" "$lic"
done

echo
echo "FFmpeg build (decides the distribution license):"
exe="$APPDIR/usr/bin/bad-connection-studio"
if [ -x "$exe" ]; then
    QT_QPA_PLATFORM=offscreen LD_LIBRARY_PATH="$APPDIR/usr/lib" "$exe" --self-test 2>/dev/null | sed -n '2p' || true
fi
for lib in "$APPDIR"/usr/lib/libavcodec.so.*; do
    if [ -f "$lib" ]; then
        strings "$lib" | grep -m1 -- '--prefix=' | sed 's/^/  configure: /' || true
    fi
done
