---
id: TASK-015
title: Port latest release-frodo through e0458069 into the modern architecture
status: Done
assignee:
  - '@codex'
created_date: '2026-08-19 03:35'
updated_date: '2026-09-08 15:39'
labels: []
milestone: m-4
dependencies: []
documentation:
  - >-
    backlog/docs/plans/doc-003 -
    TASK-015-remaining-functionality-implementation-plan.md
priority: high
ordinal: 15000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Current scope (owner, 2026-09-06): port the latest release-frodo tip, pinned after GitHub verification and fetch to e0458069453f362e544a0c12b2655879e2da6979, into the current branch fix/task-025-summon-dark-ok (base c88186b5f0c4d643d850ced7e14b6b45c197f757). The range has 72 commits, including the original 40 through PR #279 and 32 subsequent commits through PR #292 / roster merge PR #291. Preserve the modern library architecture, Placement API, networking shim, and current branch fixes. Supplemental source approved 2026-09-07: local fix/spell-room-affect-uaf-port at 1d242d46 for summoning distance, death-room XP/kill credit, poison-death punishment policy and supporting registry bounds.

The original August scope and rationale follow as historical context:

Bring upstream/release-frodo (returnoftheshadow/RotS_Live) into master: 40 commits since merge-base 73734ee5 (2026-07-08), headlined by five PRs merged upstream 2026-08-17 — #276 core-server-health (bash double-delay interrupt-ordering fix, battle-mage bash-cast resist, wizset case-insensitive OB, WAIT_STATE_FULL reentrancy), #277 MSDP reconnect parity (room data on reconnect, not just fresh login), #278 CHARSET ISO-8859-1 advertisement, #279 account-menu logout closes the connection — plus late-July fixes: bare_prompt_pending across EAGAIN retries, g_skip_next_before_enter_for dangling into unrelated moves, proxy set_nodelay failure tolerance, docker run-as-host-user.

