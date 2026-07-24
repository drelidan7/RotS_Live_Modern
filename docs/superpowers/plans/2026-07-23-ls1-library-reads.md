# LocationSystem Wave LS-1 — Library-Tier Read Conversion Implementation Plan (`arch/ls1-library-reads`)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Route every raw location-*read* inside the six source-bearing libraries through the
Stage-1 Placement APIs already seeded in `rots_entity` — `location_of(ch)` for `->in_room`,
`room_by_id(id)` / `room_by_id_total(id)` for `world[id]`, and `for (auto* occ : occupants(room))`
for `next_in_room` traversals — so that no library code outside `rots_entity`'s placement/containment
representation owners still knows how location is stored. **Zero behavior change: the APIs wrap
today's exact representation (zero-cost inlines), no struct/signature/schema changes, goldens NEVER
regenerate — any golden or boot drift is a bug in a conversion, not an expected outcome.** Reads
convert; writes stay on `set_location`/the mutation core; the representation swap is LS-3, not this
wave.

**Architecture:** Census first (T0, Opus, read-only: classify EVERY raw `->in_room`/`world[`/
`next_in_room` occurrence in the six libraries as READ / WRITE / allow-listed-internal, assign each
read its mechanical conversion form, flag the non-mechanical sites with per-site dispositions, rule
any API gap, set the batch order/sizes, name the allow-list file set, and design the per-batch
equivalence evidence → `.superpowers/sdd/ls1-census.md`); then a CONDITIONAL API-completion task
(T1, Sonnet, consumer-free, TDD — lands only what the census justifies, collapses to a no-op
otherwise); then the conversions in per-library batches in tier order (T2, Sonnet — apply the
census's per-site list with bounded byte-edits, per-batch dual-host gates, coverage riders where a
conversion touches previously-untested live code, commit per library/sub-batch); then the LS-1 grep
gate (T3, Sonnet — a checked-in ctest-registered census asserting the six converted libraries hold
ZERO raw location reads outside the allow-list); then docs (T4, Sonnet); then finalization — i386
battery, Fable whole-branch review (with sampled per-site conversion re-derivation), CI-green PR,
**MERGE-WHEN-GREEN (owner grant)**, branch delete (T5). Spec:
`docs/superpowers/specs/2026-07-23-locationsystem-program-design.md` (Wave LS-1). Parent:
`docs/superpowers/specs/2026-07-16-library-architecture-design.md` §7. Branch:
`arch/ls1-library-reads` off master @`5db2b9e` (physical layout merged; nine libraries physically
homed), HEAD/plan `24d6fec`.

**Tech Stack:** C++20, CMake presets + flat Makefiles (`src/Makefile` game build, `src/tests/Makefile`
monolithic runner), GoogleTest, the Stage-1 Placement API in `src/handler.h` + `src/entity/placement.cpp`
(`rots_entity`, L2, linked by every consumer tier), Python 3 bounded byte-edits for every existing
`.cpp`/`.h`, a new `tools/location_read_census.py` grep gate (modeled on `tools/string_view_census.py`).
No new library, no new seam header, no new linkcheck, no membership change — the nine
`*LayerAcyclicity` linkchecks stay nine and stay green.

## Architecture note — the conversion recipe (exact, transcribed from HEAD `24d6fec`)

The Stage-1 API surface is ALREADY LANDED and load-bearing after this wave. Implementers transcribe
these signatures from here; do not re-derive them.

**Declarations — `src/handler.h:68-74` (global namespace):**
```cpp
struct room_data*  room_by_id(int rnum);        // nullptr for an out-of-range id (a normal "absent")
struct room_data*  room_by_id_total(int rnum);  // preserves room_data::operator[]'s graceful fallback
struct zone_data*  zone_by_id(int znum);        // nullptr for an out-of-range id
struct index_data* obj_index_by_id(int item_number);   // nullptr for an out-of-range id
int  location_of(const struct char_data* ch);   // returns ch->in_room (const-correct)
void set_location(struct char_data* ch, int rnum);     // WRITE path -- assigns ch->in_room; NOT a read conversion
bool is_in_room(const struct char_data* ch, int rnum); // ch->in_room == rnum
```
**Definitions — `src/entity/placement.cpp`:** the four resolvers (`:126`/`:143`/`:154`/`:165`)
dispatch through `entity_hooks.h` into `rots_world`'s registered implementation; an unregistered hook
is a tripwire `abort()`. `location_of`/`set_location`/`is_in_room` (`:182`/`:187`/`:195`) are thin
`char_data::in_room` field wrappers (no hook — `char_data` is an L1 core struct).

**`occupants(room)` — `src/entity/placement.cpp:200-269` (`rots::entity` namespace):**
```cpp
inline occupant_range occupants(room_data* room);   // range-for over room->people / char_data::next_in_room
```
- Takes a **non-const `room_data*`**, yields **non-const `char_data*`** per element.
- Constructor snapshots `room->people` at construction time; **a null room yields an empty range**
  (so `occupants(room_by_id(id))` is safe when `room_by_id` returns nullptr).
- `iterator::operator++` reads `node_->next_in_room` **lazily at increment time** — i.e. it
  reproduces the SIMPLE legacy walk `for (; tmpch; tmpch = tmpch->next_in_room)`, **NOT** the
  save-next-first mutation-safe idiom `next = tmpch->next_in_room; …operate on tmpch…`. **A walk that
  removes/relocates the current node mid-iteration is therefore NOT mechanically convertible to this
  range** and is a FLAGGED site (see the flagged-site rule in Global Constraints).
