---
id: doc-001
title: TASK-015 release-frodo triage and architecture design
type: specification
created_date: '2026-09-07 03:20'
updated_date: '2026-09-07 16:07'
---
# TASK-015: release-frodo triage and architecture design

## Purpose

Port the latest live-game fixes and features into the current modernized branch while preserving
its module boundaries, portability and subsequent gameplay fixes. The owner approved this design on 2026-09-07 with the local uaf-port addition below. This is a source inventory, not an implementation-completion report. Mutable status, acceptance criteria
and next actions belong to [TASK-015](../../tasks/task-015%20-%20Port-release-frodo-delta-40-commits-through-upstream-PR-279.md).
The parent narrative is [Upstream Sync](../arc-upstream-sync.md).

## Dependencies and source baseline

As of 2026-09-06 (America/New_York), the owner explicitly selected the latest release-frodo tip.
GitHub `git ls-remote` and a subsequent `git fetch --no-tags upstream release-frodo` both confirm
`e0458069453f362e544a0c12b2655879e2da6979`. The existing tracking reference already matched.
The target is `fix/task-025-summon-dark-ok` at `c88186b5f0c4d643d850ced7e14b6b45c197f757`.
The initial working tree was clean. No branch change, merge, commit or production access occurred.

The merge base is `73734ee54e9c583170a9e0d7d09ebe9433676305`. The original range through
`e6641684` contains 40 commits; the selected range contains 72 (61 non-merges and 11 merges).
Crucially, squash `f26d3140` and already-reachable `a695658` share tree
`5ff21eca7efad94ec97ab0e403a22782627366d7`: the enormous account-management squash is entirely
accounted for by an exact tree match, rather than inferred from similar titles. All 11 merge
commits produce no remerge diff; their parent work is inventoried individually below.

The [July validation design](../../../docs/superpowers/specs/2026-07-10-upstream-sync-validation-design.md)
supplies the characterization method. Its old host limitations, golden approval and merge grants
apply only to that historical effort. Current AGENTS.md and AGENTS.local.md govern this port.

## Recommended approach

Apply the final behavior of each upstream change family to its existing modern owner, with
focused regression tests before each production slice. Preserve this branch’s room-affect
caster snapshots, FP policy, string-view boundaries and
legacy ABI decoders; extend summon targeting and kill-credit behavior with the selected local uaf-port follow-ups. Reconcile the port against the pinned upstream tip when each slice closes.
Keep the exact source commit references so a later sync can distinguish intentional skips from omissions.

A direct merge would combine the duplicate squash with flat-layout edits and obscures the actual
delta. A full-file replacement would overwrite later architectural and gameplay changes. Neither
offers a useful correctness advantage here. Behavior slices allow independently reviewable tests
and keep related upstream repair commits together.

## Architecture and implementation slices

The letters below identify design slices, not new tasks or a second work-status board. Their
execution state stays on TASK-015. Implement B first; N precedes P, M includes the TASK-030
overlap, and A precedes R’s cache invalidation work. V and D require their own focused reviews.

| Slice | Owners and behavior | Required regression evidence |
|---|---|---|
| B: bounded correctness fixes | `src/combat/skill_timer.cpp`, `src/script/script.cpp`, `src/app/act_wiz.cpp`, `src/persist/db_players.cpp`; erase-safe ticking, nested-script traversal, case-insensitive OB, bounded long descriptions | Extend `skill_timer_tests.cpp`, `script_tests.cpp`, `act_wiz_format_tests.cpp`, `db_loader_tests.cpp`. Adjacent expired entries plus a surviving timer; adjacent/nested blocks; OB/ob; long terminated description followed by another field and malformed missing-terminator rejection. Each is a separately testable sub-slice. |
| N: connections and protocol negotiation | `src/app/comm.cpp`, `src/core/include/rots/core/descriptor.h`, `src/app/protocol.cpp`, `src/rots_net.h`, `src/platform/rots_net.cpp`, `proxy/src/main.rs`; no-data would-block deferral, buffered-text/prompt ordering, bounded reads, pre-login idle cleanup, socket options and ISO-8859-1 | Extend `comm_output_tests.cpp`, `rots_net_tests.cpp`, `protocol_tests.cpp`; add local connection-harness cases where needed. Test would-block before any write versus a partial write, retained buffer and prompt break on retry, socket not writable, fatal write, incomplete input across calls, idle/active clients and charset negotiation. Run proxy tests. |
| M: MSDP | App protocol/comm/act_move/interpre, shared `protocol.h`, `src/world/weather.cpp`, `tools/account_smoke.py`, `lib/text/msdp_tbl`; bounded room resolution, descriptor/actor consistency, reconnect parity, exactly-once flush, durable dirty flags, SERVER_ID, weight/professions/spec/group | Extend `protocol_tests.cpp`, `interpre_account_menu_tests.cpp` and account smoke. Test placed/unplaced/out-of-range actors, different descriptor actor, no descriptor/protocol, account menus, switched/legacy reconnect, report/send counts, disabled-to-enabled delivery, empty/multi-member group and sanitization. |
| V: movement validation | App owns `act_move.cpp` and `act_offe.cpp`; `src/combat/ranger.cpp` reaches app through `src/combat_hooks.h` / `src/combat/combat_hooks.cpp`. Replace the upstream shared skip pointer with a synchronous validated-move hook and call-local input to app movement execution. Ordinary do_move remains the command entry. | Flee and windblast fire ON_BEFORE_ENTER once; ridden path, haze direction change, special-handler return, no exit and script veto cannot suppress a later move. Preserve post-trigger location re-reads and test relocation. Re-run dispatch registry/caller pins and CombatLayerAcyclicity. |
| D: delays and bash | `src/utils.h` WAIT_STATE_BRIEF/FULL preserve a newly queued delay after completion. `src/app/act_offe.cpp` uses the existing combat `battle_mage_handler` before forcing cast interruption. No handler ownership move. | Extend `comm_delay_tests.cpp`, `battle_mage_handler_tests.cpp` and the applicable bash fixture. Both macros retain a reentrant replacement; successful/failed battle-mage resistance, non-caster and non-battle-mage controls; deterministic RNG and combat characterization. |
| A: account lifecycle | `src/app/interpre.cpp` queues logout then CON_CLOSE. `src/persist/db_players.cpp` owns system deletion’s archive/unlink/index sequence, reached from boot sweep and `src/app/act_wiz.cpp`; reuse account APIs and existing persistence hooks. | Menu logout buffer/state; account-native and legacy deletion; unresolved owner, archive failure and unlink failure must preserve character/index consistency. Extend account/menu/db-loader tests. |
| P: password recovery | `src/persist/account_management_identity.cpp` owns recovery policy and credential verification; storage encodes fields; presentation owns prompts; app nanny/comm owns connection states and deadlines in descriptor.h. Preserve current string_view + first-null contracts. | Port upstream recovery tests using local temporary accounts and the existing fake mail path. Code expiry/resend limit/attempt cap/replay, unknown-email indistinguishability, write/mail failures, rejected passwords, cleared transient secrets and absolute deadlines not extended by typing. Account smoke mandatory. |
| R: roster | New `src/persist/roster_cache.cpp` and shared `src/roster_cache.h` belong to RotS::persist beside account_cache; app db_boot enables it. Existing persist identity/presentation/storage modules share one ordered index sequence. Session filter lives on descriptor; chosen sort lives on AccountData. | Port roster_cache/account/menu tests: cache hit/miss and write/delete invalidation, root isolation, unreadable/corrupt rows, stable ties, name selection bypasses the active filter through the shared sorted/capped algorithm, while numeric selection matches the displayed filtered roster, session filter reset, sort persistence, labelled sides and 200-row output limits. |
| C: colours and help | `src/app/color.cpp`/`act_info.cpp` render NPC slot 15, `src/color.h` declares it, `src/persist/character_json.cpp` writes mob and accepts reserved_15; existing core/character layout stays unchanged. Update tracked help_tbl/shap_tbl. | Color/character JSON tests plus mount/player/NPC display cases; old key loads, new key round-trips, player slot retained. Character JSON golden deliberately changes reserved_15 to mob; verify the exact diff before accepting it. |
| O: local operation support | `.gitignore`, `docker-compose.yml`, `scripts/rots-docker.sh`: ignore untracked autorun and adapt host-user execution to both current services with private build volumes intact. | Inspect `docker compose config`, shell syntax and harmless write probes to a fresh temporary directory/volume under the intended UID. Do not solve permissions by deleting or recursively changing ownership of existing volumes. |

