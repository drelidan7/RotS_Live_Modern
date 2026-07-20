#!/usr/bin/env bash
# Combat smoke characterization golden -- the gate every combat-pilot
# migration task runs to prove clerics.cpp/fight.cpp's observable behavior
# hasn't drifted (see .superpowers/sdd/pilot-task-1-brief.md).
#
#   scripts/combat-golden.sh capture                    Boot a fresh scratch server natively
#                                                        (bin/ageland) and save the combat
#                                                        transcript to
#                                                        docs/superpowers/goldens/combat-smoke/.
#   scripts/combat-golden.sh verify                     Same, but diff against the committed golden.
#                                                        Exit 0 = identical, 1 = drift, 2 = setup error.
#   scripts/combat-golden.sh --service rots64 verify     Run inside the rots64 (64-bit) container
#                                                        instead of natively, against
#                                                        build/linux-x64/ageland. Also settable via
#                                                        ROTS_SERVICE. Both boot the SAME bind-mounted
#                                                        lib/ world data as --native -- a diff here is
#                                                        a real cross-ABI behavior difference.
#   scripts/combat-golden.sh --native <binary-path> verify
#                                                        Boot <binary-path> directly on THIS host
#                                                        (no docker/compose), e.g. for the macOS-arm64
#                                                        preset build.
#
# Mirrors scripts/boot-golden.sh's capture/verify UX and --native/--service
# forms, but delegates the actual boot+script+compare work to
# tools/combat_smoke.py (a socket-level scripted client), since characterizing
# a scripted in-game combat exchange -- unlike boot-golden.sh's plain boot-log
# capture -- needs real telnet-protocol interaction, not just a log tail.
#
# Requires lib/world/ (see docs/BUILD.md) and a prebuilt server binary for the
# chosen mode (`scripts/rots-docker.sh compile` for the default native/rots
# case building bin/ageland; for --service rots64, build inside that
# container per AGENTS.local.md's "Linux x64 through rots64" section; python3
# is available in both container images, see Dockerfile/Dockerfile.x64).
set -euo pipefail
cd "$(dirname "$0")/.."

TRANSCRIPT_DIR=docs/superpowers/goldens/combat-smoke

SERVICE="${ROTS_SERVICE:-}"
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
case "$mode" in
  capture|verify) ;;
  *) echo "usage: $0 [--service rots64 | --native <binary-path>] capture|verify" >&2; exit 2 ;;
esac

[ -d lib/world ] || { echo "ERROR: lib/world/ missing -- cannot boot. See docs/BUILD.md." >&2; exit 2; }

if [ -n "$NATIVE_BINARY" ] && [ -n "$SERVICE" ]; then
  echo "ERROR: --native and --service are mutually exclusive." >&2
  exit 2
fi

if [ -n "$NATIVE_BINARY" ]; then
  [ -x "$NATIVE_BINARY" ] || { echo "ERROR: $NATIVE_BINARY is not executable -- build it first." >&2; exit 2; }
  echo "Running combat_smoke.py natively against $NATIVE_BINARY ($mode)..."
  python3 tools/combat_smoke.py --mode "$mode" --binary "$NATIVE_BINARY" --transcript-dir "$TRANSCRIPT_DIR"
  exit $?
fi

if [ -n "$SERVICE" ]; then
  case "$SERVICE" in
    rots) BINARY_PATH=bin/ageland ;;
    rots64) BINARY_PATH=build/linux-x64/ageland ;;
    *) echo "ERROR: unknown --service '$SERVICE' (expected 'rots' or 'rots64')" >&2; exit 2 ;;
  esac
  docker compose run --rm --pull never "$SERVICE" bash -lc "[ -f /rots/$BINARY_PATH ]" \
    || { echo "ERROR: $BINARY_PATH missing at /rots/$BINARY_PATH inside the $SERVICE container -- build it first." >&2; exit 2; }
  echo "Running combat_smoke.py inside the $SERVICE container against $BINARY_PATH ($mode)..."
  docker compose run --rm --pull never "$SERVICE" bash -lc \
    "cd /rots && python3 tools/combat_smoke.py --mode '$mode' --binary '$BINARY_PATH' --transcript-dir '$TRANSCRIPT_DIR'"
  exit $?
fi

# Default: native run against the flat-Makefile binary (bin/ageland), the
# same default boot-golden.sh uses for its no-flag `rots` case, but run
# directly on the host rather than through the i386 container -- this
# script's default caller is the per-task gate on whichever host already has
# a built binary; --service rots/--native select the container/other-preset
# forms explicitly.
DEFAULT_BINARY=bin/ageland
[ -x "$DEFAULT_BINARY" ] || { echo "ERROR: $DEFAULT_BINARY is not executable -- build it first, or pass --native/--service." >&2; exit 2; }
echo "Running combat_smoke.py natively against $DEFAULT_BINARY ($mode)..."
python3 tools/combat_smoke.py --mode "$mode" --binary "$DEFAULT_BINARY" --transcript-dir "$TRANSCRIPT_DIR"
