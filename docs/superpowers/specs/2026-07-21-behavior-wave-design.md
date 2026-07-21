# Behavior Wave (`mobact.cpp` → `rots_script`; `limits.cpp` → `rots_combat`) — Design

**Date:** 2026-07-21 · **Branch:** `arch/behavior`, off master @l4-seed-tip (treat master-to-be as
the l4-seed wave's finalized tip; that wave is merging in parallel).
· **Predecessors:** the l4-seed wave (stood up the L4 band — `rots_pathfind`{graph.cpp} +
`rots_script`{mudlle.cpp/mudlle2.cpp/script_hooks.cpp}; `zone.cpp` → `rots_world`; eight libraries,
eight `*LayerAcyclicity` linkchecks), the combat-trio/combat-pilot/combat-seed waves (`rots_combat`
4 → 6 → 8 → 11 TUs), and the migration recipe in `docs/superpowers/combat-migration-playbook.md`.
This wave applies that recipe to the **two owner-selected behavior TUs** — the mob-AI driver
(`mobact.cpp`) and the tick/limits engine (`limits.cpp`) — landing them in **different existing
libraries** (no new library targets, no new linkchecks this wave).

## Problem / evidence

Two combat-row DEFER TUs are the owner-settled next carve. Both are now unblocked because the L4
band exists: `mobact.cpp`'s `find_first_step`/`intelligent` edges resolve into `rots_pathfind`/
`rots_script`, and `limits.cpp`'s `extract_char`/`gain_exp`/`Crash_crashsave` families already have
seams from prior waves.

- **`mobact.cpp`** (479 lines; combat-census row `L2-app=2/app-output=1/app-command=12/app-session=1/
  app-other=2`). Source-grepped shape at this base (behavior-wave T0 re-derives with `nm`):
  - **All twelve `do_*` up-calls land on the existing 26-cell `combat_command` table** —
    `do_say`/`do_assist`/`do_stand`/`do_rescue`/`do_hit`/`do_flee`/`do_wear`/`do_move`/`do_wake`/
    `do_sleep`/`do_rest`/`do_sit` (distinct set confirmed by `grep -oE 'do_[a-z]+\(' mobact.cpp`).
    **Zero new cells.** Reached downward (L4→L3) once mobact is `rots_script`.
  - `intelligent` (mudlle.cpp:142) → **intra-`rots_script`** the moment mobact joins.
  - `find_first_step` (graph.cpp, :414) → **downward** L4-upper→L4-lower (`rots_script → rots_pathfind`).
  - `CAN_SEE` (visibility.cpp/`rots_combat`), `forget` (entity_lifecycle.cpp/L2, mobact:392),
    `get_confuse_modifier` (**entity_lifecycle.cpp:2568/L2**, mobact:409 — a resolved edge the census
    row did not itemize), `mudlog_aliased_mob` (char_utils.cpp:1814/L2), `char_exists`, `obj_*`,
    `act`, `number`, `mob_index`/`world` (L3-world) — **all resolve downward.**
  - **`virt_program_number` (spec_ass.cpp:315) is a NEW blocker the census mobact row never
    enumerated** — a `void*`-returning switch that returns pointers to dozens of `spec_ass`/
    `spec_pro` spec-procs; it **cannot relocate** (drags the whole spec-proc family, all still-app).
    mobact calls it at :64/:126. `spec_ass.cpp` stays app-tier, so this is a genuine `L4 → app`
    upward edge and the wave's **mobact-side named risk** (hook, see the taxonomy below).
  - `no_specials` (comm.cpp global, **read-only** at mobact:122) — disposition needed.
  - `_buf` retirement (mobact:91/123/209/218/235/329/398, local-composition). Dead decls to remove:
    `ACMD(do_get);` (:36, never called), the duplicated `do_stand`/`do_sleep`/`do_rest`/`do_sit`/
    `do_wake` block (:440-444), and the `do_*` forward decls once converted.
- **`limits.cpp`** (1633 lines; combat-census row `L2-app=7/app-output=4/app-command=1/app-session=4/
  app-other=4`). Prior-wave seams already cover: `extract_char` (entity_hooks L2 re-home), `extract_obj`/
  `stop_riding` (L2), `send_to_all`/`send_to_room` (output_seam), `do_flee` (flee cell),
  `add_exploit_record` (persist dispatch), `is_target_valid`/`on_character_died` (entity_hooks),
  `pkill_create` + `Crash_crashsave` (combat_hooks), and the `gain_exp`/`gain_exp_regardless`/
  `remove_fame_war_bonuses` hooks **limits.cpp itself registers** (combat-pilot T4b). Remaining real
  work, body-confirmed at this base:
  - **`Crash_*` ×3 CONFIRMED distinct** (objsave.cpp): `Crash_crashsave` (:939, HAS a combat_hooks
    hook), `Crash_idlesave` (:980, **no hook**), `Crash_extract_objs` (:898, **no hook**). Two new
    sibling hooks needed.
  - **`char_from_room`** (handler.cpp:349) — body calls `stop_fighting` (fight.cpp/`rots_combat`,
    L3), which **bars a clean L2 relocation** (an L2 body may not call L3). Disposition below.
  - **`recalc_zone_power`** (handler.cpp:762) / **`report_zone_power`** (handler.cpp:791, also called
    by mage.cpp:555) — zone-power accounting → `rots_world` (L3) candidates.
  - **`affect_remove_notify`** (handler.cpp:209) — body is `vsend_to_char` (L1) + `affect_remove`
    (L2), **both downward** → RELOCATE-CLEAN to L2 (the easiest of the four).
  - **`close_socket`** (comm.cpp, limits:609) → output_seam forwarder (comm-owned taxonomy).
  - **`circle_shutdown`** (comm.cpp global, **WRITTEN** at limits:656 `circle_shutdown = 1;`) — a
    write, so a read-only accessor is insufficient.
  - **`saves_spell`** (spell_pa.cpp:180) — still-app `spell_pa` DEFER TU; the leading candidate of
    the "combat-peer=10 (re-derive)" tally (RELOCATE-CLEAN to L2 per the `saves_*` precedent, or
    hook — T0 nm + body-read).
  - `_buf` retirement; `do_flee` → flee cell.

## Decision (owner-approved)

**Scope: `mobact.cpp` joins `rots_script` (L4-upper, 4th TU) and `limits.cpp` joins `rots_combat`
(L3, 12th TU).** No new library targets, no new linkchecks — the two existing linkchecks
`ScriptLayerAcyclicity` and `CombatLayerAcyclicity` are the membership gates. Certified layer order
(unchanged, every link unidirectional, **no bidirectional peer links ever**):

```
platform < core < entity < persist < world < combat < pathfind < script < app
   L0       L1      L2        L3        L3      L3        L4-lower   L4-upper   app
```

- **`mobact.cpp` → `rots_script`.** The engine/driver pairing rationale (owner-settled over a new
  sibling lib): the mob-AI activity driver belongs with the scripting engine (`mudlle`) it invokes —
  `intelligent()` becomes an intra-lib call, `find_first_step` a downward `script → pathfind` call,
  and the twelve `do_*` up-calls downward-legal `script → combat` cell dispatches. `rots_script`'s
  existing PUBLIC link set (`RotS::pathfind` + the full L3 downward set) covers every mobact edge;
  **no new link edge is needed.**
- **`limits.cpp` → `rots_combat` (12th TU).** A standard L3 DEFER-row promotion. `rots_combat`'s
  existing PUBLIC link set (platform/core/entity/persist/world) covers every resolved limits edge;
  **no new link edge is needed** (its `RotS::persist` link, added in combat-pilot T5, already carries
  the `add_exploit_record` edge).

### The `mobact` ↔ `limits` cross-edge (the wave's STOP-class closure question — RESOLVED)

The corrected closure methodology requires checking cross-edges between co-promoting TUs. **It
fires here, once:** `limits.cpp:1398` (inside SPELL_ACTIVITY affect processing) calls
`one_mobile_activity(i)` — defined in `mobact.cpp`. `one_mobile_activity` has **exactly one external
caller** (limits) besides mobact's own self-call at :61. Once `mobact` is `rots_script` (L4) and
`limits` is `rots_combat` (L3), `limits → one_mobile_activity` is an **`L3 → L4` upward edge** — and
unlike the combat-trio one-directional gate (which was intra-lib and dissolved for free), this edge
crosses a tier boundary that **does not move**, so it can **never** be a direct call. It must be
**inverted permanently.**

- **Disposition: a new `combat_hooks.h::dispatch_one_mobile_activity(char_data*)` hook** —
  `using mobile_activity_fn = void (*)(char_data*)`, void → **loud-tripwire** default. Backing
  storage in `combat_hooks.cpp` (L3, `rots_combat` — the DISPATCHER's own lib, matching the
  storage-in-the-consuming-lib shape). **Registered by `mobact.cpp`** (a legal `L4 → L3` downward
  registration; app-side boot sequence calls the registrar). **Dispatched by `limits.cpp`**
  (intra-lib once limits is `rots_combat`). mobact's own `:61` self-call stays a direct intra-lib
  call.
- **`mobact` never calls back into `limits`** (verified: no `gain_exp`/limits symbol in mobact) — so
  this is a one-directional gate, not a cycle. The two memberships remain **independently gateable**:
  the seam lands consumer-free in T1, mobact's registration and limits' dispatch both convert in T2,
  and the two membership commits (T3) have **no hard ordering** between them.

### Hook taxonomy: two inversions

- **`virt_program_number` inversion (mobact-side).** A `void*`-returning dispatcher (spec_ass.cpp:315)
  whose real body references dozens of still-app spec-procs and cannot relocate. New
  **`script_hooks.h`** cell (rots_script's own upward seam, alongside `command_interpreter`/`PERS`),
  `using virt_program_fn = void* (*)(int)`. Pointer-returning → **abort-tripwire** class (the
  `PERS`/`mudlle_converter` precedent: no safe placeholder pointer to return). Registered app-side by
  `spec_ass.cpp` (the definition owner) pre-`boot_db`. `comm.cpp:2671`/`interpre.cpp:1536-1546`'s own
  calls stay direct app→app — only mobact's edge inverts.
- **`one_mobile_activity` inversion (the cross-edge, above)** — `combat_hooks.h`, void → loud-tripwire,
  registered by mobact, dispatched by limits.

### Parent-spec §3 downstream note (as-built, not a revision)

The l4-seed §3 REVISION already left `mobact`/`spec_pro`'s honest future home open, noting both had
zero-seam downward access to `find_first_step`/`intelligent`/`command_interpreter`. This wave
**answers that question for `mobact`**: its tier is `rots_script` (L4-upper), decided by re-running
the closure check at its own promotion time exactly as that note prescribed — the driver homes with
the engine it invokes. `spec_pro`/`spec_ass` remain undecided; `virt_program_number`'s inversion is a
new data point for their eventual promotion (spec_ass now also owns a hook registration).

## Adjudication defaults (T0 confirms or overturns with `nm`/body-read evidence)

- **`one_mobile_activity`** — new `combat_hooks.h` hook (void, loud-tripwire), storage in
  `combat_hooks.cpp`, registered by mobact, dispatched by limits. **Mandatory inversion** (upward
  cross-tier edge); no relocation alternative.
- **`virt_program_number`** — new `script_hooks.h` cell, `void*(int)`, abort-tripwire, registered by
  `spec_ass.cpp`. Confirm the one live mobact edge (`comm.cpp`/`interpre.cpp` callers stay direct).
  **Rider gate (controller PRE-AUTHORIZED bounded extension):** if T0's `nm` run surfaces
  *additional* mobact→spec-proc edges of the **same shape** (a `void*`/spec-proc-dispatcher class
  function whose body drags still-app spec-procs), **extend the same `script_hooks.h` abort-cell
  pattern for up to THREE total such edges without stopping** — each with its own discriminator pair
  and disposition row. **A fourth same-shape edge, OR any edge of a different shape (a data global, a
  non-dispatcher function), is an auto-STOP** for controller adjudication.
- **`no_specials`** (comm.cpp, read-only from mobact) — **a read accessor forwarder in the comm-owned
  seam (`output_seam`) (controller-CONFIRMED; NOT a storage-move).** Rationale: session/lifecycle
  state stays with its owning subsystem (comm). The `waiting_list` storage-move (combat-pilot) is
  **not** the precedent to repeat here — it was *link-FORCED* (an lvalue-mutated global a fn-ptr hook
  cannot wrap), and its thematic misplacement into `clerics.cpp` was flagged at review; the accessor
  is the lesson-learned shape. T0 still enumerates writers/readers to confirm mobact's use is
  read-only.
- **`char_from_room`** — **HOOK at L2 `entity_hooks.h` (controller-CONFIRMED; the default stands).**
  The `extract_char` re-home precedent — a single L2 inversion any tier dispatches downward through.
  Its `stop_fighting` (L3-combat) body call bars a clean L2 *body* relocation, so the real body stays
  app-tier (calls `stop_fighting` downward), registered app-side; limits dispatches down. **Rejected
  alternative: an L3 `rots_combat` relocation (the `get_guardian_type` precedent).** It is rejected
  because `zone.cpp` is now `rots_world`, so if T0's caller enumeration finds a world-tier caller of
  `char_from_room`, an L3-combat body would force a `rots_world → rots_combat` link — the forbidden
  bidirectional peer edge. Robust-to-multi-tier wins over the marginally cheaper relocation. **T0 still
  enumerates all callers** (the hook is safe regardless, but an unexpected caller is worth recording).
- **`recalc_zone_power`/`report_zone_power`** — **default: RELOCATE to `rots_world` (L3)** (zone-power
  accounting reading world/zone data; limits→world and mage→world both downward). T0 body-reads for
  any L3-combat/entity coupling that would bar a world home; hook fallback.
- **`affect_remove_notify`** — **RELOCATE-CLEAN to L2 `entity_lifecycle.cpp`** (body-confirmed:
  `vsend_to_char` L1 + `affect_remove` L2, both downward; sibling of `affect_from_char_notify` already
  there in shape). The cheapest of limits' handler-family edges.
- **`Crash_idlesave`/`Crash_extract_objs`** — two new `combat_hooks.h` cells, siblings of the existing
  `Crash_crashsave` hook, registered app-side by `objsave.cpp`. Confirm signatures
  (`void(char_data*)` / `void(obj_data*)`) and that they are genuinely distinct from `Crash_crashsave`.
- **`close_socket`** — new `output_seam` forwarder (comm-owned taxonomy), void.
- **`circle_shutdown`** (WRITTEN by limits) — **a setter forwarder** (`request_circle_shutdown()` in
  the comm-owned `output_seam`, backed by comm.cpp) since limits *writes* it (controller-CONFIRMED;
  NOT a storage-move). Same rationale as `no_specials`: lifecycle state stays with comm, and the
  `waiting_list` link-forced storage-move is explicitly not the model. T0 body-reads the write path
  and other writers.
- **`saves_spell`** — RELOCATE-CLEAN to L2 (`char_utils_combat.cpp`, the `saves_power`/`saves_*`
  five-pack precedent — reads `char_data` fields) unless T0's body-read finds a lower floor; part of
  the "combat-peer=10 re-derive" (T0 enumerates the rest of that tally with a fresh `nm` run).
- **`gain_exp`/`gain_exp_regardless`/`remove_fame_war_bonuses`** — **leave the hooks and limits'
  self-registration UNTOUCHED.** Once limits is in-lib they are all legal (limits registers intra-lib;
  fight/clerics dispatch intra-lib). Full hook retirement (delete hook + convert fight/clerics callers
  to direct intra-lib calls) is **deferred follow-on** — do NOT delete the self-registration alone
  (that would leave the dispatch unregistered and tripwiring).
- **`_buf` retirement** — genuine globals (not local shadows) in both TUs; local-composition, no
  storage-move (shared across dozens of still-app files).

## Verification

- Byte-verbatim mandate for every body move / relocation. **Both target files are expected mixed-CRLF
  (the l4-seed CRLF map already flagged `handler.cpp`/`utility.cpp`/`comm.cpp`/`act_obj2.cpp` as
  mixed);** verify `mobact.cpp`/`limits.cpp`/every edited file per file before editing and use the
  Python byte-edit method where CRLF or the formatter hook threatens bytes (established method).
- Dispatch-equivalence discriminator (registered-reaches-real-body / unregistered-tripwire pair) for
  **every new hook** — `one_mobile_activity`, `virt_program_number`, `Crash_idlesave`,
  `Crash_extract_objs`, `char_from_room` (if hooked), `no_specials`/`circle_shutdown` accessors (if
  hooked), `close_socket` — and a real audit (not an assumption) that the twelve `do_*` cells and the
  `send_to_all`/`send_to_room` forwarders already have coverage; add only genuine gaps.
- Per-task gates both hosts: macOS arm64 + `rots64` build + full ctest (**1415 baseline** + this
  wave's additions), **all eight** `*LayerAcyclicity` linkchecks green (the six existing + pathfind +
  script — no new ones), both boot goldens, `ConvertEquivalence` 17/17,
  `python3 tools/string_view_census.py --check` exit 0. New/rewritten test files add the
  `macos-arm64-asan` preset. i386 battery at finalization only.
- Characterization goldens (`CharacterizationCombatTest.*`, `CharacterizationJson.*`,
  `PoisonRemovalScriptTest.*`, `boot-golden.sh verify`) pass **unchanged** — drift = a bug.
  **`mobact` is directly boot-golden-exercised** (mob activity, aggression, hunting, helper/bodyguard
  AI) and **`limits`** drives the tick loop (affect wear-off, autosave, zone power), so the boot
  golden is a real regression net for this wave, not a formality.

## Risks

- **The `one_mobile_activity` cross-edge is the wave's defining coupling.** It permanently inverts a
  live mob-AI entry point across the L3/L4 boundary. Its tripwire must be genuinely loud, the
  discriminator must prove `char_data*` forwards intact, and mobact's registration must run in the
  pre-`boot_db` sequence — a registration-ordering slip silently disables the SPELL_ACTIVITY mob
  re-activation path (boot golden is the backstop).
- **`virt_program_number` is a census miss surfaced by source-grep, not the census row** — treat T0's
  `nm` re-run as authoritative; if it finds *more* than one mobact edge into still-app spec-proc
  machinery, STOP and adjudicate (the mob-program dispatcher may pull additional spec_ass symbols).
- **`char_from_room`'s caller set is the STOP-risk for limits.** If T0 finds a `rots_world`
  (`zone.cpp`) or `rots_entity` caller, neither the L2-hook nor the L3-relocation defaults hold
  unchanged — STOP and adjudicate the tier before choosing.
- **`recalc_zone_power`/`report_zone_power` relocation target** depends on their bodies; if either
  calls a `rots_combat`/`rots_entity` symbol that bars a `rots_world` home, they fall to a hook.
- **No `fight.cpp` blast radius** — neither TU is the melee hub; mobact is an AI driver, limits is the
  tick engine.

## Out of scope

- No new library target and no new linkcheck — this is a two-membership growth wave, not a band
  addition.
- Full retirement of the `gain_exp`/`gain_exp_regardless`/`remove_fame_war_bonuses` hooks (requires
  converting fight.cpp/clerics.cpp callers to direct intra-lib calls first) — deferred follow-on.
- No other DEFER TU (`spec_ass`/`spec_pro`/`ranger`/`mage`/`spell_pa`) — they stay app-compiled; DEFER
  drops 7 → 5. `virt_program_number`'s inversion and `saves_spell`'s relocation are recorded data
  points for their eventual promotion.
- No int→double combat-math changes; no new smoke/determinism infrastructure.

## As-built (Tasks 0-3 complete; Task 4 docs, this section; Task 5 finalization pending)

**Status: both memberships landed, both `CombatLayerAcyclicity`/`ScriptLayerAcyclicity` green first
try, both hosts, zero census misses.** `limits.cpp` → `rots_combat` (11 → 12 TUs, commit `79dadb4`);
`mobact.cpp` → `rots_script` (3 → 4 TUs, commit `5df751e`). DEFER drops **7 → 5** (`spec_ass`/`mage`/
`spell_pa`/`ranger`/`spec_pro`). ctest 1415 → 1446 (Task 1 +19, Task 2 +12, Task 3 +0). Full
per-task evidence: `.superpowers/sdd/bw-task-{0,1,2,3}-report.md`; census: `.superpowers/sdd/
bw-census.md`.

**Task 0's one OVERTURN of this design spec's own default: `saves_spell`.** The "Adjudication
defaults" section above specced RELOCATE-CLEAN to L2 `char_utils_combat.cpp` (the `saves_power`
five-pack precedent). Task 0's body read found `saves_spell()` writes `spllog_mage_level`/
`spllog_save`/`spllog_saves` — globals storage-moved into `fight.cpp` (`rots_combat`, L3) back in
the combat-pilot wave — and `rots_entity`'s own `CMakeLists.txt` link line does not PUBLIC-link
`RotS::combat`; an L2 home would be an illegal upward write-dependency, the same class of violation
the l4-census's `equip_char`/`pkill_get_good_fame` OVERTURNs already established. **Corrected
disposition: RELOCATE to L3 `rots_combat`** (`fight.cpp`, beside the `spllog_*` storage it writes),
not L2. Every other adjudication default CONFIRMED exactly as specced (`one_mobile_activity`,
`virt_program_number`, `no_specials`, `char_from_room`, `recalc_zone_power`/`report_zone_power`,
`affect_remove_notify`, `Crash_idlesave`/`Crash_extract_objs`, `close_socket`,
`circle_shutdown`, `gain_exp` family, `_buf` retirement).

**Two resurfaced spec-adjudication gaps.** This design spec's "Remaining real work" prose dropped two
items that `combat-census.md`'s original `limits` row already carried, giving them no adjudication
default: `game_rules::big_brother::on_character_afked`/`on_corpse_decayed` (limits.cpp, 2 call
sites — no existing hook covered these two methods, only the pre-existing `target_valid_fn`/
`character_died_fn` pair) and `pkill_get_rank_by_character` (limits.cpp:1252 — genuinely
RELOCATE-CLEAN to `rots_persist`/`db_players.cpp`, since its backing storage was already
persist-tier, unlike the l4-census's `pkill_get_good_fame`/`evil_fame` OVERTURN whose storage stayed
app-tier). Task 0 dispositioned both (§6 of `bw-census.md`) before Task 1 built them; neither was
STOP-class.

**The rider gate closed at exactly 1, not the pre-authorized ceiling of 3.** `virt_program_number`'s
full enumeration (every call `mobact.cpp` makes cross-referenced against every function defined in
`spec_ass.cpp`/`spec_pro.cpp`) found exactly one same-shape edge (`mobact.cpp:64`/`:126`, both into
`spec_ass.cpp:315`) and zero 2nd/3rd/4th edges of the same or a different shape. No auto-STOP fired.
This is a genuine data point for `spec_ass`/`spec_pro`'s own eventual promotion (spec_ass's
combat-peer=39 tally is dominated by spec_pro references) — the mechanism is proven at 1 of 3 uses,
not exhausted.

