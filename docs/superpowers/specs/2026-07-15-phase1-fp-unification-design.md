# Phase 1 — Floating-Point Pipeline Unification (Design)

**Date:** 2026-07-15
**Status:** Approved by owner (design dialogue, this session)
**Part of:** Option C — "store character vitals (HP/mana/move) and combat ratings
(OB/PB/DB) as double-precision, converting the combat/stat math from integer to
continuous math." This is a three-phase program; this spec covers **Phase 1 only**.

## Program decomposition (context)

Option C is split into three independently shippable, dependency-ordered sub-projects,
each with its own spec → plan → implementation cycle:

1. **Phase 1 — FP pipeline unification (this spec).** Build-flags + verification only.
   Make floating-point bit-identical across i386/x64/arm64/MSVC and identical between
   the shipping and test builds. No source math or format changes. Closes the
   documented ship-vs-test FP gap and is valuable on its own.
2. **Phase 2 — double-precision storage + display truncation + migration.** Change
   HP/mana/move and OB/PB/DB to `double` in the runtime structs and persistence (new
   `char_file_u`/JSON schema version + migration; `save_player` text path; frozen
   32-bit legacy fixtures untouched), plus the display layer (round HP up, others down,
   death at `<= 0.0`). Math still yields integer-valued doubles, so goldens do not move.
3. **Phase 3 — convert combat/stat math to double.** `get_real_OB/parry/dodge`,
   `get_weapon_damage`, `armor_absorb`, the `fight.cpp::hit()` damage pipeline, and the
   `profs.cpp` max-HP formula go from int-truncating to continuous double math. This is
   the deliberate balance change (the ~1 HP and broader), with intentional golden regen.
   Depends on Phases 1 and 2.

Phases 2 and 3 get their own specs after Phase 1 ships.

### Status update (2026-07-23)

