---
id: TASK-010
title: Windows world-data boot check in CI
status: To Do
assignee: []
created_date: '2026-08-19 02:07'
labels: []
milestone: m-3
dependencies: []
priority: low
ordinal: 10000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Stand up a Windows host (CI or otherwise) with world data so windows-msvc gets a boot check like the other platforms. Today Windows CI verifies configure+build+ctest+characterization goldens only — no boot against world data, because no Windows world-data host exists (AGENTS.md, Phase 3 exit note).

## Why
Source: AGENTS.md 'Per-platform CMake presets' bullet and Toolchain section ('Windows CI ... does not boot against world data because no Windows world-data host is available') — a deferral standing since Phase 3 (merged 2026-07-09/10). Every other blocking platform has a boot golden; Windows is the one gap.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 A Windows environment with lib/world data can run ageland to the boot-complete point
- [ ] #2 A boot-golden-equivalent check wired into or alongside the windows-msvc CI job (or an owner-recorded decision that it stays deferred)
<!-- AC:END -->
