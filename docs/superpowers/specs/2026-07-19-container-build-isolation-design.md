# Container Build Isolation — Design

Date: 2026-07-19. Status: approved (owner), pre-implementation.
Wave branch: `arch/build-isolation` off master @27af43b (post world-seed merge).

## Problem

The `rots` (i386) and `rots64` containers bind-mount the whole repository at `/rots`, so their
CMake build trees live in the same top-level `build/` directory the host can touch. Three
documented incidents (cited in `scripts/i386-battery.sh` step 0) are all variations of one hazard:
generated CMake state (caches, generated Makefiles) crossing between host and container, or going
stale across source-list changes, and surfacing as confusing link/configure failures — including
one qemu-i386 SIGSEGV class that cost real diagnosis time before the stale-objects cause was found.

The i386 container cannot move to CMake presets (its cmake/ctest is 3.18; presets need >= 3.23),
so the root-Makefile flow (`make configure/build/test`, `BUILD_DIR := build`) must keep working
in-container.

## Decision (Option A — approved)

Mount a private Docker **named volume** over `/rots/build` in each container service:

- `rots-build-i386` -> `rots` service at `/rots/build`
- `rots-build-x64` -> `rots64` service at `/rots/build`

The container then owns a fully private build tree that persists across `docker compose run`
invocations (incremental builds keep working). The host's `build/` holds host-preset
subdirectories only (`macos-arm64`, `macos-arm64-asan`, ...). Host and container generated state
can no longer interact in either direction — the cross-environment incident class becomes
structurally impossible rather than procedurally avoided.

Rejected alternatives:
- **B: `BUILD_DIR ?= build` + per-environment override** — keeps trees host-inspectable but only
  narrows the sharing; other contamination avenues stay open, and it touches Makefile + compose +
  .gitignore + docs.
- **C: upgrade i386 image CMake and adopt the `linux-x86-legacy` preset in-container** — changes
  the canonical shipping-ABI toolchain image for a hygiene fix; highest blast radius.

## Changes

1. **docker-compose.yml**: top-level `volumes:` declaring both named volumes; one volume mount per
   service at `/rots/build` (bind mount of the repo unchanged). Comments document: the rationale
   (three incidents), the clean-rebuild gesture (`docker volume rm rots-build-i386` /
   `rots-build-x64`, or `docker compose down -v` for both), and inspection
   (`docker compose exec`/`docker compose run` + `ls`, or `docker cp`).
2. **scripts/i386-battery.sh**: step 0's pre-clean retargets to run **inside** the `rots`
   container (the volume masks the host path, so a host-side wipe now targets the wrong tree).
   Step 0 stays: the volume kills cross-environment poisoning, but same-environment staleness
   after source-list changes (2 of the 3 incidents) remains possible inside the volume; step 0
   plus the root Makefile `test` target's explicit reconfigure remain the guards for that class.
3. **One-time migration**: containers see empty volumes on first run — one full
   reconfigure/rebuild each, expected and documented. Host-side container-generated files
   (`build/CMakeCache.txt`, `build/CMakeFiles/`, `build/Testing/`, `build/linux-x64/`, any
   stray top-level generated Makefiles/archives from container runs) are deleted so host `build/`
   holds only host presets from then on. Host preset trees are untouched.
4. **Docs**: `docs/BUILD.md` gains a "Container build isolation" section (rationale, volumes,
   clean-rebuild, inspection, migration note). `AGENTS.md`'s "top-level `build/` is the
   container's — hands off" guidance inverts to: host `build/` holds host presets only; container
   trees live in named volumes; the hands-off warning is retired. `AGENTS.local.md` command
   sequences updated to match (no command changes expected for the normal gates — the compose
   commands are unchanged; only the mental model and the battery script's internals change).
5. **CI pre-check (planning-time)**: confirm the GitHub workflows (notably `legacy-32bit`) build
   fresh containers / trees rather than relying on the bind-mounted `build/`. Expected: CI is
   unaffected (fresh environment per run = no persistent volume, compose named volumes are
   created empty per CI run). Verified before implementation; if a workflow DOES reuse
   a tree, the plan gains a task to align it.

### Task 0 riders (world-seed loose ends)

- `src/zone_load.cpp:12-14`: amend the stale "byte-identical" provenance header (whole-branch
  review Important 1) — byte-identical as carved (Task 4); Task 5's cascade then converted the
  buf2 error labels to a local (see the `load_zones` comment).
- `scripts/i386-battery.sh`: stamp completed-step markers with `git rev-parse HEAD` and invalidate
  on mismatch, so a future wave's bare invocation cannot skip steps green-lit by a previous wave
  (whole-branch review Minor 3). Dovetails with the step-0 retarget work in the same file.
- AGENTS.md: replace the "7 on i386 (unverified this wave...)" skip-count caveat with the
  authoritative number extracted from the world-seed battery's saved per-step logs.

## Verification

- **Decisive test (recreates incident #2)**: deliberately write a host-path-poisoned
  `build/CMakeCache.txt` on the host, then run the i386 container's `make test` and confirm the
  container is totally indifferent (its volume masks the poisoned file). Remove the poison file
  afterward (it sits in a git-ignored path).
- Normal per-task gates: rots64 preset ctest + boot golden (first run through the new volume =
  the migration rebuild), macOS native ctest + boot golden (must be untouched by this wave),
  census exit 0.
- Finalization: full i386 battery via the updated script — doubles as the real-world proof of the
  in-container step 0 and the marker stamping; then the six blocking CI jobs.
- Game behavior: zero change. No production source is touched beyond the one `zone_load.cpp`
  comment amendment.

## Out of scope

- Pruning ageland's explicit lower-library link list (ld duplicate-libraries cosmetic; recorded).
- `weather.cpp` dead `protocol.h` include (next world-tier pass).
- The spec §7 Placement seam / `handler.cpp` weld wave — explicitly the NEXT wave after this one
  (owner-sequenced).
