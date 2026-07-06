# Phase 1: Build System + CI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make CMake the single authoritative build with per-platform presets (`linux-x64`, `linux-x86-legacy`, `macos-arm64`, `windows-msvc`) and stand up a four-platform GitHub Actions matrix where the 32-bit legacy job is required-green and the not-yet-enabled platforms run as allowed-to-fail.

**Architecture:** Phase 1 of the spec at `docs/superpowers/specs/2026-07-06-cpp-modernization-design.md`. The 32-bit ABI becomes a CMake option (`ROTS_LEGACY_32BIT`, default ON) instead of hardcoded flags; GCC-only flags get compiler/platform guards so non-Linux configures fail on *real porting issues* (Phase 2/3's job), not flag syntax. The i386 container (CMake 3.18, can't read presets) keeps the existing root-Makefile flow untouched; presets serve the host and CI runners (CMake ≥3.23).

**Tech Stack:** CMake ≥3.18 (container) / ≥3.23 (presets, host 4.3.3), GNU Make, GoogleTest, GitHub Actions, Docker (i386/debian:bullseye).

## Global Constraints

- The 32-bit container build must remain bit-for-bit behavior-identical: compile flags for the `ageland` target under the legacy configuration must not change (`-m32 -funsigned-char -fstrict-aliasing -w -g -D_FILE_OFFSET_BITS=64`, link `-m32 -rdynamic`, link `crypt`); the characterization goldens (suite 503 tests: 495 pass / 8 skip / 0 fail; `scripts/boot-golden.sh verify`) must still pass after every task that touches build files.
- Spec exit criteria for Phase 1: "one `cmake --preset` command per platform; CI green on Linux 32-bit and running (even if red) elsewhere."
- Preset names, verbatim from the spec: `linux-x64`, `linux-x86-legacy`, `macos-arm64`, `windows-msvc`.
- CI must not depend on `lib/world/` (separate repo, git-ignored): unit tests + committed goldens only; no boot smoke, no `account_smoke.py` in CI.
- ctest masking guard: every CI test step must fail loudly on zero-tests (`--no-tests=error` where ctest ≥3.20 is available) — the container's ctest 3.18 "No tests found → exit 0" trap and the upstream fixture `exit(0)` masking are both documented history (see `.superpowers/sdd/progress.md`).
- Modern CMake rules (cmake skill): target-scoped everything, no directory-level globals, no global `CMAKE_CXX_FLAGS`, explicit source lists, generator expressions for per-compiler/config logic.
- Never commit anything under `lib/players/`, `lib/plrobjs/`, `lib/exploits/`, `lib/accounts/`, `lib/world/`, or `log/`. Commit subjects imperative, ≤72 chars.
- Pushing to `origin` requires the user's one-time go-ahead (Task 4 asks before the first push).

