# Library Skeleton — Build-Flags Interface + Foundation Library — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stand up the first two pieces of the library architecture — a shared `rots_build_flags` INTERFACE library and a `rots_platform` STATIC library (the one already-acyclic layer) — with zero behavior change to `ageland` and byte-identical `ageland_tests`.

**Architecture:** Factor the compile/link flags currently duplicated inline on the `ageland` and `ageland_tests` targets into a single `rots_build_flags` INTERFACE target that every future library and executable links. Then move the 8 clean-leaf foundation translation units into a `rots_platform` STATIC library (`RotS::platform`) that consumes those flags and that `ageland` links. `ageland_tests` keeps compiling every source itself (with `TESTING` defined) so its behavior is unchanged. No source files move on disk; the `src/Makefile` build is untouched.

**Tech Stack:** CMake ≥3.18 (target-based), C++20, GoogleTest (test-only), the repo's CMakePresets (`macos-arm64`, `macos-arm64-asan`, `linux-x64` via the `rots64` container, `linux-x86-legacy`, `windows-msvc`).

## Global Constraints

- **CMake is the single source of truth for library structure** (spec §9). The `src/Makefile` is *not* modified in this plan; it keeps building a flat `ageland` because no files move.
- **No behavior change.** All characterization goldens (`CharacterizationCombatTest.*`, `CharacterizationJson.*`, `scripts/boot-golden.sh verify`) stay byte-for-byte green. This plan changes only build topology.
- **ABI/behavior flag parity is mandatory.** Every first-party target must compile and link with the identical parity flag set: `-m32` (when `ROTS_LEGACY_32BIT`), `-funsigned-char`, `-fstrict-aliasing`, the FP-determinism set (`-msse2 -mfpmath=sse` on x86, `-ffp-contract=off` everywhere on GNU-family, `/fp:precise` on MSVC), `-D_FILE_OFFSET_BITS=64` (non-MSVC), and the MSVC `/J /Zc:strictStrings-` + `/wd` ledger. Copy the exact values from `src/CMakeLists.txt` — do not re-derive them.
- **Warnings are errors:** GNU-family `-Wall -Wextra -Werror`, MSVC `/W4 /WX`. `ROTS_SUPPRESS_TEST_WARNINGS` stays OFF.
- **`rots_platform` membership is exactly these 8 TUs** (spec §3, clean leaves, no upward symbol references): `rots_net.cpp, rots_crypt.cpp, rots_rng.cpp, clock.cpp, crashsave_schedule.cpp, json_utils.cpp, safe_template.cpp, player_file_finalize.cpp`. **`signals.cpp` is NOT included** — it calls up into game/session state and stays in the server sources.
- **Verification cadence (AGENTS.local.md):** per task, build + test native `macos-arm64` and `linux-x64` (`rots64`), plus boot goldens. Do NOT run the i386 battery per task — defer to finalization. Any task adding/rewriting a test file also runs under `macos-arm64-asan`.

---

## File Structure

- `src/CMakeLists.txt` — the only file modified. Gains: (1) a `rots_build_flags` INTERFACE library holding the shared parity flags; (2) `ageland` and `ageland_tests` re-expressed to consume it; (3) a `rots_platform` STATIC library + `RotS::platform` alias; (4) a source-list split (`ROTS_PLATFORM_SOURCES` extracted from `ROTS_SERVER_SOURCES`); (5) an `nm`-based acyclicity tripwire test on GNU-family builds.
- `src/tests/platform_layer_acyclicity_test.sh` — created: a portable-ish shell tripwire asserting `librots_platform.a` imports no game-layer symbols.
- No `.cpp`/`.h` files move or change. `src/Makefile` is not touched.

---

## Task 1: Introduce `rots_build_flags` INTERFACE library (no behavior change)

Factor **`ageland`'s exact current flag set** into one INTERFACE library that `ageland` — and, from Task 2 on, every `rots_*` library — consumes, so the shipping build's flags live in one place instead of being copy-pasted per target.

