# Phase 4 Wave 4 — fight/db/comm Final Trio Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Full-catalog modernization of `src/fight.cpp` (11 sprintf-family sites), `src/db.cpp` (111), and `src/comm.cpp` (47), byte-identical in behavior, with dedicated new characterization for the two high-risk surfaces (`save_player`'s on-disk text writer; `act()`/`convert_string`).

**Architecture:** Five conversion chunks in riskiest-last order (F1 fight → D1 db loader/boot → D2 db save-writer → C1 comm misc → C2 act/convert_string), each chunk characterize-then-transform with the per-task dual local gate; then a sweep task and an exit task (i386 battery + four-platform CI + manual account smoke).

**Tech Stack:** C++20, `std::format`, GoogleTest (test-only), CMake presets + Docker (`rots64` per-task; i386 `rots` finalization-only).

**Spec:** `docs/superpowers/specs/2026-07-12-phase-4-wave-4-design.md`. **Branch:** `modernization/phase-4-wave-4` off master (`fe11bac` or later).

## Global Constraints

Every task's requirements implicitly include this section.

- **Golden stance: zero sanctioned golden changes** — boot, **combat** (`CharacterizationCombatTest.*`, seed 42 — active gates this wave), and JSON goldens are STOP-on-diff. Never run `UPDATE_GOLDENS=1` or `boot-golden.sh capture`.
- **Legacy binary decoders in db.cpp are UNTOUCHABLE** — the explicit-offset LEGACY migration converters and `crime_json` must not be modified or reflowed. `fprintf`-to-FILE* calls are NOT in the conversion family anywhere in this wave — they write files directly; converting them is gratuitous churn. Only sprintf/strcpy/strcat string composition converts.
- **RNG discipline:** conversions must not add, remove, or reorder any `number()`/`rots_rng` call. A combat-golden diff means you disturbed the RNG sequence or the bytes — STOP.
- **No third-party libraries; RNG only via `rots_rng`; formatting via `cd src && make format`** (WebKit; if a PostToolUse hook whole-file-reformats, restore to HEAD and re-apply via script — verify with `git diff` that only intended hunks remain).
- **Characterization tests PASS BEFORE the transform** (pin current behavior; not fail-first TDD). A pre-transform failure means the TEST is wrong.
- **Standing fixture rules (each one has bitten a prior wave; all mandatory):** in-place capturing-descriptor reset via `reset_capturing_descriptor(descriptor_data&, char_data*)` copied WITH its MSVC warning comment (from `src/tests/act_format_tests.cpp:69-77`); fixture chars set `tmpabilities.str = 100` (x86 SIGFPE in strength division); every fixture/builder struct fully value-initialized (`{}` — MSVC Debug 0xCC fill exposed garbage reads twice); string-emptiness checks are content checks (`s[0] != '\0'`), never pointer-compares against `""` (MSVC literal pooling); POSIX-ish test calls go through `src/tests/test_platform_compat.h` shims; **before every commit, self-check that NO `char[N]` array reaches `std::format` uncast** (`static_cast<const char*>`; global `buf`/`buf1`/`buf2` (db.h:308-310) and locals are the suspects; this class produced a Critical and two later escapes in Wave 3).
- **Transform idiom catalog (Wave 3 plan `2026-07-11-phase-4-wave-3-giants.md` §catalog, applies verbatim):** (1) one-shot `sprintf(buf,…);send_to_char(buf,ch);` → `send_to_char(std::format(…).c_str(), ch)`; (2) `buf` reused/multi-receiver → keep staging: `strcpy(buf, std::format(…).c_str())`; (3) accumulation chains → one local `std::string out`, `char stage[]` interop for legacy char*-appending helpers; (4) nullable `char*` → `nz()` (utils.h:53), existing `x ? x : "fallback"` ternaries STAY ternaries; (5) `char[N]` → `static_cast<const char*>`; (6) width fidelity `%-7s`→`{:<7}`, `%5d`→`{:>5}`; (7) `sprintbit`/`sprinttype` unconverted, composed after; (8) parser-API buffers stay `char[]` (recorded skip); (9) dead code deleted only with caller-grep proof in the commit message; (10) local-lifetime RAII only — malloc'd results consumed locally → immediate `std::string` capture + `free`; **world-graph ownership (fread_string/strdup into world structs, all of db.cpp's loader allocations) is OUT of scope** — justified skip, deferred to the RAII lifecycle-audit wave.