- Comment at `:205` says "Unused as landed" — this wave makes it load-bearing for the first time.

**The `world[id]` two-variant crux.** `world` is a `room_data` singleton whose `operator[]`
(`src/core/include/rots/core/room.h:128`, defined in `db_world.cpp`) returns a `room_data&` **with a
graceful out-of-range fallback** (a valid fallback room + mudlog, never a crash). Therefore a
behavior-preserving read conversion of `world[id]` is **per-site**, not uniform:
- `world[id].field` where the caller did **not** bounds-check `id` first → `room_by_id_total(id)->field`
  (preserves `operator[]`'s exact fallback for every input, in range or not).
- `world[id].field` where the caller already bounds-checked, or where the semantics genuinely want
  "absent" for an invalid id → `room_by_id(id)->field` (nullptr-on-invalid).
- `world[ch->in_room]` (a character's own room) → `room_by_id_total(location_of(ch))->field` (nested,
  mechanical) unless the census justifies a `room_of(ch)` convenience in T1.
- **The census rules each `world[` site's resolver choice explicitly** — a wrong resolver silently
  changes fallback behavior on an invalid id, which the goldens may not catch, so this is a T0
  per-site decision, never an implementer guess. This is the wave's single highest-subtlety class.

**Object code legitimately changes.** Unlike the physical-layout wave (pure `git mv`, `nm`-proven
identity), a read conversion CHANGES the emitted object code (a field deref becomes a function call
to a zero-cost inline). **`nm` is NOT the equivalence evidence here.** The evidence a conversion is
behavior-neutral is: full dual-host ctest all-green + both boot goldens byte-identical + (for
combat/damage-touching batches) the seed42 characterization golden unchanged + the targeted new
coverage the gap rule requires. The census designs the exact per-batch evidence set.

## Current-tree facts the census consumes (verified at HEAD `24d6fec`)

Per-library raw-site scale (`grep -c` per `.cpp`, summed per dir; `platform`/`core` are zero):

| Tier | Library | `->in_room` | `world[` | `next_in_room` | Notes |
|---|---|---|---|---|---|
| L2 | `rots_entity` | 51 | 40 | 23 | includes the ALLOW-LIST owners (placement/containment) — see below |
| L3 | `rots_persist` | 4 | 1 | 0 | **spec expected near-zero; NOT zero — see Discrepancy 1** |
| L3 | `rots_world` | 16 | 91 | 2 | `db_world.cpp` 10/67/0, `weather.cpp` 2/11/0, `zone.cpp` 4/13/2 |
| L3 | `rots_combat` | 241 | 145 | 28 | largest — `mage` 55/61/11, `ranger` 55/28/6, `fight` 49/14/1, `limits` 21/12/1, `visibility` 21/11/1, `mystic` 15/7/3, `spell_pa` 11/4/1, `olog_hai` 9/7/4, `clerics` 5/1/0 |
| L4 | `rots_pathfind` | 9 | 9 | 0 | `graph.cpp` |
| L4 | `rots_script` | 131 | 82 | 31 | **`spec_pro.cpp` 90/58/20** (huge), `mobact` 24/12/4, `mudlle` 8/7/5, `script` 9/5/2 |
| L4 | `rots_olc` | 13 | 10 | 1 | five `shape*.cpp` TUs |
| L0 | `rots_platform` | 0 | 0 | 0 | census confirms zero, records |
| L1 | `rots_core` | 0 | 0 | 0 | census confirms zero, records |

Raw grep totals (comments + writes + reads, undifferentiated — T0 re-derives per-SITE READ/WRITE/
allow-list classifications; this table carries only the SCALE): six-library sum ≈ **465 `->in_room` +
337 `world[` + 85 `next_in_room`**, plus persist's handful. The program spec's whole-game figures are
810 / 585 / ~104 across both Stage-1 waves; LS-2 carries the app-tier remainder (`src/app/*`,
342 `->in_room` alone).

**Entity allow-list (representation owners — sites do NOT convert; they ARE the implementation).** The
census marks the allow-list EXPLICITLY. Expected members: `src/entity/placement.cpp` (51 grep hits —
holds `location_of`/`set_location`/`is_in_room`/`occupants`/the resolvers and the char↔room mutation
primitives) and `src/entity/containment.cpp` (the obj↔room/char/obj containment core). The census
RULES whether `src/entity/equipment.cpp` and any other entity TU (`char_utils_combat.cpp`,
`char_utils.cpp`, `entity_lifecycle.cpp`, `environment_utils.cpp`, `object_utils.cpp`) join the
allow-list or convert — equipment owns wear-slots, not location, so its `->in_room`/`world[` reads are
expected to CONVERT unless a specific site manipulates the occupant chain directly.

Key file locations T0/T2 consume (verified): the Stage-1 API — `src/handler.h:53-74` (decls),
`src/entity/placement.cpp:126-269` (defs); `world`/`operator[]` — `src/core/include/rots/core/room.h:128`
(decl), `db_world.cpp` (def); the grep-gate precedent — `tools/string_view_census.py` (`rglob` file
discovery + `--check` mode + CI invocation per AGENTS.md). No `src/CMakeLists.txt` source-list or
linkcheck edit is expected (this wave changes bodies, not membership).

## Discrepancies against the spec (recorded per the plan-parameters rule — T0 rules each)

1. **`rots_persist` is NOT near-zero.** `src/persist/db_players.cpp` has real sites: `:1376`
   `ch->in_room = GET_LOADROOM(ch)` (a **WRITE** — stays a write, does not convert to a read API),
   `:1924` `ch->in_room != NOWHERE` and `:1925` `dispatch_room_vnum(ch->in_room)` (two **mechanical
   READs**), plus `:13` a comment (`world[ch->in_room].number`, noise). Persist is a **library** (L3),
   not app tier — so its reads belong to LS-1 (library-tier conversion), not LS-2 (app tier). Leaving
   them stranded would make the LS-2 exit criterion (no raw reads outside the allow-list)
   unmeetable. **T0 RULES persist into LS-1 scope** (a small `persist` batch, tier position between
   entity and world) — recommended — or STOPs with a rationale for deferring. This plan assumes
   inclusion; T2's batch list carries a persist batch pending T0's ruling.
2. **`zone_table[` is OUT of the LS-1 read-conversion charter.** There are ~201 raw `zone_table[`
   reads across the six libraries, and `zone_by_id()` exists as their resolver — but the program
   spec's tracked triple and every success/exit criterion are `->in_room` / `world[...]` /
   `next_in_room` ONLY. `zone_table` conversion is NOT this program's charter; the grep gate does not
   cover it. T0 RECORDS this as a conscious scope boundary (so it is not later read as an oversight);
   any `zone_table[` a `world[`→`room_by_id` conversion incidentally sits beside is left untouched.
3. **`occupants()` const-ness and shape gaps (candidate T1 work).** `occupants()` takes a non-const
   `room_data*` and yields non-const `char_data*`, and reproduces the SIMPLE walk (not the
   save-next-first idiom). If T0 finds converted read sites that (a) walk a `const room_data*`'s
   occupants, or (b) are self-room `world[ch->in_room]` patterns numerous enough to justify a
   `room_of(ch)` convenience — those become T1 API-completion items. If T0 finds none, T1 is a no-op.
   YAGNI: add only what the census justifies.

## Global Constraints

- **ZERO BEHAVIOR CHANGE — goldens NEVER regenerate; drift = STOP.** Both boot goldens
  (`docs/superpowers/goldens/boot-log.golden`, via `scripts/boot-golden.sh verify`) stay
  byte-identical at every commit; the combat characterization goldens (`src/tests/goldens/`,
  incl. `combat_transcript_seed42.txt`), `ConvertEquivalence`, and every `legacy_*_fixture.bin` are
  untouched. **NEVER run `UPDATE_GOLDENS=1` this wave.** Any golden or boot drift traced to a
  conversion is a real bug (a read that was not representation-neutral) → STOP for that batch and fix
  the conversion, never the golden.
- **READS-ONLY — a conversion that touches a WRITE site is a defect.** Only pure reads convert to the
  read APIs. Every `ch->in_room = …` / `world[id].field = …` / `next_in_room` splice or list mutation
  stays on `set_location` / the existing mutation core in `placement.cpp`/`containment.cpp`. The
  census classifies every site READ vs WRITE first; an implementer converting a write to `location_of`
  or `occupants` is a review-blocking defect. (`db_players.cpp:1376`'s `ch->in_room =` is the canonical
  do-not-touch example.)
- **The allow-list concept.** `rots_entity`'s placement/containment (and any TU the census adds)
  representation-owner files are the ONE place raw location access legitimately remains after LS-1 —
  they ARE the implementation the APIs wrap. Their sites do NOT convert; the T3 grep gate exempts
  exactly this census-named file set and nothing else.
- **`world[id]` resolver choice is per-site (T0-ruled).** `room_by_id` (nullptr-on-invalid) vs
  `room_by_id_total` (operator[] graceful fallback) is a behavior decision the census makes for each
  `world[` read; an implementer picking the wrong one silently changes out-of-range fallback and is a
  defect. When in doubt for an un-bounds-checked `world[id]`, the fallback-preserving `room_by_id_total`
  is the safe default — but the census states it explicitly per site.
- **FLAGGED sites get a per-site disposition or STOP — never a silent force-fit.** The flag class is
  sites whose semantics depend on the raw representation in trickier ways: a `next_in_room` walk that
  mutates/relocates the current node mid-iteration (needs the save-next-first idiom, NOT `occupants()`);
  code caching `in_room` across a mutation; taking `&ch->in_room`; sorting/comparing by chain-pointer
  order; pointer arithmetic on `world`. (NOTE: a `NOWHERE` comparison on the *result* of `location_of`
  — `location_of(ch) == NOWHERE` — is a MECHANICAL read, fully in-scope for Stage 1, NOT a flag.) Each
  flagged site gets an explicit disposition in the census (convert with the mutation-safe idiom; keep
  raw + allow-list-annotate with rationale; or STOP to the owner if no clean disposition exists).
- **Bounded, block-scoped byte-edits ONLY (the physical-layout lesson).** Every existing-file change
  is a Python byte-edit of a bounded, uniquely-anchored block — measure each file's CRLF profile
  before editing and preserve it (the formatter-hook conflict — see MEMORY). **NO whole-file regex/
  string replaces** (`->in_room` is a substring that appears in comments, `obj->in_room`, writes, and
  the allow-list — a blanket sed would corrupt all four). New test files are the only free-form Writes.
- **DOCKER SYNCHRONOUS, FOREGROUND, `timeout: 600000` on EVERY docker Bash call (standing stall
  lesson).** Every `docker compose run … rots64` gate runs in the FOREGROUND with the Bash tool's
  `timeout` parameter set explicitly to `600000` ms on that call — never backgrounded (a
  backgrounded container gate never resumes a subagent; see MEMORY "Subagent docker-gate stalls").
  Use `--pull never` to avoid this host's registry-probe hang.
- **Per-batch dual-host gates (both hosts, every batch that builds).** macOS arm64 (`cd src && cmake
  --preset macos-arm64 && cmake --build --preset macos-arm64 -j4 && ctest --preset macos-arm64`) AND
  `rots64` (`docker compose run --rm --pull never rots64 bash -lc 'cd /rots/src && cmake --preset
  linux-x64 && cmake --build --preset linux-x64 -j"$(nproc)" && ctest --preset linux-x64'`, `timeout:
  600000`); both boot goldens byte-identical (`scripts/boot-golden.sh --native
  build/macos-arm64/ageland verify` and `scripts/boot-golden.sh --service rots64 verify`); all NINE
  `*LayerAcyclicity` linkchecks green; `ConvertEquivalence` 17/17; `python3
  tools/string_view_census.py --check` exit 0; ctest at the running baseline (1583 + any T1/coverage
  delta). For a combat/damage-touching batch the seed42 characterization golden
  (`CharacterizationCombatTest.DamageTranscriptSeed42`) MUST stay green/unchanged — that is the
  strongest behavior-neutrality witness this wave has.
- **Coverage-gap rule (test count is NOT frozen).** When a conversion touches previously-untested live
  code, the batch adds targeted coverage in the same commit (the standing rule; ASan on any new/
  rewritten test file via `cmake --preset macos-arm64-asan`). Any T1 API-completion lands TDD tests.
  Every delta is censused per task and reconciled in T4. Baseline **1583** both hosts at master
  @`5db2b9e`; deltas MAY be positive — a drop is a bug → STOP. Flat-parity is BINDING same-commit: a
  new test file lands in BOTH `src/tests/Makefile` `SRCS` AND `src/CMakeLists.txt`'s `tests/*.cpp` list.
- **Layer discipline UNCHANGED.** The APIs live in `rots_entity` (L2), already linked by every
  consumer tier — no membership, seam, or linkcheck change is expected in LS-1. Any surfaced coupling
  follows `docs/superpowers/combat-migration-playbook.md`; an UNEXPECTED coupling (a new cross-tier
  edge, a forced new library or linkcheck) is a STOP to the owner.
- **`-Wall -Wextra -Werror` clean** (`/W4 /WX` MSVC); `-funsigned-char` / `/J`; deterministic-FP flags
  unchanged; no `rand()`/`random()`; `std::format` unchanged; `char[N]` decays via
  `static_cast<const char*>` before `std::format`. A warning after a conversion (e.g. a
  const-correctness mismatch from calling `location_of(const char_data*)` on a non-const context, or
  `occupants(room_by_id(id))` where `room_by_id` may be nullptr) means the conversion changed a
  compile contract → fix the conversion, never suppress.
- **i386 battery finalization-only** (`scripts/i386-battery.sh`, sequential, fresh-branch full run;
  per MEMORY 60–90+ min on this Apple Silicon host, can hang — never per-batch, never concurrent). A
  monolithic-runner SIGSEGV is never tolerated (clean container rebuild first, then investigate).
- **STOP-and-adjudicate (to the owner despite the merge grant):** a flagged-site class with no clean
  disposition; any Stage-1 golden or boot drift traced to a conversion; an unexpected cross-tier
  coupling or forced membership/linkcheck change; a `world[` site whose fallback semantics cannot be
  preserved by either resolver; the persist-scope ruling (Discrepancy 1) if T0 opts to defer.
- **MERGE-WHEN-GREEN (owner grant, program spec 2026-07-23).** T5 pushes, requires the six blocking
  CI jobs green (`legacy-32bit`, `linux-x64`, `sanitize-linux`, `macos-arm64`, `sanitize-macos`,
  `windows-msvc`; `clang-tidy-advisory` non-blocking), then ff-merges to master and deletes the
  branch. Named STOPs still go to the owner.
- **Process:** implementers **Sonnet**; T0 census + heavy reviews **Opus**; whole-branch review
  **Fable**. Scratch prefix **`ls1-`** in `.superpowers/sdd/`, gitignored, never committed. Docker
  synchronous foreground in subagents (see the docker rule above).

---

### Task 0: Site census + classification + batch/evidence design (read-only, Opus)

**Files:**
- Create: `.superpowers/sdd/ls1-census.md` (gitignored scratch).

**Interfaces:**
- Consumes: the Stage-1 API surface (`src/handler.h:53-74`, `src/entity/placement.cpp:126-269`); the
  six libraries' `.cpp` bodies; the two-variant `world[` contract; the spec's tracked triple and
  allow-list concept.
- Produces: the per-SITE classification (every raw `->in_room`/`world[`/`next_in_room` in
  `entity`/`persist`/`world`/`combat`/`script`/`olc`, plus the confirmed-zero record for
  `platform`/`core`); each read's mechanical conversion form; the flagged-site list with per-site
  dispositions; the API-gap ruling (→ T1 or no-op); the batch order/sizes; the allow-list file set;
  the per-batch equivalence-evidence design. Every later task cites this file.

- [ ] **Step 1: Build the base.** `cd src && cmake --preset macos-arm64 && cmake --build --preset
  macos-arm64 -j4 && ctest --preset macos-arm64`. Confirm the base: nine libraries, nine
  `*LayerAcyclicity` linkchecks, **1583** tests, `ConvertEquivalence` 17/17, HEAD `24d6fec` + this
  plan commit. Record the baseline numbers.
- [ ] **Step 2: Enumerate + classify every site.** For EVERY raw `->in_room` / `world[` /
  `next_in_room` occurrence in the six source-bearing dirs (`grep -rn` per token per dir), record
  file:line and classify: **READ** (converts), **WRITE** (stays on `set_location`/mutation core — does
  NOT convert), or **ALLOW-LISTED-INTERNAL** (a representation-owner site — does NOT convert). Confirm
  and record `platform`/`core` as zero. Include `persist` per Discrepancy 1 (rule its scope here —
  recommend inclusion). Separate comment/string-literal false hits (e.g. `db_players.cpp:13`) from
  live code. This is the master site table T2 transcribes.
- [ ] **Step 3: Assign each READ its conversion form.**
  - `->in_room` read → `location_of(ch)` (respecting const-context; a `== NOWHERE` / `!= NOWHERE`
    comparison on the result stays mechanical).
  - `world[id]` read → `room_by_id(id)` OR `room_by_id_total(id)` — **decide per site** by whether the
    original bounds-checked `id` and whether out-of-range fallback is observable (record the reasoning;
    default un-bounds-checked → `room_by_id_total`). `world[ch->in_room]` self-room →
    `room_by_id_total(location_of(ch))` (or a T1 `room_of(ch)` if Step 5 justifies it). Note the
    deref shape: `world[id].field` → `room_by_id_total(id)->field`.
  - `next_in_room` traversal → `for (auto* occ : rots::entity::occupants(room))` where `room` is the
    room pointer/`room_by_id(...)`; record the loop-variable rename and any body edits (the old
    `tmpch` → the range var). Confirm the walk does NOT mutate the current node mid-iteration (else →
    Step 4 flag).
- [ ] **Step 4: Flag the non-mechanical sites, each with a disposition.** Identify every site whose
  semantics depend on the raw representation in tricky ways: `next_in_room` walks that extract/relocate
  the current character mid-walk (the `occupants()` lazy-`++` is NOT save-next-first — these need the
  save-next idiom or stay raw+allow-listed); `in_room` cached across a mutation; `&ch->in_room` address
  taken; chain-pointer-order comparisons/sorts; pointer arithmetic on `world`. For each: give an
  explicit disposition (mutation-safe conversion form / keep-raw-with-allow-list-annotation+rationale /
  STOP). Confirm `NOWHERE`-comparison-on-`location_of`-result sites are NOT flagged (they are
  mechanical). `spec_pro.cpp` (90/58/20) and `mage`/`ranger`/`mobact` are the highest flag-density
  candidates — body-read them end-to-end.
- [ ] **Step 5: Rule the API gap → T1 scope or no-op.** From Steps 3-4, determine whether the
  conversions need any API the tree lacks: (a) an `occupants(const room_data*)` const overload (if any
  converted read walks a `const room_data*`'s occupants); (b) a `room_of(ch)`/`room_ptr_of(ch)`
  convenience returning `room_data*` (ONLY if self-room `world[ch->in_room]` sites are numerous enough
  to justify over nested `room_by_id_total(location_of(ch))` — YAGNI otherwise); (c) any other
  genuinely-missing convenience a specific site needs. List ONLY what a real site requires. If none,
  state explicitly that T1 collapses to a no-op.
- [ ] **Step 6: Batch order, sizes, and allow-list.** Set the T2 batch list in tier order:
  `entity` (convertible TUs only — allow-list excluded) → `persist` (if ruled in) → `world` → `combat`
  → `pathfind` → `script` → `olc`. Rule sub-batching for the large libraries (recommend per-TU
  sub-commits for `combat` — `mage`/`ranger`/`fight` each sizable — and `script` —
  `spec_pro.cpp`'s 168 sites alone warrant its own commit); small libs (`pathfind`, `olc`, `persist`,
  `weather`) one commit. Name the definitive **allow-list file set** (`placement.cpp` + `containment.cpp`
  at minimum; rule `equipment.cpp` and any other entity TU in or out with per-file rationale) — this is
  what T3's grep gate exempts.
- [ ] **Step 7: Per-batch equivalence-evidence design.** State explicitly that `nm` is NOT the evidence
  (conversions change object code legitimately). Define the per-batch evidence: full dual-host ctest
  all-green at the running baseline; both boot goldens byte-identical; nine linkchecks; ConvertEquivalence
  17/17; string-view census exit 0; **and for every combat/damage-touching batch, the seed42
  characterization golden unchanged**. Identify which batches are combat/damage-touching (all of
  `combat`, plus any `script`/`world` TU feeding the damage/visibility path). Specify the coverage-gap
  riders each batch likely needs (which converted TUs have thin/no existing coverage — e.g. does
  `spec_pro.cpp` / `graph.cpp` / `weather.cpp` have targeted tests? — so T2 adds them per the gap rule).
- [ ] **Step 8: T3 grep-gate design.** Specify the checked-in mechanism: a `tools/location_read_census.py`
  modeled on `tools/string_view_census.py` (rglob file discovery, `--check` mode, exit non-zero on a
  violation) that asserts the six converted libraries contain ZERO raw `->in_room`/`world[`/
  `next_in_room` outside the named allow-list, PLUS a ctest registration so it runs in both build
  systems' test flows (mirroring how the standing gates invoke the string-view census). Define the
  allow-list encoding (a file set, like string-view-exceptions' owner-path column) and how a legitimate
  allow-listed line is annotated. State that `zone_table[` is NOT in the gate (Discrepancy 2).
- [ ] **Step 9: Write the census.** Finalize `ls1-census.md`: the master site table (per-site
  READ/WRITE/allow-list + conversion form + resolver choice), the flagged-site dispositions, the API-gap
  ruling, the batch list with sub-batching and allow-list file set, the per-batch evidence + coverage-rider
  map, the T3 gate design, and the two recorded discrepancies' rulings. Flag any STOP candidate with
  evidence. No code changes this task.

---

### Task 1: API completion (consumer-free, Sonnet — CONDITIONAL on the census)

> Lands ONLY the API additions T0 Step 5 justified (an `occupants(const room_data*)` const overload;
> a `room_of(ch)` convenience; nothing else). Consumer-free (no call site converts here — that is T2),
> TDD, each addition a zero-cost inline wrapping today's representation. **If T0 Step 5 found NO gap,
> this task is a NO-OP: record "no API gap — T1 skipped" in the task report and proceed to T2.**

**Files (only if the census justified an addition):**
- Modify: `src/handler.h` and/or `src/entity/placement.cpp` (the census-named additions; bounded
  Python byte-edit; preserve CRLF).
- Create (if a new test file): `src/tests/placement_api_tests.cpp` or extend an existing
  `src/tests/placement_tests.cpp` — TDD tests for each new API (const-overload iteration, `room_of`
  returning the same pointer `room_by_id_total(location_of(ch))` would). Wire a NEW file into BOTH
  `src/tests/Makefile` `SRCS` AND `src/CMakeLists.txt`'s `tests/*.cpp` list (~line 1404 region) in
  the SAME commit.

**Interfaces:**
- Consumes: `ls1-census.md` Step 5 (the exact justified additions and their contracts).
- Produces: the API surface T2's conversions need, each proven equivalent to the existing
  representation by a failing-then-passing test — with ZERO consumers changed yet.

- [ ] **Step 1:** For each census-justified addition, write the failing test FIRST (TDD): e.g. a const
  overload iterating a `const room_data*`'s occupants yields the same sequence as the non-const range;
  `room_of(ch)` returns the identical pointer to `room_by_id_total(location_of(ch))`. Confirm it fails
  (API absent), then implement the zero-cost inline, confirm it passes.
- [ ] **Step 2:** Implement each addition as a thin inline wrapping today's representation (the const
  overload mirrors `occupant_range`'s lazy walk over a `const room_data*` yielding `const char_data*`;
  `room_of` is `return room_by_id_total(location_of(ch));`). No behavior beyond what the existing APIs
  already express. Bounded byte-edit; preserve CRLF.
- [ ] **Step 3:** Flat-parity if a new test file (both build systems, same commit); grep-verify both
  lists contain it.
- [ ] **Step 4:** Full dual-host gates + `macos-arm64-asan` (new/changed test file). Goldens UNCHANGED
  (consumer-free — if any golden drifts here, an addition touched live behavior → bug → STOP). Commit
  (API + tests). If no-op: no commit; record the skip.

---

### Task 2: Per-library read conversions in tier order (Sonnet, per-batch commits)

> Batch order and sub-batching per `ls1-census.md` Step 6 (default: `entity` convertible TUs →
> `persist` if ruled in → `world` → `combat` (per-TU sub-commits) → `pathfind` → `script`
> (`spec_pro.cpp` its own commit) → `olc`). Each batch applies the census's per-site conversion list
> for that library with bounded byte-edits, runs the full per-batch evidence set, adds coverage riders
> where the census flagged untested touched code, and commits. Golden/boot drift or ANY test failure =
> STOP for that batch (a conversion was not representation-neutral).

**Files (per batch `<lib>`/`<TU>`):**
- Modify: the batch's `.cpp` files (bounded Python byte-edits per the census's per-site list —
  `->in_room` → `location_of`, `world[id]` → the census-chosen resolver, `next_in_room` walks →
  `occupants(...)` range-for; preserve CRLF; NEVER a whole-file regex). WRITE sites and allow-listed
  sites are left untouched.
- Modify/Create (if the census flagged untested touched code): the batch's targeted coverage tests
  (extend an existing `src/tests/*_tests.cpp` or a new file, flat-paired same-commit; ASan on a
  new/rewritten file).

**Interfaces:**
- Consumes: `ls1-census.md` (the per-site conversion list, resolver choices, flagged dispositions,
  coverage-rider map, per-batch evidence design); the Stage-1 API; any T1 addition.
- Produces: `<lib>`'s reads routed through the Placement APIs; the representation no longer exposed to
  that library's non-allow-listed code; behavior byte-identical (goldens unchanged).

- [ ] **Step 1 (per batch):** Apply the census's per-site conversions for this batch as bounded
  byte-edits. For each `world[` site use the census-specified resolver (`room_by_id` vs
  `room_by_id_total`) — do NOT guess. For each `next_in_room` walk use `occupants(...)`; for a
  census-FLAGGED walk use the flagged disposition (mutation-safe idiom or keep-raw+annotate), never a
  naive `occupants()` substitution. Do NOT touch WRITE or allow-listed sites.
- [ ] **Step 2 (per batch):** Add the census's coverage riders for any previously-untested converted
  live code (gap rule), flat-paired same-commit; ASan (`macos-arm64-asan`) if a test file is new/
  rewritten.
- [ ] **Step 3 (per batch):** Full per-batch evidence set (Global Constraints + census Step 7): dual-host
  build + ctest all-green at the running baseline; both boot goldens byte-identical; nine linkchecks;
  ConvertEquivalence 17/17; string-view census exit 0; **seed42 characterization golden unchanged for a
  combat/damage-touching batch**. Docker gate FOREGROUND, `timeout: 600000`. ANY golden/boot drift or
  test failure = STOP (fix the conversion, never the golden). Commit per batch/sub-batch.
- [ ] **Step 4:** After all batches, confirm (via a dry run of the T3 census script or a manual grep)
  that the six libraries hold raw `->in_room`/`world[`/`next_in_room` ONLY inside the allow-list. Record
  the per-batch evidence + coverage deltas for the T4 reconciliation and T5 report.

---

### Task 3: The LS-1 grep gate (Sonnet)

> A checked-in, ctest-registered census (`tools/location_read_census.py`, modeled on
> `tools/string_view_census.py`) that asserts the six converted libraries contain ZERO raw location
> reads outside the census-named allow-list — the wave's exit criterion for the library tier and the
> regression guard LS-2 extends to the app tier.

**Files:**
- Create: `tools/location_read_census.py` (`--check` mode; rglob-discovers `src/{entity,persist,world,
  combat,pathfind,olc,script}/*.cpp`; flags any raw `->in_room`/`world[`/`next_in_room` outside the
  allow-list file set; exit non-zero on violation). Allow-list encoded as a named file set (mirroring
  string-view-exceptions' owner-path column), with a documented per-line annotation convention for any
  legitimately-retained allow-listed access.
- Modify: `src/CMakeLists.txt` (register a ctest `add_test(NAME LocationReadCensus …)` invoking the
  script — mirror the existing linkcheck/ctest registration blocks; bounded byte-edit) AND wire it into
  the flat-Makefile test flow / root `Makefile` `test` target if the census/CI cadence uses that path
  (the recurring new-check-missing-from-a-build-list gap — verify BOTH systems run it).
- Modify (if a doc-side allow-list registry is used): a small `docs/superpowers/` allow-list note the
  script reads, per the census's Step 8 encoding.

**Interfaces:**
- Consumes: `ls1-census.md` Step 8 (the gate design + allow-list file set).
- Produces: `location_read_census.py --check` exit 0 on the converted tree; a ctest that FAILS if any
  future edit reintroduces a raw library-tier location read outside the allow-list.

- [ ] **Step 1:** Write `tools/location_read_census.py` per the census design; run `--check` — it must
  exit 0 against the fully-converted tree (proving T2 left zero raw reads outside the allow-list) and
  exit non-zero against a deliberately re-inserted raw read (a smoke self-test of the gate).
- [ ] **Step 2:** Register it as a ctest in `src/CMakeLists.txt` and confirm it runs under both
  `ctest --preset macos-arm64` and the `rots64` ctest; wire it into the flat-Makefile/root `Makefile`
  test path too if that path is part of the standing cadence (verify with a `make -n`/dry check — do
  NOT let a new check exist in only one build system).
- [ ] **Step 3:** Full dual-host gates: build + ctest (now +1 for `LocationReadCensus`, both hosts);
  both boot goldens byte-identical; nine linkchecks; ConvertEquivalence 17/17; string-view census exit
  0; the new gate green. ASan not required (a Python gate + a CMake test registration, no C++ test-file
  content change) unless a C++ test wrapper is added. Commit (the gate + its registration).

---

### Task 4: Docs (Sonnet)

**Files:**
- Modify: `AGENTS.md` — add a SHORT Testing-Guidelines chain entry for Wave LS-1 (baseline **1583**,
  per-batch/T1/coverage deltas reconciled to the final count; the six libraries' reads now routed
  through the Stage-1 Placement APIs; the new `LocationReadCensus` gate; no membership/DEFER change —
  combat row stays DONE; cite the finalization i386 numbers once T5 measures them, or mark "pending
  T5"). Match the house chain style (dense, per-wave, reconciled).
- Modify: `docs/BUILD.md` — Library-layering / location note: Stage 1 library-tier read conversion
  DONE; the Placement API surface (`location_of`/`room_by_id`/`room_by_id_total`/`occupants`) is now
  load-bearing; the `world[id]` two-variant resolver rule; the allow-list file set; the new
  `LocationReadCensus` regression gate; `zone_table` explicitly out of the location-read charter
  (Discrepancy 2); LS-2 (app tier) and LS-3 (representation swap) noted as the remaining arc.
- Modify: `docs/superpowers/specs/2026-07-23-locationsystem-program-design.md` — append an LS-1
  **As-built** section (the final batch list as landed; the persist-scope ruling; any T1 API addition;
  the flagged-site dispositions; the allow-list file set; the grep-gate mechanism; the reconciled test
  deltas; any STOP raised and its disposition).
- Modify: load-bearing CURRENT-state references in other docs the census flags (playbook/specs) where
  they describe the Stage-1 APIs as "unused/seeded only"; historical narration is EXEMPT (house
  precedent — do not rewrite past-tense wave narration).

**Interfaces:**
- Consumes: the T2 as-landed batch reports + coverage deltas; T0's classification/allow-list; the T3
  gate; the T5 i386 numbers (or "pending T5").

- [ ] **Step 1:** Write the AGENTS.md chain entry + BUILD.md location note (Python byte-edit any `.md`
  under a CRLF profile — measure first). Append the spec As-built section.
- [ ] **Step 2:** Sweep the flagged CURRENT-state doc references; update to reflect the APIs are now
  load-bearing; leave historical narration untouched.
- [ ] **Step 3:** Gates: docs-only needs no build gate, but run `string_view_census.py --check`
  (exit 0); if any `src` comment/build file was touched incidentally, run the full dual-host gate once.
  Commit (docs; the As-built append may be a separate commit).

---

### Task 5: Finalization (i386 battery → Fable whole-branch review → CI-green PR → MERGE-WHEN-GREEN)

- [ ] **Step 1: i386 battery** — `scripts/i386-battery.sh` (sequential, fresh-branch full run; per
  MEMORY 60–90+ min, can hang — never concurrent). Because conversions changed object code, the
  container must configure CLEAN (stale objects would link pre-conversion bodies — the stale-object
  rule). Expect ctest N/N (N = 1583 + wave delta) with the same 6 did-not-run skips; the monolithic
  reconciliation exact (the ctest-only linkcheck count rises by 1 for `LocationReadCensus` — nine
  linkchecks + the new gate; 23 − 17 monolithic-only `PerRace/ConvertEquivalence.*` = the identical
  6-test remainder both ways — the standing method); boot golden matches. A monolithic SIGSEGV = clean
  rebuild + investigate, never tolerate.
- [ ] **Step 2: Whole-branch review (Fable)** — special attention: (a) **sampled per-site conversion
  correctness** — re-derive a random sample of ~30 converted sites against the pre-conversion code
  (`git show`), confirming each is a pure read, the right resolver (`room_by_id` vs `room_by_id_total`)
  was chosen for its bounds/fallback semantics, and each `occupants()` walk does not mutate the current
  node mid-iteration; (b) **the flagged-site dispositions** — each was applied as the census ruled, no
  silent force-fit; (c) **the grep gate's allow-list justification** — every allow-listed file is a
  genuine representation owner, not a conversion that was skipped; (d) no WRITE site was converted; no
  golden regenerated; no membership/linkcheck/struct change. Doc-only fixes during the battery are OK;
  any `src`/build-file fix invalidates the battery → re-run Step 1.
- [ ] **Step 3: Push + PR + CI** — open the PR; require all six blocking CI jobs green (`legacy-32bit`,
  `linux-x64`, `sanitize-linux`, `macos-arm64`, `sanitize-macos`, `windows-msvc`; `clang-tidy-advisory`
  non-blocking). PR body: Stage-1 library-tier read conversion, zero behavior change (goldens
  byte-identical), the new `LocationReadCensus` gate, the reconciled test deltas.
- [ ] **Step 4: MERGE-WHEN-GREEN (owner grant)** — once all six jobs are green, ff-merge to master and
  delete `arch/ls1-library-reads`.
- [ ] **Step 5: Bookkeeping** — ledger entry (LS-1 landed; six libraries' reads routed through the
  Placement APIs; `LocationReadCensus` gate live; combat DEFER stays 0; test count reconciled); MEMORY
  update (`library-split-progress.md`: LocationSystem program status LS-1 DONE → **LS-2 (app-tier read
  conversion, `src/app/*` + test fixtures) next**, then LS-3 the swap; the new grep gate LS-2 extends
  to the app tier). Report the merged wave to the owner.

---

## Uncertainties flagged inside this plan (for the owner)

1. **`rots_persist` scope (Discrepancy 1).** The spec named six source-bearing libraries and expected
   persist near-zero, but `db_players.cpp` has 2 real read sites (+1 write, +1 comment). Persist is a
   library, so its reads belong to LS-1, not LS-2 — leaving them stranded breaks LS-2's exit criterion.
   This plan RECOMMENDS T0 rule persist into LS-1 scope (a tiny batch). If T0 instead defers, that is a
   named STOP to the owner (LS-2's app-tier grep gate would need to carve a library exception —
   undesirable).
2. **`world[id]` resolver choice is the highest-subtlety class.** `room_by_id` (nullptr-on-invalid) vs
   `room_by_id_total` (operator[] graceful fallback) is a per-site behavior decision the goldens may
   NOT catch (an invalid-id fallback path is rarely exercised in a boot/seed42 transcript). The census
   makes each choice explicit and the Fable review re-derives a sample — but this is where a silent
   behavior change is most likely to slip a green gate. Called out so the owner weights the review here.
3. **`occupants()` is not the save-next-first idiom.** Its lazy `operator++` reads `next_in_room` at
   increment time, so any `next_in_room` walk that extracts/relocates the current character
   mid-iteration is NOT mechanically convertible and MUST be flagged (census Step 4). A missed one
   would iterate a mutated chain — a real bug the goldens might or might not surface. The
   highest-density candidates are `spec_pro.cpp` (20 `next_in_room`), `mage.cpp` (11), `ranger.cpp`
   (6), `mobact.cpp`/`olog_hai.cpp`/`mudlle.cpp`.
4. **API-gap ruling drives whether T1 exists.** T1 is conditional on T0 Step 5. This plan expects at
   most a small addition (a const `occupants` overload and/or a `room_of(ch)` convenience) or a no-op;
   if T0 surfaces a larger gap (e.g. an occupant-walk variant the seeded range cannot express), that is
   a scope signal to the owner before T2 begins.
5. **`zone_table[` deliberately excluded (Discrepancy 2).** ~201 `zone_table[` reads exist and
   `zone_by_id()` resolves them, but the program's tracked triple and every gate are `->in_room`/
   `world[`/`next_in_room` only. This plan keeps `zone_table` out of LS-1 as a conscious scope
   boundary; if the owner wants zone-read encapsulation, it is a separate charter, not a silent add.
