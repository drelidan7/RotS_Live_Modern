# Project journal

The story of RotS_Live_Modern, one dated entry per merged slice, notable event, or triage
decision. Newest entries at the bottom. Dates verified against git log, not doc prose.

## Pre-2026 — Inheritance
The repo carries the full history of RotS (Return of the Shadow), a Tolkien-themed MUD server,
including years of upstream Noobinabox gameplay PRs through 2023 and the 2026 release-frodo /
account-management branches. The modernization program begins from this living legacy codebase.

## 2026-07-06 — Modernization begins: Phases 0-1
Phase 0 merged the upstream account-management work behind a characterization harness (goldens
pinning existing behavior byte-for-byte), and Phase 1 stood up CMake presets with a four-platform
CI matrix (`f9a17877`, `b39ce732`). The pattern that defines everything after — pin behavior
first, then change with proof — starts here.

## 2026-07-07 — JSON persistence and the 64-bit port
Phase 2a made live saves/loads JSON-only, demoting the legacy binary formats to one-time
migration decoders (`f9b849e2`); Phase 2b brought the 64-bit runtime and macOS native port
(`5c64b95d`), with a lossy-salvage pass for corrupt legacy files (`7de3aa3f`). The i386
container stays on as the canonical shipping ABI guard.

## 2026-07-09..10 — Upstream sync under validation
The upstream account-management stream (savebench, autosave rewrite, crash fixes, MSDP
JSON-sanitize) merged through a validation design rather than blind (`a637ece0`, `10536a93`).

## 2026-07-12..13 — std::format waves and zero-warning hardening
Phase 4 converted the big output files to `std::format` (waves 3-4: `ad8243e9`, `79c9149a`),
Phase 5 reached zero warnings with `-Werror`/`/WX` on all platforms (`bd8c216e`), and the
backlog-cleanup + RAII lifecycle-audit waves (`b87aa625`, `6292a3e3`) paid down leaks, aliasing,
and char_data/obj_data lifecycle debt.

## 2026-07-16..18 — The library split begins
`rots_platform` seeded a real library architecture (`d8f5a73e`), followed by the header split,
logging seam, db split, and the L2/L3 tiers: `rots_entity`, `rots_persist`, `rots_convert`'s
de-weld proof (PRs #3-#8). Each library ships with a CI-linked `*LayerAcyclicity` linkcheck —
architecture as a build failure, not a convention.

## 2026-07-19..22 — The combat row closes; the L4 band stands up
A five-wave arc (combat-seed → blocker-buster → combat-pilot → combat-trio → behavior →
spell-family → spec-pair) moved all 11 deferred combat TUs plus `profs` into libraries, closing
the combat row at DEFER=0. Along the way the L4 band appeared (`rots_pathfind`, `rots_script`,
`rots_olc`), the codebase's two permanent L3→L4 inversions were named and fenced, and the test
suite grew from 1316 to 1510 with the i386 battery reconciling exactly every wave.

## 2026-07-23 — Double-interior combat math (fp-interiors)
PR #19 (master @`c793e879`) converted the four core combat formula families to double interiors
behind a single `rots::fp::to_game_int` boundary, int storage unchanged; the seed42 damage
transcript stayed byte-identical. Phase 2 (double storage) deferred until all player data is
account-native JSON.

## 2026-07-23..24 — Physical layout
A zero-delta wave `git mv`'d every production `.cpp` into `src/<lib>/`/`src/app/` (final battery
at `09ad7b7d`) — the logical architecture became the directory tree, with `nm` object-identity
diffs proving nothing changed.

