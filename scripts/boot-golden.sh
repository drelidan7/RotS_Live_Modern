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
#   scripts/boot-golden.sh --native <binary-path> verify
#                                               Boot <binary-path> directly on THIS host — no
#                                               docker/compose involved — against the repo's real
#                                               lib/ (cwd is the repo root, same as the container's
#                                               /rots; the binary's own -d default is "lib", so a
#                                               relative chdir("lib") resolves the same way). Used
#                                               for the macOS-native runtime (Task 7): same poll/
#                                               normalize/compare, so a diff here is a real native-
#                                               vs-container behavior difference, not noise.
#
# Requires lib/world/ (see docs/BUILD.md) and a prebuilt server binary for the chosen
# service (see `scripts/rots-docker.sh compile` for `rots`; for `rots64`, `cd src &&
# cmake --preset linux-x64 && cmake --build --preset linux-x64` inside the rots64
# container; for `--native`, `cd src && cmake --preset macos-arm64 && cmake --build
# --preset macos-arm64` on the host). The server is booted just long enough to finish
# world load, then killed — this script never rebuilds the binary.
#
# The launch mirrors the launch half of `scripts/rots-docker.sh boot`: same
# working dir (/rots, or the repo root for --native) and same flags (no -p,
# matching the plain-telnet dev boot) — but skips that script's `make setup &&
# make all`, since this script characterizes the boot *log*, not the build.
set -euo pipefail
cd "$(dirname "$0")/.."

GOLDEN=docs/superpowers/goldens/boot-log.golden

# `--service <name>` selects the compose service to boot in (default `rots`, the
# 32-bit i386 container; `rots64` is the 64-bit sibling from Task 6). Also
# settable via ROTS_SERVICE so callers that already export it don't need the
# flag. `--native <binary-path>` bypasses docker/compose entirely and runs that
# binary directly on the host instead (mutually exclusive with --service in
# spirit; --native simply takes priority below if both are given). The mode
# (capture/verify) is the first remaining positional arg.
SERVICE="${ROTS_SERVICE:-rots}"
NATIVE_BINARY=""
args=()
while [ $# -gt 0 ]; do
  case "$1" in
    --native)
      NATIVE_BINARY="$2"
      shift 2
      ;;
    --native=*)
      NATIVE_BINARY="${1#--native=}"
      shift
      ;;
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

if [ -n "$NATIVE_BINARY" ]; then
  BINARY_PATH="$NATIVE_BINARY"
else
  # Each service builds its binary at a different path: the 32-bit Makefile
  # build produces ./bin/ageland (bind-mounted, host-visible); the 64-bit
  # CMake linux-x64 preset puts it at ./build/linux-x64/ageland, which since
  # the build-isolation wave lives in the rots-build-x64 NAMED VOLUME and is
  # visible only inside the container -- so service mode defers the
  # existence check to the container (below) instead of stat-ing the host
  # path.
  case "$SERVICE" in
    rots) BINARY_PATH=bin/ageland ;;
    rots64) BINARY_PATH=build/linux-x64/ageland ;;
    *) echo "ERROR: unknown --service '$SERVICE' (expected 'rots' or 'rots64')" >&2; exit 2 ;;
  esac
fi

[ -d lib/world ] || { echo "ERROR: lib/world/ missing — cannot boot." >&2; exit 2; }
if [ -n "$NATIVE_BINARY" ]; then
  [ -x "$BINARY_PATH" ] || { echo "ERROR: $BINARY_PATH is not executable — build it first (see script header)." >&2; exit 2; }
else
  docker compose run --rm --pull never "$SERVICE" bash -lc "[ -f /rots/$BINARY_PATH ]" \
    || { echo "ERROR: $BINARY_PATH missing inside the $SERVICE container's build volume — build it first (see script header)." >&2; exit 2; }
fi

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

# Same boot/poll/kill sequence as capture_log, but runs the binary directly on
# the host (no docker) for --native. Uses a private tmp raw-log path (PID-
# suffixed) rather than /tmp/boot-golden.raw so a concurrent container run
# can't collide with it.
capture_log_native() {
  local raw
  raw="$(mktemp /tmp/boot-golden-native.XXXXXX)"
  ( "$BINARY_PATH" > "$raw" 2>&1 & echo $! > "$raw.pid" )
  local pid
  pid="$(cat "$raw.pid")"

  # Save whatever EXIT/INT/TERM traps the caller already had installed (the
  # capture/verify case blocks below set `trap 'rm -f "$tmp"' EXIT` before
  # calling into here) so this function's own kill-the-server trap can be
  # cleanly layered on top and then unwound back to exactly that, instead of
  # the old `trap - EXIT INT TERM`, which cleared every handler for those
  # signals process-wide — wiping out the caller's tmp-file cleanup trap and
  # leaking the normalized boot log on every --native run.
  local saved_exit_trap saved_int_trap saved_term_trap
  saved_exit_trap="$(trap -p EXIT)"
  saved_int_trap="$(trap -p INT)"
  saved_term_trap="$(trap -p TERM)"

  trap 'kill "$pid" 2>/dev/null || true' EXIT INT TERM
  local i
  for i in $(seq 1 60); do
    sleep 1
    grep -q 'Entering game loop' "$raw" 2>/dev/null && break
  done
  if ! grep -q 'Entering game loop' "$raw" 2>/dev/null; then
    echo "ERROR: server did not reach \"Entering game loop\" within 60s" >&2
    kill "$pid" 2>/dev/null || true
    eval "${saved_exit_trap:-trap - EXIT}"
    eval "${saved_int_trap:-trap - INT}"
    eval "${saved_term_trap:-trap - TERM}"
    rm -f "$raw" "$raw.pid"
    return 1
  fi
  kill "$pid" 2>/dev/null || true
  eval "${saved_exit_trap:-trap - EXIT}"
  eval "${saved_int_trap:-trap - INT}"
  eval "${saved_term_trap:-trap - TERM}"
  sleep 1
  cat "$raw" | normalize
  rm -f "$raw" "$raw.pid"
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
  local capture_fn=capture_log
  [ -n "$NATIVE_BINARY" ] && capture_fn=capture_log_native
  if ! "$capture_fn" > "$1"; then
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
  *) echo "usage: $0 [--service rots|rots64 | --native <binary-path>] capture|verify" >&2; exit 2 ;;
esac