### Boundary decisions

Networking retains `SocketType`, `rots_net::write_socket`, `last_error` and
`error_is_would_block`; POSIX errno and raw int handles must not leak into the portable app
logic. Best-effort socket options can be added as narrow platform helpers. Upstream only
retries would-block before any bytes are written; preserve and test its explicit partial-write
failure behavior unless a separate design approves a buffered-offset rewrite.

MSDP room updates resolve the supplied actor through Placement, validate before dereference,
and retain the output-seam registration of `msdp_room_update_impl`. The existing rent paths
already use `stash_load_room_vnum`/`peek_load_room_vnum`; do not replace those with raw fields.
TASK-030 is the same inverted-guard defect, not an additional migration. Its existing two
characterization tests deliberately preserve wrong behavior and must be replaced, with the
intentional behavior change recorded. Reconcile TASK-030 only when its own criteria pass.

Movement’s once-only validation data must be tied to the synchronous execution and requested
direction. A private movement helper accepts whether that transition was already checked;
the normal command passes false, while the registered checked-move hook passes true. Haze
changing direction clears that privilege. This keeps mutable request state off a global
character pointer and keeps ranger from linking to an app-owned variable. Implementation must
preserve actor revalidation and location re-reads after callbacks; the flag is not a lifetime proof.
Exact helper extraction and hook signatures are settled in V’s bounded implementation plan
after reading the complete movement body and its dispatch tests; this design does not certify them yet.

The roster cache owns only persisted display/sort summaries, never char_data pointers. It is
single-threaded, disabled by default for tools/tests, enabled by app boot, and invalidated at
successful character-write/deletion boundaries. Its borrowed read parameters use normalized
string views; keys stored across calls own their strings. Account lookup caching remains a
separate responsibility. No new persist-to-app or persist-to-combat dependency is permitted.

New production/test translation units must join all applicable lists: src/CMakeLists.txt,
src/Makefile and src/tests/Makefile. Keep nine library-layer checks and the converter link
probe intact. Do not import upstream Makefiles or POSIX-only test fixtures wholesale.

### Class responsibilities

- `descriptor_data`: owns one connection’s transient protocol, login and roster-session state;
  the new prompt flag, absolute deadline and roster controls fit that existing lifetime.
- `account::AccountData`: owns the persisted account record; failed-login, recovery and chosen
  roster-sort fields belong here, with optional-field defaults for older records.
- `roster_cache::RosterSummary` (new): carries the persisted fields needed to display, sort and
  filter one roster entry, with a readable flag and clamped profession coefficients.
- `game_timer::skill_timer`: owns skill cooldown expiry; only traversal behavior changes.
- `player_spec::battle_mage_handler`: evaluates battle-mage effects for a character; bash uses
  its existing interruption decision. No class shape change is proposed.

### Intentional observable changes

These are live-game behavior ports: improved timing/triggers, working room reports, logout
disconnect, description truncation, account-aware deletion, recovery prompts, larger sortable
rosters, new MSDP variables and NPC colours. They cannot be described as behavior-neutral.
Preserve upstream outputs and policies unless a current-branch invariant requires an explicit
adaptation. The expected character JSON key change is reserved_15 -> mob; retain input
compatibility. Do not recapture unrelated combat or boot goldens to hide drift.

## Additional approved source: local uaf-port

On 2026-09-07 the owner approved the design subject to incorporating the further summon and
kill-credit fixes in the local RotS_Live uaf-port, then continuing to the next step. The source is
`/Users/drelidan/Projects/GitHub/RotS_Live/.claude/worktrees/uaf-port`, branch
`fix/spell-room-affect-uaf-port`, pinned to `1d242d46b10a2462cb538d789fb23a6047e687dd`.
Its local release-frodo baseline is `e65027f0a3f92e3692220c676f6281f2dad195c9`, older than the
release tip selected for this migration. Its 38-commit supplemental range is tracked separately
from the original 72; do not replace the release inventory with this branch or replay their
shared release changes. Both source checkouts contain unrelated runtime/crash artifacts; only
committed source was inspected, and nothing there was modified.

The source largely ports TASK-018/019/020/021/025/026 from this modern tree back into the flat
layout. Those implementations are overlap, not new work by virtue of different commit hashes.
The final source changes introduce these additional slices:

| Slice | Selected behavior and source | Modern owners / required tests |
|---|---|---|
| U1: summon distance | `ef84bcb`: squared coordinate distance replaces XOR in spell_summon. Dark-room TAR_DARK_OK and linkdead guards already exist here; retain existing hiding/invisibility/blindness restrictions. | `src/combat/mage.cpp`; extend `src/tests/mage_tests.cpp` with equal, positive and negative coordinate-delta save outcomes and keep `summon_targeting_tests.cpp` green. A same-zone delta must yield 0, not the old XOR result 4. |
| U2: remote credit and local XP | `ea88227` plus `1c2b358`: group_gain walks the death room even when the credited killer is remote/unplaced, gates that killer’s own share on presence, and excludes a remote victim target from its target-share arm. | `src/combat/fight.cpp::group_gain` through Placement and occupants; adapt `fight_credit_tests.cpp` remote-actor/unplaced-actor/local-fighter/group tests, with a same-room positive control and remote-target exclusion. Credit and room-tick non-engagement stay intact. |
| U3: poison punishment | `dd92094`, `b61a025`, `ddb7ace`, `fcfb0a0`, `5771982`, `a1a8cb3`, `1c2b358`, `c28286a`: only PC SPELL_POISON deaths classify by engagement with a real mob; attribution stays independent. | `src/combat/fight.cpp` and combat declarations currently in `src/handler.h`; `src/tests/fight_credit_tests.cpp` DeathClassification/PoisonDeathPunishment suites and existing poison/tick tests; retain all three-argument die/raw_kill callers and current output/persist hooks. |
| U4: registry bounds | `8e987fb`: remove_char_exists rejects an out-of-range identity before touching either registry. The current modern function lacks that guard. | `src/entity/entity_lifecycle.cpp`; add negative/upper-bound/no-valid-entry-corruption tests using existing registry APIs. Preserve the current identity lookup and caster-snapshot ownership model. |

