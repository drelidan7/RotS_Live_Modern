# Phase 1 — FP Pipeline Unification Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make floating-point bit-identical across i386/x64/arm64/MSVC and identical between the shipping `ageland` and `ageland_tests` builds — via build flags, a compile-time guard, a smoke test, and docs. No source math, struct, or persistence changes. (Spec: `docs/superpowers/specs/2026-07-15-phase1-fp-unification-design.md`.)

**Architecture:** Hoist one shared deterministic-FP flag set (`-msse2 -mfpmath=sse` on GNU x86, `-ffp-contract=off` on all GNU targets, `/fp:precise` on MSVC) and apply it to both shipping and test targets in both build systems (CMake + Makefiles). Add `src/fp_policy.h` whose `static_assert` fails the shipping build if x87/fast-math regress, and `fp_determinism_smoke_tests.cpp` as a runtime tripwire. Prove the combat golden still passes across the matrix (expected: no regen).

**Tech Stack:** CMake presets (`macos-arm64`, `linux-x64`, `windows-msvc`, `macos-arm64-asan`), the i386 `rots` / 64-bit `rots64` containers, GNU Make (`src/Makefile`, `src/tests/Makefile`), GoogleTest.

## Global Constraints

- Branch `feat/fp-unification` (already created; spec committed as `ab8e6b7`). Never commit to master. Merge is the owner's decision.
- **NEVER use Edit/Write on EXISTING `.cpp`/`.h` files** — a PostToolUse clang-format hook rewrites edited C++ files and buries the change under pre-existing drift. Add `#include` lines to the existing combat TUs (`fight.cpp`, `utility.cpp`, `profs.cpp`, `limits.cpp`) via **Python byte-edits (binary/latin-1) through Bash**. BRAND-NEW files (`src/fp_policy.h`, `src/tests/fp_determinism_smoke_tests.cpp`) may use the Write tool. Build files (`CMakeLists.txt`, `src/Makefile`, `src/tests/Makefile`) and docs (`.md`) are NOT C++ and may use Edit. Never run clang-format / `make format`.
- `-Wall -Wextra -Werror` (GNU) / `/W4 /WX` (MSVC) must stay clean.
- **Do NOT run `UPDATE_GOLDENS`.** Phase 1 must not move `combat_transcript_seed42.txt`. If the golden fails after a flag change, that is a finding to investigate and report — never regenerate it.
- The deterministic FP subset is `+ − × ÷ sqrt`, `double`-only. Do not introduce `long double`, `-ffast-math`, or any libm transcendental in this phase.
- Verification cadence (AGENTS.local.md): each task builds + full ctest on `macos-arm64`. Flag/format-affecting tasks also run `rots64` (linux-x64). The new-test task runs the `macos-arm64-asan` gate. The i386 legacy battery (which exercises the `src/Makefile` x87→SSE flip on real i386) runs at finalization — owner-triggered.
- Commit messages: imperative subject ≤72 chars; body ends with
  `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`
  and the `Claude-Session:` line (see Bash tool git instructions).

## Key file locations (verified at plan time)

- `src/Makefile:36` — `REQ_CXXFLAGS = -m32 -std=c++20 -Wall -Wextra -Werror -fstrict-aliasing -funsigned-char -g -D_FILE_OFFSET_BITS=64` (shipping i386; **no FP flags today** → x87).
- `src/tests/Makefile:26` — `CXXFLAGS = ... -msse2 -mfpmath=sse ...` (tests already SSE; needs `-ffp-contract=off`).
- `src/CMakeLists.txt:162` — `target_compile_options(ageland PRIVATE ...)` (shipping target; no FP flags today).
- `src/CMakeLists.txt:~442` — `target_compile_options(ageland_tests PRIVATE $<$<AND:${ROTS_GNULIKE},${ROTS_X86}>:-msse2 -mfpmath=sse>)` (test-only SSE block; `ROTS_GNULIKE`/`ROTS_X86` vars defined ~151-160).
- `src/CMakeLists.txt:243/254` — test sources list (where `cpp20_format_smoke_tests.cpp`, `llp64_probe_tests.cpp` are registered).
- `src/tests/Makefile:286` — `SRCS = ...` (test sources; add the new smoke test here).
- Combat TU include heads: `fight.cpp:11`, `utility.cpp:23`, `profs.cpp:3` (all begin `#include "platdef.h"`); `limits.cpp:11` begins `#include "limits.h"`.
- `docs/BUILD.md` — has `## Notes` (~115) and build-matrix sections; add an FP-determinism subsection.

---

### Task 1: Green baseline on the branch

**Files:** none (verification only).

