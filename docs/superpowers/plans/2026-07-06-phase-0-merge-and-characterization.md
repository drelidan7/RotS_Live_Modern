# Phase 0: Account-Management Merge + Characterization Harness — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Merge `upstream/account-management` into this fork (bringing JSON persistence, accounts, and the GoogleTest suite), verify the 32-bit build/boot/tests, tag the modernization baseline, and build the characterization harness (deterministic PRNG, combat transcript goldens, JSON round-trip goldens, boot-log goldens).

**Architecture:** This is Phase 0 of the approved spec at `docs/superpowers/specs/2026-07-06-cpp-modernization-design.md`. Everything runs in the existing 32-bit Docker container (`scripts/rots-docker.sh`); nothing changes platform yet. The characterization goldens captured here are the "before" snapshots every later phase is diffed against.

**Tech Stack:** C++17 (`-m32`, g++ in i386/debian:bullseye container), GNU Make, GoogleTest (via `libgtest-dev`), git.

## Global Constraints

- All builds/tests/boots happen inside the 32-bit container: `docker compose run --rm rots bash -lc '<cmd>'` (or `scripts/rots-docker.sh`). The Makefile's `-m32` cannot build natively on this arm64 Mac.
- The game must boot and the full test suite must pass at the end of every task that touches code.
- Never commit anything under `lib/players/`, `lib/plrobjs/`, `lib/exploits/`, `lib/accounts/`, `lib/world/`, or `log/` (git-ignored runtime data; contains live player data).
- Gameplay behavior must not change. The only intentional behavioral change in this whole phase is swapping `std::rand()` for an owned PRNG (Task 7) — same distributions, different sequences; goldens are captured *after* that swap.
- Preserve the fork's auto-delete guard in `db.cpp` (Task 3) — imported/legacy characters must never be auto-pruned at boot.
- Commit messages: imperative, ≤72-char subject.

**Key branch/refs used throughout:**
- Merge source: `upstream/account-management` (26 commits ahead of merge base `bfee1a4`)
- Fork side: `master` (34 commits ahead; only src-side changes are `src/Makefile` rewrite, `src/CMakeLists.txt` rewrite, and an 8-line `db.cpp` guard)
- Work branch: `modernization/phase-0`

---

### Task 1: Pre-merge hygiene commit

Clean the working tree so the merge starts from a committed state, and extend `.gitignore` so junk/runtime files can't conflict again.

**Files:**
- Modify: `.gitignore`
- Commit: `lib/text/help_tbl`, `lib/text/wizh_tbl` (already-modified runtime text tables), deletion of `lib/.DS_Store`

**Interfaces:**
- Produces: a clean `git status` on `master`, which Task 2 requires before merging.

- [ ] **Step 1: Review the pending text-table changes**

Run: `git diff lib/text/help_tbl | head -50 && git diff lib/text/wizh_tbl | head -30`
Expected: content edits to help/wizhelp tables (in-game edits synced to disk). If instead you see corruption/binary noise, STOP and ask the user.

- [ ] **Step 2: Extend .gitignore**

Append to `.gitignore` (keep existing content):

```gitignore
# macOS Finder junk
.DS_Store

# Local data backups
lib-backup-*.tar.gz

# Account runtime data (never commit)
lib/accounts/
```

- [ ] **Step 3: Stage and commit**

```bash
git add .gitignore lib/text/help_tbl lib/text/wizh_tbl
git add -u lib/.DS_Store
git commit -m "chore: sync text tables, ignore DS_Store/backups/accounts"
```

- [ ] **Step 4: Verify clean tree**

Run: `git status --porcelain`
Expected: only the untracked `lib-backup-pre-sync-2026-07-01.tar.gz` and `lib/accounts/` lines are gone (now ignored); no modified/deleted entries remain.

---

### Task 2: Start the merge; resolve doc/config conflicts

Begin `git merge upstream/account-management` on a work branch and resolve the 20 non-`src/` conflicts. The merge commit itself happens at the end of Task 3.

**Files (all conflict resolutions):**
- Delete: `.DS_Store`, `docs/.DS_Store`
- Modify: `.gitignore`, `AGENTS.md`, `Dockerfile`, `docs/BUILD.md`, `docs/README.md`
- Keep ours: `docs/data-formats/{maze-files,mudlle-and-scripts,object-rent-files,player-save,shop-files,socials-and-messages,text-tables,world-files}.md`, `docs/systems/{cleric-mystic-system,combat-loop,magic-system,specializations,stats-and-character-power}.md`

**Interfaces:**
- Consumes: clean tree from Task 1.
- Produces: an in-progress merge with only `src/CMakeLists.txt`, `src/Makefile`, `src/db.cpp` still conflicted (Task 3 resolves those and commits).

**Why these resolutions:** both branches independently authored the same doc set; the fork's copies were later verified and corrected against live game data, so the fork's docs win. Build/config files get a union.

- [ ] **Step 1: Create work branch and start merge**

```bash
git checkout -b modernization/phase-0 master
git merge --no-ff upstream/account-management
```

