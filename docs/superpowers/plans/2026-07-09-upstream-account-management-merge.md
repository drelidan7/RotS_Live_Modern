# Upstream `account-management` Merge Plan (2026-07-09)

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development.
> Full assessment (read it first): `.superpowers/sdd/upstream-merge-assessment.md`.

**Goal:** Merge `upstream/account-management` (returnoftheshadow/RotS_Live, 12 commits:
savebench/atomic-finalize/account-cache/autosave-rewrite #269, negative-hp crash fix #272,
whistle-flee #273, script death fixes #270, `account unlockselect`, teleport@93, ranger
keywords, + release-frodo docs) into `master`, then into `modernization/phase-3`, keeping
both sides' independent work intact — and record the follow-on work it creates for Phase 3.

**Interrupt context:** Phase 3 Task 1 is complete (review approved). This merge is
sequenced BEFORE Phase 3 Task 2 because upstream touches the same files Tasks 2–4 rewrite
(comm.cpp heartbeat, db.cpp save paths, interpre.cpp).

## Global Constraints

- All the Phase 3 plan's Global Constraints apply (battery ×3, goldens byte-identical =
  STOP on diff, never commit lib/ or log/, backup tarball before any live-data-touching
  boot, `.claude/.no-autoformat` before C++ edits).
- The merge must preserve OUR master's Phase 0–2b work everywhere both sides touched a
  file (json_utils parse_long strtoll/ERANGE fix, JSON persistence, rots_crypt, RNG seam,
  -iquote/-isystem build hygiene, macOS test fixes in interpre_account_menu_tests.cpp).
- The merge must preserve UPSTREAM's semantics faithfully — its policy changes
  (autosave 240s→30s + un-gated Crash_save_all, alt-session level 95→91) were authored
  deliberately upstream by the repo owner; carry them, flag them in the merge commit
  message and the completion summary, and MEASURE the autosave cost (M3) rather than
  silently widening it.
- AGENTS.md rule: account/login changes require `make smoke-account` as a separate gate —
  `cf3e281` rewires `nanny()`, so it applies to this merge.
- Post-merge test baselines WILL grow (5 new gtest files). New counts get recorded in the
  ledger and become the Phase 3 baselines; a FAILURE is a stop, a changed pass/skip total
  from new tests is expected.

## Locked conflict resolutions (from the trial merge + assessment §2/§3)

