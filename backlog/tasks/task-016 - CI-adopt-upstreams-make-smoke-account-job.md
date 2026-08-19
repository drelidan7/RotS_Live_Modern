---
id: TASK-016
title: 'CI: adopt upstream''s make smoke-account job'
status: To Do
assignee: []
created_date: '2026-08-19 03:42'
labels: []
milestone: m-3
dependencies: []
priority: medium
ordinal: 16000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Add a seventh CI job running the proxy-backed account smoke flow (make smoke-account) on ubuntu-24.04, cloned from upstream release-frodo's build-test-smoke job (its only CI capability we lack). The tooling needs zero porting — tools/account_smoke.py, account_smoke_tests.py, and the root Makefile target are byte-identical between the trees (verified 2026-08-18), and upstream proves the flow green on a bare runner daily. Skip make test in the new job (unit coverage is owned by the six existing jobs); the smoke-account target already depends on setup+build.

Latency design constraint (owner, 2026-08-18): CI utility is real but so is iteration-cycle latency. First PR run must MEASURE whether the job extends the wall-clock critical path — the i386 legacy job likely dominates, in which case a parallel ~10-15 min job adds nothing to merge latency. If it DOES extend the critical path, propose mitigations (dependency caching, prebuilt image, or non-required-but-always-run status) to the owner before marking it required; required-status designation is the owner's branch-protection call either way.

Sequencing note: ideally lands before TASK-015 starts — the release-frodo port changes exactly the login/connection paths this gate exists for.

## Why
Source: owner request, 2026-08-18 conversation ('Throw it on the board. The CI tests have a lot of utility, but also introduce latency to the iteration cycle'), following the same-day upstream CI inspection: upstream runs make smoke-account in CI while our AGENTS.md gate is local-only, enforced by memory and review discipline. A CI job makes the mandatory account/login gate structural. The latency trade-off is the owner's stated tension and is baked into the ACs, not left to implementer taste.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Seventh job in .github/workflows/ci.yml running make smoke-account with upstream's proven dependency list; green on its introduction PR
- [ ] #2 Critical-path impact measured on that PR (job wall-clock vs the slowest existing job) and reported to the owner with a required/non-required recommendation
- [ ] #3 Owner decides required-status designation; branch protection updated accordingly (owner action, gh api or repo settings)
- [ ] #4 AGENTS.md verification-cadence wording and README's CI section updated in the same PR (both currently say no smoke flows run in CI)
<!-- AC:END -->