- [ ] **Step 1:** `cd src && cmake --preset macos-arm64 && cmake --build --preset macos-arm64 -j4 && ctest --preset macos-arm64`
Expected: `100% tests passed` (~1242 tests), including `CharacterizationCombat*`. If red, STOP and report — do not proceed.

No commit.

---

### Task 2: Unify the deterministic-FP flag set across all build systems

**Files:**
- Modify: `src/CMakeLists.txt` (apply shared FP flags to `ageland` at :162 and `ageland_tests`; remove the standalone test-only SSE block ~442)
- Modify: `src/Makefile:36` (add FP flags to `REQ_CXXFLAGS`)
- Modify: `src/tests/Makefile:26` (add `-ffp-contract=off`)

**Interfaces:**
- Produces: identical deterministic-FP compilation for `ageland` and `ageland_tests` in both build systems. GNU x86 gets `-msse2 -mfpmath=sse -ffp-contract=off`; GNU arm64 gets `-ffp-contract=off` only; MSVC gets `/fp:precise`.

- [ ] **Step 1 (CMake):** Add a single source of truth near the `ROTS_GNULIKE`/`ROTS_X86` definitions (`src/CMakeLists.txt` ~160), e.g. a CMake `list`/variable `ROTS_FP_OPTIONS` expanding to:
  `$<$<AND:${ROTS_GNULIKE},${ROTS_X86}>:-msse2 -mfpmath=sse>` `$<${ROTS_GNULIKE}:-ffp-contract=off>` `$<$<CXX_COMPILER_ID:MSVC>:/fp:precise>`.
  Apply it via `target_compile_options(ageland PRIVATE ${ROTS_FP_OPTIONS})` and `target_compile_options(ageland_tests PRIVATE ${ROTS_FP_OPTIONS})`. Delete the now-duplicate test-only `-msse2 -mfpmath=sse` block (~442), leaving its explanatory comment folded into the shared definition. (Edit tool is fine — CMakeLists.txt is not a C++ file.)
- [ ] **Step 2 (Makefiles):** In `src/Makefile:36`, append `-msse2 -mfpmath=sse -ffp-contract=off` to `REQ_CXXFLAGS`. In `src/tests/Makefile:26`, add `-ffp-contract=off` to `CXXFLAGS` (it already has `-msse2 -mfpmath=sse`). Add a one-line comment in each pointing to the shared FP-determinism policy.
- [ ] **Step 3 (build + test, macos-arm64):** `cd src && cmake --preset macos-arm64 && cmake --build --preset macos-arm64 -j4 && ctest --preset macos-arm64`.
  Expected: clean build (`-ffp-contract=off` is valid on arm64; `-msse2` is correctly gated out), `100% tests passed`, `CharacterizationCombat*` PASS with **no golden change**. Confirm `git status` shows no modification to `src/tests/goldens/combat_transcript_seed42.txt`.
- [ ] **Step 4 (build + test, rots64 / linux-x64):**
  `docker compose run --rm --pull never rots64 bash -lc 'cd /rots/src && cmake --preset linux-x64 && cmake --build --preset linux-x64 -j"$(nproc)" && ctest --preset linux-x64'`.
  Expected: `100% tests passed`, combat golden green.
- [ ] **Step 5 (commit):**
```
build: unify deterministic FP flags across shipping and test

Hoist one -msse2/-mfpmath=sse (GNU x86) + -ffp-contract=off (all GNU) +
/fp:precise (MSVC) set and apply it to both the ageland and
ageland_tests targets and to src/Makefile / src/tests/Makefile, so the
shipping and test binaries compute floating point identically. This
flips the i386 shipping build from x87 to SSE, aligning it with the
behavior the goldens have always described. No source math changes.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01JC3iKasKTrJEbvQPZy5Nsv
```

---

### Task 3: Compile-time guard header (`src/fp_policy.h`) + include in combat TUs

**Files:**
- Create: `src/fp_policy.h` (Write tool OK — new file)
- Modify: `src/fight.cpp:11`, `src/utility.cpp:23`, `src/profs.cpp:3`, `src/limits.cpp:11` (add the include — Python byte-edits only)

**Interfaces:**
- Produces: any TU including `fp_policy.h` fails to compile under x87 (`FLT_EVAL_METHOD > 0`) or `-ffast-math`. Because these TUs are in `ageland`, a regression fails the shipping build.

