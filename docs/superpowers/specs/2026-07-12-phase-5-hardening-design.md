# Phase 5 — Hardening: warnings-clean, sanitizer CI, clang-tidy (design)

**Date:** 2026-07-12 · **Parent spec:** `2026-07-06-cpp-modernization-design.md` (Phase 5) ·
**Predecessor:** Phase 4 complete (Waves 1-4; Wave 4 merged to master `79c9149` 2026-07-12,
four-platform CI green) ·
**Branch:** `modernization/phase-5-hardening` off master (`79c9149` or later).

## User decisions binding this effort (2026-07-12)

1. **Scope: warnings + sanitizers + tidy.** Drive `-Wall -Wextra` clean and gate it; add
   ASan/UBSan CI jobs; check in `.clang-tidy` (advisory this phase).
2. **`-funsigned-char`: the existing 4-compiler pin IS the accepted resolution** (GNU-family
   `-funsigned-char` + MSVC `/J`, already present in every build path — verified 2026-07-12,
   `src/CMakeLists.txt:80/113/324`, `src/Makefile:32`, `src/tests/Makefile:26`). The parent
   spec allowed the pin as resolution; the audit-away is NOT pursued. Document in BUILD.md.
3. **32-bit retirement: MOVED OUT of Phase 5 into its own future phase** (owner, 2026-07-12:
   "I won't be retiring the 32-bit version for a bit"). It activates only on the owner's
   confirmation that live player data is fully migrated off the binary/text-legacy formats.
   Until then the i386 preset, container, and CI job remain required, and the
   `legacy_*_fixture.bin` 32-bit-only golden rules stay in force.
4. **Const strategy for `-Wwritable-strings`: real const-correctness PLUS `std::string_view`
   where feasible** — string tables → `const char* const[]`; parameters/locals consumed by
   length-aware C++ code → `std::string_view`; NUL-terminated C-string consumers keep
   `const char*`. **Boundary rule: never pass `string_view::data()` to an API that assumes
   NUL termination.**
5. **Enforcement: `-Werror` everywhere, including MSVC `/W4 /WX`.** GNU-family
   (`-Wall -Wextra -Werror`) flips once AppleClang + g++14 are clean; MSVC is a staged final
   campaign (census via CI, triage, documented `/wd` suppressions for noise classes, then
   `/WX`) with the accepted cost that MSVC iteration is push-and-watch CI cycles (no local
   Windows host).
6. **Execution: approach A — category ladder,** real-bug classes first; the const campaign
   as one coherent cross-module sub-effort in module-sized commits.

## Baseline census (AppleClang 21, `-Wall -Wextra`, full ageland_tests build, 2026-07-12)

