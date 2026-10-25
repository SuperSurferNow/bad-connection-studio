#!/usr/bin/env bash
# Run a CI step; on failure, also publish the last lines of its output as a
# GitHub error annotation, which is readable from the checks API even where
# raw job logs are not.
#   scripts/ci-step.sh "<title>" <command> [args...]
set -uo pipefail
title="$1"; shift
log="$(mktemp)"
"$@" 2>&1 | tee "$log"
rc=${PIPESTATUS[0]}
if [ "$rc" -ne 0 ]; then
    # Annotations are capped at ~4 KB: keep the last lines, each shortened.
    # Pure bash/coreutils (minimal container images may lack awk).
    msg=""
    while IFS= read -r line; do
        line="${line//$'\r'/}"
        line="${line//%/%25}"
        msg+="${line:0:200}%0A"
    done < <(tail -n 40 "$log")
    echo "::error title=${title} (exit ${rc})::${msg}"
fi
rm -f "$log"
exit "$rc"
