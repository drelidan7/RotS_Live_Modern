# Cluster B Wave (`script.cpp` + the six `shape*.cpp` OLC editors) — Design

**Date:** 2026-07-21 · **Branch:** `arch/cluster-b`, off master @`12f9f2d` (behavior wave merged).
· **Predecessors:** the behavior wave (`rots_combat` 12 TUs, `rots_script` 4 TUs, first permanent
L3→L4 inversion), the l4-seed wave (stood up the L4 band — `rots_pathfind`/`rots_script`), and the
migration recipe in `docs/superpowers/combat-migration-playbook.md`. Scoping evidence:
`.superpowers/sdd/world-growth-census.md` (the world-growth census, sections 4-6), whose Cluster B
component this wave promotes. This is the **owner-selected next wave** (chosen over the
spell-family closure and the spec_pro/spec_ass pair), consolidating the authoring band while the
L4 patterns are hot and building more L4 precedent ahead of spec_pro's tier question.

## Problem / evidence

Cluster B is the world-growth census's 7-TU connected component: `script.cpp` (the runtime script
driver) plus the six interactive OLC editors (`shapemdl.cpp`, `shapemob.cpp`, `shapeobj.cpp`,
`shaperom.cpp`, `shapescript.cpp`, `shapezon.cpp`). All seven are still app-compiled. Census
facts at the scoping base (T0 re-derives everything with `nm` at the wave base):

- **Blocking-edge counts:** `script.cpp` = 20 (the heaviest TU ever censused), `shapemob` = 3,
  `shaperom`/`shapescript` = 2, `shapemdl`/`shapeobj`/`shapezon` = 1.
- **The shared session edge:** every one of the six editors calls
  `string_add_init(descriptor_data*, char**)` (modify.cpp:175) — the interactive multi-line text
  editor state machine (writes `d->str`/`d->max_str`/`d->len_str`, calls `send_to_char`).
  Genuinely session-coupled, cannot relocate; **one** shared hook breaks it for all six.
- **Intra-cluster graph:** `shapemob.cpp` is the hub (`shape_center_*` fan-out to the other five;
  `clean_text`/`shape_standup`/`get_permission`/`get_text` fan-in from four of them);
  `shapemdl → mudlle_converter` (mudlle.cpp — now `rots_script`, so a **legal downward edge**);
  and the **mutual edge** `script.cpp → get_param_text` (shapescript.cpp) /
  `shapescript.cpp → find_script_by_number` (script.cpp) — the pair that constrains membership
  under the no-bidirectional-links invariant.
- **Edges the behavior wave already resolved since the census was written** (T0 confirms each):
  - `gain_exp` (script.cpp) — the census's named combat-peer STOP-risk. `limits.cpp` is now
    `rots_combat` (L3), so this is a legal downward call from the L4 band. **Dissolved.**
  - `char_from_room` (script.cpp) — the behavior wave's `entity_hooks.h` hook exists; if
    script.cpp's call shape matches, conversion is mechanical.
  - `pkill_get_rank_by_character` (script.cpp) — relocated to `rots_persist`/`db_players.cpp` in
    the behavior wave; now a legal downward call.
- **The remaining combat-peer STOP-risk:** `virt_assignmob` (spec_ass.cpp, called at
  shapemob.cpp:1838). `spec_ass.cpp` is DEFER-row, still app-tier, and cannot relocate (same
  spec-proc-family drag as `virt_program_number`). Same shape as the behavior wave's
  `dispatch_virt_program_number` cell — the parent spec's pre-authorized rider gate covers ≤3
  same-shape edges; this is **edge 2 of 3**.
- **New `combat_command` cells needed:** `emote` (script.cpp → `do_emote`), `action`
  (script.cpp → `do_action`), `shutdown` (shapemob.cpp:2105 → `do_shutdown`, a confirmed-genuine
  builder "quit and save" path). The 26-cell table grows to 29 (dismount precedent).
- **Output seam:** `send_to_room_except(...)` (script.cpp) — a small forwarder addition, same
  shape as the blocker-buster wave's seven.
- **Relocate candidates needing body-reads (T0 adjudicates each):** `find_action` (act_soci.cpp),
  `find_eq_pos` (act_obj2.cpp), the `perform_drop`/`perform_give`/`perform_wear`/`perform_remove`
  quartet (act_obj1/2.cpp), `pkill_get_evil_fame`/`pkill_get_good_fame` (pkill.cpp), and
  `get_param_text` (shapescript.cpp — the mutual-edge breaker).
- `_buf` retirement applies in `shaperom.cpp`/`shapescript.cpp` (local composition, standard).

