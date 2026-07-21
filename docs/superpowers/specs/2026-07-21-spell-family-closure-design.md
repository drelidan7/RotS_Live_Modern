# Spell-Family Closure Wave (`spell_pa.cpp` + `mage.cpp` + `ranger.cpp` → `rots_combat`) — Design

**Date:** 2026-07-21 · **Branch:** `arch/spell-family`, off master @`92ba890` (Cluster B merged).
· **Predecessors:** every prior combat wave (`rots_combat` at 12 TUs) and the migration recipe in
`docs/superpowers/combat-migration-playbook.md` — this wave is the playbook's own "Ambitious tier"
row: the spell-casting family promoted as ONE closed set, the intra-subset rule at full scale.
· **Program context:** this is **Wave A of the owner-approved combat-row completion program**
(DEFER 5 → 0 across two back-to-back waves). Wave B (`spec_pro` → `spec_ass`) has its charter in
§Wave B below and proceeds under the owner's explicit autonomous-spec-gate + merge-when-green
grants, its spec written against this wave's as-built outcome.

## Problem / evidence

`spell_pa.cpp` is the spell-casting registrar and `do_cast` hub: its combat-census row shows
**combat-peer=76** — nearly every edge a same-family cross-reference into `mage.cpp`/`mystic.cpp`/
`ranger.cpp`/`battle_mage_handler.cpp`/`clerics.cpp`/`fight.cpp`. Per the playbook's intra-subset
rule those edges only dissolve when every still-app member of the family promotes **in the same
membership commit**. With `mystic`/`battle_mage`/`clerics`/`fight` already in-lib, the closed set
is exactly `spell_pa` + `mage` + `ranger`. Promoting them takes `rots_combat` 12 → 15 TUs and the
combat DEFER list 5 → 2.

The playbook's per-TU rows for these three predate four merged waves and are **known-stale in the
wave's favor**; the T0 census re-derives everything with `nm` at the wave base. Edges already
resolved by prior waves (T0 confirms each): `waiting_list` (storage-move to `clerics.cpp`,
combat-pilot), `add_follower` + `stop_follower`/`stop_riding`/`obj_from_char` (L2, combat-trio /
placement-seam), the `saves_*` five-pack + `saves_power` (L2), `record_spell_damage` +
`check_break_prep` (in-lib `fight.cpp`), `is_target_valid`/`on_character_died` (entity_hooks),
`char_from_room` (entity_hooks, behavior wave), `report_zone_power` (`rots_world`, behavior wave),
`break_spell`/`send_to_room`/`abort_delay`/`complete_delay`/`get_from_txt_block_pool`
(output_seam, blocker-buster), the visibility family incl. `see_hiding` (in-lib, blocker-buster),
`do_flee`/`do_trap`-class ACMD up-calls (the 29-cell `combat_command` table).

Genuinely open dispositions (the census adjudicates each; defaults below):

