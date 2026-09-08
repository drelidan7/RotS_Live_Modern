# Arc: Upstream Sync

## Intent
Bring live-game changes from the parent depot (`returnoftheshadow/RotS_Live`, branch
`release-frodo`) into this modernized tree on a recurring basis. Upstream still builds on
the flat pre-modernization layout, so each sync is a port, not a mechanical merge: logic
lands re-expressed through the modernized seams (physical layout, Placement API, hooks,
std::format) and re-proven under this depot's gates.

## Stories
- As a player on the live game, I want fixes made against the live server to also exist in
  the modernized server, so that the eventual cutover loses nothing.
- As the modernized depot's owner, I want each upstream change re-proven under this tree's
  gates (characterization goldens, censuses, smoke flows), so that porting never silently
  regresses the modernization's guarantees.

## Specs & decisions
- docs/superpowers/specs/2026-07-10-upstream-sync-validation-design.md — the validation
  method the third sync established (merge under characterization, not blind).
- AGENTS.md "Instruction Precedence" — the parent/child depot relationship.

## Tasks
- TASK-015 — Port latest release-frodo through e0458069 into the modern architecture
  (Done 2026-09-08; scope grew from 40 to 72 release commits plus 38 supplemental
  uaf-port commits; triage in `backlog/docs/specs/doc-001`, execution plans in
  `backlog/docs/plans/doc-002`/`doc-003`)

## How it unfolded
- 2026-07-06: first upstream account-management merge (`91bc44ae`) — JSON persistence,
  accounts, tests — folded in as Phase 0's foundation.
- 2026-07-09: second sync (`a637ece0`) — savebench, autosave rewrite, crash fixes.
- 2026-07-10: third sync (`10536a93`) — MSDP JSON-sanitize, prac command, spec-save fix —
  merged under the upstream-sync-validation design.
- 2026-08-18: arc reactivated; TASK-015 filed for the 40-commit delta accumulated since
  merge-base `73734ee5` (2026-07-08), headlined by upstream PRs #276-#279 (merged there
  2026-08-17).
- 2026-09-06: owner re-pinned the scope to the latest release-frodo tip, `e0458069` — 72
  commits (the original 40 through PR #279 plus 32 more through PR #292 / roster merge
  PR #291) — and had the work done directly on the current branch. doc-001 mapped every
  commit before any code moved.
- 2026-09-07: owner added the local `fix/spell-room-affect-uaf-port` follow-ups (38 commits
  at `1d242d46`: summon distance, death-room XP/kill credit, poison-death punishment,
  registry bounds) as a supplemental source and directed autonomous execution through
  the remainder. Slices B/N/M/V/D/A/P/R/C/O/U landed as a port, not a merge — every change
  re-expressed through the modern owners (roster cache and sort/filter in `rots_persist`,
  reconnect parity and CHARSET in the output seam and `protocol.cpp`, the before-enter
  flow inside LS-2's re-read reasoning). 1989 → 2194 tests; all local legs green including
  `make smoke-account`, the i386 battery and the three censuses; the one disclosed golden
  drift is the character-slot-15 JSON key rename with legacy read compatibility.
- 2026-09-07 22:00: the port went to master as `746c3358` ("Migrate all upstream changes
  in."). CI failed exactly one required job, Linux x64 ASan+UBSan: five
  `InterpreAccountMenu` tests passed their assertions and then tripped LeakSanitizer —
  stack test descriptors leaking a `pProtocol` the ported reconnect-parity path now
  allocates, and two new roster tests never returning a promoted `large_outbuf`. Production
  was clean; the local cadence simply had no leak-detection leg (macOS ASan cannot run
  LSan).
- 2026-09-08: fixture-only fix pushed as `fa4d9b25`; CI run 34182495749 green on all six
  required jobs. TASK-015 Done. The Linux leak-detection gate is now part of the
  machine-local verification cadence.
