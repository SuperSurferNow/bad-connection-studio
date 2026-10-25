#!/usr/bin/env bash
# Dependency/license report for a Windows staging directory built with MSYS2.
#   scripts/license-report-msys2.sh <install-dir>
set -euo pipefail
DIR="$1"
echo "Bad Connection Studio (Windows) - bundled dependency and license report"
echo "Generated: $(date -u +%Y-%m-%dT%H:%MZ), MSYS2 ${MSYSTEM:-?}"
echo
echo "NOT bundled: virtual camera component (separate signed installer), virtual audio"
echo "cable driver (e.g. VB-CABLE, installed by the user), Windows system DLLs."
echo
printf '%-40s %-44s %s\n' "DLL" "PACKAGE" "LICENSE"
find "$DIR" -iname '*.dll' | sort | while read -r dll; do
    name="$(basename "$dll")"
    src="$(command -v "$name" 2>/dev/null || true)"
    pkg="$( [ -n "$src" ] && pacman -Qoq "$src" 2>/dev/null || echo unknown)"
    lic="$( [ "$pkg" != unknown ] && pacman -Qi "$pkg" | sed -n 's/^Licenses *: //p' || echo unknown)"
    printf '%-40s %-44s %s\n' "${dll#$DIR/}" "$pkg" "$lic"
done
echo
echo "FFmpeg configuration:"
ffmpeg_dll="$(find "$DIR" -iname 'avcodec-*.dll' | head -1)"
[ -n "$ffmpeg_dll" ] && strings "$ffmpeg_dll" | grep -m1 -- '--prefix=' | sed 's/^/  /'
