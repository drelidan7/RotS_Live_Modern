# Phase 4 Wave 4 — fight/db/comm final trio: full-catalog modernization (design)

**Date:** 2026-07-12 · **Parent spec:** `2026-07-06-cpp-modernization-design.md` (Phase 4) ·
**Predecessor:** Wave 3 (`2026-07-11-phase-4-wave-3-design.md`, merged to master `ad8243e`
2026-07-12, four-platform CI green ×2) ·
**Branch:** `modernization/phase-4-wave-4` off master (`ad8243e` or later).

## User decisions binding this wave (2026-07-12)

1. **Scope: the final trio, in place.** `fight.cpp` (2,920 lines, 11 sprintf-family sites),
   `db.cpp` (5,463 lines, 111 sites), `comm.cpp` (2,460 lines, 47 sites). No file splitting.
2. **Risk posture: include both high-risk surfaces, gated by extra pins.** `comm.cpp::act()`
   (the game's output engine) and `db.cpp::save_player`'s live line-oriented TEXT player-save
   writer (an ON-DISK format for non-account characters — byte drift there is data corruption,
   not cosmetics) are both converted, each behind dedicated new characterization (see Testing).
3. **Execution: approach A** — per-file chunks, riskiest last globally: F1 (fight) → D1 (db
   loader/boot messaging) → D2 (db save_player + text writer) → C1 (comm misc) → C2 (act()
   alone) → sweep → exit.
4. **Catalog and rules carry from Wave 3 verbatim** (its spec §Golden stance + the plan's
   10-item idiom catalog): `std::format` target; `nz()` vs kept-ternary discipline;
   `static_cast<const char*>` on every `char[N]` reaching `std::format`; sprintbit/sprinttype
   unconverted; parser buffers stay `char[]`; dead code deleted only with caller-grep proof;
   **local-lifetime RAII only** — `db.cpp`'s world-graph string ownership (fread_string/strdup
   into world structs) belongs to the future RAII lifecycle-audit wave, justified-skip here.
5. **Cadence (standing, twice-amended 2026-07-11):** per-task DUAL local gate (macOS native +
   rots64, boot goldens byte-identical); i386 container battery + four-platform remote CI at
   finalization only; new-test macOS ASan gate per task.

## Wave-specific hard constraints

- **Legacy binary decoders in db.cpp are untouchable** — the explicit-offset LEGACY-documented
  migration converters (and `crime_json`) must not be modified, "simplified," or reflowed.
- **Combat goldens join boot goldens as active gates** (`CharacterizationCombatTest.*`, seed
  42): `fight.cpp` conversions must not add, remove, or reorder any `number()`/RNG call —
  a combat-golden diff is STOP, same status as boot-golden drift.
- **Zero sanctioned golden changes** (boot, combat, JSON). No `UPDATE_GOLDENS`/`capture`.
- **`save_player` conversion is gated by round-trip byte-comparison**, not terminal pins: the
  new suite writes a fixture character through the REAL writer pre-transform, pins the file
  bytes, and post-transform output must be byte-identical (plus reload-equality).
- **Standing fixture rules** (all bitten in prior waves, now mandatory): in-place capturing
  descriptor reset with the MSVC warning comment; `tmpabilities.str = 100` on fixture chars
  (x86 SIGFPE); fully value-initialized builders/fixture structs (MSVC 0xCC fill); content
  checks not pointer-compares for string emptiness (MSVC literal pooling); POSIX calls via
  `test_platform_compat.h`; char[N]-cast self-check before every commit.

## Chunk map (7 implementation tasks + exit)

1. **F1 — fight.cpp (11 sites):** death messages, syslog lines. Gates: combat goldens +
   existing `damage_tests`/`fight_proc_tests`/`characterization_combat_tests`; new pins only
   where a site's output isn't already golden-covered (plan decides per-site).
2. **D1 — db.cpp loader/boot messaging (~majority of 111 sites):** boot-sequence logs, world
   -file error strings. The boot golden is dense byte-coverage here; per-site pins only for
   fixture-reachable non-boot paths.
3. **D2 — db.cpp save_player + text writer:** new `SavePlayerRoundTrip` suite (see Testing)
   BEFORE any conversion; then convert the writer's format sites. Legacy decoders excluded by
   name.
4. **C1 — comm.cpp misc:** prompt assembly, logging, greeting/connection strings.
5. **C2 — act() alone:** new `ActTokenExpansion` suite pinning the full token surface BEFORE
   conversion. Converted only after F1/D1/D2/C1 are landed and green (riskiest-last).
6. **Sweep:** all three files grep-clean of sprintf/vsprintf/strcpy/strcat or carrying
   written justifications; alloc-site audit against the local-RAII rule.
7. **Exit:** docs baselines; i386 battery; four-platform CI push; **manual telnet account
   smoke** (USV-style nanny() drive — `make smoke-account` remains env-blocked) because
   comm.cpp touches connection/greeting surfaces (AGENTS.md requirement); exit note; final
   whole-branch review; merge decision to the owner.

## Testing

- **New suites:** `src/tests/db_save_roundtrip_tests.cpp` (`SavePlayerRoundTrip`): fixture
  `char_data` → real `save_player` writer → temp path via platform shims → pin exact file
  bytes pre-transform → post-transform byte-identity + reload-equality. 
  `src/tests/comm_act_tests.cpp` (`ActTokenExpansion`): every `$` token ($n/$N/$m/$s/$e/$o…),
  capitalization variants, TO_CHAR/TO_ROOM/TO_VICT/TO_NOTVICT routing, hide-invisible/CAN_SEE
  gating — pinned against the unchanged engine. `fight.cpp` pins only if F1's per-site review
  finds golden-uncovered output (file `src/tests/fight_format_tests.cpp` only if needed).
- Characterization contract carries: tests PASS against unchanged source pre-transform.
- ASan gate per new/extended test file; suites registered in both CMakeLists.txt and
  tests/Makefile WITH header-dependency lines (Wave 3 final-review lesson).

## Risks

- **act() regression = every message breaks:** last-converted, behind token pins, with the
  full 979+ suite and all goldens as the integration net.
- **save_player byte drift = player-data corruption:** round-trip pin gates it; STOP on diff.
- **db.cpp boot-log drift:** boot golden pins it byte-for-byte already.
- **RNG disturbance in fight.cpp:** combat goldens catch any sequence change instantly.
- **MSVC-only surprises:** standing fixture/cast/guard rules target every class that has
  actually bitten (0xCC fill, literal pooling, NRVO descriptor copies, POSIX shims);
  windows-msvc still waits until finalization by design — accepted, documented exposure.

## Exit criteria

All sites in the trio converted or justified in writing; `SavePlayerRoundTrip` +
`ActTokenExpansion` green on all local platforms; boot + combat + JSON goldens byte-identical
throughout; i386 battery green; four-platform CI green; manual account smoke clean; docs
test-count baselines updated; exit note written; final whole-branch review; merge decision to
the owner.