> **Critical:** `ageland` and `ageland_tests` do **not** share an identical flag set today — `ageland` has `-g`, `-fstrict-aliasing`, per-config `-O0 -DDEBUG`/`-O2 -DNDEBUG`, and MSVC `/J`, which the test target lacks; the test target has `TESTING`, the `-idirafter` include handling, and `GTest`. So this interface encodes **only `ageland`'s flags**, and **`ageland_tests` is left completely untouched** (it keeps its own inline flags). Unifying the two targets' flags is explicitly out of scope — it would change the test build.

**Files:**
- Modify: `src/CMakeLists.txt` (the `target_compile_options`/`target_compile_definitions`/`target_link_options` blocks for `ageland` at lines ~183-221 only). Do NOT touch the `ageland_tests` blocks.

**Interfaces:**
- Produces: an INTERFACE target `rots_build_flags` carrying, as INTERFACE usage requirements, `ageland`'s exact compile options, compile definitions, `cxx_std_20` feature, and `-m32` link option. Consumers get them by `target_link_libraries(<t> PRIVATE rots_build_flags)` (or `PUBLIC` for a library that must propagate them).
- Consumes: the existing `ROTS_GNULIKE`, `ROTS_X86`, `ROTS_FP_OPTIONS`, `ROTS_LEGACY_32BIT` definitions (already set earlier in the file — leave them where they are).

- [ ] **Step 1: Capture the current compile flags for a representative TU (the "before" baseline)**

Configure with the native preset and record the exact compile command for one server source that will end up in the platform library, so Task 2 can prove nothing changed.

Run:
```bash
cd src
cmake --preset macos-arm64 -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
python3 - <<'PY'
import json
cc = json.load(open("../build/macos-arm64/compile_commands.json"))
for e in cc:
    if e["file"].endswith(("json_utils.cpp","fight.cpp")):
        print(e["file"].split("/")[-1], "::", e["command"])
PY
```
Expected: two lines printed (one for `json_utils.cpp`, one for `fight.cpp`) showing the full flag list. Save this output — it is the baseline to diff against in Step 4 and in Task 2.

- [ ] **Step 2: Add the `rots_build_flags` INTERFACE library**

Insert this block in `src/CMakeLists.txt` immediately after the `ROTS_FP_OPTIONS` definition (currently ends at line ~182, just before `target_compile_options(ageland ...)`):

```cmake
# --- Shared build-flags interface --------------------------------------------
# The shipping/game flag set (ageland's exact flags), in one place. ageland and
# every rots_* library (from Task 2 on) inherit it via
#   target_link_libraries(<target> PRIVATE rots_build_flags)   # PUBLIC for libs
# so the -m32 ABI / FP-determinism / unsigned-char behavior contracts hold
# identically across the whole shipping link. INTERFACE (not PUBLIC/PRIVATE)
# because the library has no sources of its own — it only imposes requirements.
# NOTE: ageland_tests deliberately does NOT consume this — its flag set differs
# (TESTING, -idirafter include handling, no -g/-fstrict-aliasing/per-config-opt)
# and is left exactly as-is. This block must be a verbatim copy of ageland's
# current options/definitions/link-options so ageland is provably unchanged.
add_library(rots_build_flags INTERFACE)
target_compile_features(rots_build_flags INTERFACE cxx_std_20)
# ISO dialect, not GNU (matches the Makefile's -std=c++20). Applied to every
# consumer because CXX_EXTENSIONS is a target property, not an INTERFACE one; a
# consuming target must also set it (see the ageland/ageland_tests setters,
# left in place below).
target_compile_options(rots_build_flags INTERFACE
    $<$<BOOL:${ROTS_LEGACY_32BIT}>:-m32>
    $<${ROTS_GNULIKE}:-funsigned-char -fstrict-aliasing -Wall -Wextra -Werror -g>
    $<$<CXX_COMPILER_ID:MSVC>:/J /W4 /WX /wd4244 /wd4267 /wd4456 /wd4458 /wd4459 /wd4702 /wd4127 /wd4310 /Zc:strictStrings->
    ${ROTS_FP_OPTIONS}
    $<${ROTS_GNULIKE}:$<$<CONFIG:Debug>:-O0 -DDEBUG>>
    $<${ROTS_GNULIKE}:$<$<CONFIG:Release>:-O2 -DNDEBUG>>
)
target_compile_definitions(rots_build_flags INTERFACE
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:_FILE_OFFSET_BITS=64>
    $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS>
)
target_link_options(rots_build_flags INTERFACE
    $<$<BOOL:${ROTS_LEGACY_32BIT}>:-m32>
)
```

