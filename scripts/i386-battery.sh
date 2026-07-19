#!/usr/bin/env bash
# i386-battery.sh -- the AGENTS.local.md "Finalization-only i386 battery"
# sequence, as a per-step-resumable script (world-seed wave Task 6; no
# battery script existed before this -- the entity-complete wave's version
# was session scratch, never committed).
#
# Run this at branch/wave finalization, before merge -- NOT per task
# (AGENTS.md "Verification Cadence"; AGENTS.local.md "Apple Silicon
# verification cadence"). On an Apple Silicon host the qemu-i386 emulation
# this battery drives takes 60-90+ minutes and can hang.
#
# Usage:
#   scripts/i386-battery.sh              Run steps 0-3 in order, skipping any
#                                         step already marked complete.
#   scripts/i386-battery.sh <N>          Run only step N (0, 1, 2, or 3),
#                                         ignoring its marker -- for retrying
#                                         one failed step without repeating
#                                         the whole battery.
#   scripts/i386-battery.sh --reset      Clear all completed-step markers
#                                         (does NOT touch build/ or log/
#                                         itself) so the next bare run starts
#                                         from step 0 again.
#
# Steps run SEQUENTIALLY, never concurrently, by design: concurrent
# qemu-i386 jobs have been observed to be markedly slower and less reliable
# than running one at a time (AGENTS.local.md). This script never
# backgrounds or parallelizes its own steps; if you want parallelism, run
# separate steps from separate shells yourself and accept the risk.
#
# Step 0 -- pre-clean the in-volume build/ tree.
#   Runs INSIDE the `rots` container (docker compose run) and deletes
#   build/CMakeCache.txt, build/CMakeFiles/, and build/Testing/ -- the
#   generated CMake metadata for the top-level, non-preset build/ tree,
#   which now lives in the container-private rots-build-i386 named volume
#   (build-isolation wave, commit bdfacb2) rather than the host bind mount
#   -- WITHOUT touching the host's preset subdirectories
#   (build/macos-arm64/, build/linux-x64/, build/linux-x86-legacy/,
#   build/macos-arm64-asan/) or any already-built host-side top-level
#   archive (build/librots_*.a). This class of failure -- the
#   container-shared build/ tree's CMake metadata going stale relative to
#   CMakeLists.txt/ROTS_*_SOURCES changes because of container/host
#   bind-mount mtime skew, not because CMake's own dependency tracking is
#   wrong -- has bitten this repo's finalization batteries three times on
#   record (see .superpowers/sdd/progress.md): (1) the header-split wave's
#   finalization battery (CoreLayerAcyclicity target missing from a stale
#   tree, fixed by a root-Makefile reconfigure at commit 3280c73); (2) the
#   entity-seed wave's i386 battery round 2 (container /rots/build's cache
#   got poisoned with HOST paths -- a root-Makefile fixer's `make -n test`
#   verification still executed the `+`-prefixed `cmake --build` line on
#   the host, since GNU make runs `+`-prefixed recipe lines even under -n,
#   regenerating build/'s cache host-side), fixed by cleaning the top-level
#   cache and keeping the preset subdirs; and (3) the entity-complete
#   wave's i386 battery round 2 (ageland_tests failed to link interpre-tier
#   symbols in the container ONLY -- both hosts were otherwise green off
#   the same CMakeLists.txt), explicitly logged there as the "3rd
#   occurrence" of this class, fixed the same way: wipe
#   CMakeCache.txt/CMakeFiles/Testing, keep the preset subdirs and
#   archives. Step 0 makes that fix the default first move of every
#   battery run instead of a diagnosis someone has to rediscover a third
#   time.
#
# Step 1 -- `docker compose run --rm --pull never rots bash -lc 'cd /rots &&
#   make test'`: the root Makefile's ageland + ageland_tests + all five
#   *_linkcheck targets, then ctest, inside the `rots` (32-bit i386)
#   container.
# Step 2 -- `docker compose run --rm --pull never rots bash -lc 'cd
#   /rots/src/tests && make clean && make tests && ../../bin/tests'`: the
#   i386 monolithic test runner. MUST start from /rots/src/tests -- its
#   golden paths are relative to that directory.
# Step 3 -- `scripts/boot-golden.sh verify`: the i386 boot-log
#   characterization check.
#
# Each step writes its own timestamped log under log/i386-battery/ and, on
# success, drops a completed-step marker beside it, stamped with the
# `git rev-parse HEAD` the step ran against -- so a rerun after a
# mid-battery failure (or an externally-killed background run, both of
# which have happened to prior batteries per the ledger above) skips every
# already-green step instead of repeating a 60-90 minute run from scratch.
# Markers are per-commit: a marker left over from a prior wave's HEAD does
# NOT green-light a skip once HEAD has moved (--reset still clears them
# outright, unchanged). Every step is also runnable individually (pass its
# number) for a targeted retry.
set -euo pipefail
cd "$(dirname "$0")/.."

