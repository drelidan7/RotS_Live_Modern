---
id: TASK-015
title: 'Port release-frodo delta (40 commits through upstream PR #279)'
status: To Do
assignee: []
created_date: '2026-08-19 03:35'
labels: []
milestone: m-4
dependencies: []
documentation:
  - docs/superpowers/specs/2026-07-10-upstream-sync-validation-design.md
priority: high
ordinal: 15000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Bring upstream/release-frodo (returnoftheshadow/RotS_Live) into master: 40 commits since merge-base 73734ee5 (2026-07-08), headlined by five PRs merged upstream 2026-08-17 — #276 core-server-health (bash double-delay interrupt-ordering fix, battle-mage bash-cast resist, wizset case-insensitive OB, WAIT_STATE_FULL reentrancy), #277 MSDP reconnect parity (room data on reconnect, not just fresh login), #278 CHARSET ISO-8859-1 advertisement, #279 account-menu logout closes the connection — plus late-July fixes: bare_prompt_pending across EAGAIN retries, g_skip_next_before_enter_for dangling into unrelated moves, proxy set_nodelay failure tolerance, docker run-as-host-user.

This is a PORT, not a mechanical merge. Upstream is still on the flat pre-modernization layout (src/act_move.cpp vs our src/app/act_move.cpp etc.), and several changes land exactly where our waves refactored: the MSDP room-update path (msdp_room_update_impl relocation + LS-3b's stashed-VNUM handling), act_move's before-enter flow (LS-2's R7 re-read reasoning around call_trigger(ON_BEFORE_ENTER) — upstream's g_skip_next_before_enter_for fix touches that exact machinery), interpre/comm's account-menu and prompt paths (heavily reworked by account-management + output_seam), and battle_mage_handler (now rots_combat, L3). Earlier commits in the range (test files, account_smoke evolution) need commit-by-commit triage: some may already exist here in modernized form via the July syncs.

## Why
Source: owner request, 2026-08-18 conversation — 'We need to add a backlog task of medium to high priority for merging release-frodo into this branch. Merge-conflicting changes went in there a day or two ago, and bringing the logic over will have some challenges.' Delta verified against the fetched upstream branch same day (git log master..upstream/release-frodo, 40 commits, five 2026-08-17 merge commits e6641684/8ca8ae3c/a5e6c878/0e9193eb). Priority high within the owner's medium-to-high band: these are live-game fixes, the conflict surface grows with every wave master lands, and the longest-idle upstream sync so far (5+ weeks) is already the program's largest.

## Method note
Follow docs/superpowers/specs/2026-07-10-upstream-sync-validation-design.md's port-under-characterization approach; per-commit triage table (already-here / port / skip-with-reason) before any code moves.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Per-commit triage table for all 40 commits: each marked already-present, ported (with the master-side file mapping), or skipped with reason
- [ ] #2 Every ported behavior change lands with a test or characterization pin on this tree; goldens regenerated only for disclosed intentional drift
- [ ] #3 make smoke-account passes (MANDATORY — #279 logout, MSDP, prompt/EAGAIN and CHARSET changes sit squarely on the login/connection path)
- [ ] #4 Both censuses exit 0 and no ported code reintroduces raw location spellings or flat-layout paths
- [ ] #5 Full verification cadence: both hosts green, boot goldens byte-identical or disclosed, i386 battery + six CI jobs at finalization
<!-- AC:END -->