**Task 3's memberships landed first-try clean, both commits.** `limits.cpp`'s membership commit
needed zero census misses — every up-call it carried had already been converted onto a hook/seam in
Task 2, so the membership move was a pure `CMakeLists.txt` source-list edit. Same for `mobact.cpp`.
This is the third consecutive wave (after combat-trio and l4-seed) to achieve a zero-census-miss
membership gate, further validating the census → closure check → seams → conversions → membership →
verification recipe order this playbook has followed since the combat-pilot wave's own STOP forced
the restructure.

**The STAYED verdict (Task 3's cleanup step).** The `gain_exp`/`gain_exp_regardless`/
`remove_fame_war_bonuses` hooks and `limits.cpp`'s own self-registration were verified — by grep, not
assumption — to still have live dispatch consumers even after `limits.cpp` became intra-lib with
`fight.cpp`/`clerics.cpp`: `fight.cpp` (six call sites — `fight.cpp:936`/`1084`/`1086`/`1116`/
`1330`/`1974`, correcting an in-file comment that said "five") and `clerics.cpp` (one site,
`clerics.cpp:248`) still dispatch through `rots::combat::gain_exp()`-style calls rather than calling
`limits.cpp`'s now-intra-lib globals directly. Per the plan's explicit rule ("if dispatch consumers
remain, the hooks and registrations STAY; delete nothing"), Task 3's third commit is comment-only
(four stale `combat_hooks.h` banners updated to document the finding, not a deletion). Converting
`fight.cpp`'s/`clerics.cpp`'s call sites back to direct `limits.cpp` calls (now legal, since both
sides are intra-lib) remains a deferred follow-on simplification, unchanged from this design spec's
own "Out of scope" framing above.

See `docs/BUILD.md`'s "The behavior wave" subsection (under "Library layering"),
`docs/superpowers/specs/2026-07-16-library-architecture-design.md`'s §3 REVISION downstream note
and §10 "step 4 ninth slice," and `docs/superpowers/combat-migration-playbook.md`'s "The behavior
wave" section for the full cross-referenced account.