### Poison punishment contract

The source's 2026-09-01 owner ruling separates punishment from attribution. A real mob is an
NPC with neither MOB_PET nor MOB_ORC_FRIEND. Engagement is either the victim targeting such
a mob or such a mob targeting the victim; visibility is irrelevant. For PC SPELL_POISON
deaths, engagement gives full mob XP loss and harsh penalties regardless of poison source;
absence gives the legacy one-tenth loss and gentle penalties, even for a mob or missing poisoner.
Every PC poison death receives EXPLOIT_POISON. EXPLOIT_MOBDEATH agrees with the penalty
classification and names the credited real mob when available, otherwise the engaged mob.
Player poisoners keep their contribution/PK records in either punishment case. Non-poison
deaths and NPC victims retain existing behavior; mist-applied ordinary poison inherits the rule,
while blaze/haze/direct mist damage does not.

Use the source's death_punishment enum and pure selectors as a contract guide. Keep existing
three-argument die/raw_kill functions as forwarding entry points and retain current namespace
and hook boundaries. Capture the victim's engagement before stop_fighting can erase or retarget
it. ON_DIE can mutate/extract characters: the source's poison-only probe reduces exposure but
does not itself prove a borrowed opponent remains live. U3's executable plan must preserve
the instant-of-death decision without dereferencing an unchecked pointer after that callback,
using the modern identity registry or an owned snapshot. Test extraction during ON_DIE and
verify both actual penalty wiring and pure classification; pure selector tests alone cannot
prove die/raw_kill dispatch the result. Source-host test limitations and manual deployment
instructions are historical evidence, not waivers or permission to access a server here.

### Supplemental commit inventory

The following rows cover every commit in local `release-frodo..fix/spell-room-affect-uaf-port`.
Overlap rows identify the modern owning task/files; compare any residual hunks during the
relevant slice before claiming full parity. Test-harness/style-only differences are not imported
wholesale because this branch has its own lifecycle fixtures, nine libraries and build gates.