| File | Resolution |
|---|---|
| `src/interpre.cpp` | Command table: `mob2csv`@248, then `convertplrobjs`@249, `convertexploits`@250, `savebench`@251 — `command[]` strings and `COMMANDO()` indices in lockstep (ours keep their live indices; upstream's savebench takes the new slot). Keep `cf3e281`'s unlock additions and upstream's other interpre changes. Verify: entries 245–252 aligned index-for-index; no numeric-literal dependents exist (already grepped: none). |
| `src/db.cpp` | Include-union (keep our `json_utils.h`/`<cstddef>`/`<cstdint>` AND upstream's `player_file_finalize.h`/`<cstdio>`). The 3 upstream-touched functions land as assessed: boot_db + account_cache enable, save_player→write_player_text+finalize (legacy text path only — must NOT touch account::write_account_character_file), write_exploits + post-write save_char. |
| `src/Makefile` | Union: our object list + comments stay; add upstream's new objects (`account_cache.o crashsave_schedule.o player_file_finalize.o save_benchmark.o savebench.o`); do NOT reintroduce upstream's `LIBS += -lcrypt` line if our side removed it (rots_crypt is vendored); take upstream's dependency-rules block where it references files we have. Verify each new name appears exactly once. |
| `src/CMakeLists.txt` | Union both source lists; add the 5 new prod sources to server+test targets and the 5 new test files to the test target. Verify exactly-once per list. |
| `.gitignore` | Union (our `tags` + upstream `.DS_Store` entries). |
| `Dockerfile` | Keep ours (already has cmake/gtest/python3); add `pkg-config` from upstream's line. |
| `FEATURES.md`, `WIP.md` | Re-delete (our babc62a "drop imported junk files" decision stands); upstream's content remains reachable upstream, and release-notes/ additions are kept. |
| `proxy/.DS_Store` | Delete (matches upstream hygiene + merged .gitignore). |

**Known silent gap (not a textual conflict — MUST be fixed in M1):** upstream wired its 5
new test files + new prod sources only into CMake. Our 32-bit runner builds from
`src/tests/Makefile` (SRCS + game-object list) — add all of them there too, or the
container runner silently loses coverage the other two runtimes have.

---

### Task M1: Execute the merge (scratch worktree, branch `merge/upstream-account-management`)

Work in the existing trial worktree (`<scratchpad>/merge-trial`, currently mid-merge on a
detached master checkout — create branch `merge/upstream-account-management` there first).
Apply the locked resolutions above; fix the tests/Makefile silent gap; stage everything;
verify `git diff --cached` against the resolution table; commit the merge with a message
enumerating: the 249–251 renumber, the two carried policy changes (30s autosave, 95→91),
FEATURES/WIP re-deletion, and the tests/Makefile wiring addition.
Build + run the full unit suite natively (macOS preset works in the worktree; goldens are
in-repo — run ctest, expect 587 old + new tests, all pass, goldens byte-identical). NO
boot-golden here (worktree has no lib/ data).

### Task M2: Three-runtime battery + smokes (main checkout, `modernization/phase-3`)

Merge `merge/upstream-account-management` into `modernization/phase-3` (current branch of
the main checkout — no branch switching). Then:
1. Battery ×3: 32-bit container (`make test` + runner from `/rots/src/tests` +
   `scripts/boot-golden.sh verify`), rots64 (`ctest --preset linux-x64` +
   `boot-golden.sh --service rots64 verify`), macOS (`ctest --preset macos-arm64` +
   `boot-golden.sh --native build/macos-arm64/ageland verify`). Record new baselines.
2. `make smoke-account` (nanny() overlap gate).
3. Live in-game dispatch smoke (BACKUP TARBALL of lib/ first): as implementor run
   `savebench 10`, `convertplrobjs` (no-arg/status form), `convertexploits` (status form),
   `account unlockselect` — each must dispatch to its own handler (risk #2).
4. Goldens byte-identical everywhere; any diff = STOP.

### Task M3: Autosave-cost measurement (risk #1) + policy record

Measure what upstream never did: `Crash_save_all()` wall-clock against OUR JSON
object-save path — a focused gtest or instrumented timing around the loop with a
character carrying a large nested inventory (savebench profiles character JSON only).
Acceptance: per-cadence cost ≪ 30s headroom at representative population; record numbers
in the ledger. If it's pathological, propose (don't silently apply) a config widening.
Record in the ledger + completion summary: 30s cadence and 95→91 both carried as
upstream-authored policy.

### Task M4: Land, push, propagate, re-baseline Phase 3

1. Fast-forward `master` to the merge commit without switching checkouts
   (`git fetch . merge/upstream-account-management:master` — ff-only), push master +
   `modernization/phase-3`, watch CI: 3 required jobs green, windows-msvc allowed-fail.
2. Update ledger + docs: new battery baselines; CLAUDE.md/AGENTS.md test-count mentions
   (~500→new totals) if stale.
3. Phase 3 plan deltas (append a "Post-merge amendments" section to the Phase 3 plan):
   - Task 2 (chrono): comm.cpp heartbeat region now runs `AutosaveTimer::tick` via
     `crashsave_schedule.cpp` (already steady_clock-friendly; `stopwatch.h` is prior art).
     Scout-anchor line numbers in the plan have drifted — re-anchor before dispatch.
   - Task 3 (filesystem): `player_file_finalize.cpp` (rename/remove + `<dirent.h>` sibling
     cleanup scan) joins the migration file list.
   - Task 5 (MSVC/POSIX): new POSIX surface — `player_finalize_tests.cpp`,
     `save_benchmark_tests.cpp` (`<dirent.h>`, `<unistd.h>`, `<sys/stat.h>`) — add to the
     POSIX-ism census.
   - Battery baselines: replace 587-count expectations with post-merge counts everywhere
     the plan states exact numbers.
4. Resume Phase 3 Task 2 on the merged base.

## Exit criteria

Merge commit on master + phase-3; battery ×3 green at new baselines; goldens
byte-identical; smoke-account green; dispatch smoke correct; CI green (3 required);
autosave cost measured + recorded; Phase 3 plan re-anchored; ledger updated.