- **`show_tracks` (ranger.cpp's edge into `graph.cpp` = `rots_pathfind`, L4) — the marquee
  edge.** Ranger at L3 calling L4 is an upward edge across a tier boundary that never moves.
  RELOCATE-or-hook: if `show_tracks`' body is display logic over graph state reachable from L3,
  relocate it down; if it is genuinely pathfind-internal, invert via a `combat_hooks.h` hook that
  `graph.cpp` (lib) registers — the codebase's **third permanent L3→L4 inversion**, same legal
  registration shape as `call_trigger`/`one_mobile_activity`.
- **Interpre parse helpers** `report_wrong_target`/`target_from_word` (spell_pa) — body-read;
  RELOCATE if parse-pure, hook if session-coupled.
- **Mage's act_info display helpers** `do_look`/`do_identify_object`/`list_char_to_char` and
  act_move's `prohibit_item_stay_zone_move` — ordinary helpers, NOT ACMD cells (`do_look`'s use
  here is a direct helper call, per the census row's own note); body-read each.
- **`msdp_room_update`** (protocol/session) — likely output_seam-class forwarder or hook.
- **Ranger's ~9 door/move parse helpers** (act_move family) — body-read each; expected a mix of
  RELOCATE (parse-pure) and hook (session/door-state-coupled).
- **db_boot globals** (~4 remaining in ranger's app-session tally) — read accessors or
  storage-moves per the established `no_specials`/`circle_shutdown` precedents.
- **Color-sequence pair** (`_color_sequence` storage + `get_color_sequence`, color.cpp) —
  accessor or storage-move per body-read.
- **`descriptor_list`** (spell_pa's app-session read) — the established zone-populated-style
  accessor shape (`world_hooks`/`output_seam` per owner taxonomy of the reading TU).
- **`_buf` retirements** in all three TUs (local composition, the standing method).

## Decision (owner-approved)

**One wave, playbook task shape, ONE membership commit for all three TUs.** `rots_combat` grows
12 → 15; **no new library, no new linkcheck** — `CombatLayerAcyclicity` (both hosts, then i386)
is the membership gate. Task structure: T0 census (read-only; the largest yet — 3 TUs and the
76-peer hub re-derivation) → T1 seams/relocations consumer-free → T2 `spell_pa.cpp` conversions
→ T3 `mage.cpp` + `ranger.cpp` conversions → T4 the joint membership commit → T5a docs + the
carried sweep backlog → T5b finalization (battery → PR → merge under the per-wave grant).

Adjudication defaults (T0 confirms or overturns with `nm`/body-read evidence): the table above;
plus — any `do_*` up-call found beyond the 29 cells gets a new cell only on a confirmed real call
shape (dismount precedent); any spec-proc-dispatcher edge consumes the rider gate's LAST slot
(2 of ≤3 used — a second new one is an auto-STOP); a genuinely new seam taxonomy is an auto-STOP.

Carried riders: the cluster-b whole-branch review's deferred src-comment sweep (fight.cpp:3272-74
/:3358-60 + combat_hooks.h:330-336 stale `call_trigger` "app-tier permanently" banners;
editor_hooks.{h,cpp} stale "starts in ROTS_SERVER_SOURCES" banner; the uncommented
test-recording-globals family) rides T5a.

**Flat-Makefile parity is a BINDING per-task rule this wave (owner-directed, from the cluster-b
battery's two catches): every source-list change to `src/CMakeLists.txt` must land its flat
counterpart in the SAME commit** — a new production TU goes into `src/Makefile`'s AND
`src/tests/Makefile`'s object lists; a new test file goes into `src/tests/Makefile`'s `SRCS`; a
new linkcheck target joins the root `Makefile`'s explicit `test`-target build list (not expected
this wave — no new library). The i386 battery's flat/monolithic steps are the only builds that
exercise these files, so a missed pairing surfaces ~65 minutes into finalization instead of at
the task gate; each task's implementer verifies the pairing explicitly (grep, not assumption) and
each task's reviewer treats a CMake-only source addition as an Important finding.

## Wave B charter (spec_pro → spec_ass, autonomous under owner grants)

After this wave merges, Wave B proceeds without owner review gates, under the explicit grants of
2026-07-21: autonomous-spec-gate (its spec/plan written against this wave's as-built, committed
on `arch/spec-pair`) and merge-when-green. Scope: `spec_pro.cpp` promotes first (its home tier —
`rots_combat` vs the L4 band — adjudicated at its own T0 census, the mobact precedent; its
combat-peer=17 shrinks by whatever this wave dissolves), then `spec_ass.cpp` (combat-peer=39,
dominated by spec_pro). **Pre-authorized new seam family:** spec_ass's three spec-proc
*registrar* edges — `gen_board` (boards), `postmaster` (mail), `receptionist` (objsave), fn-ptr
registrations INTO other subsystems — exactly that 3-edge shape; a 4th registrar edge or any
different coupling class is an auto-STOP. The `void*`-dispatcher rider gate has ONE slot left
(virt_program_number, virt_assignmob used 2 of ≤3); a second new same-shape edge beyond it is an
auto-STOP. DEFER reaches 0 when both land; the combat row is done.

## Verification

Per-task: macOS arm64 + `rots64` builds, full ctest, characterization goldens, both boot goldens,
`string_view_census --check`, ASan preset on any new/substantially-rewritten test file. Spell
paths are boot-golden-light, so the coverage-gap rule is expected to fire: relocated/converted
spell-family bodies that surface untested get targeted ctest riders. The combat smoke harness
stays capture-only/informational. Finalization: the sequential i386 battery (three steps,
per-commit markers), monolithic reconciliation (1468 baseline; ctest-only linkcheck delta stays
9), all six blocking CI jobs; then the pre-authorized ff-merge. Test-count deltas tracked per
task and reconciled in T5a, the AGENTS.md chain style.

## Risks

- **The 76-peer re-derivation is the largest census surface yet**; the mitigations are the
  playbook's census-methodology correction (build-wiring ground truth, not thematic rows) and
  the joint-commit structure (a missed intra-family edge fails `CombatLayerAcyclicity` at T4,
  not at runtime).
- **The ~15 body-read dispositions** (parse/display/door helper families) are the cost-variance
  driver; all have known seam shapes, so they add work, not STOPs.
- **`show_tracks`** may become a third permanent inversion — architecturally sanctioned by the
  two precedents, but each permanent inversion is recorded in BUILD.md and the parent spec at
  landing time, with the hook-owner-promotion banner lesson applied (no "app-tier permanently"
  claims anywhere near it).
- **Three-TU joint membership means a bigger single gate**: a T4 linkcheck failure bisects by
  temporarily staging membership per-TU locally (never committed) to attribute the missing edge.

## Out of scope

Wave B's implementation (charter only, above); any change to spell behavior, mana/skill
formulas, or combat outcomes (byte-verbatim moves + seam conversions only, goldens unchanged);
Stage 2 LocationSystem; the rots_commands census; int→double.

## Process

Subagent-driven: Sonnet implementers, Opus census/heavy reviews, Fable whole-branch review.
Briefs/reports/census in `.superpowers/sdd/` (never committed). Python byte-edits for all
existing `.cpp`/`.h`. Docker gates synchronous in subagents. i386 battery finalization-only.
Owner grants for THIS program (explicit, 2026-07-21): merge-when-green for both waves +
autonomous-spec-gate for Wave B. No authority carries beyond this program.
