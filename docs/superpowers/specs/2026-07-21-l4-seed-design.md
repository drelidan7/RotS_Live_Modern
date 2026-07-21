# L4-Seed Wave (rots_pathfind + rots_script stand up an L4 band; zone.cpp reset half → rots_world) — Design

**Date:** 2026-07-21 · **Branch:** `arch/l4-seed`, off master @b6f6b76 (post combat-trio merge)
· **Predecessors:** the world-seed wave (`rots_world` stood up, 3 TUs), the combat-seed/combat-pilot/
combat-trio waves (`rots_combat` 4 → 6 → 8 → 11 TUs), and the migration recipe finalized in
`docs/superpowers/combat-migration-playbook.md`. This wave applies that recipe's census → closure →
seams → conversions → membership shape **one tier up**: it stands up the **first two libraries of a
new L4 orchestration/authoring band** above the L3 peer tier, and rides one independent L3 growth
(`zone.cpp`).

## Problem / evidence

Five candidate TUs from the world-growth census (`.superpowers/sdd/world-growth-census.md`,
2026-07-20 @b6f6b76) are the natural next carve: `graph.cpp` (pathfinding), `mudlle.cpp` +
`mudlle2.cpp` (the mob-program/script engine), and `zone.cpp`'s reset half. The census's own §9
recommends the self-closed 3-TU **Cluster A** `{graph, mudlle, mudlle2}` plus the fully-independent
`zone.cpp` rider (§6: `zone.cpp` has **zero** intra-candidate edges in either direction). Cluster B
(`script.cpp` + the six `shape*.cpp` OLC editors) is explicitly out of scope this wave — its
`shapemdl.cpp → mudlle_converter` bridge dissolves for free via the existing
`world_hooks.h::dispatch_mudlle_converter` seam (census §6), so A and B are separately schedulable.