Expected: `CONFLICT` output listing 23 files (2 × .DS_Store, .gitignore, AGENTS.md, Dockerfile, docs/BUILD.md, docs/README.md, 13 docs/data-formats+systems files, src/CMakeLists.txt, src/Makefile, src/db.cpp). Merge stops with "fix conflicts and then commit".

- [ ] **Step 2: Delete Finder junk**

```bash
git rm -f .DS_Store docs/.DS_Store
```

- [ ] **Step 3: Keep the fork's verified docs (13 files)**

```bash
git checkout --ours docs/data-formats/maze-files.md docs/data-formats/mudlle-and-scripts.md \
  docs/data-formats/object-rent-files.md docs/data-formats/player-save.md \
  docs/data-formats/shop-files.md docs/data-formats/socials-and-messages.md \
  docs/data-formats/text-tables.md docs/data-formats/world-files.md \
  docs/systems/cleric-mystic-system.md docs/systems/combat-loop.md \
  docs/systems/magic-system.md docs/systems/specializations.md \
  docs/systems/stats-and-character-power.md docs/BUILD.md docs/README.md
git add docs/
```

(`docs/BUILD.md` has zero branch-only lines — ours is a superset. `docs/README.md`: ours reflects the verified doc statuses; the branch's version predates the live-data verification and claims "original world data is no longer available", which is now false.)

- [ ] **Step 4: Resolve .gitignore (union)**

Open `.gitignore`, remove the conflict markers, keep BOTH sides' lines. The branch side contributes exactly these three lines — make sure all three survive (dedupe `lib/accounts/`, which Task 1 also added):

```gitignore
lib/accounts/
tools/__pycache__/
cmake_test_discovery*.json
```

Then: `git add .gitignore`

- [ ] **Step 5: Resolve AGENTS.md (ours + branch's build-target docs)**

Keep the fork's version as the base (it has the load-bearing "Dead / Unused Code" section and Docker/32-bit notes). From the branch side, graft in its new root-Makefile build/test documentation. View the branch version with `git show upstream/account-management:AGENTS.md`. Concretely:

1. Resolve to ours: `git checkout --ours AGENTS.md`
2. In the "Build, Test, and Development Commands" section, add these lines (adapted from the branch; the root `Makefile` arrives with this merge):

```markdown
- Root Makefile wrappers (from the account-management merge; run inside the
  32-bit container): `make configure` (CMake tree), `make build`, `make test`
  (GoogleTest unit tests), `make smoke-account` (proxy-backed account smoke flow).
- Account/login/authentication changes REQUIRE `make smoke-account` (or
  `tools/account_smoke.py`) as a separate validation step — `make test` is
  intentionally unit-test-only.
```

3. Do NOT bring over the branch's "Planning Workflow" section (`FEATURES.md`/`WIP.md` don't exist on this fork).
4. `git add AGENTS.md`

- [ ] **Step 6: Resolve Dockerfile (ours + test/link deps)**

Keep the fork's version (it already adds `cmake`), and extend the apt line so the GoogleTest suite can build and link in-container (tests link `-lgtest -lcrypt`):

```dockerfile
RUN apt-get update && apt-get install -y --no-install-recommends \
        g++ make cmake telnet procps ca-certificates \
        libgtest-dev libcrypt-dev python3 \
    && rm -rf /var/lib/apt/lists/*
```

(Debian bullseye's `libgtest-dev` ships prebuilt static libs, so `-lgtest` links without extra steps. `python3` is for `tools/account_smoke.py` when run in-container.)

Then: `git add Dockerfile`

- [ ] **Step 7: Verify only src conflicts remain**

Run: `git diff --name-only --diff-filter=U`
Expected output, exactly:

```
src/CMakeLists.txt
src/Makefile
src/db.cpp
```

Do NOT commit yet — Task 3 finishes the merge.

---

### Task 3: Resolve src conflicts and commit the merge

The fork rewrote `src/Makefile` and `src/CMakeLists.txt` (structure, flag parity, dead-code exclusion); the branch added 5 new translation units and rewrote `db.cpp` (JSON loading + reformat). Resolution: fork's build-file structure + branch's new sources; branch's `db.cpp` + fork's auto-delete guard.

**Files:**
- Modify: `src/Makefile`, `src/CMakeLists.txt`, `src/db.cpp`

**Interfaces:**
- Consumes: in-progress merge from Task 2.
- Produces: the merge commit. New TUs that later tasks link against: `account_management.cpp`, `character_json.cpp`, `exploits_json.cpp`, `json_utils.cpp`, `objects_json.cpp` (the other `account_management_*.cpp` files are `#include`d into `account_management.cpp`, NOT compiled separately).

- [ ] **Step 1: Resolve src/Makefile (ours + new objects)**

`git checkout --ours src/Makefile`, then:

1. Add to the `OBJFILES` list (alphabetical placement, matching the file's style):
   `account_management.o character_json.o exploits_json.o json_utils.o objects_json.o`
2. Append dependency stanzas. Copy them verbatim from the branch:
   `git show upstream/account-management:src/Makefile | grep -B1 -A2 '_json.o :\|account_management.o :'`
   They will look like this (paths relative to `src/`, no `../` prefix — verify against the command output above):

```makefile
account_management.o : account_management.cpp account_management.h account_management_types.h \
	account_management_identity.h account_management_storage.h account_management_assets.h \
	account_management_migration.h account_management_presentation.h account_management_internal.cpp \
	account_management_identity.cpp account_management_storage.cpp account_management_assets.cpp \
	account_management_migration.cpp account_management_presentation.cpp json_utils.h

character_json.o : character_json.cpp character_json.h json_utils.h structs.h

exploits_json.o : exploits_json.cpp exploits_json.h json_utils.h db.h

json_utils.o : json_utils.cpp json_utils.h

objects_json.o : objects_json.cpp objects_json.h json_utils.h structs.h
```

(If the fork's Makefile uses pattern rules instead of per-object stanzas, adding the five names to `OBJFILES` may be all that's needed — read the fork Makefile's compile rule first.)

3. `git add src/Makefile`

- [ ] **Step 2: Resolve src/CMakeLists.txt (ours + new sources + branch test targets)**

`git checkout --ours src/CMakeLists.txt`, then:

1. Add the 5 new sources to the explicit `add_executable(ageland ...)` list (keep alphabetical order, keep `combat_manager.cpp` excluded — it's dead code).
2. Check the branch's CMake for test integration worth porting:
   `git show upstream/account-management:src/CMakeLists.txt | grep -n 'test\|gtest' | head -20`
   If it defines a test executable/target, port that block, adjusted to the fork's style (explicit source lists, no `file(GLOB)`). If tests remain Makefile-only on the branch, skip — `src/tests/Makefile` is the test entry point either way.
3. Keep the fork's "Flag parity" block untouched (it mirrors `src/Makefile` flags including `-m32`).
4. `git add src/CMakeLists.txt`

- [ ] **Step 3: Resolve src/db.cpp (theirs + auto-delete guard)**

```bash
git checkout --theirs src/db.cpp
```

Then locate `build_player_index` in the file and find the pruning loop — search for `player_table[nr].level < 20` (the branch reformatted the file, so line numbers moved; the condition text survives). Re-apply the fork's guard so it reads (adapt whitespace to the surrounding reformatted style):

```cpp
    // Automatic deletion of inactive low-level characters at boot is DISABLED.
    // Set to true to restore the original pruning (sub-level-20 chars inactive
    // for > level*7 days had a ~51% chance of deletion on each boot). Kept off
    // so imported/legacy characters are never auto-removed.
    const bool enable_auto_delete = false;

    for (nr = 0; nr <= top_of_p_table; nr++) {
        if (enable_auto_delete && player_table[nr].level < 20 &&
```

(The only change to the branch's version: declare `enable_auto_delete` above the loop and prepend `enable_auto_delete && ` to the existing condition.)

Then: `git add src/db.cpp`

- [ ] **Step 4: Verify no conflicts remain and commit the merge**

```bash
git diff --name-only --diff-filter=U   # expected: empty
git commit -m "Merge upstream/account-management (JSON persistence, accounts, tests)"
```

- [ ] **Step 5: Sanity-check the merged tree**

Run: `ls src/account_management.cpp src/character_json.cpp src/json_utils.cpp Makefile tools/account_smoke.py`
Expected: all exist (root `Makefile` and `tools/` arrived from the branch without conflict).

---

### Task 4: Rebuild the container; build + unit tests green (32-bit)

**Files:**
- No new files; fixes whatever the merge broke (expect small breaks: missing objects in Makefile, include drift).

**Interfaces:**
- Consumes: merge commit from Task 3.
- Produces: `bin/ageland` builds in-container; `src/tests` suite passes — every later task's verification depends on both.

- [ ] **Step 1: Rebuild the Docker image (Dockerfile changed in Task 2)**

Run: `scripts/rots-docker.sh build`
Expected: image builds; apt installs `libgtest-dev libcrypt-dev python3` without error.

- [ ] **Step 2: Compile the game**

Run: `scripts/rots-docker.sh compile`
Expected: compiles to completion (warnings and "deprecated" notices are normal). If link fails on missing symbols from the new TUs, re-check the `OBJFILES` additions from Task 3 Step 1.

- [ ] **Step 3: Build and run the unit test suite**

```bash
docker compose run --rm rots bash -lc 'cd /rots/src/tests && make tests && ls'
```

Find the produced binary name in the `ls` output (historically `bin/tests`; the branch Makefile builds `$(EXECUTABLE)`), then:

```bash
docker compose run --rm rots bash -lc 'cd /rots/src/tests && ./bin/tests'
```

Expected: all tests PASS. The suite links with `-Wl,--wrap=_Z6numberv -Wl,--wrap=_Z6numberii` (RNG seam) and `-lgtest -lcrypt`; if `-lgtest` fails to link, the image rebuild in Step 1 didn't pick up the new Dockerfile.

- [ ] **Step 4: Commit any build fixes**

```bash
git add -A src/ && git commit -m "fix: post-merge build repairs for 32-bit container"
```

(Skip if no fixes were needed.)

---

### Task 5: Boot smoke + account smoke

**Files:**
- No source changes expected; runtime dirs only.

**Interfaces:**
- Consumes: green build from Task 4.
- Produces: verified-bootable baseline that Task 6 tags.

- [ ] **Step 1: Ensure runtime dirs exist (including the new accounts dir)**

```bash
docker compose run --rm rots bash -lc 'cd /rots/src && make setup'
mkdir -p lib/accounts
```

- [ ] **Step 2: Boot the server**

Run: `scripts/rots-docker.sh boot`
Expected: boot log shows world loading (zones, rooms, mobs) and "entering game loop" style output; server listens on :1024 without `-p`. If it aborts on a missing `lib/accounts`-style path, create the path it names and re-boot (the account system's storage bootstrap is new).

- [ ] **Step 3: Telnet connect + create a throwaway character**

From the host: `telnet localhost 1024` — walk the new account/login flow far enough to reach the game menu with a test account (e.g. account `smoketest`, throwaway password). This proves the account layer works against a fresh `lib/accounts/`.

- [ ] **Step 4: Run the account smoke script**

```bash
python3 tools/account_smoke.py --help
```

Read its usage, then run its default flow against the booted server (it drives the login/account flow over telnet). Expected: script reports success. If it needs the Rust proxy (`make smoke-account` variant), prefer the direct-port variant; do not fight proxy setup in this task.

- [ ] **Step 5: Stop the server, verify nothing runtime got staged**

```bash
git status --porcelain
```

Expected: empty (lib/accounts/, players, logs all ignored).

---

### Task 6: Tag the modernization baseline

**Interfaces:**
- Consumes: verified boot from Task 5.
- Produces: tag `modernization-baseline-32bit` — the diff anchor for all later phases.

- [ ] **Step 1: Tag and verify**

```bash
git tag -a modernization-baseline-32bit -m "32-bit baseline: account-management merged; build+tests+boot verified"
git tag -l 'modernization-*'
```

Expected: tag listed.

---

### Task 7: Deterministic, platform-independent PRNG (TDD)

Replace `std::rand()` inside the RNG entry points with an owned, seedable `std::mt19937` (its output sequence is fully specified by the C++ standard — identical on glibc/macOS/MSVC). This is what makes cross-platform golden comparison possible in later phases. Distributions are preserved; only the sequence changes — which is why goldens are captured AFTER this task.

**Files:**
- Create: `src/rots_rng.h`, `src/rots_rng.cpp`
- Create: `src/tests/rots_rng_tests.cpp`
- Modify: `src/utility.cpp` (functions `number()`, `number(double)`, `number(int,int)` around lines 928–970; `dice()` at ~972), `src/comm.cpp:161` and `src/comm.cpp:251` (seeding), `src/Makefile`, `src/CMakeLists.txt`, `src/tests/Makefile` (add the new TU)
- Test: `src/tests/rots_rng_tests.cpp`

**Interfaces:**
- Produces (used by Tasks 8–9 and all later phases):

```cpp
// src/rots_rng.h
namespace rots_rng {
void seed(unsigned int seed_value);   // reseed the global engine
unsigned int next();                  // next raw 32-bit draw
}
```

- [ ] **Step 1: Write the failing test**

Create `src/tests/rots_rng_tests.cpp`:

```cpp
#include "../rots_rng.h"
#include <gtest/gtest.h>

TEST(RotsRng, SameSeedProducesSameSequence)
{
    rots_rng::seed(12345u);
    unsigned int first[8];
    for (unsigned int& value : first) {
        value = rots_rng::next();
    }

    rots_rng::seed(12345u);
    for (unsigned int expected : first) {
        EXPECT_EQ(expected, rots_rng::next());
    }
}

TEST(RotsRng, DifferentSeedsDiverge)
{
    rots_rng::seed(1u);
    unsigned int a = rots_rng::next();
    rots_rng::seed(2u);
    unsigned int b = rots_rng::next();
    EXPECT_NE(a, b);
}

// Pins the engine choice itself: std::mt19937's output for a given seed is
// defined by the C++ standard, so this value must be identical on every
// platform/compiler. If this test ever fails, the engine changed — which
// invalidates every characterization golden.
TEST(RotsRng, EngineIsStandardMt19937)
{
    rots_rng::seed(5489u); // mt19937 default seed
    // First output of std::mt19937 with its default seed, per the standard.
    EXPECT_EQ(3499211612u, rots_rng::next());
}
```

- [ ] **Step 2: Add the test TU to src/tests/Makefile and run to verify it fails**

Add `rots_rng_tests.o` (and a stanza mirroring the neighbors, depending on `../rots_rng.h`) plus `rots_rng.o` to the test Makefile's object lists, then:

```bash
docker compose run --rm rots bash -lc 'cd /rots/src/tests && make tests'
```

Expected: FAIL to compile — `rots_rng.h: No such file or directory`.

- [ ] **Step 3: Implement rots_rng**

Create `src/rots_rng.h`:

```cpp
#pragma once

// Owned, seedable PRNG for all game randomness. std::mt19937's sequence is
// fully specified by the C++ standard, so a given seed produces identical
// draws on every platform/compiler — the property the characterization
// goldens depend on. Replaces std::rand()/random(), whose sequences are
// libc-specific.
namespace rots_rng {
void seed(unsigned int seed_value);
unsigned int next();
}
```

Create `src/rots_rng.cpp`:

```cpp
#include "rots_rng.h"

#include <random>

namespace {
// Single global engine: game randomness was previously one global rand()
// stream; keeping one engine preserves that structure.
std::mt19937 engine;
}

namespace rots_rng {

void seed(unsigned int seed_value)
{
    engine.seed(seed_value);
}

unsigned int next()
{
    return static_cast<unsigned int>(engine());
}

}
```

- [ ] **Step 4: Run the test to verify it passes**

```bash
docker compose run --rm rots bash -lc 'cd /rots/src/tests && make tests && ./bin/tests --gtest_filter=RotsRng.*'
```

Expected: 3 tests PASS. (`EngineIsStandardMt19937` passing on the 32-bit container proves platform-independence of the engine constant.)

- [ ] **Step 5: Route the game's RNG through rots_rng**

In `src/utility.cpp` (add `#include "rots_rng.h"` at the top):

```cpp
// returns a random number from 0.0 to 1.0
double number()
{
    // 2^32 as double; next() is a full 32-bit draw, so this is uniform [0,1).
    return rots_rng::next() / 4294967296.0;
}
```

and in `number(int from, int to)`, replace the single `std::rand()` call:

```cpp
    return (rots_rng::next() % upper_end) + from;
```

`number(double max)`, `number_d`, and `dice()` are implemented in terms of these two — verify they contain no direct `std::rand()` calls of their own (read them; the current code funnels through `number()`/`number(int,int)`).

In `src/comm.cpp`, replace both seed sites with the owned seeding (keep one seed for the whole process):

```cpp
    // comm.cpp:161 — was: std::srand(std::time(0));
    rots_rng::seed(static_cast<unsigned int>(std::time(0)));
```

```cpp
    // comm.cpp:251 — was: srandom(time(0));  (seeded the now-unused random())
    rots_rng::seed(static_cast<unsigned int>(time(0)));
```

Add `#include "rots_rng.h"` to `comm.cpp`, and `rots_rng.o` / `rots_rng.cpp` to `src/Makefile` `OBJFILES` and the `add_executable` list in `src/CMakeLists.txt`.

- [ ] **Step 6: Audit for remaining direct libc RNG use**

```bash
grep -rn '\brand()\|\brandom()\|\bsrand(\|\bsrandom(' src/*.cpp src/*.h | grep -v rots_rng
```

Expected: no hits outside comments after Step 5. Any hit found must be converted to `rots_rng::next()` (mod-range like `number(int,int)` does) — libc `random()`/`rand()` sequences are platform-specific and would poison the goldens.

- [ ] **Step 7: Full rebuild + full test suite + boot smoke**

```bash
scripts/rots-docker.sh compile
docker compose run --rm rots bash -lc 'cd /rots/src/tests && make tests && ./bin/tests'
scripts/rots-docker.sh boot   # confirm clean boot, then stop it
```

Expected: build clean, all tests pass (the `--wrap=number` test seam is unaffected — the wrapped symbols kept their signatures), server boots.

- [ ] **Step 8: Commit**

```bash
git add src/rots_rng.h src/rots_rng.cpp src/utility.cpp src/comm.cpp \
        src/Makefile src/CMakeLists.txt src/tests/
git commit -m "feat: owned mt19937 PRNG behind number()/dice() for cross-platform determinism"
```

---

### Task 8: Combat/damage characterization transcript golden

A seeded end-to-end transcript through the live damage path (`damage()` in `fight.cpp` — the same function the existing `damage_tests.cpp` exercises), captured to a committed golden file. Later phases re-run this after every transform; any drift means behavior changed.

**Files:**
- Create: `src/tests/characterization_combat_tests.cpp`
- Create (generated, then committed): `src/tests/goldens/combat_transcript_seed42.txt`
- Modify: `src/tests/Makefile` (add the TU)

**Interfaces:**
- Consumes: `rots_rng::seed` from Task 7; the fixture pattern from `src/tests/damage_tests.cpp` (`DamageTestContext`, `ensure_test_world`); `int damage(char_data*, char_data*, int dam, int attacktype, int hit_location)`.
- Produces: the golden-update convention used by all characterization tests: run with env `UPDATE_GOLDENS=1` to (re)write goldens, run without to compare.

- [ ] **Step 1: Write the transcript test**

Create `src/tests/characterization_combat_tests.cpp`. Model the fixture on `DamageTestContext` in `src/tests/damage_tests.cpp` — read that file first and reuse its setup verbatim where possible (it already solves world/room bootstrap for tests):

```cpp
#include "../rots_rng.h"
#include "../spells.h"
#include "../utils.h"
#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

int damage(char_data* attacker, char_data* victim, int dam, int attacktype, int hit_location);

namespace {

const char* const kGoldenPath = "goldens/combat_transcript_seed42.txt";

std::string read_file(const char* path)
{
    std::ifstream in(path);
    std::ostringstream contents;
    contents << in.rdbuf();
    return contents.str();
}

} // namespace

// Characterization, not specification: this pins CURRENT behavior of the live
// damage path under a fixed PRNG seed. If a refactor changes this transcript,
// the refactor changed game behavior.
TEST(CharacterizationCombat, DamageTranscriptSeed42)
{
    // Reuse the exact context type from damage_tests.cpp. If DamageTestContext
    // is in an anonymous namespace there, lift it into a shared header
    // (src/tests/damage_test_context.h) rather than duplicating it.
    DamageTestContext context;

    rots_rng::seed(42u);

    std::ostringstream transcript;
    for (int round = 0; round < 100; ++round) {
        // Rolls drive dam and hit_location through the same public RNG the
        // game uses, so the transcript covers armor/location/death handling.
        int dam = number(1, 60);
        int location = number(0, MAX_BODYPARTS - 1);
        int result = damage(&context.attacker, &context.victim, dam, SKILL_BAREHANDED, location);
        transcript << round << ' ' << dam << ' ' << location << ' ' << result
                   << ' ' << GET_HIT(&context.victim) << '\n';
        if (GET_POSITION(&context.victim) == POSITION_DEAD) {
            transcript << "victim dead at round " << round << '\n';
            break;
        }
    }

    if (std::getenv("UPDATE_GOLDENS") != nullptr) {
        std::ofstream out(kGoldenPath);
        out << transcript.str();
        SUCCEED() << "golden updated";
        return;
    }

    EXPECT_EQ(read_file(kGoldenPath), transcript.str())
        << "Combat transcript drifted from golden. If the change is intentional, "
           "rerun with UPDATE_GOLDENS=1 and commit the new golden.";
}
```

Adaptation notes (do these while writing, not later): use the real macro/constant names from the codebase — check `MAX_BODYPARTS` exists in `structs.h` (`grep -n 'MAX_BODYPARTS\|BODYPARTS' src/structs.h`), check `SKILL_BAREHANDED`'s real name in `spells.h` (`grep -n 'BAREHANDED\|TYPE_HIT' src/spells.h`), and match `GET_HIT`/`GET_POSITION` usage as seen in `damage_tests.cpp`. If `DamageTestContext` needs lifting into a header, do it as part of this step.

- [ ] **Step 2: Add TU to the test Makefile; build; verify the compare-mode failure**

Add `characterization_combat_tests.o` to `src/tests/Makefile` (stanza mirroring `damage_tests.o`), create the goldens dir, then:

```bash
mkdir -p src/tests/goldens
docker compose run --rm rots bash -lc 'cd /rots/src/tests && make tests && ./bin/tests --gtest_filter=CharacterizationCombat.*'
```

Expected: FAIL — golden file is empty, transcript is not. (This is the TDD "red": it proves compare mode actually compares.)

- [ ] **Step 3: Generate and commit the golden**

```bash
docker compose run --rm rots bash -lc 'cd /rots/src/tests && UPDATE_GOLDENS=1 ./bin/tests --gtest_filter=CharacterizationCombat.*'
docker compose run --rm rots bash -lc 'cd /rots/src/tests && ./bin/tests --gtest_filter=CharacterizationCombat.*'
```

Expected: first run SUCCEED (golden updated); second run PASS (compare mode). Inspect the golden — it must show varying damage, locations, HP decreasing, and (likely) a death line. A transcript of all-identical lines means the seed isn't reaching the rolls; debug before committing.

- [ ] **Step 4: Commit**

```bash
git add src/tests/characterization_combat_tests.cpp src/tests/goldens/combat_transcript_seed42.txt src/tests/Makefile
git commit -m "test: seeded combat-damage characterization transcript golden"
```

---

### Task 9: Character JSON round-trip golden

Pins the JSON save format byte-for-byte: serialize a fixed fixture → compare to golden; parse the golden → re-serialize → identical. Catches both format drift and nondeterministic serialization.

**Files:**
- Create: `src/tests/characterization_json_tests.cpp`
- Create (generated, then committed): `src/tests/goldens/character_seed_fixture.json`
- Modify: `src/tests/Makefile`

**Interfaces:**
- Consumes: `character_json::CharacterData`, `character_json::serialize_character_to_json(const CharacterData&)`, `character_json::deserialize_character_from_json(const std::string&, CharacterData*, std::string*)` (all in `src/character_json.h`); the golden-update convention from Task 8.

- [ ] **Step 1: Write the round-trip test**

Create `src/tests/characterization_json_tests.cpp`:

```cpp
#include "../character_json.h"
#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

namespace {

const char* const kGoldenPath = "goldens/character_seed_fixture.json";

std::string read_file(const char* path)
{
    std::ifstream in(path);
    std::ostringstream contents;
    contents << in.rdbuf();
    return contents.str();
}

// A fixture exercising every field family: strings (incl. characters needing
// JSON escaping), flags, professions, abilities, points. Extend it whenever a
// new field family is added to CharacterData.
character_json::CharacterData make_fixture()
{
    character_json::CharacterData character;
    character.character_name = "Goldenfix";
    character.title = "the \"Characterization\" Fixture";
    character.description = "Line one.\nLine two with a tab:\t.";
    // Populate every remaining field of CharacterData with distinct non-default
    // values while writing this test — open src/character_json.h beside this
    // file and set each member (abilities, points, professions, flag vectors,
    // equipment/inventory if present) so the golden covers the full schema.
    return character;
}

} // namespace

TEST(CharacterizationJson, SerializeMatchesGolden)
{
    std::string json = character_json::serialize_character_to_json(make_fixture());

    if (std::getenv("UPDATE_GOLDENS") != nullptr) {
        std::ofstream out(kGoldenPath);
        out << json;
        SUCCEED() << "golden updated";
        return;
    }

    EXPECT_EQ(read_file(kGoldenPath), json)
        << "Character JSON format drifted. If intentional (schema change), bump "
           "CHARACTER_JSON_SCHEMA_VERSION, rerun with UPDATE_GOLDENS=1, commit.";
}

TEST(CharacterizationJson, GoldenRoundTripsByteStable)
{
    std::string golden = read_file(kGoldenPath);
    ASSERT_FALSE(golden.empty()) << "run SerializeMatchesGolden with UPDATE_GOLDENS=1 first";

    character_json::CharacterData parsed;
    std::string error;
    ASSERT_TRUE(character_json::deserialize_character_from_json(golden, &parsed, &error)) << error;
    EXPECT_EQ(golden, character_json::serialize_character_to_json(parsed));
}
```

The `make_fixture()` body must be completed against the real `CharacterData` definition — every member set to a distinct value. That is part of this step, not deferred work.

- [ ] **Step 2: Build; verify red; generate golden; verify green**

Add `characterization_json_tests.o` to `src/tests/Makefile`, then:

```bash
docker compose run --rm rots bash -lc 'cd /rots/src/tests && make tests && ./bin/tests --gtest_filter=CharacterizationJson.*'          # expect FAIL (no golden)
docker compose run --rm rots bash -lc 'cd /rots/src/tests && UPDATE_GOLDENS=1 ./bin/tests --gtest_filter=CharacterizationJson.SerializeMatchesGolden'
docker compose run --rm rots bash -lc 'cd /rots/src/tests && ./bin/tests --gtest_filter=CharacterizationJson.*'                        # expect PASS both
```

Inspect `src/tests/goldens/character_seed_fixture.json`: fields populated, escaping correct (`\"`, `\n`, `\t` visible as JSON escapes).

- [ ] **Step 3: Commit**

```bash
git add src/tests/characterization_json_tests.cpp src/tests/goldens/character_seed_fixture.json src/tests/Makefile
git commit -m "test: character JSON serialize + round-trip goldens"
```

---

### Task 10: Boot-log golden script

World-load characterization: boot the server, capture the load-summary lines (zone/room/mob/obj counts), normalize away timestamps/pids, diff against a committed golden. This is the only golden that needs world files, so it's a script, not a unit test.

**Files:**
- Create: `scripts/boot-golden.sh`
- Create (generated, then committed): `docs/superpowers/goldens/boot-log.golden`

**Interfaces:**
- Consumes: `scripts/rots-docker.sh` container conventions; world files at `lib/world/` (present locally, git-ignored).
- Produces: `scripts/boot-golden.sh capture` and `scripts/boot-golden.sh verify` — later phases run `verify` at every phase boundary.

- [ ] **Step 1: Write the script**

Create `scripts/boot-golden.sh` (mark executable):

```bash
#!/usr/bin/env bash
# Boot-log characterization golden.
#
#   scripts/boot-golden.sh capture   Boot in the container, save normalized boot
#                                    log to docs/superpowers/goldens/boot-log.golden
#   scripts/boot-golden.sh verify    Boot the same way and diff against the golden.
#                                    Exit 0 = identical, 1 = drift.
#
# Requires lib/world/ (see docs/BUILD.md). The server is booted just long enough
# to finish world load, then killed.
set -euo pipefail
cd "$(dirname "$0")/.."

GOLDEN=docs/superpowers/goldens/boot-log.golden
mode="${1:-verify}"

[ -d lib/world ] || { echo "ERROR: lib/world/ missing — cannot boot." >&2; exit 2; }

# Boot, give world-load time to finish, capture output, kill.
capture_log() {
  docker compose run --rm rots bash -lc '
    cd /rots && (./bin/ageland 2>&1 & echo $! > /tmp/rots.pid; sleep 20; kill $(cat /tmp/rots.pid) 2>/dev/null; true)' \
  | normalize
}

# Keep only stable world-load lines; strip dates, times, pids, addresses.
normalize() {
  grep -Ei 'boot|load|zone|room|mob|obj|reset|shop' \
    | sed -E 's/[A-Z][a-z]{2} [A-Z][a-z]{2} +[0-9]+ [0-9:]+ [0-9]{4}//g;
              s/[0-9]{2}:[0-9]{2}(:[0-9]{2})?//g;
              s/pid [0-9]+/pid N/g;
              s/0x[0-9a-f]+/ADDR/g'
}

case "$mode" in
  capture)
    mkdir -p "$(dirname "$GOLDEN")"
    capture_log > "$GOLDEN"
    echo "captured $(wc -l < "$GOLDEN") lines to $GOLDEN"
    ;;
  verify)
    [ -f "$GOLDEN" ] || { echo "ERROR: no golden; run capture first." >&2; exit 2; }
    if diff -u "$GOLDEN" <(capture_log); then
      echo "boot log matches golden"
    else
      echo "BOOT LOG DRIFTED from $GOLDEN" >&2
      exit 1
    fi
    ;;
  *) echo "usage: $0 capture|verify" >&2; exit 2 ;;
esac
```

Adaptation note (do while writing): check how `scripts/rots-docker.sh boot` actually launches the binary (working dir, flags — it runs WITHOUT `-p`) and mirror that invocation exactly inside `capture_log`; adjust the `sleep 20` if world load takes longer on this machine.

- [ ] **Step 2: Capture, then verify (red→green in script form)**

```bash
chmod +x scripts/boot-golden.sh
scripts/boot-golden.sh verify   # expected: exit 2, "no golden; run capture first"
scripts/boot-golden.sh capture
scripts/boot-golden.sh verify   # expected: "boot log matches golden"
```

Inspect the golden: it must contain the world-load count lines (rooms/zones/mobs). An empty or 2-line golden means the grep pattern missed this codebase's actual boot phrasing — read the raw boot output and widen the pattern.

- [ ] **Step 3: Run verify a second time to confirm stability**

Run: `scripts/boot-golden.sh verify`
Expected: still matches (proves normalization removed all run-to-run noise). If it drifts between two identical boots, fix `normalize` until stable.

- [ ] **Step 4: Commit**

```bash
git add scripts/boot-golden.sh docs/superpowers/goldens/boot-log.golden
git commit -m "test: boot-log characterization golden script"
```

---

### Task 11: Phase 0 exit checklist + docs

**Files:**
- Modify: `AGENTS.md` (testing section), `CLAUDE.md` (gotchas)

**Interfaces:**
- Consumes: everything above.
- Produces: the documented Phase 0 exit state the Phase 1 plan builds on.

- [ ] **Step 1: Run the complete exit-criteria suite in one pass**

```bash
scripts/rots-docker.sh compile
docker compose run --rm rots bash -lc 'cd /rots/src/tests && make tests && ./bin/tests'
scripts/boot-golden.sh verify
```

Expected: build clean, all tests pass (including the three new characterization suites), boot golden matches. This matches the spec's Phase 0 exit criterion: "merged tree builds in the 32-bit container, boots, passes all tests; goldens captured and committed."

- [ ] **Step 2: Document the harness**

In `AGENTS.md` "Testing Guidelines", add:

```markdown
- Characterization goldens pin current behavior: gtest suites `CharacterizationCombat.*`
  / `CharacterizationJson.*` (goldens in `src/tests/goldens/`) and
  `scripts/boot-golden.sh verify`. If a change intentionally alters behavior,
  regenerate with `UPDATE_GOLDENS=1` (or `boot-golden.sh capture`) and say so
  in the commit message. Unintentional drift = a bug in your change.
- All game randomness flows through `rots_rng` (mt19937, platform-independent).
  Never call `rand()`/`random()` directly.
```

In `CLAUDE.md` "Non-obvious gotchas", add:

```markdown
- **Characterization goldens gate refactors.** `src/tests/goldens/` +
  `scripts/boot-golden.sh` pin behavior under seed 42; run them after any
  combat/persistence/boot change. RNG is owned (`src/rots_rng.*`) — libc
  `rand()`/`random()` are banned.
```

- [ ] **Step 3: Commit and merge back to master**

```bash
git add AGENTS.md CLAUDE.md
git commit -m "docs: characterization harness + owned-RNG conventions"
git checkout master && git merge --no-ff modernization/phase-0
```

Phase 0 complete. The Phase 1 plan (CMake presets + CI matrix) gets written after this lands, against the merged tree.

---

## Plan Self-Review Notes

- **Spec coverage:** Task 2–5 = spec "merge + verify"; Task 6 = "tag"; Task 7 = spec's owned-PRNG requirement; Tasks 8–10 = the three golden families (combat, save/load round-trip, boot log). Phase 0 fully covered; later phases get their own plans per the scope check.
- **Known adaptation points** (flagged inside their steps, to be resolved by the implementer *during* the step, never deferred): exact test-binary path (Task 4), `DamageTestContext` header lift + real constant names (Task 8), full `make_fixture()` population (Task 9), boot-log grep phrasing (Task 10).
- **Type consistency:** `rots_rng::seed(unsigned int)` / `rots_rng::next()` used identically in Tasks 7, 8; golden-update convention (`UPDATE_GOLDENS=1`) identical in Tasks 8, 9; `scripts/boot-golden.sh capture|verify` matches Task 11 usage.