## Decision (owner-approved)

**One wave, six tasks, single branch/PR/battery.** The owner granted an explicit per-wave
**merge-when-green** authorization (same terms as PRs #13-16: battery green + all seven CI checks
green on the PR → controller fast-forward merges and deletes the branch).

### Membership (default lean — T0 census adjudicates with `nm`/body-read evidence)

- The **six `shape*` editors → a NEW `rots_olc` static library** at the top of the L4 band. The
  certified order extends to
  `platform < core < entity < persist < world < combat < pathfind < script < olc < app`.
- **`script.cpp` joins `rots_script`** (runtime driver homes with the runtime band) **IF** the T0
  body-read shows `get_param_text` relocates cleanly out of `shapescript.cpp` (or the mutual edge
  otherwise breaks one-directionally). **ELSE** `script.cpp` rides into `rots_olc` with
  `shapescript.cpp` (intra-lib mutual edge, invariant satisfied) — the honest fallback, accepted
  by the owner with the naming caveat recorded.
- Either outcome adds a ninth CI-enforced **`OlcLayerAcyclicity`** linkcheck, same shape as the
  existing eight. If `script.cpp` lands in `rots_script` instead, `ScriptLayerAcyclicity` covers
  it and `rots_olc` PUBLIC-links `RotS::script`.
- The intra-subset rule (playbook) applies at full strength: the six editors co-migrate in one
  membership commit (the `shape_center_*` fan-in/fan-out makes standalone promotion impossible);
  `script.cpp`'s membership commit is separate only if it targets a different library.

### Task structure

- **T0 — census refresh (read-only).** Fresh `nm` closure over all 7 TUs at the wave base;
  confirm the three behavior-wave-resolved edges; adjudicate every relocate candidate and the
  membership question above; enumerate call shapes for the three new cells; produce
  `.superpowers/sdd/cb-census.md`. STOP rules per playbook (a genuinely new seam taxonomy or a
  rider-gate breach → owner).