- [ ] **Step 3: Route `ageland` through `rots_build_flags`, removing the now-duplicated inline flags**

In `src/CMakeLists.txt`, DELETE the `ageland` `target_compile_options` block (the parity one at ~183-188), its `target_compile_definitions` block (~212-215), the per-config optimization block (~217-221), and the `-m32` entry inside `target_link_options` (~189-193, keep the `-rdynamic` line). Then link the interface. The result for `ageland` should read:

```cmake
add_executable(ageland ${ROTS_SERVER_SOURCES})
set_target_properties(ageland PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(ageland PRIVATE
    rots_build_flags
    $<$<PLATFORM_ID:Windows>:ws2_32>
)
target_link_options(ageland PRIVATE
    # -rdynamic: GNU spelling; the clang driver translates it on macOS. MSVC: none.
    $<${ROTS_GNULIKE}:-rdynamic>
)
```
(The `target_compile_features(ageland PRIVATE cxx_std_20)` line is now redundant with the interface but harmless — remove it for cleanliness.) **Do not modify the `ageland_tests` target in this task.**

- [ ] **Step 4: Reconfigure and diff the compile command against the baseline**

Prove BOTH that `ageland`'s TUs are unchanged AND that `ageland_tests`'s TUs are unchanged (the latter because Task 1 must not touch the test target at all).

Run:
```bash
cd src
cmake --preset macos-arm64 -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
python3 - <<'PY'
import json
cc = json.load(open("../build/macos-arm64/compile_commands.json"))
# fight.cpp / json_utils.cpp are each compiled into BOTH ageland and
# ageland_tests, so expect two entries per file (one per target). The `output`
# path distinguishes them (…/ageland.dir/… vs …/ageland_tests.dir/…).
for e in cc:
    if e["file"].split("/")[-1] in ("fight.cpp","json_utils.cpp"):
        print(e.get("output",""), "::", e["command"])
PY
```
Expected: the flag set for each is identical to the Step 1 baseline — `ageland`'s copy still has `-fstrict-aliasing -g` and config `-O0 -DDEBUG`; the test copy still has `TESTING` and the `-idirafter` include path and still LACKS `-g`/`-fstrict-aliasing` (unchanged). Ordering may differ but the *set* must match the baseline for each. If any flag changed, fix the interface (it must be a verbatim copy of ageland's flags) before continuing.

- [ ] **Step 5: Build and run the full test suite on native macOS**

Run:
```bash
cd src
cmake --build --preset macos-arm64 -j4
ctest --preset macos-arm64
```
Expected: build succeeds with no warnings; ctest reports all tests passing (skips as normal for the platform, ~71 on macOS). Characterization suites `CharacterizationCombatTest.*` and `CharacterizationJson.*` pass.

- [ ] **Step 6: Boot-golden the native binary**

Run:
```bash
cd ..
scripts/boot-golden.sh --native build/macos-arm64/ageland verify
```
Expected: boot golden verifies (PASS). This confirms the reflagged `ageland` boots and behaves identically.

- [ ] **Step 7: Commit**

```bash
git add src/CMakeLists.txt
git commit -m "build: factor ageland's flags into rots_build_flags INTERFACE lib"
```

