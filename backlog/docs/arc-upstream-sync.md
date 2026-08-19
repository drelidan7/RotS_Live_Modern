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
- TASK-015 — Port release-frodo delta (40 commits through upstream PR #279)

## How it unfolded
- 2026-07-06: first upstream account-management merge (`91bc44ae`) — JSON persistence,
  accounts, tests — folded in as Phase 0's foundation.
- 2026-07-09: second sync (`a637ece0`) — savebench, autosave rewrite, crash fixes.
- 2026-07-10: third sync (`10536a93`) — MSDP JSON-sanitize, prac command, spec-save fix —
  merged under the upstream-sync-validation design.
- 2026-08-18: arc reactivated; TASK-015 filed for the 40-commit delta accumulated since
  merge-base `73734ee5` (2026-07-08), headlined by upstream PRs #276-#279 (merged there
  2026-08-17).
