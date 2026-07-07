#!/usr/bin/env bash
# Boot-log characterization golden.
#
#   scripts/boot-golden.sh capture             Boot in the container, save normalized
#                                               boot log to docs/superpowers/goldens/boot-log.golden
#   scripts/boot-golden.sh verify               Boot the same way and diff against the golden.
#                                               Exit 0 = identical, 1 = drift, 2 = setup/boot error.
#   scripts/boot-golden.sh --service rots64 verify
#                                               Same, but boot the 64-bit container/binary instead
#                                               of the default 32-bit `rots` one. Also settable via
#                                               the ROTS_SERVICE env var. Both boot the SAME bind-
#                                               mounted lib/ data — normalization/compare logic is
#                                               identical either way, which is the point: the golden
#                                               is ABI-neutral text, so a diff here is a real
#                                               cross-ABI behavior difference, not noise.
#
# Requires lib/world/ (see docs/BUILD.md) and a prebuilt server binary for the chosen
# service (see `scripts/rots-docker.sh compile` for `rots`; for `rots64`, `cd src &&
# cmake --preset linux-x64 && cmake --build --preset linux-x64` inside the rots64
# container). The server is booted just long enough to finish world load, then
# killed — this script never rebuilds the binary.
#
# The launch mirrors the launch half of `scripts/rots-docker.sh boot`: same
# working dir (/rots) and same flags (no -p, matching the plain-telnet dev
# boot) — but skips that script's `make setup && make all`, since this script
# characterizes the boot *log*, not the build.
set -euo pipefail
cd "$(dirname "$0")/.."

GOLDEN=docs/superpowers/goldens/boot-log.golden

# `--service <name>` selects the compose service to boot in (default `rots`, the
# 32-bit i386 container; `rots64` is the 64-bit sibling from Task 6). Also
# settable via ROTS_SERVICE so callers that already export it don't need the
# flag. The mode (capture/verify) is the first remaining positional arg.
SERVICE="${ROTS_SERVICE:-rots}"
args=()
while [ $# -gt 0 ]; do
  case "$1" in
    --service)
      SERVICE="$2"
      shift 2
      ;;
    --service=*)
      SERVICE="${1#--service=}"
      shift
      ;;
    *)
      args+=("$1")
      shift
      ;;
  esac
done
mode="${args[0]:-verify}"

# Each service builds its binary at a different path relative to the /rots bind
# mount: the 32-bit Makefile build produces ./bin/ageland; the 64-bit CMake
# linux-x64 preset (binaryDir "${sourceDir}/../build/${presetName}" in
# CMakePresets.json) puts it at ./build/linux-x64/ageland. Both paths are bind-
# mounted, so they're visible identically from the host and from inside either
# container.
case "$SERVICE" in
  rots) BINARY_PATH=bin/ageland ;;
  rots64) BINARY_PATH=build/linux-x64/ageland ;;
  *) echo "ERROR: unknown --service '$SERVICE' (expected 'rots' or 'rots64')" >&2; exit 2 ;;
esac

[ -d lib/world ] || { echo "ERROR: lib/world/ missing — cannot boot." >&2; exit 2; }
[ -f "$BINARY_PATH" ] || { echo "ERROR: $BINARY_PATH missing — build it first (see script header)." >&2; exit 2; }

# Boot exactly as `scripts/rots-docker.sh boot` launches the binary (cd /rots,
# ./<binary>, no -p). Poll the growing log for "Entering game loop." (the line
# the live code logs once world load finishes, per comm.cpp) instead of a fixed
# sleep, so capture time tracks real boot speed under QEMU emulation. Exits
# non-zero (without emitting a partial log) if boot doesn't reach that line
# within 60s, so callers can distinguish boot failure from log drift.
capture_log() {
  docker compose run --rm "$SERVICE" bash -lc "
    cd /rots
    ./$BINARY_PATH > /tmp/boot-golden.raw 2>&1 &
    pid=\$!
    for i in \$(seq 1 60); do
      sleep 1
      grep -q 'Entering game loop' /tmp/boot-golden.raw 2>/dev/null && break
    done
    if ! grep -q 'Entering game loop' /tmp/boot-golden.raw 2>/dev/null; then
      echo 'ERROR: server did not reach \"Entering game loop\" within 60s' >&2
      kill \"\$pid\" 2>/dev/null || true
      exit 1
    fi
    kill \"\$pid\" 2>/dev/null || true
    sleep 1
    cat /tmp/boot-golden.raw
  " | normalize
}