**Key facts an implementer needs:**
- Host: macOS arm64, CMake 4.3.3, no ninja → POSIX presets use `"Unix Makefiles"`.
- Container: `scripts/rots-docker.sh`, CMake/ctest 3.18 — cannot read `CMakePresets.json`; root `Makefile` already works around `--test-dir` (`cd build && ctest`).
- Current `src/CMakeLists.txt` is already target-based (post-Phase-0) but hardcodes `-m32` and GCC-isms on both targets; `find_package(GTest REQUIRED)` is unconditional; `ageland_tests` links `-Wl,--wrap=_Z6numberv -Wl,--wrap=_Z6numberii` (GNU ld only — ld64/MSVC don't support `--wrap`; the seam breaks on macOS until Phase 2 finds a replacement).
- `.github/workflows/ci.yml` is currently `workflow_dispatch:`-only (neutralized upstream import).
- GitHub runners: i386 containers run natively on amd64 (no QEMU) — the Docker legacy job is fast in CI.

---

### Task 1: Parameterize the ABI and platform axes in src/CMakeLists.txt

Make the 32-bit build a configuration choice and guard toolchain-specific flags, without changing a single compile flag in the legacy configuration.

**Files:**
- Modify: `src/CMakeLists.txt` (header comment ~1-21, flag-parity block ~66-81, crypt link ~84-86, tests block ~88-183)

**Interfaces:**
- Produces (Task 2's presets set these; Task 3's CI overrides them per job):
  - `ROTS_LEGACY_32BIT` — BOOL option, **default ON**; ON adds `-m32` (compile+link).
  - `ROTS_BUILD_TESTS` — BOOL option, **default ON**; OFF skips GTest discovery and the `ageland_tests` target entirely.
- Existing target names `ageland`, `ageland_tests`, and custom targets `setup`/`format` keep their names — the root Makefile and Task 3's CI call them.

- [ ] **Step 1: Record the legacy baseline compile/link command (the "before" for parity)**

```bash
docker compose run --rm rots bash -lc 'cd /rots && rm -rf build && make configure && cmake --build build --target ageland --verbose -j8 2>&1 | grep -m2 -o "g++.*utility\.cpp\|g++.*-o ageland.*" ' > /tmp/flags-before.txt; cat /tmp/flags-before.txt
```

Expected: one compile line containing `-m32 -funsigned-char -fstrict-aliasing -w -g -D_FILE_OFFSET_BITS=64` and (if the link line matched) `-m32 -rdynamic`. Keep `/tmp/flags-before.txt` for Step 5. (If the grep patterns miss, run without the grep and copy the `utility.cpp` compile line and the final link line by hand — the point is a saved before-snapshot.)

- [ ] **Step 2: Bump the version floor and add the options block**

In `src/CMakeLists.txt`, change the `cmake_minimum_required` line and add options directly after `project()`:

```cmake
cmake_minimum_required(VERSION 3.18...3.28)
project(RotS LANGUAGES CXX)

# --- Build-configuration axes (set by CMakePresets.json / CI; defaults preserve
# --- the historical 32-bit container build) -----------------------------------
# ON: build the historical 32-bit binary (-m32 compile+link). The i386 container
# and the root Makefile rely on this default. Presets for 64-bit platforms turn
# it OFF (those builds are expected to fail until Phase 2 of the modernization
# plan lands the 64-bit port).
option(ROTS_LEGACY_32BIT "Build the legacy 32-bit (-m32) binary" ON)
# OFF skips GoogleTest discovery and the ageland_tests target entirely — for
# platforms where GTest isn't installed yet (e.g. the windows-msvc preset).
option(ROTS_BUILD_TESTS "Build the ageland_tests GoogleTest binary" ON)
```

- [ ] **Step 3: Rewrite the flag blocks with option/compiler/platform guards**

Replace the current flag-parity block (`target_compile_options(ageland PRIVATE -m32 ...)` + `target_link_options(ageland PRIVATE -m32 -rdynamic)` + `target_link_libraries(ageland PRIVATE crypt)`) with:

```cmake
# --- Flag parity with src/Makefile (behavior-affecting — keep in sync) --------
# The historical flags, now split along two axes:
#   * ROTS_LEGACY_32BIT gates -m32 (compile AND link).
#   * Compiler guards keep GCC/Clang spellings away from MSVC, which fails hard
#     on unknown flags. MSVC gets /J — the -funsigned-char equivalent — because
#     unsigned char is a load-bearing behavior assumption (spec Phase 5 audits
#     it away; until then every compiler must pin it).
# GNU-family flags: -funsigned-char / -fstrict-aliasing / -w / -g are identical
# on GCC, Clang, and AppleClang.
set(ROTS_GNULIKE "$<CXX_COMPILER_ID:GNU,Clang,AppleClang>")
target_compile_options(ageland PRIVATE
    $<$<BOOL:${ROTS_LEGACY_32BIT}>:-m32>
    $<${ROTS_GNULIKE}:-funsigned-char -fstrict-aliasing -w -g>
    $<$<CXX_COMPILER_ID:MSVC>:/J /w>
)
target_link_options(ageland PRIVATE
    $<$<BOOL:${ROTS_LEGACY_32BIT}>:-m32>
    # -rdynamic: GNU spelling; the clang driver translates it on macOS. MSVC: none.
    $<${ROTS_GNULIKE}:-rdynamic>
)
# _FILE_OFFSET_BITS is a glibc knob; harmless but meaningless off Linux/MSVC.
target_compile_definitions(ageland PRIVATE
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:_FILE_OFFSET_BITS=64>
)

# account_management*.cpp calls libc crypt(): a separate libcrypt on Linux,
# inside libc on macOS (no -lcrypt exists there), absent on Windows (the
# Windows build stays red on this symbol until Phase 3 replaces crypt()).
if(UNIX AND NOT APPLE)
    target_link_libraries(ageland PRIVATE crypt)
endif()
```

Note: `-D_FILE_OFFSET_BITS=64` was previously inside `target_compile_options`; it moves to `target_compile_definitions` (same command line, correct CMake idiom). `-g`/`-O0`/`-DDEBUG` per-config block stays as-is but wrap the `-O0 -DDEBUG`/`-O2 -DNDEBUG` options in the same `$<${ROTS_GNULIKE}:...>` guard (MSVC uses different spellings; it gets CMake's own per-config defaults, which is fine for an expected-red build).

- [ ] **Step 4: Gate the tests block and guard its GNU-isms**

Wrap the entire tests section (from `include(CTest)` through `gtest_discover_tests(...)`) in:

```cmake
if(ROTS_BUILD_TESTS)
    # ... existing tests block ...
endif()
```

Inside it, apply the same treatment to `ageland_tests`:
- `find_package(GTest REQUIRED)` stays, but now only runs when tests are ON.
- Mirror Step 3 on `ageland_tests`'s compile options: `$<$<BOOL:${ROTS_LEGACY_32BIT}>:-m32>` replaces any bare `-m32`; wrap `-msse2 -mfpmath=sse` and the `-w`/`-Wall -Wextra` pair in `$<${ROTS_GNULIKE}:...>`; guard `_FILE_OFFSET_BITS=64` as in Step 3 (keep the `TESTING` define unconditional).
- Link options: the `--wrap` seam is GNU-ld-only. Replace the current block with:

```cmake
    # test_random_utils.cpp wraps number() for deterministic RNG in tests.
    # -Wl,--wrap is GNU ld only: ld64 (macOS) and MSVC link.exe have no
    # equivalent, so the RNG seam does not exist off Linux yet — Phase 2 must
    # replace it (e.g. a link-time indirection) before macOS tests can pass.
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_options(ageland_tests PRIVATE
            -Wl,--wrap=_Z6numberv
            -Wl,--wrap=_Z6numberii
        )
    endif()
```

- Link libraries: `GTest::gtest` stays unconditional (inside the gate); `crypt` gets the same `if(UNIX AND NOT APPLE)` guard as Step 3.
- `ROTS_GOLDEN_DIR`, the `-m32`-independent definitions, and `gtest_discover_tests(... WORKING_DIRECTORY ...)` stay unchanged.

- [ ] **Step 5: Verify legacy parity in the container (the critical gate)**

```bash
docker compose run --rm rots bash -lc 'cd /rots && rm -rf build && make configure && cmake --build build --target ageland --verbose -j8 2>&1 | grep -m2 -o "g++.*utility\.cpp\|g++.*-o ageland.*"' > /tmp/flags-after.txt
diff /tmp/flags-before.txt /tmp/flags-after.txt && echo PARITY-OK
```

Expected: `PARITY-OK` — flag ordering may shift; if the diff shows ONLY reordering of identical flags, record that and continue; any added/removed/changed flag is a failure — fix before proceeding.

- [ ] **Step 6: Full legacy verification battery**

```bash
docker compose run --rm rots bash -lc 'cd /rots && make test'
docker compose run --rm rots bash -lc 'cd /rots/src/tests && make tests && /rots/bin/tests'
scripts/boot-golden.sh verify
```

Expected: ctest 503/503; Makefile runner 495 pass / 8 skip / 0 fail; "boot log matches golden". (The src/tests Makefile is untouched by this task, but run it anyway — it proves the tree still serves both runners.)

- [ ] **Step 7: Verify the OFF-axes configure on the host (no build)**

```bash
cd src && cmake -S . -B ../build/probe-64 -DROTS_LEGACY_32BIT=OFF -DROTS_BUILD_TESTS=OFF && echo CONFIGURE-OK; rm -rf ../build/probe-64
```

Expected: `CONFIGURE-OK` on the arm64 Mac (no GTest needed, no -m32). This proves the options exist and gate correctly; the *build* is allowed to fail (that's Phase 2's work — do not attempt to fix compile errors from a 64-bit build in this phase).

- [ ] **Step 8: Commit**

```bash
git add src/CMakeLists.txt
git commit -m "build: make 32-bit ABI and tests configurable CMake axes"
```

---

### Task 2: CMakePresets.json

**Files:**
- Create: `src/CMakePresets.json` (must live next to the top-level CMakeLists.txt, which is `src/`)
- Modify: `.gitignore` (add `CMakeUserPresets.json`)

**Interfaces:**
- Consumes: `ROTS_LEGACY_32BIT`, `ROTS_BUILD_TESTS` from Task 1.
- Produces: configure/build/test presets named exactly `linux-x64`, `linux-x86-legacy`, `macos-arm64`, `windows-msvc` — Task 3's CI and the docs in Task 5 invoke them as `cmake --preset <name>` (from `src/`), `cmake --build --preset <name>`, `ctest --preset <name>`.

- [ ] **Step 1: Write src/CMakePresets.json**

```json
{
  "version": 6,
  "cmakeMinimumRequired": { "major": 3, "minor": 23, "patch": 0 },
  "configurePresets": [
    {
      "name": "base",
      "hidden": true,
      "binaryDir": "${sourceDir}/../build/${presetName}",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "CMAKE_EXPORT_COMPILE_COMMANDS": "ON"
      }
    },
    {
      "name": "linux-x64",
      "displayName": "Linux x86-64 (64-bit; red until Phase 2)",
      "inherits": "base",
      "generator": "Unix Makefiles",
      "cacheVariables": { "ROTS_LEGACY_32BIT": "OFF" },
      "condition": { "type": "equals", "lhs": "${hostSystemName}", "rhs": "Linux" }
    },
    {
      "name": "linux-x86-legacy",
      "displayName": "Linux i386 legacy (-m32; the shipping ABI)",
      "inherits": "base",
      "generator": "Unix Makefiles",
      "cacheVariables": {
        "ROTS_LEGACY_32BIT": "ON",
        "ROTS_BUILD_TESTS": "OFF"
      },
      "condition": { "type": "equals", "lhs": "${hostSystemName}", "rhs": "Linux" }
    },
    {
      "name": "macos-arm64",
      "displayName": "macOS arm64 (64-bit; red until Phase 2)",
      "inherits": "base",
      "generator": "Unix Makefiles",
      "cacheVariables": { "ROTS_LEGACY_32BIT": "OFF" },
      "condition": { "type": "equals", "lhs": "${hostSystemName}", "rhs": "Darwin" }
    },
    {
      "name": "windows-msvc",
      "displayName": "Windows MSVC (64-bit; red until Phase 3)",
      "inherits": "base",
      "generator": "Visual Studio 17 2022",
      "architecture": { "value": "x64", "strategy": "set" },
      "cacheVariables": {
        "ROTS_LEGACY_32BIT": "OFF",
        "ROTS_BUILD_TESTS": "OFF"
      },
      "condition": { "type": "equals", "lhs": "${hostSystemName}", "rhs": "Windows" }
    }
  ],
  "buildPresets": [
    { "name": "linux-x64", "configurePreset": "linux-x64" },
    { "name": "linux-x86-legacy", "configurePreset": "linux-x86-legacy" },
    { "name": "macos-arm64", "configurePreset": "macos-arm64" },
    { "name": "windows-msvc", "configurePreset": "windows-msvc" }
  ],
  "testPresets": [
    {
      "name": "linux-x64",
      "configurePreset": "linux-x64",
      "output": { "outputOnFailure": true },
      "execution": { "noTestsAction": "error" }
    },
    {
      "name": "macos-arm64",
      "configurePreset": "macos-arm64",
      "output": { "outputOnFailure": true },
      "execution": { "noTestsAction": "error" }
    }
  ]
}
```

Design notes baked in (do not change without reason): `linux-x86-legacy` sets `ROTS_BUILD_TESTS=OFF` because linking the test binary needs a 32-bit GTest, which no runner/host has — 32-bit test coverage lives in the container path; `windows-msvc` sets it OFF because no GTest is provisioned there yet; `noTestsAction: error` is the ctest-masking guard from the Global Constraints; `condition` blocks make `cmake --list-presets` show only presets that can run on the current host.

- [ ] **Step 2: Verify preset enumeration and a real configure on the host**

```bash
cd src && cmake --list-presets
```

Expected: exactly `macos-arm64` listed (conditions hide the others on Darwin).

```bash
cd src && cmake --preset macos-arm64 -DROTS_BUILD_TESTS=OFF && echo PRESET-CONFIGURE-OK
```

Expected: `PRESET-CONFIGURE-OK` (tests forced off for this probe because GTest may not be installed on the host; the preset itself keeps tests ON for Phase 2). Then attempt the build and CAPTURE the failure as a Phase 2 artifact — do not fix anything:

```bash
cd src && (cmake --build --preset macos-arm64 -j8 2>&1 | tail -30) > ../.superpowers/phase2-seed-macos-build-errors.txt; head -20 ../.superpowers/phase2-seed-macos-build-errors.txt
```

Expected: real 64-bit/macOS porting errors (missing Linux headers like `crypt.h`/`wait.h` includes, `-m32`-free pointer truncation warnings-as-errors absent since `-w`, etc.). Whatever appears, it goes in the report verbatim — it is Phase 2's opening work-list.

- [ ] **Step 3: Add CMakeUserPresets.json to .gitignore**

Append to `.gitignore`:

```gitignore
CMakeUserPresets.json
```

- [ ] **Step 4: Re-run the container battery (presets must be invisible to it)**

```bash
docker compose run --rm rots bash -lc 'cd /rots && make test'
```

Expected: 503/503 — CMake 3.18 ignores `CMakePresets.json` entirely (it only errors on `--preset` invocations, which the container never uses).

- [ ] **Step 5: Commit**

```bash
git add src/CMakePresets.json .gitignore
git commit -m "build: add per-platform CMake presets"
```

---

### Task 3: Rewrite the CI workflow as the four-platform matrix

**Files:**
- Modify: `.github/workflows/ci.yml` (full rewrite of the neutralized import)

**Interfaces:**
- Consumes: presets from Task 2 (`cmake --preset` run from `src/`), the Docker flow (`docker compose`), the src/tests Makefile runner (`/rots/bin/tests`).
- Produces: workflow `CI` with jobs `legacy-32bit` (required), `linux-x64`, `macos-arm64`, `windows-msvc` (all `continue-on-error: true`). Task 4 pushes and verifies; Task 5 documents.

- [ ] **Step 1: Write the new .github/workflows/ci.yml**

```yaml
# Phase 1 CI matrix (modernization plan).
#
# legacy-32bit is the REQUIRED job: it builds and tests the shipping 32-bit
# binary in the same i386 container used locally. The three 64-bit jobs run
# allowed-to-fail (continue-on-error) until their enabling phase lands:
# linux-x64 / macos-arm64 turn green in Phase 2, windows-msvc in Phase 3.
# Their logs are the porting work-list, not noise.
#
# CI deliberately has NO boot smoke and NO account_smoke.py: both need
# lib/world/ (separate repo, git-ignored) and the Rust proxy. Unit tests and
# the committed characterization goldens (src/tests/goldens/) need neither.
name: CI

on:
  push:
    branches: [master, 'modernization/**']
  pull_request:
  workflow_dispatch:

permissions:
  contents: read

concurrency:
  group: ci-${{ github.ref }}
  cancel-in-progress: true

jobs:
  legacy-32bit:
    name: Linux i386 legacy (required)
    runs-on: ubuntu-24.04
    timeout-minutes: 30
    steps:
      - uses: actions/checkout@v4
        with:
          persist-credentials: false
      - name: Build i386 toolchain image
        run: docker compose build
      - name: Build game binary (src Makefile, -m32)
        run: docker compose run --rm rots bash -lc 'cd /rots/src && make setup && make -j"$(nproc)" all'
      - name: Build game + tests via CMake and run ctest
        run: docker compose run --rm rots bash -lc 'cd /rots && make test'
      - name: Run monolithic test binary (masking-proof runner)
        run: |
          docker compose run --rm rots bash -lc '
            cd /rots/src/tests && make -j"$(nproc)" tests &&
            cd /rots/src/tests && /rots/bin/tests 2>&1 | tee /tmp/tests.out
            grep -E "\[  PASSED  \] [1-9][0-9]* tests" /tmp/tests.out'

  linux-x64:
    name: Linux x64 (allowed to fail until Phase 2)
    runs-on: ubuntu-24.04
    continue-on-error: true
    timeout-minutes: 30
    steps:
      - uses: actions/checkout@v4
        with:
          persist-credentials: false
      - name: Install GoogleTest
        run: sudo apt-get update && sudo apt-get install -y libgtest-dev libcrypt-dev
      - name: Configure
        run: cd src && cmake --preset linux-x64
      - name: Build
        run: cd src && cmake --build --preset linux-x64 -j4
      - name: Test
        run: cd src && ctest --preset linux-x64

  macos-arm64:
    name: macOS arm64 (allowed to fail until Phase 2)
    runs-on: macos-14
    continue-on-error: true
    timeout-minutes: 30
    steps:
      - uses: actions/checkout@v4
        with:
          persist-credentials: false
      - name: Install GoogleTest
        run: brew install googletest
      - name: Configure
        run: cd src && cmake --preset macos-arm64
      - name: Build
        run: cd src && cmake --build --preset macos-arm64 -j4
      - name: Test
        run: cd src && ctest --preset macos-arm64

  windows-msvc:
    name: Windows MSVC (allowed to fail until Phase 3)
    runs-on: windows-2022
    continue-on-error: true
    timeout-minutes: 30
    steps:
      - uses: actions/checkout@v4
        with:
          persist-credentials: false
      - name: Configure
        run: cd src; cmake --preset windows-msvc
      - name: Build
        run: cd src; cmake --build --preset windows-msvc
```

Design notes: the required job runs BOTH runners (ctest via root Makefile, monolithic `bin/tests`) — the grep on the gtest `[  PASSED  ]` summary line is the second masking guard (a binary that runs zero tests fails the grep); the 64-bit jobs' failures surface as job-level warnings, not red runs, per the spec's allowed-to-fail requirement; `modernization/**` branch pushes trigger CI so phase branches get checked pre-merge.

- [ ] **Step 2: Static-validate the workflow**

```bash
command -v actionlint >/dev/null && actionlint .github/workflows/ci.yml || python3 -c "import yaml,sys; yaml.safe_load(open('.github/workflows/ci.yml')); print('YAML-OK')"
```

Expected: `actionlint` clean if installed, else `YAML-OK`. (Real validation is Task 4's live run.)

- [ ] **Step 3: Run the legacy job's exact commands locally (pre-flight the required job)**

```bash
docker compose run --rm rots bash -lc 'cd /rots/src && make setup && make -j8 all'
docker compose run --rm rots bash -lc 'cd /rots && make test'
docker compose run --rm rots bash -lc 'cd /rots/src/tests && make -j8 tests && cd /rots/src/tests && /rots/bin/tests 2>&1 | tail -5'
```

Expected: all pass; last command's tail shows the `[  PASSED  ] 495 tests` summary (with 8 skipped). Any deviation here would also break CI — fix before commit.

- [ ] **Step 4: Commit**

```bash
git add .github/workflows/ci.yml
git commit -m "ci: four-platform matrix; 32-bit container job required"
```

---

### Task 4: Push and verify the live CI run

**Files:** none (remote operations).

**Interfaces:**
- Consumes: the workflow from Task 3; the branch this plan executes on (`modernization/phase-1`).
- Produces: a recorded green `legacy-32bit` run URL — the spec's "CI green on Linux 32-bit" exit criterion.

- [ ] **Step 1: Get the user's go-ahead to push (BLOCKING)**

Pushing `modernization/phase-1` to `origin` (github.com/drelidan7/RotS_Live_Modern) is the first push of this effort and publishes the branch. **Ask the user before this step and do not proceed without a yes.** (Controller: surface this as a question, then record the answer in the ledger.)

- [ ] **Step 2: Push the branch**

```bash
git push -u origin modernization/phase-1
```

- [ ] **Step 3: Watch the run**

```bash
gh run list --branch modernization/phase-1 --limit 3
gh run watch --exit-status $(gh run list --branch modernization/phase-1 --workflow CI --limit 1 --json databaseId -q '.[0].databaseId')
```

Expected: run completes; `legacy-32bit` job SUCCESS. The three allowed-to-fail jobs may show failures inside a green run — capture each one's first real error with `gh run view <id> --log-failed | head -80` into `.superpowers/phase2-seed-ci-errors.txt` (linux-x64/macos = Phase 2 seeds; windows = Phase 3 seeds).

- [ ] **Step 4: If legacy-32bit is red, fix and re-push**

Diagnose from `gh run view --log-failed`. Likely culprits: Docker layer differences (apt availability), path assumptions, or the `grep` guard being too strict about gtest's summary formatting. Fix, commit with an imperative subject, push, re-watch. Do not weaken the masking greps to get green.

- [ ] **Step 5: Record the evidence**

Append to `.superpowers/sdd/progress.md` (or the run's ledger): the green run URL, and the captured allowed-to-fail error summaries. Commit nothing for this step.

---

### Task 5: Documentation + Phase 1 exit checklist

**Files:**
- Modify: `docs/BUILD.md` (new "Build matrix" section), `AGENTS.md` (build commands), `CLAUDE.md` (32-bit gotcha)

**Interfaces:**
- Consumes: everything above.
- Produces: the documented Phase 1 exit state the Phase 2 plan builds on.

- [ ] **Step 1: docs/BUILD.md — add a "Build matrix (Phase 1)" section**

Add after the existing container-build instructions (adjust surrounding prose only if it directly contradicts):

```markdown
## Build matrix (Phase 1)

The authoritative build is CMake. Presets live in `src/CMakePresets.json`
(CMake ≥ 3.23 to use presets; the i386 container's CMake 3.18 uses the root
`Makefile` flow instead).

| Preset / path | Platform | Status |
|---|---|---|
| container + root `Makefile` (`make configure/build/test`) | Linux i386 (`-m32`) | **green — the shipping ABI; CI-required** |
| `linux-x86-legacy` | Linux i386 via multilib | builds the game; tests stay in the container path |
| `linux-x64` | Linux x86-64 | red until Phase 2 (64-bit port) |
| `macos-arm64` | macOS arm64 | red until Phase 2 (64-bit port) |
| `windows-msvc` | Windows x64 MSVC | red until Phase 3 (platform layer) |

Per-platform (from `src/`): `cmake --preset <name>`, `cmake --build --preset <name>`,
`ctest --preset <name>`. `cmake --list-presets` shows what runs on this host.

CI (`.github/workflows/ci.yml`): the `legacy-32bit` job is required; the three
64-bit jobs run allowed-to-fail until their enabling phase — their logs are the
porting work-list.
```

- [ ] **Step 2: AGENTS.md — update the build-commands section**

In "Build, Test, and Development Commands", add one line (keep existing lines):

```markdown
- Per-platform CMake presets (host, CMake ≥3.23): from `src/`, `cmake --preset <linux-x64|macos-arm64|windows-msvc|linux-x86-legacy>` then `cmake --build --preset <name>`; 64-bit presets stay red until Phases 2–3 (see docs/BUILD.md "Build matrix").
```

- [ ] **Step 3: CLAUDE.md — amend the 32-bit gotcha**

Replace the first gotcha's first sentence (`**32-bit build is mandatory, so building on this machine (Apple Silicon macOS) requires Docker.**`) with:

```markdown
- **The shipping build is still 32-bit, so building the real binary on this machine (Apple Silicon macOS) requires Docker.** CMake presets for 64-bit platforms exist (`src/CMakePresets.json`) but stay red until Phase 2 of the modernization plan; don't "fix" their build errors ad hoc — they're tracked porting work.
```

(Keep the rest of that bullet unchanged.)

- [ ] **Step 4: Phase 1 exit checklist (run everything once)**

```bash
docker compose run --rm rots bash -lc 'cd /rots && make test'                    # 503/503
docker compose run --rm rots bash -lc 'cd /rots/src/tests && make tests && /rots/bin/tests'  # 495/8/0
scripts/boot-golden.sh verify                                                    # exit 0
cd src && cmake --list-presets                                                   # macos-arm64 on this host
```

Plus (from Task 4): the recorded green `legacy-32bit` CI run URL. This matches the spec's Phase 1 exit: "one `cmake --preset` command per platform; CI green on Linux 32-bit and running (even if red) elsewhere."

- [ ] **Step 5: Commit**

```bash
git add docs/BUILD.md AGENTS.md CLAUDE.md
git commit -m "docs: build matrix, presets, and CI status for Phase 1"
```

---

## Plan Self-Review Notes

- **Spec coverage:** "CMake becomes the single authoritative build" → Tasks 1–2 (the src/tests Makefile and root Makefile remain as the container's driver, per the spec's own carve-out: "the Makefile survives only as long as the 32-bit legacy guard needs it"). "CMakePresets.json presets: linux-x64, linux-x86-legacy, macos-arm64, windows-msvc" → Task 2, names verbatim. "GitHub Actions matrix ... allowed-to-fail entries until their enabling phase lands" → Tasks 3–4. "Warnings remain suppressed" → unchanged (`-w` / `/w` in Task 1). Exit criteria → Task 5 checklist.
- **Deviation from spec, justified:** the spec's `linux-x86-legacy` preset cannot serve the container (CMake 3.18 predates presets); the preset exists for multilib hosts, while the container keeps the root-Makefile flow. Documented in Task 2 design notes and Task 5's matrix table.
- **Known adaptation points** (resolve during the step, never defer): Task 1's flag-parity grep patterns (Step 1/5) may need hand-adjustment to catch the exact compile/link lines; Task 2 Step 2's captured macOS build errors are whatever they are (they're an artifact, not a gate); Task 3's gtest-summary grep must match the binary's actual `[  PASSED  ]` line format (verify in Step 3's local pre-flight).
- **Type consistency:** option names `ROTS_LEGACY_32BIT` / `ROTS_BUILD_TESTS` identical across Tasks 1/2/3; preset names identical across Tasks 2/3/5; job name `legacy-32bit` identical across Tasks 3/4/5.
