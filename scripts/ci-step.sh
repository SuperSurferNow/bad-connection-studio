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
    tail -n 40 "$log" | cut -c1-200 | sed 's/%/%25/g; s/\r//g' | awk 'BEGIN{ORS="%0A"} {print}' > "$log.tail"
    echo "::error title=${title} (exit ${rc})::$(cat "$log.tail")"
fi
rm -f "$log" "$log.tail"
exit "$rc"