### Dual local gate (end of EVERY task; Docker Desktop running)

```bash
# (a) macOS native: build, ctest, boot golden
cd src && cmake --build --preset macos-arm64 -j4 && ctest --preset macos-arm64; cd ..
scripts/boot-golden.sh --native build/macos-arm64/ageland verify
# (b) rots64: build, ctest, boot golden
docker compose run --rm rots64 bash -lc 'cd /rots/src && cmake --preset linux-x64 && cmake --build --preset linux-x64 -j"$(nproc)" && ctest --preset linux-x64'
scripts/boot-golden.sh --service rots64 verify
```

Expected: 0 failures both legs; both print `boot log matches golden`. Background any container step exceeding 10 minutes and stop with a one-line note naming it. NO i386 (`rots`) commands per task; NO per-task CI push. qemu-hang and clang-format-hook guidance from Wave 3's constraints file applies.

### macOS ASan run (per task that adds/extends a test file)

```bash
cd src
cmake --preset macos-arm64 -B ../build/macos-arm64-asan \
  -DCMAKE_CXX_FLAGS=-fsanitize=address -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address
cmake --build ../build/macos-arm64-asan -j4 --target ageland_tests
../build/macos-arm64-asan/ageland_tests --gtest_filter='<THIS TASK'\''S SUITES>.*'
cd ..
```

---

### Task 1: F1 — fight.cpp (11 sites)

**Files:**
- Modify: `src/fight.cpp:131`, `:159` (combat-message-file loader errors), `:164-:201` (five `$CH`/`$CD` prefix wraps), `:860-:872` (death_cry composition)

**Interfaces:**
- Consumes: existing suites `CharacterizationCombatTest.*` (goldens), `damage_tests.cpp`, `fight_proc_tests.cpp`; `nz()` (utils.h:53).
- Produces: fight.cpp sprintf-family-free. No new test file expected — every site is either boot-path (loader errors, covered by boot golden), combat-path (goldens), or death-cry (`death_cry`/`death_cry2` fields exercised by the Wave 2 safe_template work). Create `src/tests/fight_format_tests.cpp` ONLY if Step 1 finds a converted line no existing pin or golden covers — then register it in `src/CMakeLists.txt` + `src/tests/Makefile` SRCS **with header-dependency lines** (Wave 3 final-review lesson).

- [ ] **Step 1: Read the 11 sites in context** (`src/fight.cpp:120-210`, `:850-880`). Map each to its coverage: loader errors fire at boot (boot golden); `$CH%s`/`$CD%s` prefix wraps feed the combat message tables used by the goldens' seed-42 transcript; death-cry lines are act() templates. Record the coverage table in your report; decide whether any site needs a new pin.

- [ ] **Step 2: Transform.** Exemplars from the real sites:

```cpp
// BEFORE (fight.cpp:164)
sprintf(buf, "$CH%s", messages->die_msg.attacker_msg);
// AFTER — attacker_msg is char* (message table, heap-loaded); nz() preserves the
// glibc "(null)" byte behavior if a message slot is missing.
strcpy(buf, std::format("$CH{}", nz(messages->die_msg.attacker_msg)).c_str()); // buf reused by act() below (catalog 2)

// BEFORE (fight.cpp:862)
sprintf(buf, "Your blood freezes as you hear %s's death cry.", pers(...));
// AFTER (one-shot into act template staging — check the actual argument at the site)
strcpy(buf, std::format("Your blood freezes as you hear {}'s death cry.", pers(...)).c_str());
```

Add `<format>`/`<string>` includes once. Verify no `number()` call is added/removed/moved.

- [ ] **Step 3: Build + full macOS ctest + BOTH goldens.** Run the dual-gate macOS leg; in your report, explicitly quote the `CharacterizationCombatTest.*` results line (all pass) — combat goldens are this task's primary pin.