- **T1 — seams, consumer-free.** `dispatch_string_editor_init` hook (registered by `modify.cpp`
  at boot, abort-tripwire default per the no-death-test rule's established pattern);
  `emote`/`action`/`shutdown` cells; `send_to_room_except` forwarder; `virt_assignmob`
  `script_hooks.h` cell (rider edge 2 of ≤3). Discriminator pairs for every new cell/hook.
- **T2 — `script.cpp` conversions.** Up-call conversions onto cells/seams, relocates per T0
  adjudication, `get_param_text` disposition, `_buf`-free confirmation.
- **T3 — shape-family conversions.** The shared hook conversion ×6, `shapemob`'s
  `shutdown`/`virt_assignmob` conversions, `_buf` retirement, relocates per T0.
- **T4 — membership + linkcheck.** `ROTS_OLC_SOURCES` (and `script.cpp`'s move), the
  `OlcLayerAcyclicity` linkcheck, CMake wiring for all presets, flat-Makefile parity per the
  established pattern.
- **T5 — docs pass + finalization.** BUILD.md "Library layering" section, AGENTS.md test-chain +
  library inventory, playbook as-built section, parent-spec downstream note; the standing
  **sweep backlog rides here** (fight.cpp:1636-41 stale spllog comment; limits.cpp:54 dead
  `circle_shutdown` extern; limits.cpp dead `output_seam.h` include; output_seam.h:112 stale
  "mudlle still app-compiled" claim; interpre.cpp:99/spell_pa.cpp:51 `one_mobile_activity`
  fwd-decl strays; signals.cpp:176 phantom 1-arg `close_socket`). Then the i386 battery,
  push/PR/CI, and the pre-authorized merge.

## Adjudication defaults (T0 confirms or overturns with evidence)

| Edge / symbol | Default disposition | Overturn condition |
|---|---|---|
| `string_add_init` ×6 | One shared hook, `modify.cpp` registers | none anticipated — session-coupled, confirmed by body-read |
| `get_param_text` | RELOCATE (breaks the mutual edge; script.cpp → rots_script) | body proves it inseparable from shapescript's editor state → script.cpp rides into rots_olc |
| `virt_assignmob` | `script_hooks.h` abort-tripwire cell (rider 2/3) | none — cannot relocate by construction |
| `do_emote`/`do_action`/`do_shutdown` | New cells 27-29 | a call shape mismatch → per-case hook |
| `find_action`, `find_eq_pos`, `perform_*` ×4 | RELOCATE (L2/L3 per body) | session-coupled body → hook or stays-app with the call converted |
| `pkill_get_evil/good_fame` | RELOCATE to `rots_persist` (behavior-wave `pkill_get_rank` precedent) | storage proves session-tier |
| `gain_exp`, `char_from_room`, `pkill_get_rank_by_character` | Already resolved (behavior wave) — confirm only | census finds a second unresolved call shape |
| `send_to_room_except` | output_seam forwarder | none anticipated |
| `shapemdl → mudlle_converter` | Legal downward edge, no work | — |
| `script.cpp → update_pos`/`raw_kill`/`set_call_trigger_hook` | Legal downward edges into `rots_combat` | — |

## Verification

Per-task: native macOS arm64 + `rots64` builds, full ctest, characterization goldens, boot goldens
on both; ASan (`macos-arm64-asan`) on any new or substantially rewritten test file (trio T2
lesson: additive-only is not an exemption). Discriminator registered/unregistered pairs for every
new cell and hook; the coverage-gap rule applies to any untested live code the wave surfaces.
Finalization: the sequential i386 battery (`scripts/i386-battery.sh`, per-commit markers), the
monolithic-runner reconciliation per the standing five-wave method, boot golden, then push/PR and
all six blocking CI jobs (+`clang-tidy-advisory` non-blocking). Combat smoke harness remains
capture-only/informational. Test-count deltas recorded per task in the AGENTS.md chain style.

## Risks

- **`script.cpp`'s ~6 unadjudicated body-reads** are the largest unknown; any of the `perform_*`
  quartet proving session-coupled adds hooks, not STOPs (known shapes exist). Budgeted as normal
  playbook variance.
- **The `string_add_init` hook must reproduce the editor state machine's observable behavior
  exactly** — characterization-first tests before conversion, per the poison-hook identity-proof
  precedent.
- **Rider gate head-room:** `virt_assignmob` consumes edge 2 of the pre-authorized ≤3. A third
  same-shape edge surfacing in T0 consumes the last slot; a fourth is an auto-STOP to the owner.
- **`do_shutdown` semantics:** the builder path force-shuts-down the server; its discriminator
  test must not actually invoke a real shutdown body (default-unregistered pair only, real-body
  test via the dispatch flag pattern).
- **Naming risk if the fallback fires:** `rots_olc` containing the runtime driver is misleading;
  if T0 lands there, record the caveat in BUILD.md rather than inventing a second new library.

## Out of scope

The spell-family closure (spell_pa + mage + ranger), the spec_pro/spec_ass gated pair (combat
DEFER stays 5 this wave — Cluster B TUs were never DEFER-row members), Stage 2 LocationSystem,
the rots_commands census, int→double (blocked on all-JSON data), and any change to the six
editors' user-visible OLC behavior. `zone.cpp`, Cluster A (`graph`/`mudlle`/`mudlle2`), and the
isolated candidates are already resolved by prior waves.

## Process

Subagent-driven per the standing recipe: Sonnet implementers, Opus for the T0 census and heavy
per-task reviews, Fable for the whole-branch review (escalation-gate milestone check). Briefs,
reports, and the census live in `.superpowers/sdd/` (gitignored — never committed). Python
byte-edits for all existing `.cpp`/`.h` files (formatter-hook conflict). Docker gates run
synchronously inside subagents (auto-backgrounded gates stall). i386 battery is
finalization-only. Merge-when-green is an explicit, this-wave-only grant; no standing authority
carries forward.

## As-built (Tasks 0-4 complete; Task 5a docs, this section + sweep backlog; Task 5b finalization pending)

**Status: both memberships landed, `OlcLayerAcyclicity` (the new ninth linkcheck) green first try,
both hosts, zero census misses at any membership gate.** `script.cpp` → `rots_script` (4 → 5 TUs,
commit `5b08068`); the six `shape*.cpp` editors + `editor_hooks.cpp` → new `rots_olc` (7 TUs, commit
`749a590`). ctest 1446 → 1468 (T1 +19, T2 +2, T3 +0, T4 +1). Full per-task evidence:
`.superpowers/sdd/cb-task-{0,1,2,3,4}-report.md`; census: `.superpowers/sdd/cb-census.md`.

**The mutual-edge conditional resolved exactly as scoped — the `rots_olc` fallback never fired.**
This design's own "Decision" section made `script.cpp`'s destination depend entirely on one
body-read: RELOCATE `get_param_text` cleanly and `script.cpp` joins `rots_script`; find it
editor-state-coupled and `script.cpp` rides into `rots_olc` instead (the honest fallback, naming
caveat recorded). T0's read of `get_param_text` (`shapescript.cpp:2613`) found a pure
`int → const char*` switch over `SCRIPT_PARAM_*` constants (shared `script.h`), zero
`descriptor_data`/editor-state coupling — RELOCATE held. Task 1 moved it byte-verbatim into
`script.cpp`; Task 4's membership commit needed zero source edits as a result — a pure
`CMakeLists.txt` list move.

**Task 0's one adjudication-table OVERTURN: `find_action`.** The table above defaulted it to
RELOCATE alongside `find_eq_pos`/the `perform_*` quartet. Task 0's body-read found it reads app-tier
`soc_mess_list` (`act_soci.cpp`) — falls to a SAFE-SENTINEL (−1) accessor hook instead, the same
class as the l4-seed wave's `pkill_get_good_fame`/`pkill_get_evil_fame` OVERTURNs, not a new
taxonomy. Every other adjudication-table default CONFIRMED exactly as specced: `string_add_init`
(standalone `editor_hooks.h`, not `output_seam.h` — the header-scope overturn recorded in the
Problem/evidence section above was itself confirmed, not merely proposed), `virt_assignmob`
(rider 2/3), `do_emote`/`do_action`/`do_shutdown` (new cells 27-29), `find_eq_pos`/`perform_*`
(RELOCATE), `pkill_get_evil_fame`/`pkill_get_good_fame` (RELOCATE — to the l4-seed world hooks, not
`rots_persist`, per the l4-seed precedent this table already cited), `gain_exp`/`char_from_room`/
`pkill_get_rank_by_character` (already resolved, confirmed only), `send_to_room_except`
(output_seam forwarder), `shapemdl → mudlle_converter`/`script.cpp`'s combat-tier calls (legal
downward edges, no work).

**The rider gate closed at exactly 2, not the pre-authorized ceiling of 3.** `virt_assignmob`'s full
enumeration (every call across all 7 Cluster B TUs cross-referenced against every function defined
in `spec_ass.cpp`/`spec_pro.cpp`) found exactly the one edge this design anticipated
(`shapemob.cpp:1838`) and no second/third/fourth edge of the same or a different shape. No
auto-STOP fired. One slot remains for the still-undecided `spec_pro`/`spec_ass` pair's own eventual
promotion.

**One genuine census miss, self-resolved same-task, not a STOP.** `perform_wear`'s three file-local
helpers (`wear_message`/`ologhai_item_restriction`/`beorning_item_restriction`) were invisible to
the census's cross-TU `nm` method (their only caller was same-TU at census time). Task 1 found them
while relocating `perform_wear`, confirmed zero other callers tree-wide, and moved all three
alongside it into `fight.cpp` — the alternative (leaving them behind) would have been a real
`rots_combat → app` upward edge. Flagged for controller review per the STOP-and-adjudicate contract
rather than halted, since the resolution required no new architectural judgment and was
mechanically `nm`-verified.

**The CRLF risk this design flagged materialized exactly as anticipated, in both directions.**
`shapescript.cpp` measured pure-LF (0/2713 CRLF lines) — the lone such file among the seven; every
other touched file (the five other `shape*.cpp` editors, `script.cpp`, `act_obj1.cpp`/
`act_obj2.cpp`, and every touched header/seam file) was mixed-CRLF, confirming the "do not assume
LF" warning. `get_param_text`'s LF bytes landed verbatim inside mixed-CRLF `script.cpp`; the
`perform_*`/`find_eq_pos` family instead LF-normalized into the pure-LF `fight.cpp`/`equipment.cpp` —
two different resolutions of the same underlying rule (preserve the destination's own convention),
not an inconsistency.

**`do_shutdown`'s builder-path risk was honored exactly as scoped.** Its discriminator test uses a
recording stub, never the real force-shutdown body — the registered/unregistered pair pattern, no
death test. The non-vacuity probe for the new `OlcLayerAcyclicity` linkcheck used a different
app-tier symbol (`boot_db()`, `db_boot.cpp`) instead of `do_shutdown`, since the checker's own
CMakeLists.txt comment initially (incorrectly) named `do_shutdown`/`act_wiz.cpp` as the probe target —
a Task 4 review Minor finding, corrected in this docs pass to match the actual evidence
(`cb-task-4-report.md`).

See `docs/BUILD.md`'s "The Cluster B wave" subsection (under "Library layering"),
`docs/superpowers/specs/2026-07-16-library-architecture-design.md`'s §3 order string and "As-built
(Cluster B wave, step 4 tenth slice)" note, and `docs/superpowers/combat-migration-playbook.md`'s
"The Cluster B wave" section for the full cross-referenced account.
