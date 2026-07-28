#!/usr/bin/env bash
# LS-3b T3 boot-time perf arm (the wave's spec-mandated "boot-time delta",
# census D Sec5.2(c)). scripts/boot-golden.sh polls at 1-second granularity
# and its normalize() strips every timestamp out of what survives, so it has
# NO timing signal at all -- this is the dedicated poller that gap requires;
# it does not modify boot-golden.sh in any way.
#
# Boots the NATIVE binary directly (no docker/compose involved), the same
# way boot-golden.sh's --native mode does: cwd = repo root, no -p flag
# (matching the plain-telnet dev boot), binary launched as-is. Never rebuilds
# it -- build it first (see boot-golden.sh's own header for the build
# commands this repo uses for the macos-arm64/linux-x64 presets). The actual
# poll loop lives in scripts/ls3b_boot_time.py (20ms granularity via
# time.monotonic(), since bash's own $SECONDS/date is only 1-second-granular)
# so both the fixed 60s ceiling and the "Entering game loop." marker
# (comm.cpp's own boot-complete log line) match boot-golden.sh's, just
# measured 50x finer.
#
# Usage: scripts/ls3b-boot-time.sh [binary-path] [run-count]
#   binary-path defaults to build/macos-arm64/ageland
#   run-count   defaults to 5 (the wave's own >= 5 repetitions process rule);
#               prints each run's ms plus the median/min/max.
set -euo pipefail
cd "$(dirname "$0")/.."

BINARY_PATH="${1:-build/macos-arm64/ageland}"
RUN_COUNT="${2:-5}"

[ -d lib/world ] || { echo "ERROR: lib/world/ missing -- cannot boot." >&2; exit 2; }
[ -x "$BINARY_PATH" ] || { echo "ERROR: $BINARY_PATH is not executable -- build it first (see scripts/boot-golden.sh's header for the build commands)." >&2; exit 2; }

exec python3 "$(dirname "$0")/ls3b_boot_time.py" "$BINARY_PATH" "$RUN_COUNT"