| Commit | Subject | Disposition |
|---|---|---|
| `384ba340da34bf668623638aec358d0f2662eb8a` | plans: port of spell/room-affect UAF fixes (TASK-018/019/020/021/026) from RotS_Live_Modern | Historical source design/plan/manual evidence; fold contracts into this design, do not copy a competing board or deploy. |
| `00c9b270a512414e4c59fdfe6ee18a27c3d68fbb` | mage: deliver the fumbled fireball self-hit after the splash (TASK-018 port) | Existing modern origin: TASK-018, src/combat/mage.cpp. Residuals audited against the modern origin; no replay needed. |
| `4bed19a2183883b761ee795504ec078f89c31acc` | mage: let every other occupant fall before the caster's own quake fall (TASK-019 port) | Existing modern origin: TASK-019, src/combat/mage.cpp. Residuals audited against the modern origin; no replay needed. |
| `8405b36780238138088a3213673506b360f3b24e` | handler: char_by_abs_number() + pointer-carrying set_char_exists() (TASK-021 port, registry half) | Existing modern origin: TASK-021 registry, src/entity/entity_lifecycle.cpp. Residuals audited against the modern origin; no replay needed. |
| `0ab317a0bde67bccd0d5a31121573b57d6647333` | limits: snapshot affected_list before affect_update walks it, validate by identity (TASK-020 port) | Existing modern origin: TASK-020 snapshot traversal, src/combat/limits.cpp. Residuals audited against the modern origin; no replay needed. |
| `8ff22f6df47c712d5bfc11d95c6b79442cb097b1` | entity: caster_snapshot, a cast-time copy of the formula inputs (TASK-021 port) | Existing modern origin: TASK-021, src/entity/caster_snapshot.cpp. Residuals audited against the modern origin; no replay needed. |
| `fb16899288a6872e15fbaab446a8b9eb54158595` | combat: formula helpers read a caster_snapshot; live forms forward (TASK-021 port) | Existing modern origin: TASK-021 formula overloads in src/combat/. Residuals audited against the modern origin; no replay needed. |
| `4902306fe33d3e9f724f260e07647a8af09291a4` | fight+spells: poison remembers its origin; resolve_poisoner() reads it back (TASK-021 port) | Existing modern origin: TASK-021 poison origins in entity/combat/app owners. Residuals audited against the modern origin; no replay needed. |
| `66695512265db5f7fc33178b12e6bf449cecc60a` | fight: damage_credited() separates engagement from kill credit (TASK-021/026 port) | Existing modern origin: TASK-021/026, src/combat/fight.cpp. Residuals audited against the modern origin; no replay needed. |
| `a0698d4e57117d05975880dd36b0adf9f643fe41` | handler: room affects record their caster's snapshot (TASK-021 port) | Existing modern origin: TASK-021 room-affect records, src/entity/entity_lifecycle.cpp. Residuals audited against the modern origin; no replay needed. |
| `88155a55a9d95dacbbc656f3318b10ebf49a2da1` | combat: room affects tick from their caster's snapshot and credit kills (TASK-021 port) | Existing modern origin: TASK-021, src/combat/room_affect_tick.cpp. Residuals audited against the modern origin; no replay needed. |
| `0e0015c9dc0bf2fbc392a8ccae008a046e532c09` | spells: blaze/mist/haze/poison record their caster (TASK-021 port) | Existing modern origin: TASK-021 cast sites in src/combat/. Residuals audited against the modern origin; no replay needed. |
| `056709d326208b7e020152a35a719ea2ccc24483` | combat: die() records a kill when anybody took part (TASK-026 port) | Existing modern origin: TASK-026 contributor records, fight.cpp and app/pkill.cpp. Residuals audited against the modern origin; no replay needed. |
| `23722f09691a18748d51b094457a0a166db90515` | fix(task-1): flag the fireball splash bystander NPC to keep it out of group_gain()'s player-kill-credit path (batch verification) | Source-fixture adaptation audited: current lifecycle/room fixtures and regression controls cover the behavior; global source fixture sizing is intentionally not imported. |
| `de424587b342248adbd84dcaca778524f4dccf91` | fix(task-10): stop indexing world[] by the disambiguating room number in room_affect_tick_tests.cpp (batch verification) | Source-fixture adaptation audited: current lifecycle/room fixtures and regression controls cover the behavior; global source fixture sizing is intentionally not imported. |
| `8e987fb9603696dd0c0ef0004d9432b71f2c2692` | docs+hardening: final-review comment cleanup, registry bounds guard, Makefile deps | Ported U4 (local) bounds guard in src/entity/entity_lifecycle.cpp; credit comments audited and modern build wiring retained. |
| `d43100b100b320df05b3ab14eb33b9c274e78cb1` | summon: add TAR_DARK_OK so name targeting works in dark rooms (TASK-025 port) | Existing modern origin: TASK-025 TAR_DARK_OK in src/core/consts.cpp. Residuals audited against the modern origin; no replay needed. |
| `9f66c9cf29bedf2a580feaf1e01abbbbb5f5072c` | tests: summon dark-room targeting suite + spell_summon body/linkdead pin (TASK-025 port) | Existing modern origin: TASK-025 summon targeting/body tests and app linkdead guard. Residuals audited against the modern origin; no replay needed. |
| `ef84bcb9bdb04e7ee9f8dfa470e8d8264b000f84` | mage: spell_summon squares the zone distance instead of XOR-ing it | Ported U1 (local): distance correction in src/combat/mage.cpp. |
| `0ae2a4259a3fdbb6f1a596b82b7863effed34d9c` | style: brace every new control-flow body, name loop indices, hoist nested calls, document fixture members | Source style/compile follow-up audited with the owning port; current declarations, case scopes and local style retained. |
| `c8b84c932af7b59a5cac8b9465eb053a97235b98` | fix(style): scope the hoisted poisoner local inside its switch case (jump-to-case-label) | Source style/compile follow-up audited with the owning port; current declarations, case scopes and local style retained. |
| `71210fdc5e08f1273c33aef6779891210f0130b9` | style: re-review residuals -- brace the mist mod else, replace three ternaries, hoist damage()'s credit local | Source style/compile follow-up audited with the owning port; current declarations, case scopes and local style retained. |
| `22d7a274c8f82402f4c6cad4e0684cc6e80c58d6` | Merge branch 'release-frodo' into fix/spell-room-affect-uaf-port | Skip duplicate release integration; release commits belong to the 72-commit inventory. |
| `f4296c7104e2d28bec670fc469932ba7071c6a85` | docs(spec): poison-death classification carveout design | Historical source design/plan/manual evidence; fold contracts into this design, do not copy a competing board or deploy. |
| `017bff3c28a60ff46c9086003014dfc7a9b910a1` | docs(plan): poison-death classification implementation plan | Historical source design/plan/manual evidence; fold contracts into this design, do not copy a competing board or deploy. |
| `ea88227def0cf3428e6d02bba3787bf6ff78fe45` | fight: group_gain pays the death room's fighters when the credited killer is remote | Ported U2 (local): remote credited killer no longer blocks death-room XP. |
| `dd92094c196d8e6a162dca24eb12ff6053ec744e` | fight: death_punishment classification primitives (poison carveout) | Ported U3 (local): death_punishment and classification primitives. |
| `6e74b7f7a4c8755cc0bd9145d22d6ae742cb3d90` | fight: drop doc-path references from classification comments | Source style/compile follow-up audited with the owning port; current declarations, case scopes and local style retained. |
| `b61a025ab80c323aad2d56532023d5aa27c4b304` | fight: find_engaged_real_mob() -- either-direction engagement probe | Ported U3 (local): both-direction real-mob engagement; adapt lifetime handling. |
| `ddb7acefc63af62d9286912861ccfa39ebfc1d91` | fight: punishment selectors for XP loss and the raw_kill penalty arm | Ported U3 (local): independent XP-loss and penalty selectors. |
| `fcfb0a07f0b4ca4ee27d4e73d5dec82ef6e99fe6` | fight: mobdeath_record_mob() -- EXPLOIT_MOBDEATH follows the classification | Ported U3 (local): MOBDEATH naming follows classification. |
| `57719825966195819529aed387a0c6ecf42099a6` | fight: raw_kill() takes a death_punishment; 3-arg form forwards legacy | Ported U3 (local): classified raw_kill with legacy forwarding entry. |
| `a1a8cb3800079592f68d86d41d57b8bf467b780a` | fight: die() classifies PC poison deaths by real-mob engagement | Ported U3 (local): classify PC poison death and preserve attribution. |
| `5e530ef4076eef6c06582b4d913ad9b04dc71aed` | tests: size the shared test world once in gtest_main | Source-fixture adaptation audited: current lifecycle/room fixtures and regression controls cover the behavior; global source fixture sizing is intentionally not imported. |
| `152896107611f0b3ff87b3deb75cf757df96ce0a` | docs: poison punishment carveout in the manual test plan | Historical source design/plan/manual evidence; fold contracts into this design, do not copy a competing board or deploy. |
| `1c2b358a00645c0b6c3b3a14b5cc47526394fc97` | fight: final-review hardening -- poison-gated engagement probe, room-gated target share | Ported U2/U3 (local): room-gated target share and poison-only engagement probe. |
| `c28286a48564a89dd146edd402037e6fd06b90d5` | style: trim death-classification declaration comments to contract altitude | Ported U3 (local) contracts, adapted to the modern declaration conventions. |
| `1d242d46b10a2462cb538d789fb23a6047e687dd` | docs: manual test plan requires a current-branch deploy; scenario 2 carries the punishment checks | Historical source design/plan/manual evidence; fold contracts into this design, do not copy a competing board or deploy. |

No new class shapes are needed by U1/U2/U4. U3 adds a death_punishment value enum and pure classification functions; any owned engagement snapshot has only the responsibility of preserving the death-time classification and record identity across callbacks.

## Commit-by-commit triage

`Ported ... (local)` identifies implemented, uncommitted behavior in the modern owners. All selected release and supplemental functionality is now dispositioned; no temporary `Port` rows remain. Slice regression and validation evidence is recorded in [doc-002](../plans/doc-002%20-%20TASK-015-bounded-correctness-implementation-plan.md) and [doc-003](../plans/doc-003%20-%20TASK-015-remaining-functionality-implementation-plan.md). TASK-015 owns acceptance and finalization state; local implementation is not remote integration or release certification.
Historical-doc skips leave the original upstream document reachable at its commit; player/builder
help changes are product data and are explicitly selected. Merge skips were checked with
`git log --remerge-diff --merges HEAD..upstream/release-frodo`; there was no additional resolution patch.