- [ ] **Step 1:** Write `src/fp_policy.h`:
```cpp
#ifndef FP_POLICY_H
#define FP_POLICY_H

// Guards the cross-platform floating-point determinism contract (Phase 1;
// see docs/BUILD.md "FP determinism"). Included by the combat/stat/HP
// translation units so a regression fails the SHIPPING build, not just the
// tests. The deterministic subset is + - * / sqrt, double-only.
#include <cfloat>

// FLT_EVAL_METHOD == 0 means IEEE double evaluation (SSE / ARM). 2 == x87
// 80-bit extended precision, 1 == double-extended -- both round intermediates
// differently across platforms and are banned. -1 == indeterminate: cannot
// check at compile time, the fp_determinism smoke test backstops it.
#if defined(FLT_EVAL_METHOD) && FLT_EVAL_METHOD > 0
#error "Non-IEEE float evaluation (x87/extended precision) breaks cross-platform \
determinism. Build with -msse2 -mfpmath=sse (GNU x86). See docs/BUILD.md."
#endif

#ifdef __FAST_MATH__
#error "-ffast-math / -Ofast breaks cross-platform FP determinism. See docs/BUILD.md."
#endif

#endif // FP_POLICY_H
```
- [ ] **Step 2:** Via Python byte-edit, add `#include "fp_policy.h"` to each of `src/fight.cpp`, `src/utility.cpp`, `src/profs.cpp`, `src/limits.cpp`, immediately after their existing first `#include "..."` project header (fight/utility/profs after `"platdef.h"`; limits after `"limits.h"`). Preserve each file's line endings (check `file <path>` first — several are LF; some game files are mixed).
- [ ] **Step 3:** `grep -c '#include "fp_policy.h"' src/fight.cpp src/utility.cpp src/profs.cpp src/limits.cpp` → 1 each. `git diff --stat` shows ~1 insertion per file (no formatter reflow — if hundreds of lines changed, revert and redo via Python).
- [ ] **Step 4 (build + test):** `cd src && cmake --build --preset macos-arm64 -j4 && ctest --preset macos-arm64`.
  Expected: clean build (on arm64 `FLT_EVAL_METHOD==0`, guard passes), `100% tests passed`. Then confirm the guard actually bites: temporarily compile one TU with `-mfpmath=387` on a linux-x64 scratch build (or note it as a rots64 check in Task 6) to see the `#error` fire — document the check, then revert. (If a scratch x87 compile isn't convenient on macos-arm64, defer the positive-fire check to Task 6's rots64/i386 leg and note it here.)
- [ ] **Step 5 (commit):** `perf: guard FP determinism in combat TUs via fp_policy.h` (+ footer).

---

### Task 4: Runtime smoke test (`fp_determinism_smoke_tests.cpp`)

**Files:**
- Create: `src/tests/fp_determinism_smoke_tests.cpp` (Write tool OK)
- Modify: `src/CMakeLists.txt` (register in the test sources list near :243) and `src/tests/Makefile:286` (add to `SRCS`) — Edit tool OK (not C++... they are build files; Edit is fine)

**Interfaces:**
- Consumes: nothing. Produces: `FpDeterminismSmoke.*` gtest cases.

- [ ] **Step 1:** Write `src/tests/fp_determinism_smoke_tests.cpp` mirroring `cpp20_format_smoke_tests.cpp`'s "permanent tripwire" style:
```cpp
#include <bit>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <gtest/gtest.h>

// Permanent toolchain tripwire (Phase 1: FP determinism). The game's
// combat/stat math is + - * / sqrt in double; these must be bit-identical
// across i386(SSE)/x64/arm64/MSVC. This test fails if a build path regresses
// to x87 extended precision, enables -ffast-math/FMA contraction, or otherwise
// changes IEEE double results. Keep forever. See docs/BUILD.md "FP determinism".

TEST(FpDeterminismSmoke, IeeeDoubleEvaluation) {
    // 0 == IEEE double (SSE/ARM). x87 == 2. -1 == indeterminate (skip).
#if defined(FLT_EVAL_METHOD)
    if (FLT_EVAL_METHOD >= 0) {
        EXPECT_EQ(FLT_EVAL_METHOD, 0)
            << "Non-IEEE float evaluation (x87/extended precision) detected.";
    }
#endif
}

TEST(FpDeterminismSmoke, KnownBitPatternsAreStable) {
    // sqrt is IEEE-correctly-rounded on every conforming platform.
    EXPECT_EQ(std::bit_cast<std::uint64_t>(std::sqrt(2.0)),
              std::uint64_t{0x3FF6A09E667F3BCDULL});

    // A short + - * / chain pinned to its exact IEEE double bits; any FMA
    // contraction or fast-math reassociation shifts these bits.
    const double a = 1.0 / 3.0;
    const double b = a * 7.0 + 2.0;
    const double c = (b - 2.0) / 7.0;
    EXPECT_EQ(std::bit_cast<std::uint64_t>(b),
              std::bit_cast<std::uint64_t>(1.0 / 3.0 * 7.0 + 2.0));
    EXPECT_EQ(std::bit_cast<std::uint64_t>(c),
              std::bit_cast<std::uint64_t>(a));
}
```
  NOTE: the exact `sqrt(2.0)` bit constant `0x3FF6A09E667F3BCD` is the standard IEEE-754 double for √2; verify it in Step 3's run and, if the platform disagrees, that disagreement is itself the finding (do not "fix" the constant to match a broken build without understanding why).
