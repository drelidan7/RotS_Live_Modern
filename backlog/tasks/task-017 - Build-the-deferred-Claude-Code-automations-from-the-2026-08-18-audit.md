---
id: TASK-017
title: Build the deferred Claude Code automations from the 2026-08-18 audit
status: Done
assignee: []
created_date: '2026-08-19 23:09'
updated_date: '2026-08-19 23:27'
labels: []
milestone: m-3
dependencies: []
priority: medium
ordinal: 17000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Build the five automation artifacts the 2026-08-18 documentation-audit session identified and the owner deliberately deferred (journal 2026-08-18; recovered from that session's automation-recommender report): two in-repo skills (.claude/skills/i386-battery wrapping scripts/i386-battery.sh plus its operational trap lore, .claude/skills/rr-wave operationalizing docs/superpowers/room-resolve-playbook.md into a wave-standup checklist), two subagents (.claude/agents/adversarial-branch-reviewer codifying the dual-review institution, .claude/agents/gate-runner running the macOS leg of the AGENTS.local.md cadence with container legs explicitly handed back to the controller per the LS-2 cadence amendment), and a PreToolUse hook guarding src/tests/goldens/legacy_*_fixture.bin against 64-bit regeneration (.claude/hooks/guard-legacy-goldens.sh wired in .claude/settings.json, with an explicit HOST_GOLDENS_OK=1 override for legitimate host regeneration of non-legacy characterization goldens). Also refreshes the stale .claude/index codebase index (predates the physical-layout wave).

Owner ruled 2026-08-19: all five items plus the index refresh, one Housekeeping slice; gate-runner takes the macOS-only-plus-controller-handoff shape (Option B) because the recorded subagent docker-stall failure mode (LS-2 T2) makes the full-two-host-agent shape a bet against twice-recorded history.
## Why
Source: the 2026-08-18 documentation-audit session's automation-recommender report, reviewed and
deliberately deferred by the owner that day (journal 2026-08-18), then requested by the owner on
2026-08-19 ("Can we work on creating the skills/agents/hooks that have been identified"). What it
buys: the i386-battery trap lore has bitten the project at least four recorded times and today
lives only in private session memory, invisible to subagents and fresh sessions; the RR playbook
is the active arc (TASK-001..004) and a standup skill removes per-wave re-derivation; the dual
adversarial review and two-host gate cadence are standing institutions enforced only by prose and
memory; and the legacy-goldens 64-bit-regeneration footgun is documented in bold in AGENTS.md but
structurally unenforced. Each artifact converts recorded, repeatedly-paid lesson cost into
checked-in, gate-or-tool form -- the repo's proven pattern (prose goes stale, gates don't).
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [x] #1 Skill i386-battery exists, cites the real scripts/i386-battery.sh steps/markers, and carries the four recorded traps (Mach-O-in-bin restore, future-mtime normalization, qemu-hang vs wedged-Docker-VM signature with the correct restart sequence, orphaned-container handling) plus the monolithic-SIGSEGV and reconciliation rules
- [x] #2 Skill rr-wave sequences the RR wave standup (census, classify, ledger, gates, ceiling re-derivation, doc fold-ins, backlog lifecycle) while pointing at room-resolve-playbook.md as the source of truth rather than duplicating it
- [x] #3 Agent adversarial-branch-reviewer encodes the dual-review brief: adversarial stance, BLOCKER/MAJOR/MINOR findings format, report-dont-fix, no-concurrent-reviewers-on-one-build-tree, clean-rebuild-before-blaming-the-branch
- [x] #4 Agent gate-runner runs the full macOS leg (build, ctest, ASan on test-file changes, native boot golden, both censuses) and ends its report with the exact rots64 controller commands; it never runs docker itself
- [x] #5 Hook blocks Edit/Write to legacy_*_fixture.bin and blocks host Bash UPDATE_GOLDENS commands lacking the i386-container invocation or the HOST_GOLDENS_OK=1 override; hook script exercised directly with allowed and blocked sample inputs
- [x] #6 .claude/index refreshed via /codebase-index update
- [x] #7 Dated journal entry recording the slice; all artifacts committed
<!-- AC:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
All five artifacts built plus the index rebuild; hook verified with 13 direct tests; gate-runner shipped in the owner-chosen Option B (macOS leg + controller handoff). Incidental: .git/info/exclude's blanket .claude/ line narrowed to .claude/index/ — it had kept build-and-smoke untracked since creation.
<!-- SECTION:NOTES:END -->