| Upstream commit | Subject | Disposition | Target mapping / reason |
|---|---|---|---|
| [`f26d3140`](https://github.com/returnoftheshadow/RotS_Live/commit/f26d3140add9e8ad5c3ee7bcc00bf8560c731107) | Account management (#274) | Already present | All contents: identical tree to reachable a695658; retain this branch’s later modernization. Separate CI adoption remains TASK-016. |
| [`b886026a`](https://github.com/returnoftheshadow/RotS_Live/commit/b886026a0908a4a741e140727680ee2b4cc8a993) | fix(protocol): advertise ISO-8859-1, not UTF-8, in CHARSET negotiation | Ported N (local) | src/app/protocol.cpp: advertise ISO-8859-1; do not set the UTF-8 flag on charset acceptance. |
| [`0e9193eb`](https://github.com/returnoftheshadow/RotS_Live/commit/0e9193eb6017b02f45a8c4c868536ae5551b15db) | Merge pull request #278 from ahumbert/charset-iso-8859-1 | Skip duplicate merge | Contained commits are listed individually; the remerge-diff check found no additional resolution patch. |
| [`27dfd74e`](https://github.com/returnoftheshadow/RotS_Live/commit/27dfd74e5c98fa8c62b2717983d475e363a9ab0a) | fix(msdp): send room MSDP data on reconnect, not just fresh login | Ported M (local) | src/app/interpre.cpp: ordinary account reconnect already negotiates and updates MSDP; switched-body and legacy reconnect branches now negotiate and publish room data too, preserving existing protocol objects. |
| [`a5e6c878`](https://github.com/returnoftheshadow/RotS_Live/commit/a5e6c8789063335963574096f8c6fb5eb34057e4) | Merge pull request #277 from ahumbert/msdp-reconnect-parity-v2 | Skip duplicate merge | Contained commits are listed individually; the remerge-diff check found no additional resolution patch. |
| [`48d5c4b2`](https://github.com/returnoftheshadow/RotS_Live/commit/48d5c4b2e1da258649d126aea3c1a586ac9c9413) | fix(msdp): correct inverted NOWHERE guard in msdp_room_update() | Ported M (local) | src/app/act_move.cpp::msdp_room_update_impl; TASK-030 is the same inverted guard; corrected together with cb89a1de actor/exit bounds through the real output seam. |
| [`2d91349d`](https://github.com/returnoftheshadow/RotS_Live/commit/2d91349dbdc9a051209e857b7caf6a31c650390b) | harden(msdp): sanitize MSDPSendPair/MSDPSendList and the TERRAIN field | Ported M (local) | src/app/protocol.cpp and src/app/act_move.cpp: sanitize pair/list values and terrain, preserving protocol delimiters. |
| [`e1c3d2ab`](https://github.com/returnoftheshadow/RotS_Live/commit/e1c3d2ab4de4fd2cac2f09ce71b211a77d61d2f7) | perf(net): enable TCP_NODELAY on game socket accept and both proxy legs | Ported N (local) | src/app/comm.cpp, src/rots_net.h, src/platform/rots_net.cpp, proxy/src/main.rs: best-effort TCP_NODELAY through the portable socket API, including 3c865a3d tolerance. |
| [`ceccd2fc`](https://github.com/returnoftheshadow/RotS_Live/commit/ceccd2fc5ce0338a376aa28a76b4076eb4e5b2e7) | fix(net): don't disconnect on a momentary EAGAIN/EWOULDBLOCK write | Ported N (local) | src/app/comm.cpp: would-block result; include 9c1f40b7 so deferred output is retained. |
| [`77e5a9a7`](https://github.com/returnoftheshadow/RotS_Live/commit/77e5a9a7b0a0162711c820e73dfd60051786e5ad) | fix(net): cap process_input()'s read-loop iterations per call | Ported N (local) | src/app/comm.cpp::process_input: cap read iterations at eight without discarding incomplete lines. |
| [`79cf31a8`](https://github.com/returnoftheshadow/RotS_Live/commit/79cf31a88c089ace15c0bf06d37b18800067f80c) | fix(net): reap idle pre-login connections, add SO_KEEPALIVE | Ported N (local) | src/app/comm.cpp: keepalive and pre-login idle sweep; preserve app-owned descriptor teardown. |
| [`74fe5a48`](https://github.com/returnoftheshadow/RotS_Live/commit/74fe5a48a91b820b109a40deb746302b0cdc9380) | fix(ops): add 5s backoff before autorun restarts the game process | Skip | autorun was subsequently removed upstream by c9a8dfbf and is already untracked here; do not restore the removed deployment script. |
| [`1b2a06bb`](https://github.com/returnoftheshadow/RotS_Live/commit/1b2a06bb3b00554d2bec19bdbe22a4234f3b67fb) | fix(combat): don't skip the element shifted into an erased slot | Ported B (local) | src/combat/skill_timer.cpp::update_skill_timer: do not advance after erase; shifted entries still tick. |
| [`796462ff`](https://github.com/returnoftheshadow/RotS_Live/commit/796462fff93d56f14087da4ab24e5c41937320b2) | fix(movement): don't double-fire ON_BEFORE_ENTER on flee/windblast | Ported V (local) | src/app/act_move.cpp, src/app/act_offe.cpp, src/combat/ranger.cpp: once-only before-enter checks through the combat/app seam. |
| [`65934a41`](https://github.com/returnoftheshadow/RotS_Live/commit/65934a413d99ee34807ed1fbb2abbf1b4f1a4090) | fix(movement): consume skip-trigger flag in do_move()'s ridden branch | Ported V (local) | src/app/act_move.cpp: include the ridden branch in once-only movement validation. |
| [`999c1677`](https://github.com/returnoftheshadow/RotS_Live/commit/999c16775faa3967a13179952c1162f43dafa7fc) | fix(combat): don't silently clobber a reentrantly-queued delay | Ported D (local) | src/utils.h WAIT_STATE_BRIEF/FULL: preserve a reentrantly queued delay after complete_delay; keep output-seam dispatch. |
| [`84462052`](https://github.com/returnoftheshadow/RotS_Live/commit/84462052c6f3f039456a533304323edc51c7d0e4) | fix(scripts): stop get_next_command() from double-advancing past nested blocks | Ported B (local) | src/script/script.cpp::get_next_command: nested-block traversal must not advance twice. |
| [`9c1f40b7`](https://github.com/returnoftheshadow/RotS_Live/commit/9c1f40b7dda58dee547aa502f44832026509cb59) | fix(review): correct EAGAIN buffer-drop and haze-reroll trigger dangle | Ported N + V (local) | src/app/comm.cpp retains output on would-block; src/app/act_move.cpp consumes the validated-direction request before haze can reroll it. |
| [`5708b220`](https://github.com/returnoftheshadow/RotS_Live/commit/5708b220e41424e9bb79fafeb57f8d54a971f87a) | fix(review): msdp_room_update null-desc guard/room mismatch, WAIT_STATE_FULL log | Ported M + D (local) | Null-descriptor guard already present from TASK-025; src/app/act_move.cpp now uses the supplied actor consistently; src/utils.h preserves reentrant-delay diagnostics. |
| [`8ae32c99`](https://github.com/returnoftheshadow/RotS_Live/commit/8ae32c999470031d8bcde70619c5aef5dcacf5fc) | fix(net): stop the prompt from racing ahead of buffered game text | Ported N (local) | src/app/comm.cpp and src/core/include/rots/core/descriptor.h: output-before-prompt ordering and independent bare-prompt state. |
| [`04afc581`](https://github.com/returnoftheshadow/RotS_Live/commit/04afc5818c7da5a78fb995924daa9a4c7f444438) | fix(msdp): populate SERVER_ID's backing storage so SEND SERVER_ID works | Ported M (local) | src/app/protocol.cpp: populate SERVER_ID backing storage for later SEND, not only immediate negotiation output. |
| [`b2f05e17`](https://github.com/returnoftheshadow/RotS_Live/commit/b2f05e17e2ae4b6cd91a3689bc7e688731727f0e) | fix(msdp): stop ROOM_EXITS/WORLD_TIME double-sends, drop redundant WEATHER push | Ported M (local) | src/app/act_move.cpp and src/world/weather.cpp: flush exits/time once and remove redundant weather push through existing seams. |
| [`3027ab3e`](https://github.com/returnoftheshadow/RotS_Live/commit/3027ab3e2e99058ba802da479af285b9eddbffaa) | docs: update manual test checklist (MSDP null-desc guard, switch/return test) | Skip historical doc copy | Upstream manual checklist remains source evidence; port its relevant scenarios into this tree’s automated checks. |
| [`c9a8dfbf`](https://github.com/returnoftheshadow/RotS_Live/commit/c9a8dfbf48b76baf89203799475824972ba0d353) | chore: stop tracking autorun, gitignore it instead | Ported O (local; partial overlap) | autorun is already untracked; the exact ignore rule is added; local runtime files are untouched. |
| [`508bec45`](https://github.com/returnoftheshadow/RotS_Live/commit/508bec4503a3534b68c54247628a97c56fe44ea6) | fix(move): stop g_skip_next_before_enter_for from dangling into unrelated moves | Ported V (local) | src/app/act_move.cpp: early returns, special handlers and haze must not leave a skip request for a later move. |
| [`a837cd85`](https://github.com/returnoftheshadow/RotS_Live/commit/a837cd85b4c0f5f00d9ef871b6bccd4111d7f159) | fix(net): restore bare_prompt_pending across EAGAIN retries and short-circuited prompt writes | Ported N (local) | src/app/comm.cpp: restore bare-prompt state after an unwritten retry; set it only after an actual prompt write. |
| [`3c865a3d`](https://github.com/returnoftheshadow/RotS_Live/commit/3c865a3d34214e32d3b475795aac4c4dfad2b968) | fix(proxy): don't kill connections on a failed set_nodelay call | Ported N (local) | proxy/src/main.rs: failed set_nodelay logs a warning and preserves the connection on all three transport paths. |
| [`eca5e51d`](https://github.com/returnoftheshadow/RotS_Live/commit/eca5e51d0ee8db0b50814b1af144ef33a90e842b) | docs: scrub tmp-path reference from test checklist, add PR #276 review doc | Skip historical doc copy | Upstream checklist/review history is retained by commit reference; do not import a parallel work board. |
| [`703cfff4`](https://github.com/returnoftheshadow/RotS_Live/commit/703cfff47c191c6d6e149f37f6cde63127f7c685) | docs: update PR #276 review doc with fix status and finding 3 correction | Skip historical doc copy | Upstream review status belongs to that branch; use it as test-design evidence only. |
| [`8bd82c94`](https://github.com/returnoftheshadow/RotS_Live/commit/8bd82c9499074462a8f05634af3a4e0ef5f75c57) | fix(wizset): case-insensitive field matching for `OB` | Ported B (local) | src/app/act_wiz.cpp: case-insensitive OB field matching; capture the behavior in act_wiz_format_tests.cpp. |
| [`df8477a2`](https://github.com/returnoftheshadow/RotS_Live/commit/df8477a2c5cc63692eec5a29a0d2bd7e4cca185f) | fix(docker): run container as host user, not root | Ported O (local; adapted) | docker-compose.yml and scripts/rots-docker.sh: host-user launcher supports both services with separate per-UID/GID private build volumes; existing root-default caches remain intact. Empty volumes initialize safely; nonempty ownership mismatches refuse. |
| [`2f5c27da`](https://github.com/returnoftheshadow/RotS_Live/commit/2f5c27da2ed5a9e371263f8cd2fc7a39764f0e05) | docs: detailed writeup of the bash double-delay bug (PR #276 finding 4) | Skip historical doc copy | Bash investigation is source evidence for D; the final upstream fix is db1b89f1. |
| [`75f09ac1`](https://github.com/returnoftheshadow/RotS_Live/commit/75f09ac18a6d8b7c5b8ddca8050fdcf6a79621a6) | docs: root-cause the bash double-delay bug to interrupt-check ordering | Skip historical doc copy | Keep the corrected bash interrupt-order explanation linked to D; do not copy its stale source positions. |
| [`db1b89f1`](https://github.com/returnoftheshadow/RotS_Live/commit/db1b89f10ba4f9f233bdba612e840d4d49f7c810) | fix(combat): let battle mages resist bash's cast interruption | Ported D (local) | src/app/act_offe.cpp::do_bash uses the existing rots_combat battle_mage_handler before force-completing a cast. |
| [`29114a9f`](https://github.com/returnoftheshadow/RotS_Live/commit/29114a9f1fe6ab503cefd6602613dae7165f91a1) | docs: update manual test checklist with today's bash findings | Skip historical doc copy | Use the final bash manual scenarios when writing D tests; historical execution status is not local validation. |
| [`474831dd`](https://github.com/returnoftheshadow/RotS_Live/commit/474831dd50bbf9cd898702c66b8f3498d3ad3093) | docs(review): add missing reentrancy comment to WAIT_STATE_FULL | Ported D (local) | src/utils.h: keep the meaningful reentrancy rationale with the ported guard; avoid copying upstream formatting churn. |
| [`3c1b3f06`](https://github.com/returnoftheshadow/RotS_Live/commit/3c1b3f065ba2cb6023f938176866cdb1480afd21) | docs: close out PR #276 review -- finding 5 out of scope, all resolved | Skip historical doc copy | The upstream review close-out does not certify this port; retain its commit as provenance. |
| [`8ca8ae3c`](https://github.com/returnoftheshadow/RotS_Live/commit/8ca8ae3c2568971a517ed86c326c3ef039c72899) | Merge pull request #276 from ahumbert/core-server-health-update | Skip duplicate merge | Contained commits are listed individually; the remerge-diff check found no additional resolution patch. |
| [`c404dfca`](https://github.com/returnoftheshadow/RotS_Live/commit/c404dfca65c42030390d610ba1f3d385adf5cc28) | fix(account): close the connection when logging out of the account menu | Ported A (local) | src/app/interpre.cpp and src/tests/interpre_account_menu_tests.cpp: account logout queues Goodbye and enters CON_CLOSE. |
| [`e6641684`](https://github.com/returnoftheshadow/RotS_Live/commit/e6641684e934dcc8a7bc3fc0fa5ce2031e615ba7) | Merge pull request #279 from ahumbert/fix/account-menu-logout-closes-connection | Skip duplicate merge | Contained commits are listed individually; the remerge-diff check found no additional resolution patch. |
| [`26c0df11`](https://github.com/returnoftheshadow/RotS_Live/commit/26c0df11cd3f3ca14c3397ebc910051645be6e1e) | fix(db): truncate over-long player descriptions instead of failing the load | Ported B (local) | src/persist/db_players.cpp KEY_LONG_STR: truncate overlong terminated descriptions; still reject missing terminators. |
| [`7cfa8013`](https://github.com/returnoftheshadow/RotS_Live/commit/7cfa8013e0e0503a367fa5e3f422120fc6b60590) | Merge pull request #281 from ahumbert/fix/legacy-long-description-load | Skip duplicate merge | Contained commits are listed individually; the remerge-diff check found no additional resolution patch. |
| [`ae196394`](https://github.com/returnoftheshadow/RotS_Live/commit/ae19639461c6f02aaf7deaa34ae7096ba9369045) | fix(account): unlink account characters on system deletion | Ported A (local) | src/persist/db_players.cpp, src/db.h, src/app/act_wiz.cpp: archive, unlink through account ownership, then retire index entry; retain existing persistence hooks. |
| [`709d83d8`](https://github.com/returnoftheshadow/RotS_Live/commit/709d83d84e7786c5cca5b7fd050004007b098d86) | Merge pull request #282 from ahumbert/fix/account-aware-character-deletion | Skip duplicate merge | Contained commits are listed individually; the remerge-diff check found no additional resolution patch. |
| [`ff2842fa`](https://github.com/returnoftheshadow/RotS_Live/commit/ff2842fa6cfe2838bdc16097af69372c9f454533) | Feat/account: Reset Password (#284) | Ported P (local) | src/persist/account_management{,_identity,_presentation,_storage}.cpp, corresponding shared headers, descriptor.h, src/app/comm.cpp/interpre.cpp, account tests: failed-login notices and password reset. |
| [`eb9b167c`](https://github.com/returnoftheshadow/RotS_Live/commit/eb9b167c95a0df3e78575b24fda33d2e87e5a4ce) | feat(account): allow selecting a linked character by name | Ported R (local) | src/persist/account_management_identity.cpp and src/persist/account_management_presentation.cpp: name selection; combine with the later common ordered-roster path. |
| [`0c61b336`](https://github.com/returnoftheshadow/RotS_Live/commit/0c61b3369068a2b4016e9f01ec028e66d14204ec) | Merge pull request #285 from ahumbert/feat/account-character-select-by-name | Skip duplicate merge | Contained commits are listed individually; the remerge-diff check found no additional resolution patch. |
| [`4c949021`](https://github.com/returnoftheshadow/RotS_Live/commit/4c94902138cb3321b7b8577db5e3185655ee79c6) | feat(msdp): report carried weight, profession levels, and specialization | Ported M (local) | src/app/comm.cpp/protocol.cpp, src/protocol.h, protocol_tests.cpp: weight, profession levels/maxima and specialization. |
| [`e65027f0`](https://github.com/returnoftheshadow/RotS_Live/commit/e65027f0a3f92e3692220c676f6281f2dad195c9) | Merge pull request #283 from ahumbert/feat/msdp-carried-weight-prof-levels-spec | Skip duplicate merge | Contained commits are listed individually; the remerge-diff check found no additional resolution patch. |
| [`cb89a1de`](https://github.com/returnoftheshadow/RotS_Live/commit/cb89a1de5350a46dc2eb49821bc87ea6a220ec3b) | fix(msdp): stop publishing bogus, never-visited rooms (#286) | Ported M (local) | src/app/act_move.cpp bounds and src/app/comm.cpp CON_PLYNG filtering are implemented; src/app/objsave.cpp already separates rent VNUM storage through stash_load_room_vnum/peek_load_room_vnum. |
| [`a0438dda`](https://github.com/returnoftheshadow/RotS_Live/commit/a0438ddab9bc2573cf61209dfe2f6ce3e9639e88) | fix(account): raise linked-character cap to 200 | Ported R (local) | src/persist/account_management.cpp and account/menu tests: raise displayed linked-character cap to 200 with real output-size coverage. |
| [`3d3f90bf`](https://github.com/returnoftheshadow/RotS_Live/commit/3d3f90bf47ccf63d8429fe3cdcfc3fe4c0b80570) | Merge pull request #289 from ahumbert/fix/account-roster-cap-200 | Skip duplicate merge | Contained commits are listed individually; the remerge-diff check found no additional resolution patch. |
| [`dff58cd7`](https://github.com/returnoftheshadow/RotS_Live/commit/dff58cd7fcbde3efc88ced8b48b470d09cfc04db) | fix(msdp): stop dropping variables dirtied while MSDP is off (#288) | Ported M (local) | src/app/protocol.cpp, src/protocol.h, src/app/act_othe.cpp: dirty flags survive an undeliverable channel; mark reported values dirty when enabled. |
| [`e6bc6554`](https://github.com/returnoftheshadow/RotS_Live/commit/e6bc655495b4462654841b96df8cc5877eb5f567) | feat(color): add a separate 'mob' colour slot for NPCs (#290) | Ported C (local) | src/app/color.cpp/act_info.cpp, src/color.h, src/persist/character_json.cpp: mob slot 15, NPC/player rendering and reserved_15 reader compatibility. |
| [`5d820307`](https://github.com/returnoftheshadow/RotS_Live/commit/5d820307dfc96ff5fc69c2bcc60b8b104a44e8e4) | Added group data to msdp values. (#287) | Ported M (local) | src/app/comm.cpp/protocol.cpp, src/protocol.h, protocol tests, tools/account_smoke.py, lib/text/msdp_tbl: group table. Record feature narrative in Backlog, not upstream WIP/FEATURES copies. |
| [`358edb82`](https://github.com/returnoftheshadow/RotS_Live/commit/358edb824eba5ce9cc1a6092435ba69ff732e67e) | docs(help): document the 'mob' colour slot and unshadow HELP COLOR | Ported C (local) | lib/text/help_tbl: document mob slot and resolve HELP COLOR shadowing; tracked product help is in scope. |
| [`87d56eaa`](https://github.com/returnoftheshadow/RotS_Live/commit/87d56eaa0cb5919ce4eccd6755db08e3ba6dd7d1) | docs(help): restore builder data lost from the shape table | Ported C (local) | lib/text/shap_tbl: restore builder help data; retain local table content outside the upstream change. |
| [`3d0530fb`](https://github.com/returnoftheshadow/RotS_Live/commit/3d0530fbb72b3a2b32ea89e3920de80c2387d988) | docs: record the colour help and CMD_SEND findings | Skip historical doc copy | docs/color-command-audit.md is upstream investigation evidence. CMD_SEND findings are not a separately authorized feature implementation. |
| [`ea642b72`](https://github.com/returnoftheshadow/RotS_Live/commit/ea642b72e9c10e28e2dfd0572d1cf6273229fa1e) | docs(help): drop the dangling AUTOEXIT cross-reference from HELP SET | Ported C (local) | lib/text/help_tbl: remove the dangling AUTOEXIT cross-reference in HELP SET. |
| [`df027953`](https://github.com/returnoftheshadow/RotS_Live/commit/df0279531d46ac40782a7541c45845a57ebe3f56) | Merge pull request #292 from ahumbert/docs/color-help-mob-slot | Skip duplicate merge | Contained commits are listed individually; the remerge-diff check found no additional resolution patch. |
| [`9aa2031e`](https://github.com/returnoftheshadow/RotS_Live/commit/9aa2031e0aeaa4fb65d5e92793121dd562d8d66a) | feat(account): add roster summary cache | Ported R (local) | New src/persist/roster_cache.cpp and src/roster_cache.h; src/tests/roster_cache_tests.cpp; wire all three build lists under RotS::persist. |
| [`3543d1d2`](https://github.com/returnoftheshadow/RotS_Live/commit/3543d1d2372e113d0a07b898f1e0af94ce6f3ba0) | feat(account): invalidate roster summaries on character write | Ported R (local) | src/persist/account_management_assets.cpp and src/persist/account_management_identity.cpp invalidate successful writes/deletes; src/app/db_boot.cpp enables cache at boot. |
| [`46771aeb`](https://github.com/returnoftheshadow/RotS_Live/commit/46771aeb96a6f62f8379a51f378c1c809fedc33c) | feat(account): order and filter the linked-character roster | Ported R (local) | src/persist/account_management.cpp and src/account_management.h: one ordered/filtered index sequence; test stable ties and invalid summaries. |
| [`f22387cd`](https://github.com/returnoftheshadow/RotS_Live/commit/f22387cd71776bb868175f081db42fa26bddf521) | test(account): strengthen roster ordering coverage per review | Ported R (local) | src/persist/account_management.cpp and account tests: final ordering coverage and unreadable-row behavior. |
| [`844d4fd0`](https://github.com/returnoftheshadow/RotS_Live/commit/844d4fd093cc4e636bd1563232756d31e2878d9e) | feat(account): render and select from one ordered roster list | Ported R (local) | Persist roster/identity/presentation modules and app nanny: render and numeric-select the same active-filter ordered list. |
| [`c3548a96`](https://github.com/returnoftheshadow/RotS_Live/commit/c3548a961991f3fb73155929bbbde403ed0ab030) | fix(account): resolve name selection against the same ordered roster | Ported R (local) | Persist identity plus app nanny: name selection deliberately ignores the active session filter, using the same sorted/capped algorithm with RosterFilter::None; numeric selection uses the active filter. |
| [`d6803545`](https://github.com/returnoftheshadow/RotS_Live/commit/d680354582c2687083f25745d4e7dd4cbad6f52c) | feat(account): sort and filter keys on the character roster | Ported R (local) | Persist roster/storage/types plus descriptor.h and app comm/interpre: sort/filter keys, persisted sort, session-only filter. |
| [`67ec63dd`](https://github.com/returnoftheshadow/RotS_Live/commit/67ec63dd0341f6a25b3e5e2cdd2df05cdd645fd1) | fix(account): persist roster sort on selection, cut redundant writes, add coverage | Ported R (local) | src/app/interpre.cpp and menu tests: save changed sort when leaving roster; avoid redundant writes; adapt Makefile requirements to all build lists. |
| [`699206ce`](https://github.com/returnoftheshadow/RotS_Live/commit/699206ce781672dc3a8dd3fdc4b28c58ec9d4448) | fix(account): final review fixes for roster sort/filter branch | Ported R (local) | src/app/interpre.cpp and account/menu tests: final review fixes; preserve current portable test/build conventions. |
| [`8efeb01a`](https://github.com/returnoftheshadow/RotS_Live/commit/8efeb01a8018e4071d61075cecaf2f57d4bdf04d) | feat(account): render the side sort as labelled sections | Ported R (local) | src/persist/account_management.cpp: side sort renders labelled sections without changing selection numbers. |
| [`5178e14e`](https://github.com/returnoftheshadow/RotS_Live/commit/5178e14e173971150fe3b9cf4ec3e582c3ac6a68) | test(account): close side-sort roster coverage gaps | Ported R (local) | src/persist/account_management.cpp and account tests: side-order, invalid-race and output-boundary coverage. |
| [`e0458069`](https://github.com/returnoftheshadow/RotS_Live/commit/e0458069453f362e544a0c12b2655879e2da6979) | Merge pull request #291 from ahumbert/feat/account-roster-sort-and-filter | Skip duplicate merge | Contained commits are listed individually; the remerge-diff check found no additional resolution patch. |

## Verification and completion boundaries

The three source census baselines (`location_read_census.py --check`,
`room_resolve_census.py --check`, `string_view_census.py --check`) each exited 0 before any
source edit. The initial design-only pass claimed no build/runtime result. Batch B is now implemented locally; its current verification evidence is recorded in [doc-002](../plans/doc-002%20-%20TASK-015-bounded-correctness-implementation-plan.md). All remaining slices, including U1-U4 and N/M, are implemented locally; doc-003 records their validation and the remaining finalization gates.

For each production slice, build/test native macOS arm64 and rots64 and verify both boot
goldens. New or substantially rewritten test files also run under macos-arm64-asan. Run
all three census checks and relevant self-tests when their surfaces change; add accurate
ledger/proof entries rather than raising ratchets to make the port pass. Run account smoke
for the login/network/deletion/recovery/roster changes and the complete combined port.
The finalization gate includes the i386 battery, monolithic test reconciliation and all six
required CI jobs. Existing LSan findings on TASK-029 are findings to distinguish, not a
blanket waiver for sanitizer failures introduced by this port.

## Manual/external work

No production server or real account access is needed. Email tests use local fakes. Publishing
a PR, commits, pushes, merges, CI runs requiring a push, branch-protection changes and deployment
remain separate user-authorized actions. TASK-016 owns the proposed extra smoke-account CI job;
the account-management squash is not a reason to implement that separate CI policy here.

## Design review and chronology

2026-09-06: Owner asked to start TASK-015 against this branch and chose the latest upstream tip.
The recorded August scope expanded to 72 commits. Source inspection established exact squash
equivalence and the partially overlapping reconnect, linkdead and rent fixes. The initial
three census baselines passed. TASK-015 was marked In Progress through Backlog CLI; no source
was modified. This document records the initial delivery slice and proposed design.

Inline design review checked responsibility ownership, current file mapping, the complete
72-commit set, protocol/lifetime boundaries, duplicates, test implications and external
finalization requirements. It identified the movement up-call and Docker named-volume
adaptations as work needing focused implementation plans. It also caught that reconnect
parity is only partially present, despite one already-correct account reconnect branch.
This is not an independent code review or an approved implementation plan.

The existing arc and journal are legacy Markdown without managed document IDs; the installed
Backlog CLI lists blank IDs for them. They were read and preserved. This managed document’s
chronology and TASK-015’s dated notes record this slice; the legacy arc/journal have not been
silently converted or rewritten through raw file edits.

2026-09-07: Owner approved the design with the supplemental local uaf-port scope above and directed progression. Executable plan doc-002 covers the first bounded correctness batch; later slices receive their own focused plans. The earlier design-review paragraph remains historical.


2026-09-07 batch B: 1b2a06bb/84462052/8bd82c94/26c0df11 ported locally within combat/script/app/persist. Ten regression/control tests added, one historical timer characterization corrected. Native and ASan+UBSan: 1,999 discovered, 1,923 pass, 76 skip; rots64: 1,999 discovered, 1,921 pass, 78 skip; zero failures on all three. Both boot goldens match, all three census checks and isolated account smoke pass. Independent review findings closed and reverified. Details are in doc-002 and TASK-015. No commit, push, merge, deployment or finalization-only i386/remote CI is claimed. U1-U4 remain in the approved plan.

2026-09-07 final implementation reconciliation: all B/N/M/V/D/A/P/R/C/O and supplemental U functionality is implemented locally. Independent source-parity reviews account for all 72 release and 38 supplemental commit IDs, including the 9 N and 10 M source commits and both halves of mixed commits. The slice-owner table and doc-003 identify the target files and executed regressions. Finalization state remains on TASK-015; no remote integration or release status is implied.
