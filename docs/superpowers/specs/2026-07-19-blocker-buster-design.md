# Blocker-Buster Wave (combat-growth enablers) — Design

Date: 2026-07-19. Status: approved-in-dialogue (owner), pre-implementation.
Wave branch: `arch/blocker-buster` off master @e813138 (post combat-seed merge).
Parent spec: `docs/superpowers/specs/2026-07-16-library-architecture-design.md` §3.
Evidence: `.superpowers/sdd/blocker-census.md` (planning census, @14ac84e) and the combat-seed
wave's DEFER-11 growth inventory in `docs/BUILD.md`.

## Problem / evidence

Growing `rots_combat` beyond its 4-TU seed (toward `fight`, `mobact`, `ranger`, `mystic`,
`spec_pro`, …) is blocked by three enabler classes the combat census identified — none of which is
combat-TU migration work itself:

1. **The visibility family** (app-side `handler.cpp`/`utility.cpp` remainder): `CAN_SEE` (both
   overloads), `CAN_SEE_OBJ`, `get_char_room_vis`, `get_player_vis`, `get_char_vis`,
   `get_obj_in_list_vis`, `get_obj_vis`, `get_object_in_equip_vis`, `generic_find`, `PERS`, and
   the `get_real_OB`/`get_real_parry` pair. Census verdict: **home = L3 `rots_combat`** — zero
   L2-entity and zero L3-world callers exist; `see_hiding` is already in-row (`ranger.cpp`);
   `act` is L1 (`output_seam`); `weather_info`/`world[]` are legal L3-peer references. L2 is
   impossible (deps sit above it); L3-world would invert the coupling.
2. **Command up-calls**: ~19 distinct `do_*` ACMD targets invoked from `mobact` (13 sites),
   `spec_pro` (19), `ranger` (7), `fight` (2). Census-recommended instrument: a **boot-registered
   command-dispatch table** (new `combat_hooks.h`) modeled on the `assign_spell_pointers`
   precedent (parent spec §3): null-initialized cells, populated at boot from the command layer,
   tripwire on unregistered dispatch. NOT per-command output-seam-style hooks.
3. **Uncovered comm surface**: `send_to_room`(+variants), `send_to_all`, `break_spell`,
   `abort_delay`, `complete_delay`, and the txt-pool entry points — all comm.cpp-owned symbols
   fitting `output_seam`'s existing forwarding pattern (extend it; do NOT create a parallel
   header). `fight_messages[]` is a storage-move candidate (db_boot → fight.cpp) staged for the
   wave that migrates `fight.cpp`, not this one.

