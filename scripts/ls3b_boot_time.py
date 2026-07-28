#!/usr/bin/env python3
"""LS-3b T3 boot-time poller (the perf harness's M4/boot-time arm).

scripts/boot-golden.sh cannot supply a boot-time delta: it polls its raw log
at 1-second granularity (`for i in $(seq 1 60); do sleep 1; ...`) and its
normalize() strips every timestamp out of what it keeps, so the golden itself
carries no timing signal at all (census D Sec5.2(c) / surprise #8). This
script is the dedicated poller that finding calls for -- it does NOT modify
boot-golden.sh, it boots the same way boot-golden.sh's --native mode does
(the binary launched directly, no docker/compose, cwd = repo root, no -p
flag -- matching the plain-telnet dev boot) but polls at 20ms granularity via
time.monotonic() instead of 1s via `sleep 1`, so it can actually see
sub-second boot-time drift.

Never rebuilds the binary; the caller must already have built it (see
scripts/boot-golden.sh's own header for the build commands this repo uses
for the macos-arm64/linux-x64 presets).

Usage: ls3b_boot_time.py <binary-path> <run-count>
Prints one `key=value` line per sample plus the summary (runs/median/min/max),
matching src/location_benchmark.h's machine-readable convention so the same
grep-based capture step used for the C++ harness's report also works here.
"""
import os
import signal
import statistics
import subprocess
import sys
import tempfile
import time

# Wall-clock ceiling for a single boot attempt, seconds. Matches
# boot-golden.sh's own 60-second ceiling (`seq 1 60` at 1s/step) so a hung
# boot fails the same way here as it would there, just detected sooner
# (20ms polling vs 1s).
_BOOT_DEADLINE_SECONDS = 60.0
# Poll granularity, seconds. boot-golden.sh polls at 1s (`sleep 1`); this
# script exists specifically to see sub-second boot-time drift, so it polls
# 50x finer.
_POLL_INTERVAL_SECONDS = 0.02
# The exact marker line comm.cpp logs once world load finishes -- the same
# string boot-golden.sh's capture_log()/capture_log_native() grep for.
_BOOT_COMPLETE_MARKER = b"Entering game loop"


def boot_once(binary_path: str) -> float:
    """Launches binary_path once, waits for the boot-complete marker, and
    returns the elapsed wall-clock seconds from just-before-exec to the
    instant the marker was observed in its log. Always terminates the
    launched process before returning, whether it reached the marker or
    not, so a caller's next boot_once() never contends with a leftover
    listener on the same port."""
    with tempfile.NamedTemporaryFile(prefix="ls3b-boot-", suffix=".log", delete=False) as handle:
        log_path = handle.name

    with open(log_path, "wb") as log_out:
        start = time.monotonic()
        # No -p flag: matches boot-golden.sh's --native launch (the plain-
        # telnet dev boot on the default port), and cwd is the repo root
        # (the caller's shell wrapper `cd`s there first, mirroring
        # boot-golden.sh's own `cd "$(dirname "$0")/.."`).
        process = subprocess.Popen([binary_path], stdout=log_out, stderr=subprocess.STDOUT)
        try:
            deadline = start + _BOOT_DEADLINE_SECONDS
            elapsed = None
            while time.monotonic() < deadline:
                time.sleep(_POLL_INTERVAL_SECONDS)
                with open(log_path, "rb") as log_in:
                    if _BOOT_COMPLETE_MARKER in log_in.read():
                        elapsed = time.monotonic() - start
                        break
            if elapsed is None:
                raise RuntimeError(
                    f"server did not reach '{_BOOT_COMPLETE_MARKER.decode()}' within "
                    f"{_BOOT_DEADLINE_SECONDS:.0f}s"
                )
            return elapsed
        finally:
            try:
                process.send_signal(signal.SIGTERM)
                process.wait(timeout=5)
            except Exception:
                try:
                    process.kill()
                    process.wait(timeout=5)
                except Exception:
                    pass
            try:
                os.remove(log_path)
            except OSError:
                pass


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: ls3b_boot_time.py <binary-path> <run-count>", file=sys.stderr)
        return 2

    binary_path = sys.argv[1]
    try:
        run_count = int(sys.argv[2])
    except ValueError:
        print("run-count must be an integer", file=sys.stderr)
        return 2
    if run_count < 1:
        print("run-count must be >= 1", file=sys.stderr)
        return 2

    samples_ms = []
    for i in range(run_count):
        try:
            elapsed_seconds = boot_once(binary_path)
        except RuntimeError as exc:
            print(f"ERROR: run {i}: {exc}", file=sys.stderr)
            return 1
        ms = elapsed_seconds * 1000.0
        samples_ms.append(ms)
        print(f"boot.run[{i}].ms={ms:.3f}")

    print(f"boot.runs={run_count}")
    print(f"boot.median_ms={statistics.median(samples_ms):.3f}")
    print(f"boot.min_ms={min(samples_ms):.3f}")
    print(f"boot.max_ms={max(samples_ms):.3f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
