# Container Build Isolation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Mount a private Docker named volume over `/rots/build` in each container service so host↔container CMake-state contamination becomes structurally impossible, plus the world-seed rider fixes.

**Architecture:** Per spec `docs/superpowers/specs/2026-07-19-container-build-isolation-design.md` (Option A, approved). Compose-level named volumes (`rots-build-i386`, `rots-build-x64`); the repo bind mount is untouched, so `bin/`, `lib/`, `src/`, and the flat-make flows stay shared. `scripts/boot-golden.sh` service mode and `scripts/i386-battery.sh` step 0 retarget to in-container operation; markers gain HEAD-sha invalidation.

**Tech Stack:** docker compose named volumes, bash, CMake 3.18 (i386 container — no presets there), the root Makefile flow.

## Global Constraints

- **Zero game-behavior change.** The only production-source touch is the `zone_load.cpp:12-14` comment amendment. Goldens byte-for-byte; ctest totals stay **1281** both hosts.
- Per-task gates: macOS `macos-arm64` full ctest + native boot golden (must be untouched by this wave) AND rots64 preset + ctest + boot golden (through the new volume). Census exit 0. Full i386 battery at finalization only (controller-owned).
- **Formatter hook:** the `zone_load.cpp` edit MUST be a python byte-edit run via Bash (the PostToolUse clang-format hook mangles .cpp/.h touched by Edit/Write). Shell/YAML/Markdown files are safe for normal Edit/Write.
- CI pre-check result (verified at planning): `legacy-32bit` uses `docker compose build` + `docker compose run --rm rots` on a fresh runner — named volumes are created empty per CI run; no workflow change needed.
- Do not run any docker command while another battery/container gate is mid-run.

---

### Task 1: World-seed riders (zone_load header + AGENTS.md i386 skip count)

**Files:**
- Modify: `src/zone_load.cpp:12-14` (python byte-edit)
- Modify: `AGENTS.md` (Testing Guidelines bullet — the "7 on i386 (unverified this wave...)" clause)
- Read-only inputs: `log/i386-battery/step1-*.log`, `log/i386-battery/step2-*.log` (world-seed battery logs)

**Interfaces:** none produced; standalone doc/comment fixes.

- [ ] **Step 1: Amend the stale provenance header in zone_load.cpp**

Current text (lines 12-14 region) claims the carved body is byte-identical to zone.cpp's prior lines 42-302 — true at the Task 4 carve, falsified by world-seed Task 5's buf2 cascade. Replace the sentence with (python byte-edit; preserve surrounding lines exactly):

```
 * The carved body was byte-identical to zone.cpp's prior lines 42-302 as
 * carved (world-seed Task 4); Task 5's linkcheck cascade then converted the
 * buf2 error labels in load_zones() to a local composer -- see the comment
 * at that site for the disposition.
```

Match the file's existing comment style (leading ` * `). Verify with `git diff src/zone_load.cpp`: only the intended lines changed, no formatter churn.

- [ ] **Step 2: Extract the authoritative i386 skip count from the battery logs**

