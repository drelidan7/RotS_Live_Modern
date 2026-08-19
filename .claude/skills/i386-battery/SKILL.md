---
name: i386-battery
description: Use when running the finalization i386 battery (scripts/i386-battery.sh), when a battery step hangs or fails, when the monolithic runner SIGSEGVs, when the boot golden fails after a host build or smoke-account run, or when Docker Desktop misbehaves during a battery.
---

# Running the i386 finalization battery

`scripts/i386-battery.sh` runs the AGENTS.local.md "Finalization-only i386 battery": step 0
pre-cleans the in-volume CMake metadata, step 1 runs `make test` in the i386 `rots` container,
step 2 runs the monolithic single-process test runner from `/rots/src/tests`, step 3 verifies the
i386 boot golden. Run it once at branch/wave finalization before merge — never per task. On this
Apple Silicon host qemu-i386 takes 60–90+ minutes and can hang.

## Rules

- **Sequential only.** Never run battery steps (or two batteries) concurrently — concurrent
  qemu-i386 jobs are markedly slower and less reliable, and two jobs on the shared
  `rots-build-i386` volume corrupt each other.
- **Markers are per-commit.** Each green step drops a marker stamped with `git rev-parse HEAD`;
  a rerun skips already-green steps *for that commit only*. A marker keys on HEAD, not
  working-tree cleanliness — commit real code changes before relying on markers.
  `scripts/i386-battery.sh <N>` forces one step; `--reset` clears all markers.
- **A monolithic SIGSEGV is never tolerated** (AGENTS.md). It is usually a *fixture leaking state
  into a process-global list* — a class per-test ctest is structurally blind to because each ctest
  test gets its own process (LS-2's `waiting_list` stack-pointer leak is the canonical case).
  Start diagnosis with a clean rebuild (stale objects caused one historical false alarm), then
  bisect the fixture, never carve out the test.
- **Reconcile the counts, don't eyeball them.** ctest total = monolithic gtest-visible total
  + the CMake-ctest-only checks (the nine `*LayerAcyclicity` linkchecks, `LocationReadCensus`,
  `LocationReadCensusSelfTest`, `RoomResolveCensus`, `RoomResolveCensusSelfTest`). Monolithic
  skips = ctest skips + 17 `PerRace/ConvertEquivalence.*` (monolithic-only by design —
  `rots_convert` is CMake-only, absent from the flat build). If the arithmetic doesn't reconcile
  exactly, a test file is missing from one of the two build systems (CMakeLists.txt vs the flat
  `src/tests/Makefile` `SRCS`) — that exact gap has been caught by this reconciliation before.
  Current expected figures live in AGENTS.md's Testing Guidelines chain; re-derive, never carry
  a stale number forward.

## Pre-flight

1. Confirm HEAD is the commit you intend to certify and the tree is committed.
2. **Normalize future mtimes** if any sabotage probe or clock skew may have left them
   (they cause spurious make skips/rebuilds):
   `find src tools -newermt tomorrow -exec touch {} +`
3. Confirm Docker Desktop is healthy: `docker ps` succeeds AND
   `docker info --format '{{.ServerVersion}}'` is non-empty. (A version-only check passes while
   the VM is still broken.)

## Trap 1 — Mach-O in `bin/` (step 3 fails after a host build)

A host `make smoke-account` or native flow can leave a macOS Mach-O `bin/ageland`; step 3 then
boots a binary the i386 container cannot run. Restore the container-built ELF in-container:

```bash
docker compose run --rm --pull never rots bash -lc 'cp /rots/build/ageland /rots/bin/ageland'
```

Check with `file bin/ageland` (want ELF 32-bit, not Mach-O). This trap is recurring — it has hit
at least twice (LS-3b T9/T9b).

## Trap 2 — qemu hang vs. wedged Docker Desktop VM

Both look like a stuck step. Discriminate with `docker top <cid>`:

- **>3 process lines** → genuinely compiling; leave it alone (step 1 is ~10 min normally, but
  slow is not stuck).
- **2 process lines** (a *childless* `qemu-i386 /usr/bin/make` pegged ~100% CPU far past the
  step's normal runtime) → the Docker VM is wedged. A pristine build volume does NOT fix this;
  the cure is a proper Docker Desktop restart.

**Restart sequence (order matters):**
1. `osascript -e 'quit app "Docker"'`
2. Poll until `pgrep -f com.docker.backend` returns nothing (~10 s).
3. `open -a Docker`
4. Poll until BOTH `docker ps` succeeds and `docker info --format '{{.ServerVersion}}'` is
   non-empty (~20 s).

Skipping the polls yields a half-initialized daemon (API 500s) against which a relaunched battery
dies in ~1 minute — easy to misread as another hang.

## Trap 3 — orphaned in-container work

If a battery wrapper is killed host-side, the container keeps running. **Do not relaunch on top
of it** (two qemu jobs on the shared volume). Watch for the orphan to exit:

```bash
docker ps -q --filter name=rots_live_modern   # empty = clear to proceed
```

Stop stragglers with `docker stop <cid>` before restarting. Markers are per-commit, so a restart
resumes rather than repeats.

## Reporting

Logs land under `log/i386-battery/step<N>-<timestamp>.log`. A finalization report cites the step
log filenames, the measured ctest total/skips, the monolithic reconciliation arithmetic, and the
boot-golden result — measured from this run's logs, never carried forward.
