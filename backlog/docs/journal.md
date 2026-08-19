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