Independent census finding: the **poison-notification hook** (placement-seam deferral cluster 3)
is a **live-today** need, not a fight-wave dependency — `containment.cpp`/`object_utils.cpp` (L2)
deliberately call the app-side `unequip_char` wrapper for its poison `damage()` side effect
(reachable via `script.cpp`'s `SCRIPT_OBJ_FROM_CHAR`), so the L2→app edge persists until the
notification is hook-inverted in `entity_hooks.h`.

## Decision (owner-approved)

**Enabler-only scope.** Build all four enablers this wave, consumer-free (the proven
ship-the-seam-before-consumers pattern); migrate no DEFER-11 TU. The clerics/fight pilot is
explicitly the NEXT wave, on these seams once proven. Deliverables:

1. **Visibility family → `rots_combat`** (new TU `src/visibility.cpp` in `ROTS_COMBAT_SOURCES`,
   or split across existing seed TUs only if the per-function census argues for it — planning
   census classifies per function; verbatim moves; declarations stay in current headers;
   CombatLayerAcyclicity is the enforcement). `get_real_OB`/`get_real_parry` land beside their
   census-assigned home (with the AGENTS.md trio paragraph updated: the live trio reunites in
   combat-tier). Resolver-variant rule applies to any `world[]` access (per-site original-body
   evidence, placement-seam convention).
2. **Command-dispatch seam**: `src/combat_hooks.h` + a registration TU decision per the
   `assign_spell_pointers` precedent (the command layer populates the table at boot in
   `db_boot.cpp`'s existing registration sequence; parity in `gtest_main.cpp`). Tripwire-null
   cells. The ~19 call sites are NOT converted this wave (their TUs are still app-side — they
   convert as each TU migrates); the seam ships with registration + dispatch + tests proving the
   table resolves to the real commands.
3. **Output-seam extension**: the ~7 forwarders added to `output_seam.{h,cpp}` following its
   existing pattern (null-safe defaults per its established taxonomy; comm.cpp registers the
   real sinks where it registers the current five).
4. **Poison-notification hook**: `entity_hooks.h` gains the notification (fired by the equip
   wrapper's poison path — design detail: the hook inverts the L2 callers' dependence on the
   wrapper, letting `obj_from_char`/`extract_obj` finally move to L2 per their placement-census
   rows with the poison side effect preserved via dispatch; registration in `run_the_game` +
   `gtest_main` parity; tripwire default per the taxonomy — the planning census's hook-shape
   analysis governs the exact signature). Retires placement-seam deferral cluster 3: the two
   functions complete their deferred moves THIS wave (they are the hook's proof-of-concept
   consumers, unlike the other seams).

## Verification

- Zero behavior change: goldens byte-for-byte; ctest baseline **1343** both hosts + whatever the
  standing coverage-gap rule adds (each new seam gets seam-level tests: table-resolves-to-real-
  commands, forwarder-reaches-real-sink, poison-hook-preserves-side-effect — these are the wave's
  test additions; exact totals recorded in docs).
- Per-task both-host gates; nm single-definition per move; all 6 linkchecks green (Combat +
  Entity decisive); macOS ASan on new test files; full i386 battery + whole-branch review + six
  CI jobs at finalization.
- STOP contracts: uncensused edges; any visibility function whose per-function census verdict
  conflicts with the family-level L3-combat call; poison-hook signature surprises.

## Risks

1. Visibility is the hottest read path in the game (CAN_SEE ~106 call sites) — moves are verbatim
   with no dispatch added (direct downward calls once in combat), so no hot-path cost; reviewer
   verifies.
2. The command table's registration ordering vs first combat use — same boot-window analysis as
   every prior registration (pre-boot_db; tripwire makes gaps unmissable).
3. The poison hook changes the L2 callers from wrapper-call to primitive+dispatch — the ONE
   behavior-sensitive item; the mudscript path gets a dedicated characterization test BEFORE the
   inversion (red-proof against the current wrapper behavior, then prove identical through the
   hook).
4. Wave scope is 4 independent seams — tasks are parallelizable in principle but execute
   sequentially per SDD discipline; census surprises in the visibility family are the likeliest
   STOP source.

## Out of scope

- Migrating any DEFER-11 TU (clerics/fight pilot = next wave); `fight_messages` storage move;
  `profs`; the §7 call-site campaign; Stage 2.

## As-built

All four enablers landed, consumer-free, exactly as scoped — no DEFER-11 TU migrated. ctest:
1343 (wave baseline) → **1365** both hosts, ASan clean at every task's gate (reconciled per-task
chain: `AGENTS.md`'s "Testing Guidelines", full citations in `.superpowers/sdd/task-5-report.md`).

1. **Output-seam extension (Task 1)** landed as designed: `output_seam.{h,cpp}` gained the seven
   forwarders, `comm.cpp` registers the real sinks alongside the existing five. One post-review
   fix, not a STOP: the brief's own "match the five" steer for the txt-block-pool forwarder's
   default was wrong — a null-default would have returned a null `txt_block*` straight into
   `comm.cpp`'s `write_to_q()`, which immediately dereferences it. Fixed to a tripwire
   log-then-abort (the `entity_hooks.h` txt-pool pair's own precedent), the one exception to the
   six void forwarders' logged-no-op default. A second, unrelated fix rode along: a pre-existing
   flat-Makefile `SRCS` gap (three test files never in the i386 monolithic runner) was restored.
2. **Command-dispatch seam (Task 2)** landed as designed on the `assign_spell_pointers()`
   precedent, registered from `interpre.cpp`/`db_boot.cpp`'s existing sequence, logged-no-op
   default. **Deviation from the design's own estimate, not the architecture**: the design's "~19"
   target count was wrong in both directions — three census-listed names had no real call site
   (phantoms), and the reconciled union was actually 24, then 25 once a coordinator review caught
   a missing `do_mental` cell (`fight.cpp`'s per-tick mental-combat up-call). No call-site
   conversion happened this wave, exactly as scoped.
3. **Poison-notification hook (Task 3)** landed as designed: characterization-first (the wave's one
   behavior-sensitive item), red-proofed against the pre-inversion wrapper, then proven
   byte-identical through the inverted path — the design's own required proof shape, delivered
   exactly. `obj_from_char`/`extract_obj` completed their placement-seam-era deferred moves to
   `rots_entity` (L2), retiring deferral cluster 3. Zero fix findings at review.
4. **Visibility family → `rots_combat` (Task 4, completed by Task 4b)** landed with one **STOP,
   not a deviation**: the design's census-A input claimed the whole family belonged in
   `rots_combat`, but Task 4's build-wiring verification found `see_hiding` (the 3-arg `CAN_SEE`
   overload's dependency) still lived in `ranger.cpp`, a DEFER-11 `ROTS_SERVER_SOURCES` TU — "in
   the combat row thematically" is not "in `ROTS_COMBAT_SOURCES` today." Task 4 correctly STOPped
   and documented rather than forcing the move (7 of 12 landed; 5 stayed, with `generic_find`'s
   `search_block()` uncensused edge found the same way). Task 4b (a controller-dispatched
   follow-on, not sketched as a separate task in this design) closed both gaps: `see_hiding`
   carved out of `ranger.cpp` into `visibility.cpp`, `search_block()` relocated to
   `rots_util.cpp`/`rots_platform`, completing all 12/12. `get_real_OB`/`get_real_parry` landed in
   `visibility.cpp` alongside the family, reuniting the OB/parry/dodge trio entirely in library
   code (L2 `get_real_dodge` + L3 `get_real_OB`/`get_real_parry`) — the design's own framing ("the
   live trio reunites in combat tier"). `rots_combat` picked up its first genuine L3-peer link
   (`RotS::world` PUBLIC, for `weather_info`) as a consequence, not a separate design decision.

No STOP blocked delivery; the two census corrections (command-dispatch count, visibility-family
build-wiring) were absorbed as documented deviations per Auto Mode discipline, not treated as
scope failures. See `docs/BUILD.md`'s "`rots_combat`" section (the blocker-buster subsection) for
the full seam-by-seam account and the census-errata record.