# Normalize in two steps:
#
# 1. Strip the volatile prefix every log()/mudlog() line carries: an optional
#    mudlog "<type>, " digit plus the weekday/date/time from asctime()
#    truncated to 19 chars by "%-19.19s" (utility.cpp log()/mudlog()) — the
#    year never survives the truncation. Everything through the last " :: "
#    goes; lines without " :: " (raw fprintf warnings) pass through unchanged.
#
# 2. Keep only known world-load lines, matched as explicit anchored phrases —
#    NOT bare substrings, which is how an earlier draft leaked 21k
#    "Doing renum_zone_one on command #N." debug-counter lines (zone.cpp:193)
#    via the substring "zon". Kept:
#      - boot-phase headers (Boot db BEGIN/DONE, Loading/Renumbering/...,
#        Assigning function pointers: and its four indented sub-lines)
#      - world-file opens for the five world-data extensions
#        (.zon/.wld/.mob/.obj/.shp — the on-disk names)
#      - per-zone "Resetting <name> (rooms N-M)." reset lines (also matches
#        the "Resetting the game time:" header)
#      - world-integrity warnings: "Room N does not exist in database",
#        "Invalid virtual number in zone reset command: ...",
#        "Mobile N had its stats fixed."
#      - the "Entering game loop." milestone
normalize() {
  sed -E 's/^.*:: //' \
    | grep -E '^(Boot db -- (BEGIN|DONE)\.$|Resetting |Loading |Renumbering |Checking start rooms\.$|Booting mail system\.$|Recounting zone powers\.$|Assigning function pointers:$|   (Shopkeepers|Mobiles|Objects|Rooms)\.$|opened file world/[a-z]+/[0-9]+\.(zon|wld|mob|obj|shp)\.$|Room [0-9]+ does not exist in database$|Invalid virtual number in zone reset command: |Mobile [0-9]+ had its stats fixed\.$|Entering game loop\.$)'
}

# Boot once and write the normalized log to $1. Returns non-zero (after
# printing why) if the boot never reached the game loop, so verify can report
# "boot failed" (exit 2) instead of misreporting it as golden drift (exit 1).
capture_to() {
  if ! capture_log > "$1"; then
    echo "ERROR: boot capture failed — server did not reach \"Entering game loop\" (see message above). This is a boot failure, not golden drift." >&2
    return 1
  fi
}

case "$mode" in
  capture)
    mkdir -p "$(dirname "$GOLDEN")"
    tmp=$(mktemp)
    trap 'rm -f "$tmp"' EXIT
    capture_to "$tmp" || exit 2
    mv "$tmp" "$GOLDEN"
    chmod 644 "$GOLDEN" # mktemp made $tmp 600; keep the committed golden world-readable.
    trap - EXIT
    echo "captured $(wc -l < "$GOLDEN") lines to $GOLDEN"
    ;;
  verify)
    [ -f "$GOLDEN" ] || { echo "ERROR: no golden; run capture first." >&2; exit 2; }
    tmp=$(mktemp)
    trap 'rm -f "$tmp"' EXIT
    capture_to "$tmp" || exit 2
    if diff -u "$GOLDEN" "$tmp"; then
      echo "boot log matches golden"
    else
      echo "BOOT LOG DRIFTED from $GOLDEN" >&2
      exit 1
    fi
    ;;
  *) echo "usage: $0 [--service rots|rots64] capture|verify" >&2; exit 2 ;;
esac
