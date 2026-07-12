# Phase 4 Wave 3 — act_info / act_wiz giants: full-catalog modernization (design)

**Date:** 2026-07-11 · **Parent spec:** `2026-07-06-cpp-modernization-design.md` (Phase 4) ·
**Predecessor:** Wave 2 (`2026-07-10-phase-4-wave-2-design.md`, merged; exit at `c84b6a9`) and the
Upstream Sync & Validation effort (`2026-07-10-upstream-sync-validation.md`, merged to master at
`942c83f` with owner approval 2026-07-11) ·
**Branch:** `modernization/phase-4-wave-3` off master (`434d0f5` or later).

## User decisions binding this wave (2026-07-11)

1. **Scope: both giants, nothing else.** `act_info.cpp` (4,724 lines, 439 sprintf-family sites)
   and `act_wiz.cpp` (4,110 lines, 329 sites). The Wave-2 leftovers (script.cpp DO_SAY
   template-hardening remainder, stale `src/tags`, recorded shape\* pre-existing bugs) stay
   deferred.
2. **Depth: full catalog including RAII, in place.** sprintf/strcpy/strcat → `std::format` /
   bounded equivalents; mechanical `char[MAX_STRING_LENGTH]`-local → `std::string` conversions;
   the ~16 local `new`/`delete`/`malloc` sites → RAII — **scoped to allocations owned within
   these two files** (temporary buffers, local structures). The `char_data`/`obj_data`
   world-graph ownership audit remains its own future wave. **No file splitting** this wave.
3. **Execution: chunked characterize-then-transform** (approach A) — each chunk pins its output
   bytes with characterization tests before transforming, per the proven Wave 2 recipe.
4. **Verification cadence amendment (owner, 2026-07-11): the per-task local gate is now a
   TRIPLE compile-and-test** — (a) i386 container (`make test` ctest + tests-Makefile runner +
   `boot-golden.sh verify`), (b) macOS native (`ctest --preset macos-arm64` +
   `boot-golden.sh --native`), and (c) **rots64 (`ctest --preset linux-x64` +
   `boot-golden.sh --service rots64 verify`)**. The four-platform remote CI push stays at wave
   finalization only (windows-msvc remains the finalization-only exercise). CLAUDE.md's cadence
   paragraph is updated alongside this spec.
   **SECOND AMENDMENT (owner, 2026-07-11, mid-wave after Task 1):** the i386 container leg
   moved from per-task to **finalization-only** (Task 10) — qemu-i386 on this host costs
   60-90+ min per run and dominated iteration time. Per-task gate is now **DUAL**: macOS
   native + rots64. Task 1 ran the original triple gate; Tasks 2+ run the dual gate.
   CLAUDE.md updated again alongside.
5. Standing constraints carry: no third-party libraries in the game binary (`std::format` is the
   formatting target); RNG through `rots_rng` only; goldens STOP-on-diff; i386 container is the
   legacy guard; new-test macOS ASan gate applies to every new test file.

## Golden stance

**Zero sanctioned golden changes are anticipated this wave.** The work is format-mechanics,
buffer, and local-lifetime conversion — behavior-preserving by definition. Any boot/combat/JSON
golden diff at any point = STOP, investigate, fix the transform. There is no
`UPDATE_GOLDENS=1` path in this wave's plan.

## Chunk map (10 tasks)

Each chunk = one task with the same internal shape: **characterize → transform → triple local
gate + ASan (if the chunk added/rewrote a test file)**. Command inventory verified against
source 2026-07-11 (line anchors from that scan).

### act_info.cpp — 4 chunks, in order

- **I1 Perception:** `do_look` (:1037, the ~400-line giant), `do_read` (:1470), `do_examine`
  (:1485), `do_exits` (:1518), `do_search` (:3292), `do_map` (:3206), `do_small_map` (:3275),
  plus the room/object display helpers they share.
- **I2 Self-status:** `do_score` (:1856), `do_info` (:1626), `do_affections` (:3469),
  `do_toggle` (:2769), `do_inventory` (:2478), `do_equipment` (:2484), `do_gen_ps` (:2490),
  `do_diagnose` (:2975), `do_orc_delay` (:3393), `do_squareroot` helper (:1835). (`do_trap` is
  only *declared* in act_info.cpp — it is defined in `ranger.cpp:1166` and is OUT of scope.)
- **I3 World/social info:** `do_who` (:2125), `do_users` (:2320), `do_where` (:2644),
  `do_levels` (:2654), `do_consider` (:2724), `do_time` (:1923), `do_weather` (:1977),
  `do_help` (:2018), `do_commands` (:2915), `do_whois` (:3065), `do_fame` (:3558) +
  `do_fame_leader_string` (:3537), `do_rank` (:3718).
- **I4 Object identification:** `do_compare` (:3767), `do_stat` (player-facing, :3918),
  `do_exploits` (:4120), `do_identify_object` (:4584) + the food/light/flag-values/weapon
  display helpers (:4494-4583), `do_details` (:4654).

### act_wiz.cpp — 4 chunks, in order

- **W1 Inspection:** `do_stat_room` (:420), `do_stat_object` (:548), `do_stat_character`
  (:736), `do_vnum` (:397), `do_vstat` (:1364), `do_zone` (:1022), `do_wizstat` (:1071),
  `do_findzone` (:3788), `do_top` (:3942), `do_last` (:1859), `do_date` (:1811),
  `do_uptime` (:1831), `do_show` (:2357).
- **W2 World manipulation:** `do_at` (:256), `do_goto` (:288), `do_trans` (:319),
  `do_teleport` (:367), `do_load` (:1312), `do_purge` (:1408), `do_zreset` (:2090),
  `do_switch` (:1253), `do_return` (:1293), `do_snoop` (:1200), `do_force` (:1891).
