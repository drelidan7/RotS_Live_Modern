#!/usr/bin/env bash
# Boot-log characterization golden.
#
#   scripts/boot-golden.sh capture   Boot in the container, save normalized boot
#                                    log to docs/superpowers/goldens/boot-log.golden
#   scripts/boot-golden.sh verify    Boot the same way and diff against the golden.
#                                    Exit 0 = identical, 1 = drift, 2 = setup/boot error.
#
# Requires lib/world/ (see docs/BUILD.md) and a prebuilt bin/ageland (see
# `scripts/rots-docker.sh compile`). The server is booted just long enough to
# finish world load, then killed — this script never rebuilds the binary.
#
# The launch mirrors the launch half of `scripts/rots-docker.sh boot`: same
# working dir (/rots) and same flags (no -p, matching the plain-telnet dev
# boot) — but skips that script's `make setup && make all`, since this script
# characterizes the boot *log*, not the build.
set -euo pipefail
cd "$(dirname "$0")/.."

GOLDEN=docs/superpowers/goldens/boot-log.golden
mode="${1:-verify}"

[ -d lib/world ] || { echo "ERROR: lib/world/ missing — cannot boot." >&2; exit 2; }
[ -f bin/ageland ] || { echo "ERROR: bin/ageland missing — run 'scripts/rots-docker.sh compile' first." >&2; exit 2; }

# Boot exactly as `scripts/rots-docker.sh boot` launches the binary (cd /rots,
# ./bin/ageland, no -p). Poll the growing log for "Entering game loop." (the
# line the live code logs once world load finishes, per comm.cpp) instead of a
# fixed sleep, so capture time tracks real boot speed under QEMU emulation.
# Exits non-zero (without emitting a partial log) if boot doesn't reach that
# line within 60s, so callers can distinguish boot failure from log drift.
capture_log() {
  docker compose run --rm rots bash -lc '
    cd /rots
    ./bin/ageland > /tmp/boot-golden.raw 2>&1 &
    pid=$!
    for i in $(seq 1 60); do
      sleep 1
      grep -q "Entering game loop" /tmp/boot-golden.raw 2>/dev/null && break
    done
    if ! grep -q "Entering game loop" /tmp/boot-golden.raw 2>/dev/null; then
      echo "ERROR: server did not reach \"Entering game loop\" within 60s" >&2
      kill "$pid" 2>/dev/null || true
      exit 1
    fi
    kill "$pid" 2>/dev/null || true
    sleep 1
    cat /tmp/boot-golden.raw
  ' | normalize
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
  *) echo "usage: $0 capture|verify" >&2; exit 2 ;;
esac
