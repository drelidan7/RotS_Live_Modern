# Placement Seam & Relationship-Layer Carve (spec §7 Stage 1, scoped) — Design

Date: 2026-07-19. Status: approved-in-dialogue (owner), pre-implementation.
Wave branch: `arch/placement-seam` off master @2d94fb2 (post build-isolation merge).
Parent spec: `docs/superpowers/specs/2026-07-16-library-architecture-design.md` §7
(Placement / Containment / Equipment systems).

## Problem

`handler.cpp` and `utility.cpp` are the last two TUs of the parent spec's `rots_entity` sketch
still app-compiled. A fresh nm census (this wave's planning, master @2d94fb2) shows their welds
cluster by kind:

- **Entity-pure core:** char/obj placement, containment, and equipment mutation
  (`char_to_room`, `char_from_room`, the `obj_to_*`/`obj_from_*` family, `equip_char`,
  `unequip_char`, …) plus assorted pure helpers.
- **Structural upward couplings:** `world[]` / `room_data::operator[]` reads (L3 `rots_world`
  storage — the §7 seam); combat calls (`damage`, `raw_kill`, `stop_fighting`, `set_fighting`);
  session/output (`close_socket`, `show_character_menu`, `send_to_char`, `act`); command
  invocations (`do_look`, `do_return`); persistence (`save_char`); app-tier helpers (affect
  machinery, visibility, spec-handler queries).

Whole-TU membership would need ≈10-15 new hooks, several serving functions that are arguably
app-tier anyway. Full §7 is a multi-wave program (Stage 1 alone touches ~754 `->in_room`,
~576 `world[...]`, ~104 `next_in_room` sites game-wide).

## Decision (owner-approved)

**Scope: "seam + membership" via a tier-line split.** Carve the entity-pure relationship core out
of `handler.cpp`/`utility.cpp` into three new `rots_entity` TUs — the parent spec's three explicit
systems — with a Stage-1 read API sized to what the moved code needs. The session/command/combat-
coupled remainder stays app-compiled and calls down into the moved primitives. Explicitly
deferred: the game-wide call-site conversion campaign, Stage 2 (`LocationSystem`), `rots_combat`.

Rejected alternatives: whole-TU membership + hooks (hook sprawl serving app-tier functions);
hybrid (handler whole / utility split) — the split discipline is cleaner applied uniformly.

## Location representation (binding for this wave and inherited by Stage 2)

**Stage 1 changes who knows the representation, not the representation.** After this wave:

- `char_data::in_room` remains the integer runtime room index (rnum); rooms keep the intrusive
  occupant chain (`room->people` / `ch->next_in_room`). Byte-identical behavior; goldens pin it.
- Moved lib code asks through the API; the implementations are one-line wrappers over today's
  fields (`location_of(ch)` → `ch->in_room`; `is_in_room(ch, id)` → integer compare;
  `occupants(room)` → a range over the `people`/`next_in_room` walk).
- **Room ids are the public currency.** API functions accept/return rnum ids; `room_by_id(id)` is
  the explicit escalation to a `room_data*`. The API must not hand out raw `room_data*` as the
  default, so the Stage-2 representation swap (external map; "no location = absent"; `NOWHERE`
  sentinel retired) can happen without chasing leaked pointers.
- Stage 2 (future wave): `char_data` sheds `in_room`/`next_in_room`; a `LocationSystem` in
  `rots_entity` owns char→room and room→occupants; `rots_convert` links the library but never
  instantiates the system.

## Changes

1. **New lib TUs** (one system per file, zone-L/R-split precedent, verbatim moves):
   - `src/placement.cpp` — character↔room mutation + the Stage-1 read API.
   - `src/containment.cpp` — object↔(room|char|obj) mutation.
   - `src/equipment.cpp` — wear-slot mutation (`equip_char`/`unequip_char`/…).
   The precise function inventory comes from a per-function census classification at planning
   time. Classification contract: entity-pure functions move verbatim; functions that mainly emit
   player messages or drive session/combat behavior stay app-side calling down into moved
   primitives (function-granular re-splits preferred over new output hooks); ambiguous cases get
   STOP adjudications.
2. **Room-resolver seam:** an entity-tier hook (`resolve_room(id) → room_data*`, plus an
   occupant-iteration shim only if the census demands it), implemented by a small function in
   `rots_world` reading `world[]`, wired pre-boot by the app tier (`run_the_game` +
   `gtest_main.cpp`, registration parity — app wiring L3's implementation into L2's seam is all
   downward references). Null default: **tripwire abort** (txt-block-pool class: no sane placement
   without a world; `rots_convert` never places characters into rooms).
3. **Build wiring:** new TUs join `ROTS_ENTITY_SOURCES` ×4 build systems; `EntityLayerAcyclicity`
   becomes the automatic enforcement for everything moved. `handler.cpp`/`utility.cpp` survive as
   smaller app TUs; declarations stay in their current headers (`handler.h`, `utils.h`, …).
4. **Docs:** BUILD.md library-layering update (`rots_entity` grows to 8 TUs; honest deferrals:
   call-site campaign, Stage 2, remaining app-side handler/utility functions); parent-spec §7
   as-built note; AGENTS.md one-liner.

## Verification

- Zero behavior change: goldens byte-for-byte both hosts + i386; ctest baseline **1281** both
  hosts (+ any targeted coverage the standing coverage-gap rule adds if the census surfaces
  untested moved paths — new test files get the macOS ASan preset run).
- Per-task both-host gates (macOS arm64, rots64 incl. boot goldens, census); full i386 battery +
  whole-branch review + six CI jobs at finalization.
- nm single-definition checks per move; `EntityLayerAcyclicity` green is the wave's decisive
  structural check (as `WorldLayerAcyclicity` was for world-seed).
- Whole-branch review sanity-checks the resolver hook's hot-path cost (one indirect call on every
  placement mutation; same cost class as existing hooks).

## Risks

1. The census may reveal a smaller entity-pure core than hoped (message-emitting placement
   functions) — the plan carries STOP adjudications; a modest carve is acceptable. The milestone
   is the relationship layer living in the lib, not TU count.
2. Resolver hook on the hottest mutation path — reviewed for regression; no measurement gate
   (structurally identical to existing hook dispatches).
3. `utility.cpp`'s grab-bag may yield little — acceptable per (1).
4. Linkcheck cascades at membership time — expected-possible; STOP contract as in prior waves.

## Out of scope

- Game-wide Stage-1 call-site conversion (~1,400 sites) — follow-on waves, library by library.
- Stage 2 `LocationSystem` / `NOWHERE` retirement.
- `rots_combat`; the other three intrusive threads (`next_fighting`, `next_fast_update`, `next`).
