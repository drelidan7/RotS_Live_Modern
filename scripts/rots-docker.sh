#!/usr/bin/env bash
# Helper for building and running RotS in the 32-bit Linux container.
#
# Usage:
#   scripts/rots-docker.sh build      Build the i386 toolchain image (or `docker compose build rots64` for 64-bit).
#   scripts/rots-docker.sh compile    Run `make setup` + `make all` in the container.
#   scripts/rots-docker.sh test       Run the CMake build + gtest suite (`make test`).
#   scripts/rots-docker.sh boot       Compile (if needed) and start the server on :1024.
#                                     Runs WITHOUT -p, so you can connect by plain telnet.
#   scripts/rots-docker.sh shell      Drop into an interactive shell in the container.
#
# Connect to a running server from the host with:  telnet localhost 1024
#
# NOTE: booting requires world files at lib/world/ (see docs/BUILD.md). Compiling does not.
#
# `shell` also honors ROTS_SERVICE (default `rots`) to drop into the 64-bit
# `rots64` container instead (see docker-compose.yml / Dockerfile.x64) — e.g.
# `ROTS_SERVICE=rots64 scripts/rots-docker.sh shell`. `build`/`compile`/`boot`
# are unchanged (32-bit-specific Makefile flow); the 64-bit build uses the
# linux-x64 CMake preset directly inside a `shell` session instead.
set -euo pipefail
cd "$(dirname "$0")/.."

# Bind-mounted outputs use the host identity. CMake gets a separate per-user
# named volume, preserving existing root-owned build caches without changing
# ownership of another writer's files.
export ROTS_UID="${ROTS_UID:-$(id -u)}"
export ROTS_GID="${ROTS_GID:-$(id -g)}"

run_as_host() {
  local service="$1"
  shift
  local compose_options=()
  while [ "$1" != "--" ]; do
    compose_options+=("$1")
    shift
  done
  shift

  local project_name
  project_name="$(docker compose config --format json | python3 -c 'import json,sys; print(json.load(sys.stdin)["name"])')"
  local build_volume="${project_name}_${service}-build-uid${ROTS_UID}-gid${ROTS_GID}"

  # Only initialize an empty, dedicated volume. An unexpected nonempty volume
  # with a different owner is an error, never a recursive ownership repair.
  docker compose run --rm --pull never --no-deps --user 0:0 \
    --volume "$build_volume:/rots/build" "$service" bash -c '
      set -eu
      if [ "$(stat -c %u:%g /rots/build)" != "$1:$2" ]; then
        if [ -n "$(find /rots/build -mindepth 1 -maxdepth 1 -print -quit)" ]; then
          echo "ERROR: nonempty host-user build volume has unexpected ownership; choose a fresh volume." >&2
          exit 1
        fi
        chown "$1:$2" /rots/build
      fi
    ' _ "$ROTS_UID" "$ROTS_GID"

  # Bash 3.2 (the macOS default) treats expansion of an empty array as unset
  # under nounset, so only expand optional compose flags when they exist.
  if [ "${#compose_options[@]}" -gt 0 ]; then
    set -- "${compose_options[@]}" --volume "$build_volume:/rots/build" "$service" "$@"
  else
    set -- --volume "$build_volume:/rots/build" "$service" "$@"
  fi
  docker compose run --rm --pull never "$@"
}

SERVICE="${ROTS_SERVICE:-rots}"
cmd="${1:-boot}"
case "$cmd" in
  build)
    docker compose build rots
    ;;
  compile)
    run_as_host rots -- bash -lc 'cd /rots/src && make setup && make all'
    ;;
  test)
    # Build the TEST target (ageland_tests, not just ageland) and run the gtest binary
    # directly, passing any extra args through, e.g.:
    #   scripts/rots-docker.sh test --gtest_filter=PlayerFinalize.*
    # (`make build` only builds `ageland`; the test binary is the `ageland_tests` target.
    #  ctest's gtest_discover_tests PRE_TEST mode finds 0 tests under bullseye's cmake 3.18,
    #  but the i386 test binary itself runs fine under QEMU, so we invoke ./build/ageland_tests directly.)
    shift || true
    run_as_host rots -- bash -lc \
      'cd /rots && cmake -S src -B build && cmake --build build --target ageland_tests -j16 && ./build/ageland_tests "$@"' \
      _ "$@"
    ;;
  boot)
    if [ ! -d lib/world ]; then
      echo "ERROR: lib/world/ is missing — the server cannot boot without world files." >&2
      echo "See docs/BUILD.md (\"World files\")." >&2
      exit 1
    fi
    # -p is intentionally omitted so raw telnet connections work (no proxy IP header).
    run_as_host rots --service-ports -- bash -lc \
      'cd /rots/src && make setup && make all && cd /rots && exec ./bin/ageland'
    ;;
  shell)
    run_as_host "$SERVICE" --service-ports -- bash
    ;;
  *)
    echo "Unknown command: $cmd (use build|compile|test|boot|shell)" >&2
    exit 1
    ;;
esac
