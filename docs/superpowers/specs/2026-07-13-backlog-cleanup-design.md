# Backlog Cleanup wave — DO_SAY hardening, alias leak, aliasing cluster, MSVC narrowing (design)

**Date:** 2026-07-13 · **Predecessors:** Phase 5 merged (`bd8c216`); docs pass (`3f24b5e`) ·
**Branch:** `modernization/backlog-cleanup` off master (`3f24b5e` or later) ·
**Follow-on effort (separately spec'd):** the RAII lifecycle-audit wave (char_data/obj_data
ownership) runs AFTER this wave merges.

## User decisions binding this wave (2026-07-13)

1. **Two efforts, backlog first** — this wave is the backlog; RAII audit is its own next effort.
2. **Roster:** DO_SAY security hardening; Crash_alias_load leak; target_data operator== const&;
   buf-aliasing cluster conversion; gtest FetchContent for the macOS ASan job; MSVC narrowing
   revisit; minor nits (gtest_main comment, interpre whitespace). **Severity-ordered, MSVC last.**
3. **Fallthrough rulings (amended 2026-07-13):** BOTH sites — the Easterling language
   fallthrough (shapemob.cpp:98) and the whip damage-type fallthrough (fight.cpp:2041) — are
   marked **INVESTIGATE**: the owner needs more information before any behavior ruling. T2
   rewrites both source comments from FIXME to
   `INVESTIGATE (owner, 2026-07-13): behavior ruling pending more information; preserved
   byte-for-byte` (keeping the corroborating-evidence citations). No behavior change to either
   site this wave.

## Tasks

1. **T1 — DO_SAY template hardening (SECURITY, first).** Extend `safe_template`
   (src/safe_template.{h,cpp}, Wave 2) from exact-N-`%s` validation to the with-argument class
   used by script.cpp's five sites (`SCRIPT_DO_SAY`, `SCRIPT_DO_YELL`, `SCRIPT_SEND_TO_CHAR`,
   `SCRIPT_SEND_TO_ROOM`, `SCRIPT_SEND_TO_ROOM_X`: `sprintf(output, curr->text, txt1)` where
   `curr->text` is builder-authored world data). Behavior contract: well-formed templates are
   byte-identical (pinned by new characterization tests BEFORE the switch); malformed templates
   change from UB to safe fallback + one syslog line — the sanctioned delta, same policy as Wave
   2's `death_cry2`/shop hardening. Remove the five sites' deprecation pragmas. Update
   `docs/security-notes.md`'s entry to resolved-with-pointer.
2. **T2 — small-fix bundle.** (a) `Crash_alias_load` production leak (objsave.cpp): fix the
   alias-list ownership so `free_char` releases it — LeakSanitizer-verified (sanitize preset run
   of the covering tests + the existing sanitize.supp entry REMOVED, proving the fix). (b)
   `target_data operator==(target_data)` → `const target_data&` (interpre.cpp:~694; dead code
   but latent-leak-by-copy). (c) gtest_main.cpp world_singleton first-call-wins comment. (d)
   BOTH fallthrough comments (fight.cpp:2041, shapemob.cpp:98) → INVESTIGATE wording per
   decision 3. (e) interpre.cpp whitespace nits only if separable without churn.
3. **T3 — buf-aliasing cluster conversion.** Characterize with byte-pins, then convert to
   `std::string` composition: act_info.cpp's pragma-wrapped display cluster
   (`show_char_to_char`, `list_char_to_char`, `get_char_position_line`, `get_char_flag_line`,
   `show_mount_to_char` remnants, `do_look` case 8) and utility.cpp's `-Wrestrict` pair
   (`show_room_affection`, `show_room_weather`). Removes the last format-related pragmas —
   exit state: ZERO sprintf-family suppression pragmas in the tree; the only remaining
   dynamic-format sites are `add_prompt`'s PRF_DISPTEXT branch (already justified) and the
   T1-hardened script sites (no longer raw sprintf).
4. **T4 — macOS ASan gtest via FetchContent.** Provision GoogleTest by FetchContent for the
   `macos-arm64-asan` preset/CI job (test-only tooling, never in the game binary — same policy
   as the windows-2022 runner), so gtest is ASan-instrumented; re-enable
   `detect_container_overflow` on that job. Local sanitized suite green required.
5. **T5 — MSVC narrowing revisit (LAST; CI-cycle-based, batched).** Stratified sample of the
   1,167 `/wd4244`+`/wd4267`-suppressed sites (by file class: combat math, indices, time,
   protocol bytes) → classify real-truncation-risk vs benign legacy int math → FIX the real
   class → then either narrow suppressions (per-file pragmas around the benign clusters) or
   keep the global `/wd` with the sampling evidence documented in BUILD.md. Success = the
   decision is evidence-based and ledgered; count of real fixes is whatever the sample finds.
6. **T6 — exit.** i386 battery + 6-job CI green + exit note + final whole-branch review +
   merge decision to the owner. No account smoke (no connection/login surfaces this wave).

## Standing constraints (carry unchanged)

Goldens STOP-on-diff, zero sanctioned changes (T1/T3 byte-pins make the conversions provable);
RNG discipline; suppression discipline (this wave NETS NEGATIVE suppressions: removes the five
deprecation pragmas, the aliasing-cluster pragmas, the -Wrestrict pair, and one sanitize.supp
entry); per-task dual local gate (macOS + rots64); i386 + CI at exit; new-test ASan gate;
standing fixture rules; -Werror//WX stay green throughout (any new warning is a build failure
now — conversions must be warning-clean on all four compilers).

## Risks

- **T1 output surface:** script messages reach players; the pins must cover each of the five
  sites' well-formed path before the switch. Malformed-template behavior change is disclosed.
- **T3 aliasing semantics:** these helpers were skipped THREE TIMES for a reason — the
  `sprintf(buf, "%s...", buf, ...)` self-reference must be materialized-then-composed exactly
  (Wave 4's proven idiom); pins first; combat/boot goldens as the outer net.
- **T5 blast radius:** narrowing fixes in combat math can change values — any site whose fix
  would alter arithmetic results on the defined path is NOT "benign cleanup," it's a behavior
  change needing explicit disposition (fix only provable-bug narrowings; document the rest).
- **T4 CI-runner variance:** FetchContent build time on the macOS runner; acceptable, advisory
  measurement in-task.

## Exit criteria

DO_SAY sites hardened + security-notes updated; alias leak fixed with supp entry removed;
aliasing cluster converted with pins; zero format-suppression pragmas tree-wide; macOS ASan job
running instrumented gtest with container-overflow on; MSVC narrowing dispositioned with
evidence; suite green everywhere (count grows with new pins); goldens byte-identical; i386
battery + 6-job CI green; exit note; final review; merge decision to the owner.