## 2026-07-24..30 — The LocationSystem program
Four waves plus a follow-up (LS-1 PR #21, LS-2 PR #22/#23, LS-3a PR #25/#26/#27, LS-3b PR #28,
deferred-MINORs PR #29; final @`e68e7849`, 1860 tests) converted every location read and write to
a Placement API and swapped the representation to private handles, enforced by the fail-closed
`LocationReadCensus` gate. The program surfaced and fixed real location-correctness defects
(rnum/vnum channel mixing, load-room riders, follower placement) and established the
dual-adversarial-review + finalization-battery cadence as standing practice.

## 2026-08-01 — Room-resolve retirement program: R1 and R2
Wave R1 (PR #30, @`3e963a32`) stood up `tools/room_resolve_census.py` and the 461-row ledger
with a site-sum ratchet at 788 TODO sites; Wave R2 (PR #31, @`143f78fa`) drained the small tiers
(72 of 83 sites, ceiling → 716), fixed three real `reset_zone` defects, and wrote the
classification playbook R3+ reuses. Pending owner ruling RR-O-1 blocks only the final flip wave.

## 2026-08-18 — Backlog standup
Task tracking stood up in `backlog/` (Backlog.md CLI, manual commits, 3-digit IDs): four
milestones (Room-Resolve Retirement, LocationSystem, FP Determinism, Housekeeping), ten tasks
migrated from the standing deferred/pending items in AGENTS.md, the program specs, and project
memory — each verified against git log and the code before creation. The RR program is the
active arc; LS-4 and FP Phase 2 are captured as gated/optional campaigns. With the owner's
approval, the one tracker-style doc — the Phase 2b 64-bit seed list — was tombstoned into the
board: its live Y2038 items became TASK-011, its MSSP and monolithic-pollution sections were
verified already closed/superseded in code, and the per-wave plans/specs stay untouched as the
historical record the board cites.

## 2026-08-18 — Documentation audit
A three-agent audit re-measured every gate-checked claim true (censuses, ledger sums, library
and test counts) but found the ungated human-facing tier badly drifted across the db.cpp/
structs.h splits, the per-wave relocations, and the physical-layout wave — worst: the RNG
systems doc still describing rand() as live, AGENTS.md's own dead-code section pointing at
pre-layout paths, and five docs calling the deleted combat_manager "dormant." Full findings:
backlog/docs/doc-audit-2026-08-18.md. The owner approved capturing the repair as TASK-012
(substantive fixes), TASK-013 (mechanical path sweep), and TASK-014 (a doc-citation census in
the house gate style); the quick fixes — README's broken links and fictional CI-job
description, and the repo-level clang-format hook that contradicted the tracked
.no-autoformat opt-out — were applied the same day. Skills/subagents recommendations were
reviewed and deliberately deferred by the owner.

## 2026-08-19 — Claude Code automations slice (TASK-017)
Built the five automation artifacts the 2026-08-18 audit identified and the owner deferred,
then approved on 2026-08-19: two in-repo skills (`.claude/skills/i386-battery` — the battery
runbook plus its four recorded operational traps, previously living only in private session
memory; `.claude/skills/rr-wave` — the RR wave-standup sequence pointing at the playbook as
source of truth), two subagents (`.claude/agents/adversarial-branch-reviewer` — the dual-review
brief, findings format, and the no-concurrent-reviewers/clean-rebuild-first process rules;
`.claude/agents/gate-runner` — the macOS verification leg with the rots64 container leg handed
back to the controller, the Option-B shape the owner chose because the recorded LS-2 T2
subagent docker-stall makes a full-two-host agent a bet against twice-recorded history), and a
PreToolUse hook (`.claude/hooks/guard_legacy_goldens.py`, wired in `.claude/settings.json`)
blocking edits to `legacy_*_fixture.bin` and host `UPDATE_GOLDENS` runs lacking the i386
container or an explicit `HOST_GOLDENS_OK=1` override — 13/13 direct hook tests pass, including
the rots64-does-not-count-as-rots discrimination. The stale `.claude/index` (predated the
physical-layout wave; 267 modified + 94 deleted of 249 indexed files) was rebuilt via 13
Sonnet indexing subagents — 350 files across 22 shards, INDEX.md rewritten around the
nine-library layout, one subagent miscount (TOKEN_PATTERNS "eight") caught and corrected
against the file (eleven). Incidental finds: `.git/info/exclude`'s blanket `.claude/` line
(added by the index tooling) had silently kept `build-and-smoke` untracked since creation —
narrowed to `.claude/index/`, so the skills/agents/hook (and build-and-smoke, finally) are
actually committed. A same-day background security review of the commit flagged the hook's
parity gap (Bash cp/mv/dd/redirects onto a fixture path bypassed the Edit/Write block) and its
string-presence container/override detection; both hardened in a follow-up commit — mutating
shell references to fixture paths now require the override (read-only inspections stay free),
UPDATE_GOLDENS must appear inside the compose invocation rather than merely alongside it, the
override token is word-boundary-matched, and the docstring now states the accepted threat
model (accidental-footgun guard, not a security boundary). Battery extended 13 → 27 cases,
all passing — including the hook live-blocking this session's own test harness mid-hardening,
its first real catch.

## 2026-08-21 — RR Wave R3: the dispatch-pattern policy, and `src/combat/` classified (TASK-001 + TASK-002)
The owner folded TASK-002 into TASK-001 and ruled that the policy had to land BEFORE any
classification task — the single decision the whole wave turned on, since 45 of `src/combat/`'s
95 rows (84 of 186 sites) were the dispatch-pattern class Wave R2 had formally deferred to
"R3+'s own policy design". Task 0 ran three parallel read-only censuses (mage/mystic/spell_pa;
fight/limits/clerics/olog_hai/ranger/visibility; and a tree-wide census of the dispatch mechanism
itself) and came back with the finding that shaped everything after it: **no dispatcher checks
placement at all.** `command_interpreter` checks position only; `issue_command`'s 148 callers
check nothing; the `skills[].spell_pointer` doors check nothing on the caster; `shape_center`
bypasses even the position check. The class's tree-wide upper bound was 106 rows / 375 sites —
52% of every remaining TODO site in the program. Owner ruling R3-O-1 adopted policy (A):
`dispatch-invariant`, a sixth proof kind whose evidence is that the DISPATCHER guards the actor,
closed by a marker-anchored registry the gate checks in both directions so an eighth dispatcher
becomes a build failure rather than a free inheritance of the proof.

Nine entry-point tripwires landed first, consumer-free, with one unplaced/placed test pair each
and a measurement rather than an assertion that they are unreachable: rebuilt with every guard
replaced by `abort()`, the entire pre-existing suite produced failures only in the fixtures that
had been dispatching at NOWHERE as a test convenience, and the native boot golden still matched.
Then four classification tasks drained 138 sites — 130 in combat, plus the 5 OLC and 3
`weather_to_char` sites R2 had deferred pending exactly this policy, taking `src/olc/` to zero
TODO. Ledger-wide TODO went 716 → 579 sites; ctest 1865 → 1890.

**What the wave cost, and why, is the part worth remembering.** It spent two entire extra TASKS
repairing its own policy. Task 3d found that the `command_interpreter` guard sat 34 lines BELOW
the `target_parser` call it was supposed to dominate; Task 1c moved it and then STOPped, because
reading `do_cast` from its guard to its dispatch turned up two statements that can relocate the
actor in between — `complete_delay(ch)`, which is mainline for every prepared-spell cast and
re-enters `command_interpreter` from there into arbitrary spec procs, and `appear(ch)`, which
reaches an arbitrary further ASPELL through `affect_modify`'s APPLY_SPELL arm. Task 1d's own
full read found a THIRD one statement above the dispatch. Three readers of the same 430 lines
found two, then three; ruling R3-C-7 concluded that this class cannot be closed by exhaustion and
made the answer structural (ADJACENCY: the tripwire sits immediately before the dispatch, and an
entry that also parses the actor earlier carries a second one there). That is the wave's most
transferable finding, and R4 inherits the machinery rather than the argument.

Three real defects were fixed red-first along the way (an already-failed `spell_blink` no longer
resolves NOWHERE; an unplaced dying character's death cry no longer broadcasts into room 0's
neighbours; an unplaced corpse no longer inherits room 0's "floating here" wording), and a fourth
was filed rather than fixed: TASK-018, a live use-after-free census A found while OVERTURNING
`spell_fireball`'s own HIGH-confidence classification — the orc-fumble arm sets `victim = caster`,
so an NPC self-kill frees the caster before the row's site reads it. That row stays TODO because
of it. 29 rows / 56 sites stay TODO in combat overall, each with an enumerated reason across
eight categories, three of them named this wave (`APPLY_SPELL-window` for the login/rent-load
window, `intervening-relocation`, and `owner-punted` — the owner declined to delete a dead
function, and "the owner said so" is now a recorded category rather than an omission).

The advisory heuristic proved close to useless in this tier and the playbook now says so with
numbers: census A overturned 14 of 42 advisories, census B 11 of 14. Line drift was worse — every
citation Task 1d checked was stale, and the docs task then found several sets Task 1d had missed
or mis-diagnosed, so 15 more rows were re-pointed with every target line re-read. Both are new
pitfalls in `docs/superpowers/room-resolve-playbook.md`, alongside the `dispatch-invariant`
recipe, the seven-category stayed-TODO taxonomy and R3's measured cost row.

**Not done yet.** TASK-001 and TASK-002 both stay In Progress. What remains is T5 finalization
only: the final-HEAD `rots64` leg, `make smoke-account` (mandatory — `do_cast`, `do_use`,
`command_interpreter` and the `raw_kill`/`death_cry` path are all touched), the i386 battery, the
six blocking CI jobs, the controller's one `--check`-derived lowering of `MAXIMUM_TODO_COUNT`
585 → 579, and the dual adversarial whole-branch review. Merge is the owner's call.