LOG_DIR=log/i386-battery
MARKER_DIR="$LOG_DIR/markers"
mkdir -p "$LOG_DIR" "$MARKER_DIR"

marker_path() {
  echo "$MARKER_DIR/step$1.done"
}

step_done() {
  # Markers key on HEAD only, not working-tree cleanliness: a dirty tree at
  # an unchanged HEAD (e.g. docs-only edits) still counts as done and will
  # be skipped. This is intentional, not an oversight — finalization
  # batteries run on committed trees by convention, and adding a git-status
  # check here would force a full rerun on docs-dirty trees, which was
  # explicitly rejected. Commit (or stash) real code changes before relying
  # on markers.
  [ -f "$(marker_path "$1")" ] || return 1
  [ "$(cat "$(marker_path "$1")")" = "$(git rev-parse HEAD)" ]
}

mark_done() {
  # Stamp the marker with the commit the step ran against: a marker from a
  # previous wave (different HEAD) must not green-light a skip on this one.
  git rev-parse HEAD > "$(marker_path "$1")"
}

log_path() {
  echo "$LOG_DIR/step$1-$(date -u +%Y%m%dT%H%M%SZ).log"
}

# --- step 0: pre-clean ------------------------------------------------------
run_step0() {
  echo "== step 0: pre-clean in-volume build/ CMake metadata (keep preset subdirs + archives) =="
  local log
  log="$(log_path 0)"
  {
    # The named volume (rots-build-i386, build-isolation wave) masks the
    # bind-mounted build/, so the pre-clean must run INSIDE the container --
    # a host-side rm would target the (host-preset-only) host build/ tree.
    # Step 0 remains belt-and-braces for SAME-environment staleness after
    # source-list changes (incidents 1 and 3 of the three cited above);
    # cross-environment poisoning (incident 2) is now structurally
    # impossible.
    docker compose run --rm --pull never rots bash -lc '
      set -e
      cd /rots
      if [ -f build/CMakeCache.txt ] || [ -d build/CMakeFiles ] || [ -d build/Testing ]; then
        rm -f build/CMakeCache.txt
        rm -rf build/CMakeFiles build/Testing
        echo "removed in-volume build/CMakeCache.txt, CMakeFiles/, Testing/"
      else
        echo "nothing to remove (in-volume CMake metadata already absent)"
      fi
    '
    echo "kept (host build/, untouched by this in-volume step): any build/*/ preset subdirectory, any build/librots_*.a top-level archive"
  } | tee "$log"
  mark_done 0
}

# --- step 1: make test in the rots (i386) container -------------------------
run_step1() {
  echo "== step 1: docker compose run rots -- make test =="
  local log
  log="$(log_path 1)"
  docker compose run --rm --pull never rots bash -lc 'cd /rots && make test' 2>&1 | tee "$log"
  mark_done 1
}

# --- step 2: i386 monolithic test runner ------------------------------------
run_step2() {
  echo "== step 2: docker compose run rots -- src/tests monolithic runner =="
  local log
  log="$(log_path 2)"
  docker compose run --rm --pull never rots bash -lc \
    'cd /rots/src/tests && make clean && make tests && ../../bin/tests' 2>&1 | tee "$log"
  mark_done 2
}

# --- step 3: i386 boot golden ------------------------------------------------
run_step3() {
  echo "== step 3: scripts/boot-golden.sh verify =="
  local log
  log="$(log_path 3)"
  scripts/boot-golden.sh verify 2>&1 | tee "$log"
  mark_done 3
}

run_step() {
  case "$1" in
    0) run_step0 ;;
    1) run_step1 ;;
    2) run_step2 ;;
    3) run_step3 ;;
    *) echo "ERROR: unknown step '$1' (expected 0, 1, 2, or 3)" >&2; exit 2 ;;
  esac
}

case "${1:-}" in
  --reset)
    rm -f "$MARKER_DIR"/step*.done
    echo "cleared completed-step markers under $MARKER_DIR"
    ;;
  0|1|2|3)
    # Explicit single-step invocation always runs, regardless of its marker.
    run_step "$1"
    ;;
  "")
    # Bare invocation: run every step in order, skipping ones already marked
    # complete from a prior partial run.
    for step in 0 1 2 3; do
      if step_done "$step"; then
        echo "== step $step: already marked complete, skipping (rerun with an explicit step number, or --reset, to force) =="
        continue
      fi
      run_step "$step"
    done
    echo "i386 battery: all steps complete."
    ;;
  *)
    echo "usage: $0 [0|1|2|3 | --reset]" >&2
    exit 2
    ;;
esac