- [ ] **Step 4: Dual local gate** (Global Constraints block). Expected: green ×2.

- [ ] **Step 5: Format + commit:**

```bash
cd src && make format; cd ..
git add src/fight.cpp
git commit -m "refactor: convert fight.cpp message composition to std::format (Wave 4 F1)"
```

---

### Task 2: D1 — db.cpp loader/boot messaging (~93 sites)

**Files:**
- Modify: `src/db.cpp` — every sprintf/strcpy/strcat site OUTSIDE `write_player_text`/`save_player` (:2851-:3170, Task 3's chunk) and OUTSIDE the legacy binary decoders (untouchable — identify them in Step 1 and list them by line range in your report)

**Interfaces:**
- Consumes: boot golden (dense coverage of every boot-path log line); `db_loader_tests.cpp`.
- Produces: db.cpp sprintf-family-free outside Task 3's chunk and the untouchable decoders.

- [ ] **Step 1: Inventory and partition.** `grep -nE '\b(sprintf|strcpy|strcat)\(' src/db.cpp` → partition every hit: (a) loader/boot messaging (this task), (b) `write_player_text`/`save_player` region :2851-:3170 (Task 3 — do not touch), (c) legacy decoder / `crime_json` internals (UNTOUCHABLE — list them), (d) parser-buffer/struct-copy skips (catalog 8, recorded). The boot golden covers every line that prints during boot; note the handful of non-boot paths (runtime zone resets, error branches) — those get either a fixture pin in `db_loader_tests.cpp`'s style or a diff-obvious conversion with the site quoted in your report.

- [ ] **Step 2: Transform partition (a) only.** Typical site shape (boot logging through `log()`/`vmudlog` staging buf — catalog 1/2). Watch for `fscanf`-adjacent buffers (parser class, catalog 8 — skip with record) and `strcpy` into world-struct fields (ownership copies, NOT format composition — skip with record).

- [ ] **Step 3: Build + full macOS ctest + boot golden.** The boot golden IS this task's pin — byte-identical required.

- [ ] **Step 4: Dual local gate.** Expected: green ×2.

- [ ] **Step 5: Format + commit:** `git commit -m "refactor: convert db.cpp loader/boot messaging to std::format (Wave 4 D1)"` (src/db.cpp only).

---

### Task 3: D2 — db.cpp save_player + text writer (18 sites, round-trip-gated)

**Files:**
- Create: `src/tests/db_save_roundtrip_tests.cpp` (suite `SavePlayerRoundTrip`)
- Modify: `src/CMakeLists.txt` (alphabetized test list) + `src/tests/Makefile` `SRCS` **+ header-dependency lines**
- Modify: `src/db.cpp:2851-3170` (`write_player_text`, `save_player` sprintf-family sites ONLY — the `fprintf` calls all stay)

**Interfaces:**
- Consumes: `write_player_text(struct char_data* ch, int load_room, const char* scratch_path)` (db.cpp:2853, returns bool, writes the full text save to scratch_path without touching the live file — purpose-built for this test); `CharPlayerDataBuilder`; `test_platform_compat.h` shims; `ScopedTestWorld`.
- Produces: suite `SavePlayerRoundTrip`; the on-disk text format byte-pinned.

- [ ] **Step 1: Read `src/db.cpp:2851-3170` end to end** plus `docs/data-formats/player-save.md`. Inventory the 18 sprintf-family sites; confirm which compose text that lands in the file (convert, gated) vs. staging for syslog (convert, catalog 1/2) vs. parser/copy skips.

- [ ] **Step 2: Write the round-trip pin — BEFORE any conversion.**

```cpp
// Pins the on-disk text player-save format byte-for-byte. write_player_text()
// (db.cpp:2851) exists precisely to serialize to a caller-chosen scratch path
// without touching live data; a fixed fixture character must serialize to
// IDENTICAL bytes before and after the D2 transform. This is an on-disk
// FORMAT pin, not a terminal-output pin: drift here corrupts player files.
TEST(SavePlayerRoundTrip, FixtureCharacterSerializesToStableBytes)
{
    ScopedTestWorld test_world;
    char_data character {};                       // fully value-initialized (MSVC 0xCC rule)
    // Build a deterministic character: every field the writer reads set explicitly.
    // Use CharPlayerDataBuilder for player_data; set name/title/level/abilities/
    // conditions/saving throws per the writer's field list (player-save.md).
    ...
    const char* scratch = "players/test-roundtrip-scratch";
    ASSERT_TRUE(write_player_text(&character, 0, scratch));
    std::string first = read_entire_file(scratch);   // small local helper, std::ifstream
    ASSERT_TRUE(write_player_text(&character, 0, scratch));
    std::string second = read_entire_file(scratch);
    EXPECT_EQ(first, second);                        // writer is deterministic
    EXPECT_FALSE(first.empty());
    // Golden-in-test: pin structural landmarks so a post-transform byte drift
    // fails loudly even without a checked-in golden file.
    EXPECT_NE(first.find("#"), std::string::npos);   // adapt landmarks to the real format per player-save.md
    remove_file(scratch);                            // via test_platform_compat.h shim
}
```

Fill in the real field list and landmarks from Step 1's reading (the sketch's `...` and landmark lines MUST become concrete before this step is checked off). Add a second test that captures `first` into a file-local `static std::string pre_transform_bytes` reference produced from the SAME fixture — the practical pin is: run this suite against UNCHANGED source, save the hash it prints, transform, rerun, compare (document the hash procedure in the test's header comment and your report).

- [ ] **Step 3: Register the file; build; run suite against UNCHANGED source — PASS.** Record the pre-transform byte hash in your report. Commit tests: `git commit -m "test: pin save_player on-disk text format via round-trip (Wave 4 D2)"`

- [ ] **Step 4: Transform the 18 sites** (fprintf untouched; decoders untouched). Re-run `SavePlayerRoundTrip` — the serialized bytes must hash identically to Step 3's record. Any diff = STOP.

- [ ] **Step 5: ASan the suite** (it does real file I/O — leak/UAF checks matter). Expected: clean.

- [ ] **Step 6: Dual local gate.** Expected: green ×2.

- [ ] **Step 7: Format + commit:** `git commit -m "refactor: convert save_player text-writer composition to std::format (Wave 4 D2)"` (src/db.cpp only).

---

### Task 4: C1 — comm.cpp misc (44 sites)

**Files:**
- Modify: `src/comm.cpp` — all sprintf-family sites EXCEPT `convert_string`'s 3 (Task 5): `game_loop` (14), `close_socket` (4), `process_input` (4), `process_output` (4), `main` (3), `perform_subst` (3), `write_to_output` (3), plus singletons (`run_the_game`, `parse_startup_options`, `reject_banned_descriptor_host`, `populate_descriptor_host`, `pnew_descriptor`, `pnew_connection`, `write_to_q_lang`, `write_to_q`, `get_from_q`)

**Interfaces:**
- Consumes: `protocol_tests.cpp` + `startup_options_tests.cpp` (existing coverage of process_input/output and startup paths); boot golden (greeting/boot lines).
- Produces: comm.cpp sprintf-family-free outside convert_string.

- [ ] **Step 1: Read each function's sites in context.** CAUTION: `write_to_output`/`process_output` are the output pipeline — their buffers have size/overflow bookkeeping (`bufspace`, `small_outbuf`); a conversion there must preserve the exact truncation/overflow semantics, not just the bytes. Sites where sprintf targets a pipeline buffer with explicit space accounting are catalog-8-class skips (record) unless the site is a pure message compose. Partition accordingly.

- [ ] **Step 2: Transform** the compose-class sites. Prompt assembly and log lines are catalog 1/2. Greeting/connect messages are covered by the boot golden + protocol tests.

- [ ] **Step 3: Build + full macOS ctest + boot golden.** protocol_tests (39 MSDP/ProtocolInput cases) must stay green — they exercise process_input/output directly.

- [ ] **Step 4: Dual local gate.** Expected: green ×2.

- [ ] **Step 5: Format + commit:** `git commit -m "refactor: convert comm.cpp connection/loop messaging to std::format (Wave 4 C1)"`

---

### Task 5: C2 — act()/convert_string (3 sites, token-pinned)

**Files:**
- Create: `src/tests/comm_act_tests.cpp` (suite `ActTokenExpansion`)
- Modify: `src/CMakeLists.txt` + `src/tests/Makefile` `SRCS` + header-dependency lines
- Modify: `src/comm.cpp:2172-2368` (`convert_string`'s 3 sprintf-family sites; `act()` itself at :2333 has none — verify)

**Interfaces:**
- Consumes: `act(const char* str, int hide_invisible, char_data* ch, obj_data* obj, void* vict_obj, int type, char spam_only)` (comm.cpp:2333); `convert_string(const char* str, int hide_invisible, char_data* ch, obj_data* obj, void* vict_obj, char_data* to, const char* buf)` (comm.cpp:2172); fixtures mirrored from `act_format_tests.cpp` (RoomPairContext pattern for actor/observer, in-place descriptor resets).
- Produces: suite `ActTokenExpansion`; the token engine byte-pinned; comm.cpp fully converted.

- [ ] **Step 1: Read `convert_string` end to end (:2172-:2330).** It is a hand-rolled scanner — the scanner ARCHITECTURE STAYS (rewriting it is out of scope; only its 3 sprintf-family sites convert). Inventory every `$` token case: `$C?` color codes (N/C/Y/T/S/R/H/D/K/O/E/G), `$n/$N` (PERS), `$m/$M`, `$s/$S`, `$e/$E`, `$o/$O`, `$p/$P`, `$a/$A`, `$T`, `$F`, `$u/$U` (verify the actual case list from the source — this list is the expected shape, the source is authoritative).

- [ ] **Step 2: Write `ActTokenExpansion` pins — BEFORE any conversion.** Two-character room fixture (actor "Actor", observer descriptor capturing). One test per token family, asserting exact bytes; routing tests for TO_CHAR/TO_VICT/TO_ROOM/TO_NOTVICT; hide_invisible gate; PRF_SPAM gate; the empty-expansion early return (`if (*buf != '\0')`). Exemplar:

```cpp
// $n expands to PERS(ch, to) — the actor's name for a seeing observer.
TEST(ActTokenExpansion, DollarNExpandsToActorNameForSeeingObserver)
{
    RoomPairContext context; // mirror act_format_tests.cpp:102-141, WITH teardown
    context.actor.player.name = const_cast<char*>("Actor");
    act("$n waves.", FALSE, &context.actor, nullptr, nullptr, TO_ROOM, 0);
    EXPECT_STREQ(context.victim_descriptor.small_outbuf, "Actor waves.\n\r");
}
```

(Adapt the trailing `\n\r` to what act()/SEND_TO_Q actually append — pin the REAL bytes found by running, not assumed ones.)

- [ ] **Step 3: Register; build; run `--gtest_filter='ActTokenExpansion.*'` against UNCHANGED source — PASS.** Commit tests: `git commit -m "test: pin act()/convert_string token expansion bytes (Wave 4 C2)"`

- [ ] **Step 4: Transform convert_string's 3 sites** (they are `strcpy`-class inside the scanner — catalog 2/3; the token-case switch and pointer walk stay). Suite + full ctest green.

- [ ] **Step 5: ASan the suite.** Expected: clean.

- [ ] **Step 6: Dual local gate.** Expected: green ×2.

- [ ] **Step 7: Format + commit:** `git commit -m "refactor: convert convert_string composition sites to std::format (Wave 4 C2)"`

---

### Task 6: Sweep

**Files:**
- Modify: `src/fight.cpp`, `src/db.cpp`, `src/comm.cpp` (stragglers only)

- [ ] **Step 1: Audit greps** across all three files (sprintf family; malloc/free; `char buf[`-style locals). Build the full disposition table BEFORE changing anything: converted / catalog-8 skip / untouchable-decoder / world-graph-ownership skip — every hit dispositioned.
- [ ] **Step 2: Convert or justify every unexplained hit** (same rules; anything gnarly gets a written justification, not a gamble).
- [ ] **Step 3: char[N]-cast self-audit** across every std::format call added this wave in all three files (the Wave 3 escape class) — scripted scan, result in the report.
- [ ] **Step 4: Dual local gate.** Expected: green ×2.
- [ ] **Step 5: Commit:** `git commit -m "refactor: Wave 4 sweep — final sprintf-family closure in fight/db/comm"` with the disposition ledger in the body.

---

### Task 7: Exit — docs, i386 battery, CI, account smoke

**Files:**
- Modify: `CLAUDE.md`, `AGENTS.md` (test-count baselines → post-wave actuals), `docs/superpowers/plans/2026-07-12-phase-4-wave-4-trio.md` (exit section)

- [ ] **Step 1: Docs.** Update the two test-count passages with battery actuals; BUILD.md only if a genuinely new lesson emerged.
- [ ] **Step 2: Dual gate from clean tree + the deferred i386 battery** (`docker compose run --rm rots bash -lc 'cd /rots && make test'`; monolithic runner; `scripts/boot-golden.sh verify`) — background long steps; qemu hang = kill + rerun; tolerated flake is ONLY the documented monolithic-runner SIGSEGV signature.
- [ ] **Step 3: Manual telnet account smoke** (AGENTS.md requirement — comm.cpp touched connection surfaces; `make smoke-account` is env-blocked): USV-style nanny() drive against rots64 — create account → verify (ROTS_SENDMAIL_COMMAND capture) → create character → enter game → `look`/`score`/`who` sanity → quit → reconnect → play linked character. Transcript to `.superpowers/sdd/wave4-smoke-transcript.log`. Clean up test account/character files after; `git status` clean.
- [ ] **Step 4: Push + watch CI:** `git push -u origin modernization/phase-4-wave-4`; all four required jobs green; record run URL. MSVC-only fallout in new tests is the accepted deferred class — fix, re-push, re-watch.
- [ ] **Step 5: Exit section** in this plan (battery actuals ×4, CI URL, zero-golden confirmation, smoke result, deferred list) + commit `docs: record Phase 4 Wave 4 exit`. Then final whole-branch review (most capable model, `review-package master..HEAD`) and the merge decision to the owner.

---

## Self-review notes (write-time)

- Spec coverage: chunk map F1/D1/D2/C1/C2 ↔ Tasks 1-5 (site counts from the 2026-07-12 scout: 11 / ~93 / 18 / 44 / 3); sweep ↔ 6; exit incl. smoke ↔ 7. Both high-risk gates present (SavePlayerRoundTrip Task 3 Step 2 BEFORE transform; ActTokenExpansion Task 5 Step 2 BEFORE transform). Untouchable decoders named in constraints + Tasks 2/3. fprintf exclusion stated once in constraints, referenced in Task 3.
- Placeholder scan: Task 3 Step 2's `...`/landmark sketch is explicitly flagged as MUST-become-concrete from the same step's required reading (player-save.md + the writer source) — the plan cannot pre-state the field list without risking staleness against the real writer; the authoritative-source-wins instruction is deliberate, mirroring Wave 3's ObjFlagDataBuilder adaptation flag. Same pattern for Task 5's token-case list.
- Type consistency: `write_player_text(char_data*, int, const char*)` matches db.cpp:2853; `act(...)` matches comm.cpp:2333; `convert_string(...)` matches comm.cpp:2172; suite names consistent across tasks/ASan filters.
- Verified anchors 2026-07-12: fight sites :131-:872, save region :2851-:3170, comm function-site distribution (game_loop 14, convert_string 3, etc.).

---

## Wave 4 Exit (2026-07-12)

**Final commit:** `9e45139` (branch `modernization/phase-4-wave-4`, 12 commits off master `fe7f7f2`).
**Merge decision:** owner's (pending at write time).

### Battery actuals

- **macOS native:** ctest 1015 / 0 fail / 71 skip; boot golden byte-identical; combat goldens (seed 42) explicitly green; per-task ASan clean.
- **rots64:** ctest 1015 / 0 / 73; boot golden byte-identical.
- **i386 (finalization):** ctest 1015 / 0 / 7; boot golden byte-identical; **monolithic runner 1008 pass / 7 skip / 0 fail, ZERO SIGSEGVs** — first fully-clean monolithic i386 run on record.
- **Four-platform CI:** run 29189954472 — ALL FOUR REQUIRED JOBS GREEN ON THE FIRST ATTEMPT (no MSVC surprise; the per-task cast audits + standing fixture rules closed the class that cost Wave 3 five cycles and the USV effort four).
- **Manual account smoke (AGENTS.md requirement, comm.cpp connection surfaces):** PASS ×2 sessions (create→verify→character→game→look/score/who→quit; reconnect→linked character→game→quit); transcript `.superpowers/sdd/wave4-smoke-transcript.log`; cleanup verified.

### Deliverables

- The final trio converted: fight.cpp (9 conversions / 2 plain-copy skips), db.cpp (66 loader + 18 save-writer + 2 sweep conversions; fprintf and legacy decoders untouched by rule), comm.cpp (30 + 3 convert_string conversions; 7 pipeline-accounting skips preserved). Every remaining sprintf-family hit across the trio carries a disposition (169-hit ledger in `2a39db5`).
- New suites: `SavePlayerRoundTrip` (2 tests — on-disk text-save format byte-pinned via write_player_text round-trip, FNV hash identical pre/post) and `ActTokenExpansion` (34 tests — full real token inventory $C×12/$K/$n/$N/$m/$M/$s/$S/$e/$E/$o/$O/$p/$P/$a/$A/$T/$F/$b/$B/$$, all four routing modes, visibility gates). Suite total 979 → 1015.
- **Two pre-existing memory-safety bugs fixed** (`0050de2`): the `pwdcrypt` global-buffer-overflow in write_player_text (the Wave 3 ASan backlog item — final review upgraded it: `encrypt_line` never emits NUL/newline, so the overflow was a latent save-file line-desync corruption vector) and an uninitialized `chd` struct.
- **Build-system fix** (`9e45139`): `-MMD -MP` auto-dependency generation in src/tests/Makefile, after the finalization battery surfaced a stale-object ODR artifact (missing test_world.h dep line → wild free in ~ScopedTestWorld, i386 monolithic only). Touch-verified (12 dependent TUs rebuild).
- **Historical "tolerated qemu flake" RETIRED:** the clean-build monolithic run passed AccountManagement.FormatsCharacterPromptWithLinkedCharacterList (134 ms) and everything else — the long-tolerated SIGSEGV was almost certainly this stale-object class, not qemu. CLAUDE.md updated: any future monolithic SIGSEGV is investigated, no tolerance carve-out.

### Behavior deltas (all disclosed; everything else byte-identical, goldens untouched)

1. Live password-line bytes in text saves: old code wrote overflow-read garbage after the 10 encrypted bytes (nonzero adjacent memory, e.g. mid-combat autosave); now exactly 10 bytes + NUL. Load path truncates at 10 → zero corruption/login impact, strictly safer.
2. game_loop prompt self-referential sprintf UB and perform_subst unbounded-global overflow removed (byte-identical for all previously-defined inputs).
3. Previously-crashing/garbage paths per Wave 3 precedent (take_cstring-style null guards where old code was UB).

### Reviews

7 task reviews (Sonnet ×6, Opus for the save-writer), zero fix loops required (first wave with none); final whole-branch review (Fable): READY TO MERGE, 0 Critical / 0 Important / 8 backlog Minors; independent 115-arg char[N] scan clean; RNG-discipline and golden-integrity verified per-commit.

### Backlog (carried)

JsonUtils dangling-temporary tests (json_utils_tests.cpp:83/:96, ASan-visible, pre-existing); world[0].light shuffle pollution (seed-1, ActWizInspection→ActInfoPerception); perform_subst residual caller-buffer ceiling (pre-existing); mudlog const-correctness; test-fixture profs leaks; buf-aliasing display cluster (Wave 3 carry); RAII lifecycle-audit wave; -funsigned-char + Phase 5 hardening items.

### Phase 4 status

With the trio done, the Phase 4 transform catalog has now been applied to every targeted module (Waves 1-4). Remaining Phase 4-adjacent work is the deferred backlog above; Phase 5 (hardening: -Wall/-Wextra, sanitizers in CI, -funsigned-char audit, clang-tidy, 32-bit retirement after data migration) is next per the parent spec.