- [ ] **Step 2:** Register: add `tests/fp_determinism_smoke_tests.cpp` to the CMake test sources list (alongside `tests/cpp20_format_smoke_tests.cpp` ~:243) and `fp_determinism_smoke_tests.cpp` to `src/tests/Makefile:286` `SRCS`.
- [ ] **Step 3 (build + run):** `cd src && cmake --build --preset macos-arm64 -j4 && ctest --preset macos-arm64 -R FpDeterminismSmoke` → all pass; then full `ctest --preset macos-arm64` → `100% tests passed`.
- [ ] **Step 4 (ASan gate — new test file):** `cd src && cmake --preset macos-arm64-asan && cmake --build --preset macos-arm64-asan -j4 && ctest --preset macos-arm64-asan` → clean.
- [ ] **Step 5 (commit):** `test: add FP determinism smoke tripwire` (+ footer).

---

### Task 5: Documentation

**Files:**
- Modify: `docs/BUILD.md` (add "## FP determinism" section), `AGENTS.md` (one-line pointer in "Toolchain and Warning Policy")

- [ ] **Step 1:** In `docs/BUILD.md`, add an `## FP determinism` section stating: the deterministic subset is `+ − × ÷ sqrt`, `double`-only; banned in that path are `long double`, `-ffast-math`, and libm transcendentals (`pow`/`exp`/`log`/trig — vendor a fixed portable impl if ever needed); shipping and test builds share the FP flag set (`-msse2 -mfpmath=sse` GNU x86, `-ffp-contract=off` all GNU, `/fp:precise` MSVC) and must stay in sync; `fp_policy.h` fails the build on x87/fast-math; the `FpDeterminismSmoke` test is the runtime tripwire; regenerating `combat_transcript_seed42.txt` (`UPDATE_GOLDENS=1`) is a deliberate, reviewed act.
- [ ] **Step 2:** In `AGENTS.md` "Toolchain and Warning Policy", add one line: deterministic FP is enforced (SSE, no x87/fast-math/`long double`/transcendentals in the combat path); see `docs/BUILD.md` "FP determinism".
- [ ] **Step 3 (commit):** `docs: document the FP determinism policy` (+ footer).

---

### Task 6: Cross-matrix verification + report

**Files:** none.

- [ ] **Step 1 (macos-arm64):** full ctest + `scripts/boot-golden.sh --native build/macos-arm64/ageland verify`.
- [ ] **Step 2 (rots64 / linux-x64):** full ctest + `scripts/boot-golden.sh --service rots64 verify`. Also run the **guard positive-fire check** here if deferred from Task 3: in the container, compile one combat TU with `-mfpmath=387` and confirm `fp_policy.h`'s `#error` fires; then discard.
- [ ] **Step 3 (golden portability):** confirm `CharacterizationCombat*` passes on both platforms with **no** change to `combat_transcript_seed42.txt` (`git status` clean for goldens).
- [ ] **Step 4 (report to owner):** summarize per-platform results; state that the `src/Makefile` x87→SSE shipping flip is verified in full only by the i386 finalization battery (owner-triggered), and surface the merge decision + the one-line release-note about the i386 behavior alignment. Do NOT merge or push.

---

## Self-review notes
- Spec coverage: shared flags (Task 2), compile-time guard (Task 3), smoke test (Task 4), docs (Task 5), cross-matrix golden proof (Task 6) — matches the spec's five design elements. "No golden regen" is enforced by the Global Constraint + Task 2/6 checks.
- Risk register: formatter-hook noise on combat-TU include additions (mitigated: Python byte-edits + diff-stat check); the x87→SSE shipping flip is verified only on real i386 (mitigated: called out, deferred to the i386 battery); MSVC `FLT_EVAL_METHOD` uncertainty (mitigated: the `#if defined && >0` guard no-ops safely, smoke test backstops); the pinned `sqrt(2.0)` constant (mitigated: verify-on-run, disagreement is a finding not a patch target).
- No placeholders: every step has exact files, flags, commands, and expected results.