- **W3 Player administration:** `do_advance` (:1511), `do_restore` (:1608), `do_wizset`
  (:2693), `do_wizutil` (:2153), `do_setfree` (:3820), `do_delete` (:3150), `do_register`
  (:3510), `do_account` (:3183), `do_whoacct` (:3452), `do_invis` (:1639), `do_wizlock`
  (:1767), `do_dc` (:1730), `do_shutdown` (:1160), `do_rehash` (:3851).
  **Caution flag:** `do_account`/`do_whoacct` were reworked by the just-merged upstream
  account-management effort and already carry 13 tests in `act_wiz_tests.cpp` — this chunk
  treats that suite as its characterization base and EXTENDS it; it never replaces or weakens
  existing account tests.
- **W4 Wiz communication:** `do_emote` (:121), `do_send` (:146), `do_echo` (:172),
  `do_gecho` (:1673), `do_poofset` (:1700), `do_wiznet` (:1965).

### Tasks 9-10

- **Task 9: cross-file sweep + RAII closure.** Grep-audit both files for any remaining
  sprintf-family call, stray `char[MAX_STRING_LENGTH]` local that chunks skipped without a
  recorded reason, and the RAII site list (11 in act_info, 5 in act_wiz — exact list frozen at
  plan time); convert or record the justified exception. Both files must end
  sprintf/strcpy/strcat-free (grep-clean) or carry a written per-site justification.
- **Task 10: Exit.** Docs (CLAUDE.md/AGENTS.md test-count baselines, BUILD.md if a new lesson
  emerged), wave finalization battery: triple local gate (already green per-task) + push →
  four-platform CI green; exit note in the plan doc (battery actuals ×4, CI URL, zero-golden
  confirmation, deferred list); final whole-branch review; merge decision to the owner.

## Characterization strategy

- New test files: `src/tests/act_info_format_tests.cpp` and `src/tests/act_wiz_format_tests.cpp`,
  suites named per family (`ActInfoPerception`, `ActInfoSelfStatus`, `ActWizInspection`, …),
  following the Wave 2 `act_format_tests.cpp` pattern on the existing fixtures
  (`ScopedTestWorld`, `CharPlayerDataBuilder`, `ObjFlagDataBuilder`, capturing descriptor).
- Tests pin **exact output bytes** of the format paths being rewritten — including quirks
  (glibc `(null)` literals, right-justified pads, trailing spaces, color codes). Byte-identical
  before/after is the per-chunk proof.
- Coverage is proportional to transform surface, not command count: a command that emits one
  literal via `send_to_char` gets no test; `do_look`, `do_stat_character`, `do_who`,
  `do_wizset` get thorough multi-branch coverage.
- **Two mandatory fixture rules** (both bitten before): every `make_descriptor()`-style fixture
  includes the MSVC output self-pointer re-point line (Phase 3 Task 6 / USV finding #3); all
  POSIX-only fixture code goes through the `test_platform_compat.h` shims from the start
  (USV findings #1/#2 class).
- Every new/substantially-rewritten test file runs once under macOS ASan before its task closes
  (standing 2026-07-10 gate).

## Verification cadence (this wave)

- **Per task (triple local gate):** i386 container (`make test` + tests-Makefile runner +
  `boot-golden.sh verify`) · macOS native (`ctest --preset macos-arm64` +
  `boot-golden.sh --native build/macos-arm64/ageland verify`) · rots64
  (`ctest --preset linux-x64` + `boot-golden.sh --service rots64 verify`). Goldens
  byte-identical at every gate. No CI push per task.
- **At Task 10 only:** push `modernization/phase-4-wave-3`; all four required CI jobs green
  (windows-msvc is the wave's only Windows exercise). MSVC-only surprises in the wave's new
  test files surface here by design — the accepted, documented exposure; ASan + the
  platform-shim rule close the two classes that actually bit in Wave 2 / USV.

## Risks / error handling

- **Golden drift:** STOP-on-diff, no exceptions (see Golden stance).
- **`std::format` null-`char*`:** the giants are dense with nullable `char*` (titles,
  poofin/poofout, keeper strings). Every converted `%s` whose argument can be null gets the
  established null-guard treatment (the `dc56cc2` bug class). The `char[N]`-member decay rule
  (`static_cast<const char*>`) applies per BUILD.md.
- **Giant functions:** `do_look` and `do_stat_character` may be transformed in reviewable
  sub-passes inside their chunk's task; their tests get the chunk's largest allocation.
- **Dead code:** deleted (not modernized) only with caller-grep proof recorded in the commit
  message, per the AGENTS.md heuristic.
- **Chunk-boundary bleed:** a shared helper is transformed exactly once, by the first chunk
  that owns it; later chunks must not force re-edits to earlier chunks' output. If a later
  chunk discovers an earlier-chunk defect, it is fixed as its own commit with its own gate.
- **RAII near command semantics:** the 16 sites are local-lifetime only; any site that turns
  out to hand ownership into the world graph (e.g. a loaded object in `do_load`) is OUT of
  scope for conversion this wave and gets a recorded justification instead.

## Exit criteria

- All sprintf/strcpy/strcat sites in both files converted or covered by a written per-site
  justification; both files grep-clean of the family otherwise.
- Mechanical `char[N]`-local → `std::string` conversions done; skipped sites recorded.
- The in-scope local RAII sites converted; out-of-scope (ownership-transfer) sites documented.
- Dead code removed with proof; no behavior change anywhere: boot/combat/JSON goldens
  byte-identical, zero golden recaptures this wave.
- Characterization suites in place and green on all local platforms; new-test ASan gate
  satisfied per task.
- Four-platform CI green at finalization; docs/test-count baselines updated; exit note
  written; merge decision to the owner.
