# Phase 4 Wave 3 — act_info/act_wiz Giants Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Full-catalog idiomatic modernization of `src/act_info.cpp` (4,724 lines, 439 sprintf-family sites) and `src/act_wiz.cpp` (4,110 lines, 329 sites), byte-identical in behavior, pinned by new characterization suites.

**Architecture:** Eight command-family chunks (I1-I4 for act_info, W1-W4 for act_wiz), each one task with the same internal shape: write characterization tests that pin the CURRENT output bytes → verify green against unchanged source → transform (std::format / std::string / local RAII / dead-code deletion) → suites and goldens still green → triple local gate → commit. A cross-file sweep task and an exit task close the wave.

**Tech Stack:** C++20, `std::format` (no third-party libs), GoogleTest (test-only), CMake presets + Docker (i386 `rots`, x64 `rots64`).

**Spec:** `docs/superpowers/specs/2026-07-11-phase-4-wave-3-design.md`. **Branch:** `modernization/phase-4-wave-3` off master.

## Global Constraints

Every task's requirements implicitly include this section.

- **Golden stance: zero sanctioned golden changes.** Any boot/combat/JSON golden diff = STOP the task, root-cause, fix the transform. Never run `UPDATE_GOLDENS=1` or `boot-golden.sh capture` this wave.
- **No third-party libraries** in the game binary. `std::format` is the formatting target.
- **RNG only via `rots_rng`** — never `rand()`/`random()`.
- **Formatting:** run `cd src && make format` (WebKit style) before each commit; only on files you touched.
- **Characterization tests pass BEFORE the transform** (they pin current behavior — this is deliberately not fail-first TDD). If a test fails against unchanged source, the TEST is wrong; fix the test.
- **MSVC fixture rule (mandatory, bitten twice):** capturing descriptors are always declared then reset **in place** via a `reset_capturing_descriptor(descriptor_data&, char_data*)` helper copied with its warning comment from `src/tests/act_format_tests.cpp:69-77`. Never return a `descriptor_data` by value — `descriptor_data::output` is a self-pointer into the same object's `small_outbuf[]` and dangles on copy (MSVC Debug crash class, Phase 3 Task 6 / USV finding #3).
- **Platform-shim rule:** any POSIX-ish call in test code (mkdir, sockets, …) goes through `src/tests/test_platform_compat.h` shims from the start (USV findings #1/#2 class).
- **New-test ASan gate:** every task that adds or substantially extends a test file runs that suite once under macOS ASan (exact commands in each task) before the task closes.
- **Known tolerated flake:** on this Apple-Silicon host, the i386 container's *monolithic* runner segfaults at `AccountManagement.FormatsCharacterPromptWithLinkedCharacterList` (qemu-user artifact, documented in the USV exit). ctest-per-test i386, rots64, macOS, and real CI hardware all pass it. Tolerate exactly that one signature; investigate anything else.

### Transform idiom catalog (apply in every chunk)

1. **One-shot `sprintf(buf, fmt, …); send_to_char(buf, ch);`** where `buf` is not reused afterward → `send_to_char(std::format("…", …).c_str(), ch);` (`send_to_char` takes `const char*`, `src/comm.h:33`).
2. **`buf` reused downstream** (e.g. also handed to `mudlog`/`act`) → keep the global staging buffer, Wave 2 idiom: `strcpy(buf, std::format("…", …).c_str());`.
3. **Accumulation (`bufpt += sprintf(bufpt, …)` or `sprintf(buf + strlen(buf), …)` or `strcat` chains)** → local `std::string out;` + `out += std::format(…);` … `send_to_char(out.c_str(), ch);`. When a legacy helper appends into a `char*` (e.g. `weapon_master.append_score_message(bufpt)`), give it a small local `char stage[MAX_STRING_LENGTH];` then `out += stage;` — do not change the helper's signature this wave.
4. **Nullable `char*` arguments** (`title`, descriptions, keeper strings, `poofin`/`poofout`, …) → wrap with `nz(...)` (`src/utils.h:53`) so glibc's historical `(null)` output is preserved and `std::format` never sees a null `char*` (the `dc56cc2` crash class). If the OLD code printed via a `x ? x : "<None>"` ternary, KEEP the ternary — its output differs from `(null)` and both are pinned by tests.
5. **`char[N]` struct members passed to `std::format`** → `static_cast<const char*>(member)` (libc++/libstdc++ divergence, see BUILD.md "Formatting").
6. **`%-7s` / `%5d`-style width/justify** → `{:<7}` / `{:>5}` — pin each converted line's exact padding in a test first.
7. **`sprintbit`/`sprinttype`** fill caller buffers; do NOT convert them (they live in `utility.cpp`, another wave). Let them fill `buf1`/a local, then compose: `out += buf1;`.
8. **Mechanical `char[MAX_STRING_LENGTH]` locals → `std::string`** only where usage is plain compose-and-send. A buffer handed to parser APIs (`one_argument`, `half_chop`) stays `char[]` — record the skip in the commit message.
9. **Dead code:** delete (never modernize) only with caller-grep proof (`grep -rn 'funcname(' src/`) quoted in the commit message.
10. **Local RAII:** malloc'd `char*` results consumed locally (`nth()`, `pkill_get_string()`, `rots_asprintf`) → capture immediately as `std::string` and `free()` the source at once, or use `std::unique_ptr<char, decltype(&std::free)>`; prefer the immediate-`std::string` form. Ownership that leaves the function (e.g. `str_dup` into `char_data`) is OUT of scope — record justification.

### Triple local gate (run at the end of EVERY task; Docker Desktop must be running)

```bash
# (a) i386 container: ctest suite, monolithic runner, boot golden
docker compose run --rm rots bash -lc 'cd /rots && make test'
docker compose run --rm rots bash -lc 'cd /rots/src/tests && make tests && ../../bin/tests'
scripts/boot-golden.sh verify
# (b) macOS native: build, ctest, boot golden
cd src && cmake --build --preset macos-arm64 -j4 && ctest --preset macos-arm64; cd ..
scripts/boot-golden.sh --native build/macos-arm64/ageland verify
# (c) rots64: build, ctest, boot golden
docker compose run --rm rots64 bash -lc 'cd /rots/src && cmake --preset linux-x64 && cmake --build --preset linux-x64 -j"$(nproc)" && ctest --preset linux-x64'
scripts/boot-golden.sh --service rots64 verify
```

Expected, every leg: 0 test failures (skip counts differ per platform — that's normal); every boot-golden run prints `boot log matches golden`. NO per-task CI push — remote CI runs only in Task 10.

### macOS ASan run (per task that added/extended a test file)

```bash
cd src
cmake --preset macos-arm64 -B ../build/macos-arm64-asan \
  -DCMAKE_CXX_FLAGS=-fsanitize=address -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address
cmake --build ../build/macos-arm64-asan -j4 --target ageland_tests
../build/macos-arm64-asan/ageland_tests --gtest_filter='<THIS TASK'\''S SUITES>.*'
cd ..
```

Expected: all filtered tests pass, no ASan report. The separate `-B` build dir leaves the normal preset tree untouched.

---

### Task 1: Chunk I1 — act_info Perception (do_look family)

**Files:**
- Create: `src/tests/act_info_format_tests.cpp`
- Modify: `src/CMakeLists.txt:169` (add `tests/act_info_format_tests.cpp` to the source list, alphabetical — after `tests/act_format_tests.cpp`)
- Modify: `src/tests/Makefile:276` (add `act_info_format_tests.cpp` to `SRCS`, same position)
- Modify: `src/act_info.cpp` — `do_look` (:1037), `do_read` (:1470), `do_examine` (:1485), `do_exits` (:1518), `do_search` (:3292), `do_map` (:3206), `do_small_map` (:3275), plus room/object display helpers this family shares (list_obj_to_char / show_char_to_char / list_char_to_char — whatever the pre-transform read of :100-1036 shows feeding do_look)

**Interfaces:**
- Consumes: `reset_capturing_descriptor(descriptor_data&, char_data*)` and `SoloCharacterContext` — copied (with the MSVC warning comment) from `src/tests/act_format_tests.cpp:69-93`; `ScopedTestWorld` (`src/tests/test_world.h`, `ScopedTestWorld(int room_count = 1)` multi-room contract); `CharPlayerDataBuilder` / `ObjFlagDataBuilder` (`src/tests/`).
- Produces: `src/tests/act_info_format_tests.cpp` with suite `ActInfoPerception` — Tasks 2-4 EXTEND this same file with their own suites (`ActInfoSelfStatus`, `ActInfoWorldSocial`, `ActInfoObjectId`) and re-use its file-local fixtures.

- [ ] **Step 1: Read the whole chunk before writing tests.** Read `src/act_info.cpp:100-1625` and `:3206-3392` end to end. Inventory every sprintf-family call in the chunk's functions (`grep -n 'sprintf\|strcpy\|strcat' src/act_info.cpp | awk -F: '$1>=1037 && $1<=1625'` plus the :3206-3392 band and the helper band). Identify branches a unit test can reach with the existing fixtures (no full world load): closed/open/hidden exits, dark room, sunlit orc exits, examine-with/without-target, read fallback, map for immortal vs mortal. Note branches that genuinely need world data (some of do_look's deep room rendering) — those are covered by the boot golden's room output plus the gate, not by unit tests; record each such exclusion in the test file's header comment.

- [ ] **Step 2: Write the characterization tests.** New file `src/tests/act_info_format_tests.cpp`, modeled line-for-line on `act_format_tests.cpp`'s conventions (same include block, `extern char buf[];`, ACMD forward declarations, anonymous namespace, file-local `reset_capturing_descriptor` WITH its full warning comment). Suite `ActInfoPerception`. Pin exact bytes with `EXPECT_STREQ` on `descriptor.small_outbuf`. Exemplar (do_exits, closed-door branch — adapt values after running against real fixture data):

```cpp
// do_exits: a standing PC in room 0 with one closed, non-hidden north exit
// pins the "%-7s - Closed %s\n\r" line (act_info.cpp do_exits) byte-for-byte,
// including the width-7 left-justified direction column.
TEST(ActInfoPerception, DoExitsFormatsClosedDoorLineWithKeyword)
{
    RoomWithExitContext context; // fixture: ScopedTestWorld + PC + north exit, EX_CLOSED, keyword "gate"
    do_exits(&context.character, const_cast<char*>(""), nullptr, 0, 0);
    EXPECT_STREQ(context.descriptor.small_outbuf, "North   - Closed gate\n\r");
}
```

Write one such fixture struct per world-shape this chunk needs (solo character; room with a configured exit — mirror `DoorContext` from `act_format_tests.cpp:143+`; room with an object on the ground for do_look-at-object/do_examine). Every converted format line whose branch is fixture-reachable gets a pin; a branch that only emits a string literal needs no test.

- [ ] **Step 3: Register the new test file.** Add `tests/act_info_format_tests.cpp` to `src/CMakeLists.txt` (in the alphabetized test-source list at :169) and to `SRCS` in `src/tests/Makefile:276`.

- [ ] **Step 4: Build and run the new suite against UNCHANGED act_info.cpp — must PASS.**

```bash
cd src && cmake --build --preset macos-arm64 -j4 && \
  ../build/macos-arm64/ageland_tests --gtest_filter='ActInfoPerception.*'; cd ..
```

Expected: `[  PASSED  ]` with 0 failures. A failing pin here means the expected bytes are wrong — fix the TEST, not the source.

- [ ] **Step 5: Commit the tests.**

```bash
git add src/tests/act_info_format_tests.cpp src/CMakeLists.txt src/tests/Makefile
git commit -m "test: pin act_info perception-family output bytes (Wave 3 I1)"
```

- [ ] **Step 6: Transform the chunk.** Apply the idiom catalog to every inventoried site in do_look/do_read/do_examine/do_exits/do_search/do_map/do_small_map + shared helpers. Concrete exemplar — the do_exits accumulation loop (catalog items 3, 6):

```cpp
// BEFORE (act_info.cpp:1533-1535)
sprintf(buf + strlen(buf), "%-7s - [%7d][w:%2d] %s\n\r", exits[door],
    world[EXIT(ch, door)->to_room].number, EXIT(ch, door)->exit_width,
    world[EXIT(ch, door)->to_room].name);

// AFTER (out is the function-local std::string that replaced buf accumulation)
out += std::format("{:<7} - [{:>7}][w:{:>2}] {}\n\r", exits[door],
    world[EXIT(ch, door)->to_room].number, EXIT(ch, door)->exit_width,
    nz(world[EXIT(ch, door)->to_room].name));
```

Also fix the file-header `#include` block once (add `<format>` and `<string>`). `char* exits[] = {…}` arrays of string literals become `const char* const exits[] = {…}` while you're in the function (removes a deprecated-conversion warning source; zero behavior change).

- [ ] **Step 7: Rebuild; new suite AND full macOS suite green; goldens untouched.**

```bash
cd src && cmake --build --preset macos-arm64 -j4 && ctest --preset macos-arm64; cd ..
scripts/boot-golden.sh --native build/macos-arm64/ageland verify
```

Expected: 0 failures; `boot log matches golden`. The boot golden exercises real room/exit rendering at boot — it is this chunk's deep do_look coverage.

- [ ] **Step 8: ASan the new suite** (Global Constraints block) with `--gtest_filter='ActInfoPerception.*'`. Expected: pass, no report.

- [ ] **Step 9: Run the full triple local gate** (Global Constraints block). Expected: all three legs green, three `boot log matches golden`.

- [ ] **Step 10: Format and commit the transform.**

```bash
cd src && make format; cd ..
git add src/act_info.cpp
git commit -m "refactor: convert act_info perception family to std::format (Wave 3 I1)"
```

---

### Task 2: Chunk I2 — act_info Self-status (do_score family)

**Files:**
- Modify: `src/tests/act_info_format_tests.cpp` (add suite `ActInfoSelfStatus`)
- Modify: `src/act_info.cpp` — `do_score` (:1856), `do_info` (:1626), `do_affections` (:3469), `do_toggle` (:2769), `do_inventory` (:2478), `do_equipment` (:2484), `do_gen_ps` (:2490), `do_diagnose` (:2975), `do_orc_delay` (:3393), `do_squareroot` helper (:1835). (`do_trap` is defined in `ranger.cpp:1166` — NOT this file, do not touch.)

**Interfaces:**
- Consumes: the fixtures Task 1 created in `act_info_format_tests.cpp` (`reset_capturing_descriptor`, `SoloCharacterContext`-style contexts).
- Produces: suite `ActInfoSelfStatus` in the same file.

- [ ] **Step 1: Read the chunk** (`src/act_info.cpp:1626-1922`, `:2478-2523`, `:2769-3064`, `:3393-3468`) and inventory sprintf-family sites as in Task 1 Step 1.

- [ ] **Step 2: Write `ActInfoSelfStatus` characterization tests.** do_score is the priority: pin the first stats line, the OB/DB/PB line, both XP-needed branches (`%d` vs `%dK`), and each specialization/hunger/thirst tail line. Exemplar:

```cpp
// do_score: pins the two leading stat lines for a solo PC with known
// abilities. bufpt-accumulation converts to a single std::string in the
// transform; these bytes must not move.
TEST(ActInfoSelfStatus, DoScoreFormatsHitStaminaMovesSpiritAndCombatLine)
{
    SoloCharacterContext context;
    context.character.tmpabilities.hit = 50;   context.character.abilities.hit = 100;
    context.character.tmpabilities.mana = 40;  context.character.abilities.mana = 80;
    context.character.tmpabilities.move = 30;  context.character.abilities.move = 60;
    do_score(&context.character, const_cast<char*>(""), nullptr, 0, 0);
    EXPECT_TRUE(strstr(context.descriptor.small_outbuf,
        "You have 50/100 hit, 40/80 stamina, 30/60 moves") != nullptr)
        << context.descriptor.small_outbuf;
}
```

(Use `EXPECT_STREQ` full-buffer pins wherever the fixture fully determines the output; the `strstr` form only where trailing lines depend on derived stats the fixture doesn't pin — and say so in a comment.)

- [ ] **Step 3: Build; run `--gtest_filter='ActInfoSelfStatus.*'` against unchanged source.** Expected: PASS (same command shape as Task 1 Step 4).

- [ ] **Step 4: Commit tests:** `git add src/tests/act_info_format_tests.cpp && git commit -m "test: pin act_info self-status output bytes (Wave 3 I2)"`

- [ ] **Step 5: Transform.** do_score's `bufpt += sprintf(bufpt, …)` chain → one `std::string out` (catalog item 3); the `weapon_master.append_score_message(bufpt)` interop keeps a `char stage[MAX_STRING_LENGTH]` staging buffer per catalog item 3. do_gen_ps switches on subcommand with literal sends — likely no conversion needed beyond any sprintf present; delete nothing without caller-grep proof.

- [ ] **Step 6: Rebuild; full macOS ctest + native boot golden green** (same commands as Task 1 Step 7).

- [ ] **Step 7: ASan** with `--gtest_filter='ActInfoSelfStatus.*'`. Expected: pass.

- [ ] **Step 8: Triple local gate** (Global Constraints block). Expected: green ×3.

- [ ] **Step 9: Format + commit:** `git commit -m "refactor: convert act_info self-status family to std::format (Wave 3 I2)"` (add `src/act_info.cpp` only).

---

### Task 3: Chunk I3 — act_info World/social info (who/time/fame) + act_info RAII sites

**Files:**
- Modify: `src/tests/act_info_format_tests.cpp` (add suite `ActInfoWorldSocial`)
- Modify: `src/act_info.cpp` — `do_who` (:2125), `do_users` (:2320), `do_where` (:2644), `do_levels` (:2654), `do_consider` (:2724), `do_time` (:1923), `do_weather` (:1977), `do_help` (:2018), `do_commands` (:2915), `do_whois` (:3065), `do_fame` (:3558) + `do_fame_leader_string` (:3537), `do_rank` (:3718)

**Interfaces:**
- Consumes: Task 1's fixtures; `nth(int)` (`src/utils.h:81`, returns malloc'd `char*`); `pkill_get_string` (returns malloc'd `char*`); `rots_asprintf` (`src/platform_compat.h:16`).
- Produces: suite `ActInfoWorldSocial`; all four act_info RAII conversions land here (this chunk owns every allocation site in the file).

- [ ] **Step 1: Read the chunk** (`:1923-2777`, `:2915-3205`, `:3537-3766`) and inventory. The frozen act_info RAII list (verified 2026-07-11): `do_time` `year = nth(…)` :1948 / `free(year)` :1952; `do_fame` `pkill_get_string` results freed at :3648/:3689/:3700 and `rots_asprintf(&string, "%s", player_table[idx].name)` :3706 / `free` :3713; `do_rank` `s = nth(r + 1)` :3736 / `free(s)` :3738.

- [ ] **Step 2: Write `ActInfoWorldSocial` tests.** Priorities: do_time's Steward's-Reckoning line (pins the `nth()` ordinal-suffix output before RAII conversion), do_rank's "You are ranked %s among %s" both-faction branches, do_consider's level-delta ladder, do_who's header/footer counts for a minimal descriptor_list (reuse `ScopedDescriptorList` conventions from `act_wiz_tests.cpp`), do_levels' table rows.

```cpp
// do_rank: pins the ranked-path header INCLUDING nth()'s ordinal suffix
// ("1st"/"2nd"/"3rd"/"4th"), which the RAII conversion must not disturb.
TEST(ActInfoWorldSocial, DoRankFormatsOrdinalRankHeaderForGoodRace)
{
    RankedCharacterContext context; // fixture: PC with a pkill rank of 0 (displays "1st"), RACE_GOOD race
    do_rank(&context.character, const_cast<char*>(""), nullptr, 0, 0);
    EXPECT_TRUE(strstr(context.descriptor.small_outbuf,
        "You are ranked 1st among the free peoples of Middle-earth:\r\n") != nullptr)
        << context.descriptor.small_outbuf;
}
```

(If a pkill-rank fixture proves impractical without world data, pin `nth()` directly — `std::string first(nth(1))` etc. — in a small `NthOrdinal` test block instead, and note do_rank is then covered by the transform-diff review + gate. Do not force a fixture the harness can't support.)

- [ ] **Step 3: Build; run `--gtest_filter='ActInfoWorldSocial.*'` against unchanged source.** Expected: PASS.

- [ ] **Step 4: Commit tests:** `git commit -m "test: pin act_info world/social output bytes (Wave 3 I3)"`

- [ ] **Step 5: Transform formats AND the four RAII clusters.** RAII exemplar (catalog item 10), do_time:

```cpp
// BEFORE (:1948-1952)
year = nth(time_info.year);
bufpt += sprintf(bufpt,
    "By the Steward's Reckoning, it is "
    "the %s year of the fourth age of Arda.\r\n",
    year);
free(year);

// AFTER — capture-and-free immediately; `year` local becomes std::string;
// the char* local and its free() disappear.
std::string year = [] (char* raw) { std::string s(raw); free(raw); return s; }(nth(time_info.year));
out += std::format(
    "By the Steward's Reckoning, it is "
    "the {} year of the fourth age of Arda.\r\n",
    year);
```

If that lambda-capture reads awkwardly at a site, an equivalent named file-local helper `static std::string take_cstring(char* raw)` (same body) is fine — define it once in act_info.cpp, reuse at all four clusters, and note it in the commit. do_fame's loop frees inside a loop — same helper applies per iteration. do_who/do_users accumulation → `std::string out` per catalog item 3 (mind `page_string(ch->desc, buf, 1)` consumers: `page_string` may need the staging `buf` — check its signature; if it takes `char*`, `strcpy(buf, out.c_str())` per catalog item 2).

- [ ] **Step 6: Rebuild; full macOS ctest + native boot golden.** Expected: green; `boot log matches golden`.

- [ ] **Step 7: ASan** with `--gtest_filter='ActInfoWorldSocial.*'`. Expected: pass — this is the wave's most allocation-sensitive chunk; a leak/UAF here is a real finding, not noise.

- [ ] **Step 8: Triple local gate.** Expected: green ×3.

- [ ] **Step 9: Format + commit:** `git commit -m "refactor: convert act_info world/social family to std::format + RAII (Wave 3 I3)"`

---

### Task 4: Chunk I4 — act_info Object identification (identify/stat/exploits)

**Files:**
- Modify: `src/tests/act_info_format_tests.cpp` (add suite `ActInfoObjectId`)
- Modify: `src/act_info.cpp` — `do_compare` (:3767), `do_stat` (:3918), `do_exploits` (:4120), `do_food_display` (:4494), `do_light_display` (:4511), `do_flag_values_display` (:4522), `do_weapon_display` (:4546), `do_identify_object` (:4584), `do_details` (:4654)

**Interfaces:**
- Consumes: Task 1's fixtures; `ObjFlagDataBuilder` (`src/tests/ObjFlagDataBuilder.cpp`) for object-shaped inputs.
- Produces: suite `ActInfoObjectId`; after this task `src/act_info.cpp` must be sprintf-family-free (verified in Task 9).

- [ ] **Step 1: Read the chunk** (`:3767-4723`) and inventory. `do_weapon_display`/`do_identify_object` feed off `get_weapon_damage(obj_data*)` — the live overload (Wave 2 Task 5 resolved the twin question; check its findings in `docs/superpowers/plans/2026-07-10-phase-4-wave-2.md` before touching those calls).

- [ ] **Step 2: Write `ActInfoObjectId` tests.** Priorities: `do_weapon_display`'s damage/speed lines for a builder-constructed weapon, `do_food_display`/`do_light_display` numeric lines, `do_compare`'s better/worse/equal verdict lines, `do_exploits`' header + one row.

```cpp
// do_food_display: pins the hours-of-fullness line for a builder-made food
// object (value[0] = 24 -> "24 hours").
TEST(ActInfoObjectId, DoFoodDisplayFormatsFullnessHours)
{
    SoloCharacterContext context;
    obj_data food {};
    food.obj_flags = ObjFlagDataBuilder().withValue(0, 24).build(); // adapt to the builder's real API
    do_food_display(&context.character, &food);
    EXPECT_TRUE(strstr(context.descriptor.small_outbuf, "24") != nullptr)
        << context.descriptor.small_outbuf;
}
```

(Before writing, read `ObjFlagDataBuilder`'s actual method names and replace the `withValue` sketch with the real API — the builder exists precisely so tests don't hand-fill `obj_flag_data`.) Pin full lines with `EXPECT_STREQ` once the real fixture output is known.

- [ ] **Step 3: Build; run `--gtest_filter='ActInfoObjectId.*'` against unchanged source.** Expected: PASS.

- [ ] **Step 4: Commit tests:** `git commit -m "test: pin act_info object-identification output bytes (Wave 3 I4)"`

- [ ] **Step 5: Transform.** All catalog items; the display helpers are classic one-shot `sprintf(buf,…); send_to_char(buf, ch);` → catalog item 1. Ternary null-guards (`j->short_description ? … : "<None>"`) stay ternaries (catalog item 4).

- [ ] **Step 6: Rebuild; full macOS ctest + native boot golden.** Expected: green.

- [ ] **Step 7: ASan** with `--gtest_filter='ActInfoObjectId.*'`. Expected: pass.

- [ ] **Step 8: Triple local gate.** Expected: green ×3.

- [ ] **Step 9: Format + commit:** `git commit -m "refactor: convert act_info object-identification family to std::format (Wave 3 I4)"`

---

### Task 5: Chunk W1 — act_wiz Inspection (stat/vstat/show)

**Files:**
- Create: `src/tests/act_wiz_format_tests.cpp`
- Modify: `src/CMakeLists.txt:169-170` (add `tests/act_wiz_format_tests.cpp`, alphabetical — before `tests/act_wiz_tests.cpp`)
- Modify: `src/tests/Makefile:276` (add `act_wiz_format_tests.cpp` to `SRCS`)
- Modify: `src/act_wiz.cpp` — `do_stat_room` (:420), `do_stat_object` (:548), `do_stat_character` (:736), `do_vnum` (:397), `do_vstat` (:1364), `do_zone` (:1022), `do_wizstat` (:1071), `do_findzone` (:3788), `do_top` (:3942), `do_last` (:1859), `do_date` (:1811), `do_uptime` (:1831), `do_show` (:2357)

**Interfaces:**
- Consumes: same fixture conventions as Task 1 (fresh file-local `reset_capturing_descriptor` copy WITH the warning comment; `ScopedTestWorld`; builders). `ScopedDescriptorList` convention lives in `act_wiz_tests.cpp` — mirror it, don't include across test files.
- Produces: `src/tests/act_wiz_format_tests.cpp` with suite `ActWizInspection`; Tasks 6-8 extend this file (`ActWizWorldManip`, `ActWizPlayerAdmin`, `ActWizComm`).

- [ ] **Step 1: Read the chunk** (`:397-1159 for the stat helpers/zone/wizstat`, `:1364-1407`, `:1811-1890`, `:2357-2692`, `:3788-3819`, `:3942-end`) and inventory. This is the densest formatting chunk in the wave (the three `do_stat_*` helpers alone are ~600 lines of near-continuous sprintf).

- [ ] **Step 2: Write `ActWizInspection` tests.** Priorities: `do_stat_object`'s Name/VNum/L-Des lines including the `<None>` ternaries and CC_FIX color codes for a color-enabled and a color-disabled viewer; `do_stat_character`'s header block for a builder-made PC; `do_stat_room` title/flags lines (needs `ScopedTestWorld`); `do_date`/`do_uptime` (pin the format shape, not wall-clock values — construct expected string from the same time value the fixture injects, or pin only the literal prefix). Exemplar:

```cpp
// do_stat_object: pins the L-Des line's "None" fallback (nullable
// j->description ternary) for a colorless viewer.
TEST(ActWizInspection, DoStatObjectFormatsLDesNoneFallback)
{
    SoloCharacterContext context;
    obj_data object {};
    object.name = const_cast<char*>("widget");
    object.item_number = -1; // no obj_index entry: virt 0, SpecProc "None" branch
    do_stat_object(&context.character, &object);
    EXPECT_TRUE(strstr(context.descriptor.small_outbuf, "L-Des: None\n\r") != nullptr)
        << context.descriptor.small_outbuf;
}
```

- [ ] **Step 3: Register the file** in `src/CMakeLists.txt` and `src/tests/Makefile:276`.

- [ ] **Step 4: Build; run `--gtest_filter='ActWizInspection.*'` against unchanged source.** Expected: PASS.

- [ ] **Step 5: Commit tests:** `git commit -m "test: pin act_wiz inspection-family output bytes (Wave 3 W1)"`

- [ ] **Step 6: Transform.** The `do_stat_*` helpers are line-at-a-time `sprintf(buf,…); send_to_char(buf,ch);` (catalog item 1) mixed with `sprintbit`-into-buf (item 7) and strcat-loops over `ex_description` chains (item 3 — `std::string` accumulation). Given ~600 lines, transform ONE helper at a time and rebuild+run the suite between helpers (sub-passes per the spec's giant-function rule).

- [ ] **Step 7: Rebuild; full macOS ctest + native boot golden.** Expected: green.

- [ ] **Step 8: ASan** with `--gtest_filter='ActWizInspection.*'`. Expected: pass.

- [ ] **Step 9: Triple local gate.** Expected: green ×3.

- [ ] **Step 10: Format + commit:** `git commit -m "refactor: convert act_wiz inspection family to std::format (Wave 3 W1)"`

---

### Task 6: Chunk W2 — act_wiz World manipulation (goto/load/purge)

**Files:**
- Modify: `src/tests/act_wiz_format_tests.cpp` (add suite `ActWizWorldManip`)
- Modify: `src/act_wiz.cpp` — `do_at` (:256), `do_goto` (:288), `do_trans` (:319), `do_teleport` (:367), `do_load` (:1312), `do_purge` (:1408), `do_zreset` (:2090), `do_switch` (:1253), `do_return` (:1293), `do_snoop` (:1200), `do_force` (:1891)

**Interfaces:**
- Consumes: Task 5's fixtures in `act_wiz_format_tests.cpp`.
- Produces: suite `ActWizWorldManip`.

- [ ] **Step 1: Read the chunk and inventory.** Most of these commands' formatting is small (error strings, act() templates); the transform surface is thinner than W1. IMPORTANT SCOPE NOTE: `do_load`'s object/mob creation hands ownership into the world graph — the RAII catalog does NOT apply to those pointers (spec: recorded justification, not conversion). Only formatting converts here.

- [ ] **Step 2: Write `ActWizWorldManip` tests.** Priorities: each command's usage/error lines (fixture-reachable without world mutation: `do_goto` with a bad room number, `do_load` with bad syntax, `do_force` usage line), plus `do_at`'s "room not found" branches. Side-effecting success paths (actual teleport/load/purge) are NOT unit-tested — they need live world state; the boot golden + gate cover compile-level regressions, and the formatting conversions on those paths are pinned by reviewing the diff line-by-line against catalog item 1/2 equivalence. State this exclusion in the suite's header comment.

```cpp
// do_goto: pins the invalid-target error line (no world mutation).
TEST(ActWizWorldManip, DoGotoRejectsUnknownRoomWithErrorLine)
{
    SoloCharacterContext context;
    char argument[] = " 999999";
    do_goto(&context.character, argument, nullptr, 0, 0);
    EXPECT_TRUE(strstr(context.descriptor.small_outbuf, "No room exists with that number.") != nullptr)
        << context.descriptor.small_outbuf; // adapt expected text to the actual source string found in Step 1
}
```

- [ ] **Step 3: Build; run `--gtest_filter='ActWizWorldManip.*'` against unchanged source.** Expected: PASS.

- [ ] **Step 4: Commit tests:** `git commit -m "test: pin act_wiz world-manipulation error/usage bytes (Wave 3 W2)"`

- [ ] **Step 5: Transform** (catalog items 1-6; no RAII conversions in this chunk per Step 1's scope note — record the do_load/do_purge justification in the commit body).

- [ ] **Step 6: Rebuild; full macOS ctest + native boot golden.** Expected: green.

- [ ] **Step 7: ASan** with `--gtest_filter='ActWizWorldManip.*'`. Expected: pass.

- [ ] **Step 8: Triple local gate.** Expected: green ×3.

- [ ] **Step 9: Format + commit:** `git commit -m "refactor: convert act_wiz world-manipulation family to std::format (Wave 3 W2)"`

---

### Task 7: Chunk W3 — act_wiz Player administration (wizset/account) + wizlock RAII

**Files:**
- Modify: `src/tests/act_wiz_format_tests.cpp` (add suite `ActWizPlayerAdmin`)
- Modify: `src/act_wiz.cpp` — `do_advance` (:1511), `do_restore` (:1608), `do_wizset` (:2693), `do_wizutil` (:2153), `do_setfree` (:3820), `do_delete` (:3150), `do_register` (:3510), `do_account` (:3183), `do_whoacct` (:3452), `do_invis` (:1639), `do_wizlock` (:1767-1810 incl. the `wizlock_msg` global at :1765), `do_dc` (:1730), `do_shutdown` (:1160), `do_rehash` (:3851)
- Modify: `src/interpre.cpp:70` (extern decl) and `:2751`, `:3012` (`SEND_TO_Q(wizlock_msg, d)` consumers) — ONLY for the wizlock_msg type change

**Interfaces:**
- Consumes: Task 5's fixtures; the EXISTING `act_wiz_tests.cpp` account suite (13 tests) — the characterization base for do_account/do_whoacct. EXTEND only; never weaken or delete an existing account test.
- Produces: suite `ActWizPlayerAdmin`; `wizlock_msg` becomes `std::string wizlock_msg;` (act_wiz.cpp) with `extern std::string wizlock_msg;` (interpre.cpp) and `.c_str()` at the two SEND_TO_Q sites.

- [ ] **Step 1: Read the chunk and inventory.** Special attention: (a) `do_wizset` (:2693-3149) is the file's single biggest function — sub-pass it like W1's helpers; (b) the `wizlock_msg` cluster: `char* wizlock_msg = 0` (:1765), malloc'd at :1786/:1789 **without freeing any previous value — a pre-existing leak on every re-set**; consumed in `interpre.cpp:2751/:3012` via `SEND_TO_Q`. Trace the guards on both consumer sites: confirm under what `restrict` values they run and whether a never-set (`nullptr`) `wizlock_msg` is reachable there today (if it is, that's a latent crash the conversion also fixes — note it in the commit; if not, the conversion is pure).

- [ ] **Step 2: Write `ActWizPlayerAdmin` tests.** Priorities: `do_wizlock`'s status lines for restrict 0/1/default (`"The game is %s completely open.\n"` family, both `now`/`currently` branches) AND the "Message set to: %s" echo; `do_advance` usage/error lines; `do_invis` on/off lines; `do_restore` target-not-found line. For do_account/do_whoacct: run the existing 13 tests, then add pins ONLY for format sites those tests don't already cover (diff the sprintf inventory against the existing assertions).

```cpp
// do_wizlock: pins the level-gate status line, "now" branch (argument given).
TEST(ActWizPlayerAdmin, DoWizlockFormatsLevelGateStatusLineNow)
{
    ImplementorContext context; // fixture: PC at LEVEL_IMPL so do_wizlock's permission gate passes
    char argument[] = " 20";
    do_wizlock(&context.character, argument, nullptr, 0, 0);
    EXPECT_TRUE(strstr(context.descriptor.small_outbuf,
        "Only level 20 and above may enter the game now.\n") != nullptr)
        << context.descriptor.small_outbuf;
}
```

- [ ] **Step 3: Build; run `--gtest_filter='ActWizPlayerAdmin.*'` AND `--gtest_filter='ActWiz.*'` (existing account suite) against unchanged source.** Expected: both PASS.

- [ ] **Step 4: Commit tests:** `git commit -m "test: pin act_wiz player-admin output bytes (Wave 3 W3)"`

- [ ] **Step 5: Transform formats + the wizlock_msg RAII conversion.**

```cpp
// BEFORE (act_wiz.cpp:1765, :1786-1790)
char* wizlock_msg = 0; /* wizlock message              */
…
if (*buf2) {
    wizlock_msg = (char*)malloc(strlen(buf2) + 3);
    sprintf(wizlock_msg, "%s\n\r", buf2);
} else {
    wizlock_msg = (char*)malloc(strlen(wizlock_default) + 1);
    sprintf(wizlock_msg, "%s", wizlock_default);
}

// AFTER (act_wiz.cpp) — also fixes the pre-existing re-set leak
std::string wizlock_msg; /* wizlock message shown to blocked connections */
…
if (*buf2) {
    wizlock_msg = std::format("{}\n\r", buf2);
} else {
    wizlock_msg = wizlock_default;
}

// interpre.cpp:70
extern std::string wizlock_msg;
// interpre.cpp:2751 and :3012
SEND_TO_Q(wizlock_msg.c_str(), d);
```

If Step 1 found the consumers reachable with a never-set message, preserve the old observable behavior explicitly (guard `if (!wizlock_msg.empty())` only if the OLD code also guarded; otherwise document what the old NULL deref would have done and keep bytes identical for the set path — behavior parity, not improvement, is the contract; the leak fix is memory-only and invisible to output).

- [ ] **Step 6: Rebuild; full macOS ctest (incl. the untouched 13 account tests) + native boot golden.** Expected: green.

- [ ] **Step 7: ASan** with `--gtest_filter='ActWizPlayerAdmin.*:ActWiz.*'`. Expected: pass.

- [ ] **Step 8: Triple local gate.** Expected: green ×3.

- [ ] **Step 9: Format + commit:** `git commit -m "refactor: convert act_wiz player-admin family to std::format; wizlock_msg to std::string (Wave 3 W3)"` (add `src/act_wiz.cpp src/interpre.cpp`).

---

### Task 8: Chunk W4 — act_wiz Wiz communication (echo/wiznet/poofset)

**Files:**
- Modify: `src/tests/act_wiz_format_tests.cpp` (add suite `ActWizComm`)
- Modify: `src/act_wiz.cpp` — `do_emote` (:121), `do_send` (:146), `do_echo` (:172), `do_gecho` (:1673), `do_poofset` (:1700), `do_wiznet` (:1965)

**Interfaces:**
- Consumes: Task 5's fixtures; `RoomPairContext`-style two-character room fixture (mirror `act_format_tests.cpp:102-141`) for echo/emote observer output.
- Produces: suite `ActWizComm`. After this task `src/act_wiz.cpp` must be sprintf-family-free (verified in Task 9).

- [ ] **Step 1: Read the chunk and inventory.** SCOPE NOTE: `do_poofset`'s `*msg = str_dup(argument + i)` (:1725) stores into `char_data` specials — ownership leaves the function; RAII conversion OUT of scope, record the justification in the transform commit body. Only its formatting (if any) converts.

- [ ] **Step 2: Write `ActWizComm` tests.** Priorities: `do_echo`'s room delivery + PRF_ECHO self-echo vs "Ok." branches (two-character room fixture, assert BOTH descriptors' buffers); `do_gecho`'s descriptor-list delivery (mirror `ScopedDescriptorList`); `do_send` "sent" confirmation; `do_wiznet` line prefix.

```cpp
// do_echo: pins the observer's received line and the actor's PRF_ECHO=off
// "Ok." acknowledgment in one shot.
TEST(ActWizComm, DoEchoDeliversLineToRoomAndAcksActor)
{
    RoomPairContext context;
    char argument[] = "  The walls tremble.";
    do_echo(&context.actor, argument, nullptr, 0, 0);
    EXPECT_STREQ(context.victim_descriptor.small_outbuf, "The walls tremble.\n\r");
    EXPECT_STREQ(context.actor_descriptor.small_outbuf, "Ok.\n\r");
}
```

- [ ] **Step 3: Build; run `--gtest_filter='ActWizComm.*'` against unchanged source.** Expected: PASS.

- [ ] **Step 4: Commit tests:** `git commit -m "test: pin act_wiz communication output bytes (Wave 3 W4)"`

- [ ] **Step 5: Transform** (catalog; `sprintf(buf, "%s\n\r", argument + i)` in do_echo/do_gecho is catalog item 2 — `buf` is re-sent to multiple receivers, keep it staged: `strcpy(buf, std::format("{}\n\r", argument + i).c_str());`).

- [ ] **Step 6: Rebuild; full macOS ctest + native boot golden.** Expected: green.

- [ ] **Step 7: ASan** with `--gtest_filter='ActWizComm.*'`. Expected: pass.

- [ ] **Step 8: Triple local gate.** Expected: green ×3.

- [ ] **Step 9: Format + commit:** `git commit -m "refactor: convert act_wiz communication family to std::format (Wave 3 W4)"`

---

### Task 9: Cross-file sweep + RAII closure

**Files:**
- Modify: `src/act_info.cpp`, `src/act_wiz.cpp` (any straggler sites only)

**Interfaces:**
- Consumes: everything Tasks 1-8 landed.
- Produces: both files grep-clean of the sprintf family (or carrying written per-site justifications); the wave's RAII ledger.

- [ ] **Step 1: Grep-audit both files.**

```bash
grep -nE '\b(sprintf|vsprintf|strcpy|strcat)\(' src/act_info.cpp src/act_wiz.cpp
grep -nE '\bmalloc\(|\bfree\(' src/act_info.cpp src/act_wiz.cpp
grep -nE 'char\s+\w+\[(MAX_STRING_LENGTH|MAX_INPUT_LENGTH)\]' src/act_info.cpp src/act_wiz.cpp
```

Expected: first grep returns ONLY intentional-skip sites (each must have a comment or a commit-recorded justification — e.g. a `strcpy` into a parser-owned buffer); second returns nothing except sites justified in Tasks 3/6/7/8 commit bodies; third returns only parser-API buffers (catalog item 8 skips).

- [ ] **Step 2: Convert or justify every unexplained hit.** Small fixes land here directly (same idiom catalog); anything non-trivial goes back through its owning chunk's test suite first (add a pin, then convert).

- [ ] **Step 3: Rebuild; FULL macOS ctest + native boot golden.** Expected: green.

- [ ] **Step 4: Triple local gate.** Expected: green ×3.

- [ ] **Step 5: Commit:** `git commit -m "refactor: Wave 3 sweep — final sprintf-family/RAII closure in act_info/act_wiz"` with the justification ledger (skipped sites + reasons) in the commit body.

---

### Task 10: Exit — docs, finalization battery, CI

**Files:**
- Modify: `CLAUDE.md`, `AGENTS.md` (test-count baselines — currently "~703"/"758"; update to the post-wave actuals)
- Modify: `docs/BUILD.md` (only if a new formatting/portability lesson emerged this wave)
- Modify: `docs/superpowers/plans/2026-07-11-phase-4-wave-3-giants.md` (this file: append the Wave 3 Exit section)

- [ ] **Step 1: Update docs.** Grep for stale test counts (`grep -rn '758\|~703' CLAUDE.md AGENTS.md`) and update to the finalization actuals per platform. Add any new BUILD.md lesson (only real ones).

- [ ] **Step 2: Full finalization battery.** Triple local gate one more time from a clean tree (all three legs green, three `boot log matches golden`).

- [ ] **Step 3: Push and watch remote CI.**

```bash
git push -u origin modernization/phase-4-wave-3
gh run watch   # all four required jobs: linux-i386-legacy, linux-x64, macos-arm64, windows-msvc
```

Expected: all four green. An MSVC-only failure in the wave's new test files is the accepted deferred class — fix it here (the ASan + platform-shim rules should have prevented the two known classes), re-push, re-watch. Record the final run URL.

- [ ] **Step 4: Write the Wave 3 Exit section** in this plan doc: date, final commit, per-platform battery actuals, CI URL, zero-golden confirmation (explicit: no golden moved all wave), the RAII ledger summary, deferred list (fight/db/comm, RAII lifecycle-audit wave, Wave-2 leftovers, file-splitting).

- [ ] **Step 5: Commit:** `git commit -m "docs: record Phase 4 Wave 3 exit"`. Then final whole-branch review (`review-package master..HEAD`) and the merge-to-master decision to the owner. Do not merge without the owner's explicit approval.

---

## Self-review notes (write-time)

- Spec coverage: 8 chunks ↔ Tasks 1-8 (same command rosters, same order); sweep ↔ Task 9; exit ↔ Task 10. RAII frozen list all placed (act_info clusters → Task 3; wizlock_msg → Task 7; poofset/do_load out-of-scope justifications → Tasks 8/6). do_trap exclusion carried from the spec (ranger.cpp:1166).
- Characterization-passes-first is stated in Global Constraints AND as an explicit "must PASS against unchanged source" step in every chunk — the one deliberate inversion of the fail-first step template, inherent to characterization work.
- Exemplar code uses only verified anchors/signatures: `send_to_char(const char*, char_data*)` comm.h:33, `nz` utils.h:53, `nth` utils.h:81/utility.cpp:1806, `rots_asprintf` platform_compat.h:16, wizlock consumers interpre.cpp:70/2751/3012, fixture source act_format_tests.cpp:69-141. Fixture structs named in tests but not yet existing (`RoomWithExitContext`, `RankedCharacterContext`, `ImplementorContext`, `RoomPairContext` mirror) are defined by the same step that uses them, modeled on the cited act_format_tests.cpp patterns; two sketched fixture APIs are flagged in-place for adaptation (`ObjFlagDataBuilder` real method names, do_goto's actual error text).
- Type consistency: suite names (`ActInfoPerception`/`ActInfoSelfStatus`/`ActInfoWorldSocial`/`ActInfoObjectId`/`ActWizInspection`/`ActWizWorldManip`/`ActWizPlayerAdmin`/`ActWizComm`) match between their defining tasks, the ASan filters, and the spec.