The census's **central architectural finding (§7)** was that promoting graph/mudlle **into
`rots_world`** would force `rots_world` to PUBLIC-link `RotS::combat` — and since `rots_combat`
already PUBLIC-links `RotS::world` (blocker-buster Task 4, `visibility.cpp`'s `weather_info`), that
would create the codebase's **first bidirectional L3-peer edge**. This wave does **not** take that
path. The owner-settled architecture (below) puts pathfind and script in a band *above* combat, so
their `hit()`/`get_char_vis()`/`update_pos()`/`CAN_SEE()` calls (census §2/§3, all into already-in-lib
`rots_combat` members) become legal **downward L4→L3** edges — §7's bidirectional question is
dissolved, not answered, for graph and mudlle. The combat coupling does not vanish, though: it
lands on `zone.cpp` instead (see the zone adjudication below).

Per-TU census tallies re-derived below in T0; the source-grepped shape confirmed at @b6f6b76:

- **`graph.cpp`** (census §2: 21 resolved, 4 blocking) — `hit()` (graph.cpp:246, raw 3-arg
  melee-execution, fight.cpp/`rots_combat`), `get_char_vis` (graph.cpp:175, visibility.cpp/
  `rots_combat`) resolve **downward** once graph is L4. `do_say` ×2 (:232/:240) + `do_move` (:244)
  are app-command ACMDs — convert to the existing `combat_command::say`/`move` cells
  (`combat_hooks.h`, `rots_combat`, reached downward). `_arg`/`_buf` (db_boot globals) retire to
  locals. **Zero new seam infrastructure** — its entire cost is 3 existing-cell conversions + 2
  bounded retirements, all enabled by the L4 tier placement.
- **`mudlle.cpp`** (census §3: 44 resolved, 5 blocking) + **`mudlle2.cpp`** (7 resolved, 3 blocking)
  — `update_pos`/`CAN_SEE` (mudlle) resolve downward once script is L4. `find_first_step`
  (mudlle.cpp:299 → graph.cpp:108) is the **sanctioned intra-band `rots_script → rots_pathfind`
  edge**. `do_say` → existing `say` cell. `put_to_txt_block_pool` (mudlle.cpp:808, mudlle2.cpp:182)
  needs **one new `output_seam` forwarder**. `_buf`/`_buf2` retire. `PERS` (mudlle2.cpp, utility.cpp)
  is now a **named adjudication with a registered-forwarder default** (see below) — it is a genuine
  `L4 → app` upward edge once mudlle2 is L4, so it must resolve for `ScriptLayerAcyclicity` to pass;
  its failure is the wave's named script-side collapse condition, not a silent surprise.
  **The one heavy new hook:** `command_interpreter(char_data*, char*, waiting_type*)`
  (mudlle.cpp:862, one live call site — :894-940 are commented out) — the **full player-command
  dispatcher**, a script-issues-commands inversion (see the hook taxonomy note below).
- **`zone.cpp`** reset half (census §1: 31 resolved, 5-6 blocking) — fully independent of the other
  four (§6). Its blocking edges reach `rots_combat`/app tiers it cannot link from `rots_world`; see
  the zone adjudication.

## Decision (owner-approved)

**Scope: stand up an L4 orchestration/authoring band above L3 and below the app, seeded with its
first two libraries — `rots_pathfind` (graph.cpp) and `rots_script` (mudlle.cpp + mudlle2.cpp) — and
ride `zone.cpp`'s reset half into `rots_world` (L3) independently.** Certified layer order:

```
platform < core < entity < persist < world < combat < pathfind < script < app
   L0       L1      L2        L3        L3      L3        L4-lower   L4-upper   app
```

**Every link is unidirectional; there are NO bidirectional peer links, ever** — within the L3 peer
tier or the new L4 band. This is an owner hard-rule and the decisive constraint on the zone
adjudication below.

- **`rots_pathfind` (L4-lower):** `graph.cpp` alone. It sits **above** combat because its
  `hunt_victim()` driver calls `hit()`/`get_char_vis()` (census §2), and **below** `rots_script`
  because it is a service scripting consumes (`mudlle.cpp` calls `find_first_step`). A one-TU
  library is precedented — tier honesty over archive size (`rots_world` shipped with 3 TUs;
  `rots_combat`'s seed was 4). Links (mirroring `rots_combat`'s explicit downward list):
  `RotS::combat RotS::world RotS::persist RotS::entity RotS::core RotS::platform`.
  **Future note (follow-on, not this wave):** graph's pure BFS core could later sink to `rots_world`
  if its `hunt_victim`/`hit()` driver is separated from the BFS proper; recorded as a deferred
  option, not scoped here.
- **`rots_script` (L4-upper):** `mudlle.cpp` + `mudlle2.cpp`. Named for the **mechanism** (designers
  author behaviors), deliberately **not** `rots_ai`: the quest layer is slated for a redesign and
  the name must survive it. Links: `RotS::pathfind` (the sanctioned intra-band edge) + the full L3
  downward set. **Future home (own wave, deferred):** Cluster B (`script.cpp` + the `shape*` OLC
  editors, possibly a sibling `rots_olc` — that wave's census question).
- **Two new linkchecks — `PathfindLayerAcyclicity`, `ScriptLayerAcyclicity`** — mirror the six
  existing `*LayerAcyclicity` checks exactly (force-load the archive, resolve against the declared
  downward link set, fail on any upward reference). `ScriptLayerAcyclicity`'s normal-link set
  **includes `RotS::pathfind`** — this is the intra-band edge it certifies, the same class as
  `WorldLayerAcyclicity` including `RotS::persist` for the `world → persist` peer edge.
- **Sanctioned intra-band edge:** `rots_script → rots_pathfind` (`mudlle.cpp:299`'s
  `find_first_step`) — same one-directional peer class as `db_world.cpp → rots::persist::
  set_room_vnum_hook` (world → persist). `rots_pathfind` never references a `rots_script` symbol
  (verified in T0 — `graph.cpp` has zero edges into mudlle/mudlle2, census §6).
- **`zone.cpp`'s reset half → `rots_world`** rides this wave, fully independently (census §6/§9c).

### The zone.cpp combat/app-edge adjudication (the wave's zone-specific STOP-class question)

`zone.cpp` promotes into `rots_world` (L3), but three of its blocking edges reach tiers `rots_world`
cannot link:

- `equip_char` (fight.cpp:3093, `rots_combat`) — a `world → combat` edge. **Cannot be
  link-resolved**: `rots_combat` already PUBLIC-links `RotS::world`, so adding `RotS::combat` to
  `rots_world` is exactly the forbidden bidirectional pair. **Default disposition (controller-ruled):**
  body-read `equip_char` in T0; it is expected to be entity-pure — it wraps the L2 `attach_equipment`
  primitive, and the POISON coupling that makes the *pair* look combat-flavored lives in
  `unequip_char`, **not** `equip_char` (combat-pilot census §E finding). If T0 confirms this,
  **relocate `equip_char` to `rots_entity`/`equipment.cpp`**, a legal **downward** `world → entity`
  call that removes the edge entirely; else invert via a new `world_hooks.h::dispatch_equip_char`
  registered by `fight.cpp` (a legal `combat → world` downward registration). This **splits the
  `equip_char`/`unequip_char` wrapper pair** the combat-pilot wave moved into `fight.cpp` together —
  **acceptable**: that pairing was cohesion-motivated, not coupling-motivated, and `unequip_char`
  (with its poison-notification path) stays put in `fight.cpp`/`rots_combat`.
- `do_wear` (zone.cpp:598, `do_wear(mob, mutable_arg("all"), 0, 0, 0)`, act_obj2.cpp ACMD,
  app-command) — a `world → app` edge. The `combat_command::wear` cell (`combat_hooks.h`) is
  `rots_combat`-owned and unreachable from `rots_world`. **Disposition:** new
  `world_hooks.h::dispatch_do_wear` (or a minimal single-command hook), registered app-side
  (interpre.cpp), dispatched by zone via `rots_world`.
- `extract_char` (zone.cpp:470, handler.cpp:483, app-other — genuinely session/command-coupled,
  census §1/§10c confirms it **cannot itself leave the app tier this wave**) — a `world → app` edge.
  A `combat_hooks.h::extract_char` hook already exists (combat-pilot), but it is `rots_combat`-owned
  and unreachable from `rots_world` (a `world → combat` upward dispatch). **Disposition
  (controller-ruled): do NOT duplicate a second `extract_char` hook in `world_hooks.h`.** Instead T0
  adjudicates **RE-HOMING the existing `extract_char` inversion from `combat_hooks.{h,cpp}` down to
  L2 `entity_hooks.h`** — one shared inversion **both L3 bands dispatch downward through** (world and
  combat alike, since both PUBLIC-link `RotS::entity`). It is a mechanical move of the setter /
  dispatch / registrar-target: handler.cpp still registers the real body app-side; `fight.cpp`'s
  already-converted call sites simply change namespace (`rots::combat::` → `rots::entity::`); zone
  dispatches through the same L2 entry point. **Default = re-home**; fallback = a world-side duplicate
  hook **only** if the re-home surfaces a snag T0 documents.

The remaining two blocking edges are cheap: `is_empty(int)`'s `descriptor_list` walk
(zone.cpp:691-703) is genuine session state → new `world_hooks.h::dispatch_is_zone_populated`
returning a bool computed app-side; `pkill_get_good_fame`/`pkill_get_evil_fame` (zone.cpp:202-227,
pkill.cpp two-line accessors) → RELOCATE-CLEAN to `rots_persist` alongside `pkill_json.cpp`, or a
two-cell persist-hooks query (T0 chooses). **Net zone cost is heavier than the census's
"existing wear-cell reuse" sketch implied** — that sketch predated the no-bidirectional rule, which
converts every zone → combat/app edge from a link into a hook or relocation. All the resulting hooks
are the well-precedented `world_hooks.h` fn-ptr/tripwire pattern; nothing here is architecturally
stuck (census §8: zone is SEAM-NEEDED, not DEFER).

### Hook taxonomy: the `command_interpreter` inversion

`mudlle.cpp:862` calls the **entire** player-command dispatcher (not one ACMD) so a mob-program can
execute arbitrary commands as if typed by the mob — what `intelligent()`/mob-programs fundamentally
*are*. It is **not** a `combat_command` cell (that table dispatches individual `do_*` ACMDs; this is
the dispatcher itself) and **not** an `output_seam` forwarder (no output composition). It is a
registered-fn-ptr hook in the `world_hooks.h`/`persist_hooks.h`/`entity_hooks.h` lineage, landing in
a **new `script_hooks.h`** (rots_script's upward seam, mirroring `world_hooks.h` for rots_world):
`using command_interpreter_fn = void (*)(char_data*, char*, waiting_type*)`, registered app-side by
`interpre.cpp` (which already forward-declares `command_interpreter`), dispatched by `mudlle.cpp`.
**Null default = a loud tripwire** (an unregistered mob-program command is a real error class, not a
silent best-effort push like the weather-MSDP hook) — every shipped binary registers pre-boot. This
inverts the single busiest entry point in the command layer, which is why it carries explicit owner
sign-off as the wave's heaviest new seam.

### Parent-spec §3 revision (not merely an as-built note)

`docs/superpowers/specs/2026-07-16-library-architecture-design.md` §3 must be **revised**, not just
appended: document (a) the now-explicit **L3 stratification** `persist < world < combat` that the
peer-tier waves actually produced (the original "peer tier, not a sub-stack" framing is now a
certified partial order among the three, each edge one-directional); (b) the **new L4 band** above
L3, stratified internally exactly as L3 turned out to be (`pathfind < script`), with the app above
it; and (c) the downstream notes — `mobact`/`spec_pro`'s honest future home is the **L4 band** (their
`find_first_step`/`intelligent`/`command_interpreter` edges point there, decidable at their
promotion), and `ranger`'s lone `show_tracks` edge (into graph.cpp/`rots_pathfind` after this wave)
resolves relocate-or-hook at the spell-family wave. Also enumerate the fuller census finding
(§6): `spec_pro.cpp:2172` is a **fourth** combat-row caller of `find_first_step` (alongside
`mobact`/`ranger`), all becoming legal downward app→L4 calls once graph promotes.

## Task skeleton

- **T0 — Verification census + adjudications (read-only).** Re-derive `graph`/`mudlle`/`mudlle2`/
  `zone` at @b6f6b76 with the `nm -u` → symbol-map method (census §0), now against **six** library
  object dirs. Re-run the closure check for Cluster A `{graph, mudlle, mudlle2}` (self-closed) and
  `zone` (isolated). **Adjudicate the census's sketch-level items:** the `command_interpreter` hook
  shape (confirm one live call site, exact signature, tripwire default); `equip_char` relocate-vs-hook
  (body-read for entity-purity, expected clean per census §E); the `extract_char` **re-home** from
  `combat_hooks.h` to L2 `entity_hooks.h` (confirm the mechanical move + fight.cpp namespace change);
  `do_wear`/`is_zone_populated` hook shapes; the `PERS` forwarder home (script_hooks.h vs. dedicated
  cell, abort-tripwire class); the two zone relocates (pkill fame pair); graph's `_arg`/`_buf`
  retirement sites and the new `output_seam` forwarder identity (`put_to_txt_block_pool`); and
  **verify `world_hooks.h::dispatch_mudlle_converter`
  keeps working** with `mudlle.cpp` now L4 (registrar `mudlle.cpp:1384`, an L4→L3 downward
  registration; dispatcher `db_world.cpp:218`, L3-world — state the tier math explicitly).
- **T1 — Seams + retirements (consumer-free, all TUs still app-compiled).** The `extract_char`
  re-home (`combat_hooks.h` → L2 `entity_hooks.h`, shared by both L3 bands); the two zone
  `world_hooks.h` hooks (`dispatch_do_wear`, `dispatch_is_zone_populated`) + `equip_char`
  relocation-or-hook per T0; the `command_interpreter` `script_hooks.h` hook (new header); the `PERS`
  forwarder (hooks family, abort-tripwire); the `put_to_txt_block_pool` `output_seam` forwarder; the
  pkill fame relocation. Each lands and gates green before any call site converts to it.
- **T2 — Conversions (all TUs still app-compiled).** graph: `do_say`×2/`do_move` → `say`/`move`
  cells, `_arg`/`_buf` retirement. mudlle: `command_interpreter` → hook dispatch, `do_say` → `say`
  cell, `put_to_txt_block_pool` → forwarder, `_buf`/`_buf2` retirement. mudlle2: shares mudlle's
  forwarder + `say` cell. zone: `extract_char`/`do_wear`/`is_empty`/`equip_char` → their seams,
  pkill fame → relocated calls. Discriminator audit per newly-exercised hook/cell.
- **T3 — Memberships + NEW LIBRARY TARGETS.** Stand up `rots_pathfind` + `rots_script` (CMake
  targets, `RotS::` aliases, both linkchecks, root-Makefile hand-list per the entity-seed lesson);
  `zone.cpp` → `ROTS_WORLD_SOURCES`. Commit ordering explicit (see plan): zone→world can land
  independently; pathfind must precede script (script links it).
- **T4 — Docs.** Parent-spec §3 revision (above) + §10 as-built slice; `docs/BUILD.md` "Library
  layering" (two new sections + the two linkchecks); `docs/superpowers/combat-migration-playbook.md`
  (the L4-band data point, the no-bidirectional resolution of §7); `AGENTS.md` (test totals + the
  two new libraries in the build-command inventory).
- **T5 — Finalization.** i386 battery (`scripts/i386-battery.sh`), whole-branch review + fix wave,
  push + PR + six blocking CI jobs. **Merge is the owner's** — present the CI-green PR and stop.

## Adjudication defaults (T0 confirms or overturns with `nm`/body-read evidence)

- **`command_interpreter`** — new `script_hooks.h` hook, `void (*)(char_data*, char*, waiting_type*)`,
  loud-tripwire default, registered by `interpre.cpp` pre-boot; one live consumer (mudlle.cpp:862).
- **`equip_char`** — relocate to `rots_entity` (`equipment.cpp`, the placement-seam neighborhood) if
  entity-pure; else `world_hooks.h::dispatch_equip_char` registered by `fight.cpp`. Relocate
  preferred (removes the edge; a downward call needs no runtime indirection). Expected entity-pure:
  it wraps the L2 `attach_equipment` primitive, and the POISON coupling is in `unequip_char`, not
  `equip_char` (combat-pilot census §E). This **splits the `equip_char`/`unequip_char` pair** the
  pilot moved into `fight.cpp` together — acceptable (cohesion-, not coupling-motivated);
  `unequip_char` stays put.
- **`extract_char`** — **RE-HOME the existing `combat_hooks.{h,cpp}` `extract_char` inversion down to
  L2 `entity_hooks.h`** (one shared inversion both L3 bands dispatch downward through), NOT a second
  `world_hooks.h` duplicate. Mechanical move of setter/dispatch/registrar-target; handler.cpp still
  registers the real body app-side (its body cannot leave the app tier — census §10c); fight.cpp's
  converted call sites change namespace (`rots::combat::` → `rots::entity::`); zone dispatches through
  the same L2 entry point. Fallback = world-side duplicate **only** if the re-home surfaces a T0-
  documented snag.
- **`do_wear` / `is_zone_populated`** — new `world_hooks.h` hooks; real bodies stay app-tier
  (act_obj2.cpp / comm.cpp), registered app-side.
- **`put_to_txt_block_pool`** — one new `output_seam.h` forwarder (mirrors the 7 blocker-buster
  added), shared by mudlle.cpp + mudlle2.cpp; body-check against the existing
  `get_from_txt_block_pool` overload for equivalence first.
- **`find_first_step`** — no conversion; it becomes the sanctioned `rots_script → rots_pathfind`
  intra-band call the moment both libraries exist.
- **`do_say`/`do_move`** — existing `combat_command::say`/`move` cells; confirm registration +
  ACMD-signature match (identical shape to the combat-trio wave's `olog_hai` `do_move` conversion).
- **`PERS`** (mudlle2.cpp:—, utility.cpp) — **named adjudication, default = a registered `PERS`
  forwarder** in the **hooks family** (cross-owner rule: an inversion, NOT an `output_seam` entry),
  home per T0 (`script_hooks.h` or a dedicated cell). `PERS` is pointer-returning, so its unregistered
  default is the **abort-tripwire class** (per the established taxonomy: a pointer-returning hook
  cannot safely return a usable default, so an unregistered call aborts loudly rather than dangling —
  the `mudlle_converter` precedent). **Closure consequence, stated loudly:** `mudlle.cpp` needs
  `mudlle2.cpp` (14 helper edges), so if `PERS` fails to resolve as a clean forwarder, the **entire
  script side of the wave collapses to `rots_pathfind` + `zone` only** — that is the honest, named
  fallback scope, not a surprise discovered at `ScriptLayerAcyclicity`.
- **`pkill_get_good_fame`/`pkill_get_evil_fame`** — RELOCATE-CLEAN to `rots_persist` (or a two-cell
  persist-hooks query); two-line accessors reading pkill's own static ranking globals.

## Verification

- Byte-verbatim mandate for every body move / relocation (Python byte-edits where CRLF or the
  formatter hook threatens bytes — the established method; reviewers re-diff moved spans). All four
  candidate files read as plain LF at @b6f6b76, but **verify per file before editing**, do not assume
  (playbook lesson; `clerics.cpp` was CRLF while `fight.cpp` was LF).
- Dispatch-equivalence discriminator (registered-reaches-real-body / unregistered-tripwire pair) for
  **every new hook class** — `command_interpreter`, `extract_char`, `do_wear`, `is_zone_populated`,
  `equip_char` (if hooked) — and an audit (a real read, not an assumption) that `say`/`move`/the
  `output_seam` forwarder already have coverage; add only genuine gaps.
- Per-task gates both hosts: macOS arm64 + `rots64` build + full ctest (**1398 baseline** + this
  wave's additions), **all eight** `*LayerAcyclicity` linkchecks (the six existing + the two new),
  both boot goldens, `ConvertEquivalence` 17/17, `python3 tools/string_view_census.py --check`
  exit 0. New/rewritten test files add the `macos-arm64-asan` preset. i386 battery at finalization
  only.
- Characterization goldens (`CharacterizationCombatTest.*`, `CharacterizationJson.*`,
  `PoisonRemovalScriptTest.*`, `boot-golden.sh verify`) pass **unchanged** — drift = a bug in the
  change. `mudlle`/`graph`/`zone` are directly boot-golden-exercised (mob programs, hunting,
  zone resets), so the boot golden is a real regression net for this wave, not a formality.

## Risks

- **The `command_interpreter` inversion is the wave's highest-risk seam.** It inverts the busiest
  command-layer entry point. Its tripwire default must be genuinely loud (an unregistered mob-program
  command is an error), and the discriminator test must prove `char_data*`/`char*`/`waiting_type*`
  all forward intact. A single live call site limits blast radius, but a registration-ordering slip
  (registered after `boot_db` instead of before) would silently disable every mob program — the boot
  golden is the backstop.
- **zone.cpp's edge cost is heavier than the census sketched.** The no-bidirectional rule turns three
  link-resolvable edges into hooks/relocation. If `equip_char` body-reads as combat-coupled (not
  entity-pure), it becomes a fourth new `world_hooks.h` hook rather than a clean relocation — still
  bounded, but T1's scope grows. STOP-and-adjudicate if T0 finds `equip_char` reaches combat-only
  symbols.
- **First L4 band + first two new library targets since the L3 peers.** The CMake target/linkcheck
  pattern is well-precedented (six existing `*LayerAcyclicity`), but two targets land at once and
  `rots_script` links `rots_pathfind` — the intra-band edge must be wired before `rots_script`'s
  linkcheck can pass. Order the commits so `rots_pathfind` exists first.
- **`fight.cpp` blast radius does not apply** — no candidate TU is the melee hub; graph/mudlle are
  service/authoring drivers, zone is world data.

## Out of scope

- **Cluster B** — `script.cpp` + `shapemdl/shapemob/shapeobj/shaperom/shapescript/shapezon.cpp`
  (census §5/§9b). Its `shapemdl.cpp → mudlle_converter` bridge already dissolves via the existing
  `world_hooks.h::dispatch_mudlle_converter` seam, so Cluster B is independently schedulable in its
  own future wave (that wave's census question: `script.cpp`'s ~6 unadjudicated RELOCATE candidates,
  the shared `string_add_init` OLC-editor hook, and the `gain_exp`/`virt_assignmob` combat-peer
  STOP-risks).
- No other L3 combat/world DEFER TU (`mobact`/`spec_pro`/`ranger`/`mage`/`limits`/`spell_pa`/
  `spec_ass`) — they stay app-compiled; their `find_first_step`/`intelligent`/`show_tracks` edges
  become legal downward app→L4 calls once this wave lands, and are recorded follow-on.
- No graph BFS-core sink to `rots_world` (the pathfind future note) — deferred until the hunt driver
  separates.
- `PERS` is IN scope this wave as a registered forwarder (see Adjudication defaults), NOT relocated
  or deferred — its failure narrows the wave to pathfind+zone rather than being out of scope. No
  int→double combat-math changes. No new
  smoke/determinism infrastructure — the combat-pilot harness carries forward informational.

## As-built (Tasks 0-3; Task 4 is this docs pass; Task 5 finalization pending)

**Status: GO, full scope delivered.** Every adjudication default in this design either held or
overturned cleanly (below); the named collapse condition (`PERS`) did not fire; Cluster A's full
3-TU scope and `zone.cpp` both landed. Eight libraries, eight `*LayerAcyclicity` linkchecks. ctest
1398 → 1415 across Tasks 1-3 (T0 read-only). Full per-task evidence:
`.superpowers/sdd/l4-task-{0,1,2,3}-report.md`; census `.superpowers/sdd/l4-census.md`.

- **T0 OVERTURN 1 — `equip_char` is NOT entity-pure; the design's own default was wrong, and it
  corrected a controller claim, not just a design-doc guess.** This design's problem statement
  (above) states plainly: "the POISON coupling that makes the *pair* look combat-flavored lives in
  `unequip_char`, **not** `equip_char` (combat-pilot census §E finding)" — attributed to a
  controller-approved adjudication default, not merely an unverified assumption. T0's body-read
  (`fight.cpp:3093-3155`) found `equip_char` carries its **own** `damage()`/`raw_kill()`
  poison-coupling block, structurally identical to `unequip_char`'s. Tracing the claim to its actual
  source (`pilot-census.md` §3.8) showed it was a **caller-side** observation ("`fight.cpp` calls
  `unequip_char` only") misread as a body-content claim; §3.8 itself says both wrappers reference
  `damage`/`raw_kill`. The opus review of T0 independently re-derived the same body-read and
  confirmed the overturn was correct — this was a genuine correction of a prior wave's claim as it
  had been carried into this design doc, not a routine "design said X, code said X, confirmed" check.
  Consequence: `equip_char` falls to the named fallback (`world_hooks.h::dispatch_equip_char`,
  registered by `fight.cpp`, unmoved) instead of relocating; neither half of the
  `equip_char`/`unequip_char` pair splits after all (the design's own anticipated "acceptable split"
  never happens, since nothing moves).
- **T0 OVERTURN 2 — `pkill_get_good_fame`/`pkill_get_evil_fame` are NOT relocate-clean.** The design
  called these "RELOCATE-CLEAN to `rots_persist` ... two-line accessors reading pkill's own static
  ranking globals." T0 found `good_ranking`/`evil_ranking` are real, actively-mutated globals defined
  in **`pkill.cpp` itself** (`ROTS_SERVER_SOURCES`, app-tier), not `pkill_json.cpp`
  (`rots_persist`) — moving just the two accessors would compile in the final `ageland` link (the
  linker resolves everything together) but would fail `PersistLayerAcyclicity` by design, since that
  linkcheck force-loads `rots_persist` standalone against only its declared downward libraries. Falls
  to the design's own named fallback: a `world_hooks.h` two-cell query-hook pair (int, safe-sentinel
  `0` default), registered by `pkill.cpp` unmoved.
- **CRLF map correction.** This design's Verification section claims "All four candidate files read
  as plain LF at @b6f6b76." Measured (T0): `zone.cpp` is **pure CRLF** (703/703 lines); `graph.cpp`
  (411/421), `mudlle.cpp` (1383/1385), `mudlle2.cpp` (475/477) are all **mixed CRLF+LF**. Every T1/T2
  edit to all four used the Python binary-mode byte-edit method as a result — not a fallback for
  files that happened to need it, but the mandatory method for the wave's entire scope. The same
  discovery extended in transit to `utility.cpp`/`utils.h`/`db.h`/`pkill.cpp`/`act_obj2.cpp`/
  `handler.h`/`handler.cpp`, none of which this design's four-file CRLF check ever covered.
- **T3's storage-relocation adjudication.** T1 placed `dispatch_command_interpreter()`'s backing
  storage in `interpre.cpp` (following this design's own Step 1 instruction, "backing storage +
  dispatch in `interpre.cpp`"), which seemed consistent with the `assign_spell_pointers`/
  `register_combat_command_dispatch` precedent this design cites. Once Task 2 converted
  `mudlle.cpp`'s call site, that placement became a live `rots_script → app` upward edge — invisible
  until `rots_script_linkcheck` first ran against it at Task 3 (two undefined symbols:
  `dispatch_pers`/`dispatch_command_interpreter`). Adjudicated in-flight (not a STOP, since the fix
  was mechanical and unambiguous): relocated both hooks' backing storage byte-verbatim into
  `script_hooks.cpp` — which already held `dispatch_pers()`'s storage for the identical "seam header,
  no single owning caller" reason — making it `rots_script`'s third member; `interpre.cpp` keeps only
  the registrar. This design's own Step 1 text is the one place this design under-specified the
  correct precedent (the `world_hooks.h`/`db_world.cpp` "storage lives in the *promoting* library"
  shape, not the `assign_spell_pointers` shape it actually cited) — recorded here as the design
  correction it is, not silently absorbed into the as-built without comment.
- **The say-cell gap fill.** `combat_command::say` had been registered since the combat-seed wave
  (predating this design by three waves) but had **zero** `issue_command()` callers anywhere in the
  tree until Task 2's own 15 new call sites (graph ×2, mudlle ×11, mudlle2 ×2) — a genuine,
  standing discriminator gap this design's Verification section's audit requirement caught, not a
  new gap this wave introduced. Closed with the standard registered/unregistered pair.
- **`is_empty` retirement.** Beyond this design's own scope (which only asked for a hook,
  `dispatch_is_zone_populated`, real body registered by `comm.cpp`), Task 2 additionally retired
  `zone.cpp`'s own `is_empty(int)` function body outright — not just its call sites — once a
  tree-wide re-check confirmed zero other callers remained. A verified-safe cleanup beyond the
  letter of the design, not a deviation from it.
- **PERS confirmed; the collapse condition did not fire.** T0's body-read confirmed `PERS`'s own body
  (`utility.cpp:915-961`) genuinely cannot relocate (its `CC_USE`/`CC_NORM` macros expand to
  `color.cpp` state, real app-tier) — consistent with, not contradicting, the blocker-buster wave's
  prior finding. The *forwarder* question, this design's actual scope, resolved cleanly as an
  abort-tripwire class (`script_hooks.h`, per the design's own recommended home) — Cluster A's full
  3-TU scope survived exactly as the design's non-collapse path anticipated.
- **Everything else in this design held as specced**: the `command_interpreter` hook shape (void,
  loud tripwire, `interpre.cpp` as original registrar); `do_wear`/`is_zone_populated` (cheap, one
  call site each); `extract_char`'s re-home (mechanical, zero snag); the `put_to_txt_block_pool`
  forwarder (one new symbol, safe-no-op PUT class); the `rots_pathfind`-before-`rots_script` commit
  ordering; both linkchecks' shape (though their actual link lists lean on PUBLIC transitivity more
  than this design's literal "mirroring `rots_combat`'s explicit downward list" text suggested — see
  `docs/BUILD.md`'s "L4 band" subsection for the as-built link-set account).