Phase 3's math (item 3 above) shipped **early**, ahead of Phase 2, as double-precision
*interiors* with `int` storage unchanged — the fp-interiors wave
(`docs/superpowers/specs/2026-07-22-fp-interiors-design.md`, branch `feat/fp-interiors`,
2026-07-23). Four core-combat formula families (max-HP/vitals recalc, the OB/PB/DB rating trio,
`fight.cpp::hit()`'s damage formula, and the speed/energy-regen chain) now compute in
full-precision `double`, landing each result back into its unchanged `int` field/return through
exactly one `rots::fp::to_game_int` (`std::lround`) boundary — see `docs/BUILD.md` "FP
determinism" for the policy. **Phase 2 (double-precision storage + migration) remains blocked**,
unchanged from this spec's original ordering, on player persistence becoming fully
account-native JSON. The only benefit still waiting on Phase 2 is fractional carry across
ticks/relogs — everything else Phase 3 promised (single-rounding combat math, eliminating the
per-step truncation bias) is now live.

## Phase 1 goal

Floating-point results are bit-identical across the full build matrix
(i386 legacy, linux-x64, macos-arm64, windows-msvc) AND identical between the shipping
`ageland` binary and the `ageland_tests` binary — verified once across the matrix and
guarded against regression. Achieved purely through build flags, a compile-time guard,
a smoke test, and documentation. **No source math changes, no struct changes, no
persistence changes.**

## Why this is the foundation (background)

- The repo already documents (`src/tests/Makefile`, `src/CMakeLists.txt` ~434-441,
  `src/tests/characterization_combat_tests.cpp:17-24`) that the **shipping i386 binary
  uses x87 80-bit extended-precision** float math while **every test/CI/golden build
  uses SSE**. They round differently. This is tolerated today only because live float
  usage is tiny (essentially `rots_rng::next() / 4294967296.0` feeding `roll > chance`
  comparisons), where the rounding difference cannot realistically flip a boolean.
- The RotS combat/stat/HP math is entirely `+ − × ÷` and `std::sqrt` — the exact set
  IEEE-754 **mandates** correctly (bit-exact) rounded on every conforming platform.
  There are **no** `pow`/`exp`/`log`/trig calls (verified: the `log(` sites are the
  game's own logging function) and **no** `long double`. This places the whole pipeline
  inside the reproducible subset, so unification is achievable — the only active
  divergence sources are x87 (a flag) and FMA contraction (a flag).
- Phases 2–3 make float load-bearing; the ship-vs-test gap must be closed first or the
  goldens stop describing the shipping binary.

## Design

### 1. Single shared deterministic-FP flag set, applied to shipping AND tests

Today the SSE flags exist only on the `ageland_tests` CMake target and in
`src/tests/Makefile`; the shipping `ageland` target and `src/Makefile` carry none.
Define the deterministic-FP flag set **once** and apply it to **both** `ageland` and
`ageland_tests`, and to `REQ_CXXFLAGS` in `src/Makefile` and `src/tests/Makefile`, so
shipping and test are FP-identical by construction:

- **GNU-family, x86 (incl. i386):** `-msse2 -mfpmath=sse` — eliminates x87 80-bit
  extended precision (`FLT_EVAL_METHOD` 2 → 0).
- **GNU-family, all targets (x86 AND arm64):** `-ffp-contract=off` — prevents the
  compiler from fusing `a*b + c` into a single-rounded FMA. This is the flag that makes
  arm64 (NEON, which can fuse) match x86, and making it explicit means the guarantee
  survives a future move off `-O0`. It is **not** x86-gated (unlike `-msse2`).
- **MSVC:** pin `/fp:precise` explicitly (the default, but pinned so it can't silently
  change) — never `/fp:fast`.
- **Banned (already true, keep enforced):** `-ffast-math`/`-Ofast`/`/fp:fast`,
  `long double`.

Implementation notes:
- In CMake, gate `-msse2 -mfpmath=sse` on `$<AND:${ROTS_GNULIKE},${ROTS_X86}>` (the
  existing `ROTS_GNULIKE`/`ROTS_X86` generator-expression variables), and
  `-ffp-contract=off` on `${ROTS_GNULIKE}` (all GNU-like, incl. arm64). Apply the same
  block to `ageland` and `ageland_tests` (extract to a helper `function()`/variable so
  there is one source of truth, not two copies).
- The current test-only SSE block (`src/CMakeLists.txt` ~442) is subsumed by the shared
  application; remove the standalone duplicate.
- `src/Makefile`: add `-msse2 -mfpmath=sse -ffp-contract=off` to `REQ_CXXFLAGS` (this is
  the change that flips shipping i386 x87 → SSE). `src/tests/Makefile` already has
  `-msse2 -mfpmath=sse`; add `-ffp-contract=off` and a comment pointing at the shared
  origin.

### 2. Acknowledged behavior effect (the only one)

Adding SSE to the shipping i386 build flips the live i386 game from x87 to SSE. This
changes the *existing* float paths (`double number()` RNG scaling; already-float regen
in `limits.cpp`) to round as SSE — i.e. to match what the tests and goldens have always
described. It is a one-time alignment of shipping behavior to tested behavior, not a new
divergence, and is effectively mandatory for the Option C program. It should be noted in
the release notes as a (tiny) behavior change to the live i386 game.

No other behavior changes: no math, struct, or format changes in Phase 1.

### 3. Regression guard — two layers

**Layer A — compile-time guard that fails the SHIPPING build (`src/fp_policy.h`):**
A new header, included by the combat/stat translation units (`fight.cpp`, `utility.cpp`,
`profs.cpp`, `limits.cpp`), containing:
```cpp
#include <cfloat>
static_assert(FLT_EVAL_METHOD == 0,
    "Deterministic FP requires SSE/IEEE double evaluation (FLT_EVAL_METHOD==0); "
    "x87 extended precision (==2) is banned — see docs/BUILD.md FP determinism.");
#ifdef __FAST_MATH__
#error "-ffast-math breaks cross-platform FP determinism — see docs/BUILD.md."
#endif
```
Because these TUs are compiled into `ageland` itself, x87 or fast-math creeping back
fails the **shipping build**, not merely the test build — a stronger guard than a test.
(MSVC does not define `FLT_EVAL_METHOD` identically in all modes; guard the
`static_assert` so it is a no-op where the macro is unavailable, and rely on the smoke
test there. Confirm the exact MSVC behavior during implementation.)

**Layer B — runtime smoke test (`src/tests/fp_determinism_smoke_tests.cpp`):**
Mirrors `cpp20_format_smoke_tests.cpp`/`llp64_probe_tests.cpp`. Pins a short
`+ − × ÷ sqrt` computation to its exact expected SSE bit result (compare via
`std::bit_cast<uint64_t>` of the double, not `==` on a rounded value), so any FP-unit or
contraction divergence trips it. Also assert `FLT_EVAL_METHOD == 0` at runtime as a
belt-and-suspenders check where the compile-time assert can't run. Registered in both
`src/CMakeLists.txt` and `src/tests/Makefile`.

### 4. Verification — one-time cross-matrix golden proof

Build `ageland` (shipping flags) and `ageland_tests` (now sharing those flags) on
i386-legacy, linux-x64, macos-arm64, and windows-msvc; confirm
`src/tests/goldens/combat_transcript_seed42.txt` passes on all four.

- **Expectation: no golden regeneration.** The golden was authored by the SSE test
  binary; this change makes *shipping* match that existing SSE golden rather than moving
  the golden. If a regen is somehow required, that is a signal to investigate (per the
  repo's "unintentional drift = a bug" rule) — it must be understood, not blindly
  `UPDATE_GOLDENS`-ed.
- The load-bearing check is cross-platform identity: the same golden passing on
  i386(SSE-forced) / x64 / arm64(contraction-off) / MSVC. arm64's newly-explicit
  `-ffp-contract=off` is the most likely place to surface a shift; if the golden was
  passing on arm64 before (it was, per current CI), explicit contraction-off should be
  equal-or-safer, but it must be reconfirmed.

### 5. Documentation

Add an "FP determinism" section to `docs/BUILD.md` and a one-line pointer in `AGENTS.md`
("Toolchain and Warning Policy"):
- The deterministic FP subset is `+ − × ÷ sqrt`, `double`-only.
- Banned in the deterministic path: `long double`, `-ffast-math`, libm transcendentals
  (`pow`/`exp`/`log`/trig). If a future formula needs one, vendor a fixed portable
  implementation rather than calling platform libm.
- Shipping and test builds share the FP flag set; they must stay in sync.
- How to regenerate the combat golden (`UPDATE_GOLDENS=1`) and that a regen is a
  deliberate, reviewed act.

## Verification cadence (this phase)

- Per change: macos-arm64 build + full ctest (incl. the new smoke test + combat golden).
- Phase completion: the full four-platform matrix build + combat-golden proof
  (i386-legacy, linux-x64, macos-arm64, windows-msvc), plus rots64 + boot goldens per
  the machine cadence. The i386 finalization battery before merge is the owner's call.
- Because a new test file is added (`fp_determinism_smoke_tests.cpp`), run it under the
  macOS AddressSanitizer preset before finalization (new-test-file rule).

## Out of scope (Phase 1)

- Any change to a struct field type, `char_file_u`, JSON schema, or `save_player`.
- Any change to combat/stat/HP math or the goldens' *content* (beyond confirming they
  still pass).
- Removing or rewriting the existing narrow float usage (`double number()` scaling,
  float regen) — it stays as-is; Phase 1 only changes how it is *compiled*.