5,203 warnings total: `-Wwritable-strings` 3,270 · `-Wunused-parameter` 1,154 ·
`-Wdeprecated-declarations` 428 · `-Wfortify-source` 51 · `-Warray-bounds` 51 ·
`-Wunused-but-set-variable` 47 · `-Wignored-qualifiers` 40 · `-Wundefined-var-template` 38 ·
`-Wunused-variable` 29 · `-Wdangling-else` 19 · `-Wlogical-op-parentheses` 14 ·
`-Wchar-subscripts` 9 · `-Wdeprecated-copy-with-user-provided-copy` 8 ·
`-Wparentheses-equality` 6 · tail <40. (g++14's set differs; its census is taken in-task
from the rots64 build log. MSVC `/W4` census is Task 9's first CI run.)

## Standing constraints (carry from Phase 4, all binding)

Zero sanctioned golden changes (boot / combat / JSON — STOP-on-diff); byte-identical
observable behavior except individually disclosed fixes on broken/UB paths; no third-party
libraries; RNG only via `rots_rng` (no RNG call added/removed/moved); per-task DUAL local
gate (macOS native + rots64: ctest + boot goldens); i386 battery + four-platform CI +
finalization at exit only; new-test ASan gate; the standing fixture rules (in-place
descriptor resets, value-init, content-emptiness, platform shims, char[N]→std::format
casts); make-format scope discipline (commit only intended hunks).

## The ladder (9 tasks)

1. **Real-bug classes (~140):** `-Warray-bounds`, `-Wfortify-source`, `-Wchar-subscripts`,
   `-Wdangling-else`, `-Wlogical-op-parentheses`, `-Wparentheses-equality`,
   `-Wundefined-var-template`. Every instance individually dispositioned: latent bug (fix —
   behavior change on a broken path = disclosed delta) vs. benign (restructure so the
   compiler sees the intent, or targeted suppression with an in-code comment). Highest
   review scrutiny of the ladder.
2. **`-Wunused-parameter` (~1,154):** `[[maybe_unused]]` applied once at the ACMD/SPECIAL
   -style macro parameter lists (the bulk); unnamed parameters for genuine stragglers.
3. **`-Wdeprecated-declarations` (~428):** migrate where trivial; deliberate legacy API use
   gets the platform-appropriate define/suppression with a comment.
4. **Const/`string_view` campaign (~3,270):** per decision 4. Cross-module by nature
   (signature ripples); executed in module-sized commits, each batch gated by the full
   suite + goldens. Reviewers treat every `.data()` call as a named risk.
5. **Tail sweep to zero** on AppleClang AND g++14 (`-Wunused-but-set-variable`,
   `-Wignored-qualifiers`, `-Wunused-variable`, `-Wdeprecated-copy-*`, anything the g++14
   census adds).
6. **Sanitizer CI:** ASan+UBSan jobs (linux-x64 + macOS) running the full ctest suite, plus
   a sanitized boot-golden run. Includes fixing the known ASan-visible backlog items so the
   jobs can be green with leak detection ON: JsonUtils dangling-temporary tests
   (json_utils_tests.cpp:83/:96), test-fixture `profs` leaks, and whatever the first full
   sanitized boot surfaces (each dispositioned).
7. **`.clang-tidy` check-in:** modernize/bugprone/performance checks curated to match the
   established idioms (std::format target, string_view boundary rule, rots_rng ownership);
   wired into CI as an advisory (non-blocking) job this phase.
8. **GNU-family gate flip:** delete `-w` from every build path (`src/Makefile`,
   `src/CMakeLists.txt` game target, `ROTS_SUPPRESS_TEST_WARNINGS` default), add
   `-Wall -Wextra -Werror`; prove on macOS + rots64 + (at finalization) i386.
9. **MSVC campaign:** `/W4` census CI run → triage table (real fix vs documented `/wd`
   suppression per noise class, each with a ledger entry) → iterate to clean → flip
   `/W4 /WX`. Explicitly last; CI-cycle iteration accepted.

## Verification

Behavior gate unchanged: per-task dual local gate; suite 1015+ green; boot/combat/JSON
goldens byte-identical. Warning progress: census script (clean probe build, per-flag
counts) recorded per task; a category task exits at zero for its flags on BOTH AppleClang
and g++14. Task 6's sanitizer jobs must be green in CI before Task 8's `-Werror` flip
lands. Task 1 dispositions and Task 4 `.data()` calls are named review risks. Finalization:
i386 battery (incl. the new flags), four-platform CI green with the new required sanitizer
jobs, exit note, final whole-branch review, merge decision to the owner.

## Risks

- **Const/`string_view` ripple:** overload resolution and lifetime assumptions can shift —
  module-sized batches, suite+goldens per batch, boundary rule, `.data()` review flags.
- **`-Werror` vs future compiler upgrades:** pinned toolchains make new-warning arrival a
  controlled event; noted in BUILD.md.
- **MSVC census unknown (likely thousands, C4244/C4267 class):** staged last; documented
  suppressions legitimate for noise classes.
- **Sanitizers will find real pre-existing bugs:** that is the point; each fix dispositioned
  and output-neutrality proven via sanitized boot-golden.
- **i386 g++14 target-specific warnings:** surface at finalization; fixed before exit.

## Exit criteria

Zero warnings + `-Werror` (`/W4 /WX`) on all four platforms; `-w` deleted everywhere;
ASan/UBSan CI jobs green and required; `.clang-tidy` checked in, advisory job wired;
`-funsigned-char` resolution and the 32-bit-retirement future-phase trigger documented in
BUILD.md/CLAUDE.md; suite green everywhere; goldens byte-identical; four-platform CI green;
exit note; final review; merge decision to the owner.
