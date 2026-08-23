---
id: TASK-029
title: >-
  Two pre-existing LSan leaks reproduce only in the rots64 linux-x64-sanitize
  run, not on CI
status: To Do
assignee: []
created_date: '2026-08-23 13:40'
labels: []
milestone: m-3
dependencies: []
priority: low
ordinal: 29000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
`ctest --preset linux-x64-sanitize` inside the `rots64` container at master `36bf92ee` fails two
tests with LeakSanitizer reports that the identical CI job (Linux x64 ASan+UBSan, run 32636344287)
passes: `DbLoader.BuildPlayerIndexRemainsConsistentAfterVersionedMigrationRetiresStaleFlatFile`
(two `create_entry()` player-index allocations, `db_players.cpp:1512/:1517`, via
`hydrate_account_native_character_file_from_migration`) and
`ShapeMob.ImplementProtoCopiesMobProtoWithoutCorruptingItsDamageMap` (`new_mob`/`clear_char`/
`implement_proto` string allocations, `shapemob.cpp:237/:1847/:1851`, `entity_lifecycle.cpp:869`).
Neither file was touched by the TASK-018..026 branches (`git log 2869784b..HEAD` on them is empty).
The CI/container discrepancy is unexplained — the bind-mounted `lib/` (which carries host
smoke-account residue) and the container's gcc/libasan build are the two candidate differences.
<!-- SECTION:DESCRIPTION:END -->

## Why
Source: controller finalization of TASK-026 (2026-08-23): the first-ever full Linux-sanitize run
in the container. Until explained, the container cannot serve as a local stand-in for the CI
sanitize job (it reports failures CI does not), which weakens the verification ladder's Linux leg.

## Acceptance Criteria
- [ ] Explain the discrepancy (lib/ state vs toolchain) and make the container run match CI.
- [ ] Fix or scope both leaks with tests that pin the fixture cleanup.