---

## Task 2: Extract the `rots_platform` STATIC library

Move the 8 clean-leaf foundation TUs into `RotS::platform`, linked by `ageland`. Keep them compiled directly into `ageland_tests` (with `TESTING`) so test behavior is byte-identical.

**Files:**
- Modify: `src/CMakeLists.txt` (the `ROTS_SERVER_SOURCES` list ~63-79; the `ageland` target; the `ageland_tests` `add_executable`)

**Interfaces:**
- Consumes: `rots_build_flags` (Task 1).
- Produces: static library target `rots_platform` and alias `RotS::platform`, containing exactly the 8 foundation TUs; a `ROTS_PLATFORM_SOURCES` CMake variable listing them.

- [ ] **Step 1: Split the source list**

In `src/CMakeLists.txt`, add a `ROTS_PLATFORM_SOURCES` variable just before `set(ROTS_SERVER_SOURCES ...)` and REMOVE those 8 filenames from `ROTS_SERVER_SOURCES`:

```cmake
# Foundation layer (spec §3, library architecture): clean-leaf TUs with zero
# upward symbol references. Extracted into the rots_platform STATIC library.
# NOTE: signals.cpp is deliberately NOT here — it references descriptor_list /
# calls hupsig()/unrestrict_game(), i.e. it depends UP on the game, so it stays
# in ROTS_SERVER_SOURCES (app layer).
set(ROTS_PLATFORM_SOURCES
    rots_net.cpp rots_crypt.cpp rots_rng.cpp clock.cpp
    crashsave_schedule.cpp json_utils.cpp safe_template.cpp player_file_finalize.cpp
)
```
Then delete `rots_net.cpp`, `rots_crypt.cpp`, `rots_rng.cpp`, `clock.cpp`, `crashsave_schedule.cpp`, `json_utils.cpp`, `safe_template.cpp`, `player_file_finalize.cpp` from the `ROTS_SERVER_SOURCES` list. (`signals.cpp` stays in `ROTS_SERVER_SOURCES`.)

- [ ] **Step 2: Define the `rots_platform` library and link it into `ageland`**

Add, immediately after the `rots_build_flags` block from Task 1:

```cmake
# --- L0: foundation library --------------------------------------------------
add_library(rots_platform STATIC ${ROTS_PLATFORM_SOURCES})
add_library(RotS::platform ALIAS rots_platform)
set_target_properties(rots_platform PROPERTIES CXX_EXTENSIONS OFF)
# PUBLIC: rots_platform needs the parity flags to BUILD, and every consumer must
# link with the same -m32/FP set — so the requirement propagates.
target_link_libraries(rots_platform PUBLIC rots_build_flags $<$<PLATFORM_ID:Windows>:ws2_32>)
# tests/product .cpp reach these headers via bare quoted includes ("rots_net.h");
# the sources live in src/, so the compiler's own "directory of the including
# file" entry already resolves them for TUs in src/. No extra include dir needed
# while the headers stay in src/ (they relocate in a later plan).
```
Then link it into `ageland` (add `RotS::platform` to `ageland`'s `target_link_libraries`):

```cmake
target_link_libraries(ageland PRIVATE
    RotS::platform
    rots_build_flags
    $<$<PLATFORM_ID:Windows>:ws2_32>
)
```

- [ ] **Step 3: Keep `ageland_tests` compiling the platform sources directly**

The test binary must still compile the 8 sources itself (under `TESTING`), NOT link `rots_platform` (which is built without `TESTING`) — that keeps test behavior byte-identical and avoids duplicate symbols. Change its `add_executable` to add the platform sources back explicitly:

```cmake
    add_executable(ageland_tests ${ROTS_SERVER_SOURCES} ${ROTS_PLATFORM_SOURCES} ${ROTS_TEST_SOURCES})
```
Do NOT add `RotS::platform` to `ageland_tests`'s link libraries.

- [ ] **Step 4: Configure and confirm the library is built and linked**

Run:
```bash
cd src
cmake --preset macos-arm64
cmake --build --preset macos-arm64 -j4
ls -l ../build/macos-arm64/librots_platform.a
```
Expected: `librots_platform.a` exists; `ageland` and `ageland_tests` both link and build with no duplicate-symbol errors and no warnings.

- [ ] **Step 5: Run the full test suite and boot golden**

Run:
```bash
ctest --preset macos-arm64
cd ..
scripts/boot-golden.sh --native build/macos-arm64/ageland verify
```
Expected: all tests pass (same counts as Task 1 Step 6); boot golden verifies. Behavior is unchanged.

- [ ] **Step 6: Verify the Linux x64 container build (second required host)**

Run:
```bash
docker compose run --rm --pull never rots64 bash -lc 'cd /rots/src && cmake --preset linux-x64 && cmake --build --preset linux-x64 -j"$(nproc)" && ctest --preset linux-x64'
scripts/boot-golden.sh --service rots64 verify
```
Expected: configure/build/ctest all pass on `linux-x64`; `librots_platform.a` builds there too; boot golden verifies.

- [ ] **Step 7: Commit**

```bash
git add src/CMakeLists.txt
git commit -m "build: extract rots_platform STATIC library (foundation layer, 8 leaf TUs)"
```

---

## Task 3: Add the foundation-layer acyclicity tripwire

Guard the "no upward edges" property so a future change that makes a platform TU call up into the game fails a fast, dedicated check instead of silently re-welding the layer.

**Files:**
- Create: `src/tests/platform_layer_acyclicity_test.sh`
- Modify: `src/CMakeLists.txt` (register the test via `add_test`, GNU-family only)

**Interfaces:**
- Consumes: the built `librots_platform.a` from Task 2.
- Produces: a CTest test named `PlatformLayerAcyclicity`.

- [ ] **Step 1: Write the tripwire script (the failing-condition check)**

Create `src/tests/platform_layer_acyclicity_test.sh`:

```bash
#!/usr/bin/env bash
# Fails if the rots_platform foundation library imports any game-layer symbol —
# i.e. if a "clean leaf" TU gained an upward dependency on the game. Uses the
# archive's UNDEFINED (imported) symbol list; a pure leaf imports only libc /
# libstdc++ / build-provided symbols, never game entities like char_to_room.
set -euo pipefail
archive="${1:?usage: platform_layer_acyclicity_test.sh <path/to/librots_platform.a>}"

# nm -u lists undefined symbols; -C demangles C++ names so the denylist can be
# plain source identifiers. macOS nm lacks -C, so fall back to c++filt.
if nm -uC "$archive" >/dev/null 2>&1; then
    undefined="$(nm -uC "$archive")"
else
    undefined="$(nm -u "$archive" | c++filt)"
fi

# Game-layer symbols the foundation must NEVER reference. Representative, not
# exhaustive: any real upward edge pulls in at least one of these hubs.
denylist='char_to_room|obj_to_char|char_from_room|descriptor_list|interpret_command|world\[|boot_db|game_loop'

if echo "$undefined" | grep -Eq "$denylist"; then
    echo "FAIL: rots_platform imports a game-layer symbol (upward edge):" >&2
    echo "$undefined" | grep -E "$denylist" >&2
    exit 1
fi
echo "PASS: rots_platform imports no game-layer symbols"
```
Make it executable:
```bash
chmod +x src/tests/platform_layer_acyclicity_test.sh
```

- [ ] **Step 2: Register the test in CMake (GNU-family only)**

In `src/CMakeLists.txt`, inside the `if(ROTS_BUILD_TESTS)` block after `gtest_discover_tests(...)`, add:

```cmake
    # Foundation-layer acyclicity tripwire (spec §11). GNU-family only: relies on
    # nm/c++filt and a .a archive layout. MSVC's guarantee is structural instead
    # — rots_platform links only rots_build_flags, no game library.
    if(NOT MSVC)
        add_test(
            NAME PlatformLayerAcyclicity
            COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tests/platform_layer_acyclicity_test.sh"
                    "$<TARGET_FILE:rots_platform>"
        )
    endif()
```

- [ ] **Step 3: Run the tripwire directly to confirm it PASSES on the current pure library**

Run:
```bash
cd src
cmake --preset macos-arm64 && cmake --build --preset macos-arm64 -j4
tests/platform_layer_acyclicity_test.sh ../build/macos-arm64/librots_platform.a
```
Expected: `PASS: rots_platform imports no game-layer symbols`.

- [ ] **Step 4: Prove the tripwire FAILS on an upward edge (negative test)**

Temporarily append a game-symbol reference to a platform TU and confirm the check catches it:
```bash
cd src
printf '\nextern void char_to_room(void*, int);\nvoid* rots_platform_tripwire_probe() { return (void*)&char_to_room; }\n' >> clock.cpp
cmake --build --preset macos-arm64 -j4 2>/dev/null || true
tests/platform_layer_acyclicity_test.sh ../build/macos-arm64/librots_platform.a; echo "exit=$?"
git checkout -- clock.cpp
cmake --build --preset macos-arm64 -j4
```
Expected: the middle run prints `FAIL: rots_platform imports a game-layer symbol` and `exit=1`; after `git checkout`, the rebuild restores the clean library. (If the build cache doesn't refresh the archive, `touch clock.cpp` before rebuilding.)

- [ ] **Step 5: Run the tripwire through CTest**

Run:
```bash
cd src
ctest --preset macos-arm64 -R PlatformLayerAcyclicity -V
```
Expected: `PlatformLayerAcyclicity` runs and passes.

- [ ] **Step 6: Sanitizer gate for the new test (AGENTS.local.md new-test rule)**

The new test is a shell script, not a compiled TU, so ASan instrumentation does not apply to it — but run the ASan preset once to confirm the library split itself is clean under instrumentation:
```bash
cd src
cmake --preset macos-arm64-asan
cmake --build --preset macos-arm64-asan -j4
ctest --preset macos-arm64-asan
```
Expected: build + all tests (including `PlatformLayerAcyclicity`) pass with no ASan/UBSan reports.

- [ ] **Step 7: Commit**

```bash
git add src/tests/platform_layer_acyclicity_test.sh src/CMakeLists.txt
git commit -m "test: add rots_platform foundation-layer acyclicity tripwire"
```

---

## Finalization (not per-task — run once before merge)

- [ ] Run the i386 battery per AGENTS.local.md (sequential): `docker compose run --rm --pull never rots bash -lc 'cd /rots && make test'`, then the monolithic runner from `/rots/src/tests`, then `scripts/boot-golden.sh verify`. Confirm the CMake `linux-x86-legacy` preset still builds `ageland` + `librots_platform.a` and passes ctest.
- [ ] Confirm the untouched `src/Makefile` still builds a flat `ageland` (files did not move): `docker compose run --rm --pull never rots bash -lc 'cd /rots/src && make clean && make all'`.
- [ ] Windows MSVC path is configure/build/ctest only (no boot check); confirm the `windows-msvc` preset still configures and builds with `rots_platform` present and `PlatformLayerAcyclicity` correctly skipped (MSVC branch).

---

## Self-Review Notes

- **Spec coverage:** implements spec §10 step 1 (build-flags interface + foundation library) and seeds §11 (acyclicity enforcement) for L0. It does NOT attempt the god-header split (§5), `db.cpp`/`rots_convert` (§4), or the middle libraries — those are later plans, correctly out of scope.
- **No files move; Makefile untouched** — satisfies the Global Constraint and keeps the change purely additive on the CMake side.
- **`signals.cpp` correctly excluded** from `rots_platform` per the spec correction.
- **`ageland_tests` behavior preserved** by compiling platform sources directly (with `TESTING`) rather than linking the library.