The world-seed battery logs live under `log/i386-battery/`. The step-1 log is `cd build && ctest --output-on-failure` output (passing tests' gtest output is hidden there, so `[  SKIPPED  ]` lines are NOT reliably visible); the step-2 log is the monolithic runner's full gtest output. Method:
- `grep -c '^\[  SKIPPED \]' log/i386-battery/step2-*.log` → total monolithic skips.
- `grep '^\[  SKIPPED \]' log/i386-battery/step2-*.log | grep -c 'ConvertEquivalence'` → the monolithic-only, by-design skip-gated ConvertEquivalence cases (they RUN in the ctest flow via `rots_convert`; the monolithic runner skip-gates them by design — see the entity-complete wave ledger).
- The ctest-flow i386 skip count = (total − ConvertEquivalence-gated) **only if** every remaining skip is platform-gated the same way in both flows; spot-check the non-ConvertEquivalence skip names against the macOS/rots64 skip categories in `.superpowers/sdd/task-6-report.md`.
- If the numbers reconcile cleanly, replace AGENTS.md's "7 on i386 (unverified this wave; last measured at the persist-split wave)" with the measured number and a parenthetical "(measured from the world-seed finalization battery logs)". If they do NOT reconcile cleanly, document BOTH numbers with one line each on what each counts — honesty over false precision.

- [ ] **Step 3: Gates (light — comment/doc-only)**

Run: `cd src && cmake --build --preset macos-arm64 -j4` (expect trivial success; zone_load.cpp recompiles) and `python3 tools/string_view_census.py --check` (expect exit 0). Full ctest not required for comment-only changes.

- [ ] **Step 4: Commit**

```bash
git add src/zone_load.cpp AGENTS.md
git commit -m "docs: world-seed riders — zone_load provenance amendment + measured i386 skip count"
```

### Task 2: Named volumes + migration + boot-golden service-mode fix

**Files:**
- Modify: `docker-compose.yml`
- Modify: `scripts/boot-golden.sh:80-99` (comment + service-mode existence check)
- Host cleanup (not committed — git-ignored paths): `build/CMakeCache.txt`, `build/CMakeFiles/`, `build/Testing/`, `build/linux-x64/`, `build/librots_*.a`

**Interfaces:**
- Produces: compose services with `/rots/build` backed by named volumes `rots-build-i386` / `rots-build-x64` — Task 3's battery step 0 and Task 4's docs rely on these exact names.

- [ ] **Step 1: Add the named volumes to docker-compose.yml**

In each service's `volumes:` list add the build mount after the repo bind mount, and add a top-level `volumes:` section at the end of the file:

```yaml
  # (inside services.rots)
    volumes:
      - .:/rots
      # Container-private CMake tree (see docs/BUILD.md "Container build
      # isolation"): masks the bind-mounted build/ so host and container
      # generated state can never interact. Clean rebuild: docker volume rm
      # rots-build-i386 (or `docker compose down -v` to clear both).
      # Inspect: docker compose run --rm rots ls /rots/build
      - rots-build-i386:/rots/build

  # (inside services.rots64)
    volumes:
      - .:/rots
      # Same isolation for the 64-bit sibling; volume rots-build-x64.
      - rots-build-x64:/rots/build

# (top level, end of file)
volumes:
  # Named build volumes. Motivated by three stale-generated-tree incidents
  # (see scripts/i386-battery.sh step 0's citations): host<->container CMake
  # state can no longer cross. CI runners create these empty per run.
  rots-build-i386:
  rots-build-x64:
```

- [ ] **Step 2: Fix boot-golden.sh service mode**

Two edits at `scripts/boot-golden.sh:80-99`:
(a) Rewrite the stale comment ("Both paths are bind-mounted, so they're visible identically from the host...") to:

```bash
  # Each service builds its binary at a different path: the 32-bit Makefile
  # build produces ./bin/ageland (bind-mounted, host-visible); the 64-bit
  # CMake linux-x64 preset puts it at ./build/linux-x64/ageland, which since
  # the build-isolation wave lives in the rots-build-x64 NAMED VOLUME and is
  # visible only inside the container -- so service mode defers the
  # existence check to the container (below) instead of stat-ing the host
  # path.
```

(b) Replace the host-side service-mode existence check (`[ -f "$BINARY_PATH" ] || { ... }` in the else-branch) with an in-container check:

```bash
else
  docker compose run --rm --pull never "$SERVICE" bash -lc "[ -f /rots/$BINARY_PATH ]" \
    || { echo "ERROR: $BINARY_PATH missing inside the $SERVICE container's build volume — build it first (see script header)." >&2; exit 2; }
fi
```

The `--native` branch keeps its host-side `[ -x ]` check unchanged. Run `bash -n scripts/boot-golden.sh`.

- [ ] **Step 3: One-time host-side migration cleanup**

```bash
rm -f build/CMakeCache.txt build/librots_platform.a build/librots_core.a build/librots_entity.a build/librots_persist.a build/librots_world.a
rm -rf build/CMakeFiles build/Testing build/linux-x64
ls build/   # expect ONLY host preset subdirs (macos-arm64, macos-arm64-asan, ...)
```

These are git-ignored container artifacts now masked in-container; nothing to commit.

- [ ] **Step 4: The decisive poison test (recreates incident #2)**

```bash
printf 'CMAKE_HOME_DIRECTORY:INTERNAL=/Users/drelidan/POISON\n' > build/CMakeCache.txt
docker compose run --rm --pull never rots bash -lc 'cd /rots && make configure && grep CMAKE_HOME_DIRECTORY build/CMakeCache.txt'
# Expected: configure succeeds fresh in the (empty) volume; grep prints
# CMAKE_HOME_DIRECTORY:INTERNAL=/rots — the poisoned host file is invisible.
rm build/CMakeCache.txt
ls build/  # poison gone; host presets intact
```

- [ ] **Step 5: rots64 migration rebuild + gates**

```bash
docker compose run --rm --pull never rots64 bash -lc 'cd /rots/src && cmake --preset linux-x64 && cmake --build --preset linux-x64 -j"$(nproc)" && ctest --preset linux-x64'
scripts/boot-golden.sh --service rots64 verify
```
Expected: full configure+build in the fresh volume (the one-time migration cost), ctest 1281/1281, boot golden matches through the new in-container existence check. Then the macOS-untouched gate:
```bash
cd src && ctest --preset macos-arm64   # expect 1281/1281, zero rebuild churn
cd .. && scripts/boot-golden.sh --native build/macos-arm64/ageland verify
python3 tools/string_view_census.py --check
```

- [ ] **Step 6: Commit**

```bash
git add docker-compose.yml scripts/boot-golden.sh
git commit -m "build: container-private named volumes over /rots/build (+ boot-golden in-container check)"
```

### Task 3: Battery script — in-container step 0 + HEAD-sha markers

**Files:**
- Modify: `scripts/i386-battery.sh` (`run_step0()` ~lines 100-115; `mark_done()`/`step_done()` ~lines 84-94; header comment)

**Interfaces:**
- Consumes: the `rots-build-i386` volume name from Task 2 (referenced in comments only; the wipe happens through `docker compose run rots`, which resolves the volume itself).

- [ ] **Step 1: Retarget step 0 in-container**

Replace `run_step0()`'s host-side `rm` block with:

```bash
    # The named volume (rots-build-i386, build-isolation wave) masks the
    # bind-mounted build/, so the pre-clean must run INSIDE the container --
    # a host-side rm would target the (host-preset-only) host build/ tree.
    # Step 0 remains belt-and-braces for SAME-environment staleness after
    # source-list changes (incidents 1 and 3 of the three cited above);
    # cross-environment poisoning (incident 2) is now structurally
    # impossible.
    docker compose run --rm --pull never rots bash -lc '
      cd /rots
      if [ -f build/CMakeCache.txt ] || [ -d build/CMakeFiles ] || [ -d build/Testing ]; then
        rm -f build/CMakeCache.txt
        rm -rf build/CMakeFiles build/Testing
        echo "removed in-volume build/CMakeCache.txt, CMakeFiles/, Testing/"
      else
        echo "nothing to remove (in-volume CMake metadata already absent)"
      fi
    '
```

Also update the header's step-0 description line and the "keep preset subdirs + archives" echo to say "in-volume" (the volume holds no host presets, but the phrasing must not claim to touch host build/).

- [ ] **Step 2: HEAD-sha marker stamping**

```bash
mark_done() {
  # Stamp the marker with the commit the step ran against: a marker from a
  # previous wave (different HEAD) must not green-light a skip on this one.
  git rev-parse HEAD > "$(marker_path "$1")"
}

step_done() {
  [ -f "$(marker_path "$1")" ] || return 1
  [ "$(cat "$(marker_path "$1")")" = "$(git rev-parse HEAD)" ]
}
```

Update the header comment's marker description accordingly (markers are per-commit; `--reset` unchanged).

- [ ] **Step 3: Verify marker invalidation + step 0 standalone**

```bash
bash -n scripts/i386-battery.sh
mkdir -p log/i386-battery/markers && echo 0000000000000000000000000000000000000000 > log/i386-battery/markers/step0.done
scripts/i386-battery.sh 0     # explicit single-step always runs; observe in-container wipe output
grep -q "$(git rev-parse HEAD)" log/i386-battery/markers/step0.done && echo MARKER-STAMPED-OK
```
Then confirm skip behavior: temporarily re-run `scripts/i386-battery.sh 0` — explicit invocation runs regardless (existing contract); the stale-sha invalidation is exercised for real at finalization's full run. Do NOT run steps 1-3 here.

- [ ] **Step 4: Commit**

```bash
git add scripts/i386-battery.sh
git commit -m "build: battery step-0 wipes in-volume, markers invalidate on HEAD change"
```

### Task 4: Docs

**Files:**
- Modify: `docs/BUILD.md` (new "Container build isolation" section near the container/build-matrix material)
- Modify: `AGENTS.md` (the "Top-level `build/` is the container's — hands off" guidance, wherever it appears in Verification Cadence / testing prose)
- Modify: `AGENTS.local.md` (same inversion in the machine-local cadence notes)

**Interfaces:** consumes the volume names and gestures exactly as implemented in Tasks 2-3.

- [ ] **Step 1: BUILD.md section** — cover: rationale (three incidents, cite the battery script), the two volume names and which service owns each, what stays bind-mounted (`bin/`, `src/`, `lib/`, flat-make trees), clean-rebuild (`docker volume rm rots-build-i386|rots-build-x64`, `docker compose down -v`), inspection (`docker compose run --rm <svc> ls /rots/build`), the one-time migration note (first post-change container run reconfigures from scratch), and CI (fresh empty volumes per run; no workflow change).
- [ ] **Step 2: AGENTS.md / AGENTS.local.md inversion** — replace every "top-level `build/` is the container's — hands off; never `make -n test` on host" instance with: host `build/` holds host-preset subdirectories only; container CMake trees live in named volumes (`rots-build-i386`/`rots-build-x64`) and cannot be touched from the host; the `make -n test` warning is retired (a host `make -n test` can no longer reach container state — though it still generates a host-side tree, which is now harmless and host-owned).
- [ ] **Step 3: Gates + commit** — census exit 0 (docs-only otherwise);

```bash
git add docs/BUILD.md AGENTS.md AGENTS.local.md
git commit -m "docs: container build isolation as-built (volumes, migration, inverted hands-off rule)"
```

### Task 5: Finalization (controller-owned — not for an implementer subagent)

- [ ] **Step 1:** Full i386 battery via `scripts/i386-battery.sh` (fresh volume ⇒ step 1 does a from-scratch qemu configure+build; expect all three steps green and markers stamped with the branch HEAD).
- [ ] **Step 2:** Whole-branch review (most capable model), fix wave if needed.
- [ ] **Step 3:** Push; PR against master describing the isolation change, the migration note for other checkouts of this repo, and the world-seed riders. Merge = owner's call.

---

## Self-Review Notes

- **Spec coverage:** compose volumes (T2), battery retarget (T3), migration (T2 S3), docs (T4), CI pre-check (done at planning, recorded in Global Constraints), poison test (T2 S4), riders (T1 + T3 S2), finalization (T5). The boot-golden service-mode fix was discovered during planning and folded into T2 (its gate depends on it).
- **Placeholder scan:** all edits carry exact code; the AGENTS.md skip-count edit is deliberately evidence-driven with an explicit reconcile-or-document-both contract.
- **Consistency:** volume names `rots-build-i386`/`rots-build-x64` used identically in T2/T3/T4; marker functions match the script's existing `marker_path` contract; `--pull never` used on every compose invocation per AGENTS.local.md.