This is a PORT, not a mechanical merge. Upstream is still on the flat pre-modernization layout (src/act_move.cpp vs our src/app/act_move.cpp etc.), and several changes land exactly where our waves refactored: the MSDP room-update path (msdp_room_update_impl relocation + LS-3b's stashed-VNUM handling), act_move's before-enter flow (LS-2's R7 re-read reasoning around call_trigger(ON_BEFORE_ENTER) — upstream's g_skip_next_before_enter_for fix touches that exact machinery), interpre/comm's account-menu and prompt paths (heavily reworked by account-management + output_seam), and battle_mage_handler (now rots_combat, L3). Earlier commits in the range (test files, account_smoke evolution) need commit-by-commit triage: some may already exist here in modernized form via the July syncs.

## Why
Source: owner request, 2026-08-18 conversation — 'We need to add a backlog task of medium to high priority for merging release-frodo into this branch. Merge-conflicting changes went in there a day or two ago, and bringing the logic over will have some challenges.' Delta verified against the fetched upstream branch same day (git log master..upstream/release-frodo, 40 commits, five 2026-08-17 merge commits e6641684/8ca8ae3c/a5e6c878/0e9193eb). Priority high within the owner's medium-to-high band: these are live-game fixes, the conflict surface grows with every wave master lands, and the longest-idle upstream sync so far (5+ weeks) is already the program's largest.

## Method note
Follow docs/superpowers/specs/2026-07-10-upstream-sync-validation-design.md's port-under-characterization approach; per-commit triage table (already-here / port / skip-with-reason) before any code moves.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [x] #1 Per-commit triage and final disposition for all 72 commits through e0458069: already-present, ported with target-branch file mapping and validation evidence, or skipped with reason
- [x] #2 Every ported behavior change lands with a test or characterization pin on this tree; goldens regenerated only for disclosed intentional drift
- [x] #3 make smoke-account passes (MANDATORY — #279 logout, MSDP, prompt/EAGAIN and CHARSET changes sit squarely on the login/connection path)
- [x] #4 All three censuses (location-read, room-resolve, string-view) exit 0; no raw location spelling, flat-layout path or reversed library dependency is reintroduced
- [x] #5 Full verification cadence: both hosts green, boot goldens byte-identical or disclosed, i386 battery + six CI jobs at finalization
- [x] #6 Local uaf-port follow-ups at 1d242d46 are dispositioned and adapted: summon distance, remote-credit death-room XP, poison punishment/attribution separation and supporting registry bounds; existing modern UAF fixes retained and regression-tested
<!-- AC:END -->

## Implementation Plan

<!-- SECTION:PLAN:BEGIN -->
1. Completed: all selected functionality from72 release and38 supplemental commits implemented in the current architecture; final dispositions in doc-001.
2. Completed: independent source/architecture/style reviews, regression and characterization tests, native/Linux/macOS sanitizer suites, all host boot goldens, account smoke, proxy/Python tests and census/layer gates.
3. Completed: canonical i386 CMake+CTest, clean Makefile monolithic run and boot golden; counts reconciled in doc-003.
4. Remaining external acceptance only: six required remote CI jobs on these exact changes. TASK-015 stays In Progress with AC5 open until authorized publication and passing CI; no automatic commit, push, merge, deployment or CI waiver. No implementation slice remains pending.
<!-- SECTION:PLAN:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
2026-09-06: Owner requested starting this port on the current branch and explicitly selected the latest release-frodo tip. Verified via git ls-remote and git fetch; local tracking ref already matched. Scope expanded from 40 to 72 commits; priority remains high. Initial work is source triage and architecture design before production edits. No commits, push, merge or production-server access authorized.

2026-09-06 initial triage delivered: managed design doc-001 maps every one of the 72 commits. f26d3140 and already-reachable a695658 have identical tree 5ff21eca7efad94ec97ab0e403a22782627366d7. All 11 merge commits have empty remerge diffs. Three census --check baselines independently exited 0. Reconnect parity is only partially present; TASK-030 overlaps the MSDP guard correction. No production code ported and no acceptance criterion checked. Next: owner review of the concrete architectural design, then bounded correctness slice B. Existing arc/journal have no managed IDs (CLI lists blank IDs); preserved them and recorded the slice chronology in doc-001 and these notes.

2026-09-07: Owner approved doc-001 with the explicit addition of local RotS_Live uaf-port summon/kill-credit follow-ups and directed moving to the next step. Source resolves to fix/spell-room-affect-uaf-port at 1d242d46b10a2462cb538d789fb23a6047e687dd, worktree /Users/drelidan/Projects/GitHub/RotS_Live/.claude/worktrees/uaf-port. Its 38-commit range above local release-frodo e65027f0 is a supplemental source, not a replacement for the pinned 72-commit release range. Selected additional behavior: ef84bcb summon distance square; ea88227 + 1c2b358 death-room XP distribution with remote credit; dd92094 through a1a8cb3 plus final hardening poison punishment classification; 8e987fb registry bounds guard. Earlier TASK-018/019/020/021/025/026 ports overlap this tree and require residual comparison, not replay. Source working trees have unrelated runtime modifications/untracked crash artifacts and remain read-only. Design approval is recorded; proceed to per-slice executable plans and implementation without another design-approval question. Work remains on the explicitly requested current branch.

2026-09-07 batch B complete locally (uncommitted): ported 1b2a06bb cooldown iteration, 84462052 nested-script traversal, 8bd82c94 wizset OB case matching and 26c0df11 long-description truncation into current library owners. Added 10 tests, corrected the old timer skip characterization, and observed discriminating pre-fix failures including the nested unterminated-script crash. Full native and ASan+UBSan suites: 1999 discovered / 1923 pass / 76 skip; rots64: 1999 discovered / 1921 pass / 78 skip; zero failures. Both boot goldens match; all three census checks exit 0; isolated native/proxy account smoke passes. Independent read-only review findings closed and final source diff reverified. Source pins and branch/HEAD unchanged. doc-002 records exact behavior, fixture corrections and evidence. Parent acceptance criteria remain unchecked because later release slices and local uaf-port U1-U4 are pending; i386 and six remote CI jobs remain whole-port finalization gates. No Git commit/integration or production access performed.

2026-09-07 owner directed continuous autonomous execution through all remaining functionality. doc-003 governs remaining work and combined finalization. No commit/push/merge or production access grant inferred.

Autonomous continuation: U/C/O implemented and independently reviewed; native2019 discovered green, Linux/ASan full runs plus corrected colour-menu rechecks clean, both boot goldens match. D delay/bash regressions reproduced and13 focused tests pass; independent review closed. A/P/R account and V movement integration in progress under doc-003. All changes remain local/uncommitted; final lifecycle smoke and whole-port gates outstanding.

2026-09-07 A/P/R and V/D coherent batch verified locally. Full native and ASan+UBSan: 2155 discovered, 2079 passed, 76 platform skips; Linux x64: 2155 discovered, 2077 passed, 78 skips; zero failures. Both boot goldens match. All three census checks and the location/room-resolve self-tests pass (string-view has no self-test command). Isolated account/proxy lifecycle smoke passes after narrowly porting logout reconnect and roster prompt expectations. Movement review findings reproduced (null mount after early follower callback; replacement registry-slot passenger moved without its own check), fixed and independently re-reviewed PASS. Actual heap-free mount/rider/windblast tests pass ASan; registry generation/ABA redesign remains outside this port. Account archive rollback/symlink and pipelined recovery secret fixes reviewed; Linux mail-failure fixture now drains stdin before reporting failure. Source-parity audit accounts for all72 release and38 supplemental rows; no missing selected production behavior found outside pending N/M. U1-U4 and modern UAF overlap satisfy AC6. Next: N/M regressions and implementation, then full finalization; task remains In Progress.

2026-09-07 final implementation: all B/N/M/V/D/A/P/R/C/O and local U behavior ported into current owners. Exact72+38 inventory reconciled in doc-001; independent source/architecture/style reviews closed. Full native/ASan2194 discovered:2118pass76skip; Linux2194:2116pass78skip; zero failures. Final two string_view logging locals rebuilt and124 account-menu tests pass on all three. Both final boot goldens match. Final isolated account/proxy lifecycle smoke passes; Python54/54 and Rust3/3 including WebSocket pass. All3census checks and location/room-resolve self-tests exit0; all9layer checks/converter green. Disclosed golden drift only character slot15 JSON key reserved_15->mob, with legacy read compatibility. doc-003 records behavior owners, tests and log paths. AC1-4 and6 met locally. i386 final battery running sequentially; AC5 remains open because it also requires all six remote CI jobs for these exact changes. No commit/push/merge/publication or live access performed.

2026-09-07 final local gate complete: i386 step0/1/2/3 exit0 in sequence. CMake CTest2194 discovered/2187pass/7skip/0fail; clean monolithic2181 discovered/2157pass/24skip/0fail/no crash; boot golden matches. Monolithic count plus13 standalone gates equals CTest2194. Its17 additional ConvertEquivalence skips are the flat Makefile's absent CMake-only converter path; all17 passed in CMake. Logs: log/i386-battery/step1-20260907T160431Z.log, step2-20260907T163233Z.log, step3-20260907T170205Z.log. All74 frozen source/build/data hashes unchanged. All functionality, local review and local validation finished; TASK-030 also closed. AC5 remains open solely for the six exact-change remote CI jobs, unavailable for this uncommitted local diff. No remaining implementation slice, commit, push, merge or live-server action. doc-003 records full counts, skips, provenance and external completion boundary.

2026-09-08: the push of 746c3358 to master failed exactly one of the six required CI jobs, Linux x64 ASan+UBSan (run 34178608118): five InterpreAccountMenu tests failed on LeakSanitizer reports after passing their assertions (UnlockSelectAllowsOneDifferentLinkedCharacterSelectionAndConsumesAtEntry, both StaleAccountBackedCharacterMenuAllowsSelection* tests, RosterSortReturningToTheStoredValueDoesNotWriteOnLeaving, UnknownStoredSortFallsBackAndFilterOnlyVisitDoesNotPersist). Reproduced in rots64 with the linux-x64-sanitize preset and detect_leaks=1. Two test-fixture leak classes, no production leak: (1) the ported reconnect-parity ProtocolCreate() in nanny allocates pProtocol for stack test descriptors that never run close_socket (production descriptors already own one from new_descriptor); (2) the two new roster tests overflow small_outbuf and never return the promoted large_outbuf to bufpool. Fix (local, uncommitted): ProtocolDestroy at the end of the three reconnect tests and ScopedDescriptorLargeOutbufReturn in the two roster tests (src/tests/interpre_account_menu_tests.cpp only). Verified: rots64 linux-x64-sanitize full ctest with detect_leaks=1 and the CI suppressions file 2194/2194, 0 failed; macOS macos-arm64-asan full ctest 2194/2194, 0 failed; changed lines clang-format clean. Why local gates missed it: the local cadence ran the macOS ASan preset (no LeakSanitizer) and the plain Linux preset only; AGENTS.local.md (ignored, machine-local) now adds the Linux leak-detection gate mirroring CI. AC5 stays open until the fix is committed and pushed and all six jobs pass.

2026-09-08 DONE: fixture-leak fix committed and pushed as fa4d9b25 (Fix CI tests). CI run 34182495749 on master at fa4d9b25: all six required jobs pass (Linux x64, Linux x64 ASan+UBSan, Linux i386 legacy, macOS arm64, macOS arm64 ASan+UBSan, Windows MSVC) plus advisory clang-tidy. AC5 is met on the exact changes now on master; AC1-4 and AC6 were met locally per the 2026-09-07 notes. The port is MERGED on master (746c3358 + fa4d9b25), not a branch; no deployment to the live server is implied. Remaining limitation: the local verification cadence had no LeakSanitizer leg before this task, which is why the fixture leaks reached CI; the machine-local AGENTS.local.md now names the Linux leak-detection gate.
<!-- SECTION:NOTES:END -->
