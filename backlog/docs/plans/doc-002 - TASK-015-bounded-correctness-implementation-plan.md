---
id: doc-002
title: TASK-015 bounded correctness implementation plan
type: specification
created_date: '2026-09-07 13:03'
updated_date: '2026-09-07 13:24'
---
# TASK-015 Bounded Correctness Implementation Plan

> Execute inline under the approved current-branch scope, using the test-driven-development and verification-before-completion workflows. No commits or branch integration are authorized. Task checkboxes below describe the execution sequence; TASK-015 owns recorded state.

**Goal:** Port the four independent correctness fixes in design slice B with discriminating tests.

**Architecture:** Each change remains in its existing owner: combat timer, script traversal, app admin command and persist text loader. No new production interface or library dependency is needed.

**Tech Stack:** C++20, GoogleTest, native macOS arm64 and rots64 CMake presets.

**Spec:** [Approved migration design, including local uaf-port supplement](../specs/doc-001%20-%20TASK-015-release-frodo-triage-and-architecture-design.md).

## Purpose and dependencies

This executable plan covers B only. The parent design maps N/M/V/D/A/P/R/C/O and the added U1-U4 slices; each receives a focused implementation plan before code moves. The owner approved proceeding on 2026-09-07. TASK-015 remains the one migration task; this is not a claim to complete the entire port.

## Global constraints

- Keep the current branch and preserve unrelated files. No commits, pushes, merges, production access or runtime-data edits.
- C++20; current project formatting and warnings-as-errors; current Placement, first-null text and output-seam contracts.
- Retain nine libraries and current build lists. These tests extend existing compiled files, so no build-list edit is needed.
- Native and rots64 build/full CTest/boot verification for the batch; macOS ASan+UBSan for modified test files. All three censuses must pass. i386 and remote CI remain finalization gates.
- Preserve all human-signed notes. No golden regeneration is expected.

## Class responsibilities

- game_timer::skill_timer owns skill cooldown expiry; only its iteration changes.
- Existing SkillTimerCharacter/SkillTimerTest, RoomPairContext and ScopedPlayerTableEntry own their test fixtures; reuse their current lifetimes without adding fields.
- No class shapes change.

## B1: expire every timer once per tick

**Files:** src/combat/skill_timer.cpp; src/tests/skill_timer_tests.cpp.
**Interfaces:** existing add_skill_timer(const char_data&, int, int), update_skill_timer(), is_skill_allowed(const char_data&, int), report_skill_status(int, char*). No new API.

- [x] Add these two tests. The old loop leaves adjacent zero counters allocated and fails to decrement a surviving timer shifted into an erased global-cooldown slot.

```cpp
TEST_F(SkillTimerTest, ErasesAdjacentExpiredEntriesInTheSameTick)
{
    SkillTimerCharacter first_player(next_player_id());
    SkillTimerCharacter second_player(next_player_id());
    auto& timer = game_timer::skill_timer::instance();
    timer.add_skill_timer(first_player.character, SKILL_DEFEND, 2);
    timer.add_skill_timer(second_player.character, SKILL_DEFEND, 2);
    timer.update_skill_timer();
    timer.update_skill_timer();
    EXPECT_FALSE(timer.is_skill_allowed(first_player.character, SKILL_DEFEND));
    timer.update_skill_timer();
    EXPECT_TRUE(timer.is_skill_allowed(first_player.character, SKILL_DEFEND));
    EXPECT_TRUE(timer.is_skill_allowed(second_player.character, SKILL_DEFEND));
}

TEST_F(SkillTimerTest, DecrementsTheTimerShiftedPastAnExpiredGlobalCooldown)
{
    SkillTimerCharacter first_player(next_player_id());
    SkillTimerCharacter second_player(next_player_id());
    auto& timer = game_timer::skill_timer::instance();
    timer.add_skill_timer(first_player.character, SKILL_DEFEND, 5);
    timer.add_skill_timer(second_player.character, SKILL_DEFEND, 5);
    timer.update_skill_timer();
    timer.update_skill_timer();
    timer.update_skill_timer();
    char report[256] = {};
    const int player_id = static_cast<int>(second_player.character.specials2.idnum);
    timer.report_skill_status(player_id, report);
    const std::string expected = std::format("{:<30} {:<3} (seconds)\n\r", utils::get_skill_name(SKILL_DEFEND), 2);
    EXPECT_STREQ(report, expected.c_str());
}
```

- [x] Run the focused tests against the unchanged implementation; require failures on both named defects.
- [x] Update the older characterization test: a counter reaching zero still survives until the next tick, but the implicit global cooldown now expires on call 3 instead of call 4. Remove its historical skipped-entry expectation and update the fixture-drain rationale.
- [x] Replace the loop with the same semantics as upstream 1b2a06bb, using named locals:

```cpp
for (size_t timer_index = 0; timer_index < m_skill_timer.size();) {
    auto& timer_data = m_skill_timer[timer_index];
    if (timer_data.counter > 0) {
        --timer_data.counter;
        ++timer_index;
    } else {
        m_skill_timer.erase(m_skill_timer.begin() + timer_index);
    }
}
```

- [x] Run every SkillTimerTest, not only the two new cases.

## B2: nested script traversal

**Files:** src/script/script.cpp; src/tests/script_tests.cpp.
**Interfaces:** existing script_data* get_next_command(script_data*); add its declaration to the test file beside other local test declarations.

- [x] Add these node-chain cases. Adjacent END and END_ELSE_BEGIN delimiters must not be skipped by the recursive return plus an unconditional increment. The malformed chain is a positive control.

```cpp
TEST(GetNextCommand, ReturnsAfterOuterEndWhenNestedEndIsAdjacent)
{
    script_data outer_begin {};
    script_data inner_begin {};
    script_data inner_end {};
    script_data outer_end {};
    script_data following_command {};
    outer_begin.command_type = SCRIPT_BEGIN;
    inner_begin.command_type = SCRIPT_BEGIN;
    inner_end.command_type = SCRIPT_END;
    outer_end.command_type = SCRIPT_END;
    following_command.command_type = SCRIPT_ABORT;
    outer_begin.next = &inner_begin;
    inner_begin.next = &inner_end;
    inner_end.next = &outer_end;
    outer_end.next = &following_command;
    EXPECT_EQ(get_next_command(&outer_begin), &following_command);
}

TEST(GetNextCommand, ReturnsAfterElseBoundaryImmediatelyFollowingNestedBlock)
{
    script_data outer_begin {};
    script_data inner_begin {};
    script_data inner_end {};
    script_data else_boundary {};
    script_data else_command {};
    outer_begin.command_type = SCRIPT_BEGIN;
    inner_begin.command_type = SCRIPT_BEGIN;
    inner_end.command_type = SCRIPT_END;
    else_boundary.command_type = SCRIPT_END_ELSE_BEGIN;
    else_command.command_type = SCRIPT_ABORT;
    outer_begin.next = &inner_begin;
    inner_begin.next = &inner_end;
    inner_end.next = &else_boundary;
    else_boundary.next = &else_command;
    EXPECT_EQ(get_next_command(&outer_begin), &else_command);
}

TEST(GetNextCommand, UnterminatedBlockReturnsNull)
{
    script_data begin {};
    script_data command {};
    begin.command_type = SCRIPT_BEGIN;
    command.command_type = SCRIPT_ABORT;
    begin.next = &command;
    EXPECT_EQ(get_next_command(&begin), nullptr);
}
```

- [x] Run GetNextCommand.* before the code change; the first two must fail and the unterminated case must pass.
- [x] Keep initial next and final return behavior, but replace the for loop with upstream 84462052's conditional advancement. Rename the edited parameter/local to current_command and document the local traversal contract:

```cpp
current_command = current_command->next;
while (current_command && current_command->command_type != SCRIPT_END
    && current_command->command_type != SCRIPT_END_ELSE_BEGIN) {
    if (current_command->command_type == SCRIPT_BEGIN) {
        current_command = get_next_command(current_command);
    } else {
        current_command = current_command->next;
    }
}
if (current_command) {
    return current_command->next;
}
return nullptr;
```

- [x] Run GetNextCommand.*, GetRoomParam.* and TriggerRoomEvent.* to cover existing script behavior.

## B3: case-insensitive wizset field lookup

**Files:** src/app/act_wiz.cpp; src/tests/act_wiz_format_tests.cpp. This is a refined test home: the existing RoomPairContext in the format test file provides a real command fixture; act_wiz_tests.cpp is largely account administration.
**Interfaces:** existing ACMD(do_wizset) and strn_cmp(std::string_view, std::string_view, int). No new interface.

- [x] Insert the test after the RoomPairContext definition/namespace close. NPC target avoids the unrelated player-save path. The real command must mutate OB for lower, upper and mixed case.

```cpp
TEST(ActWizPlayerAdmin, DoWizsetMatchesObWithoutCaseSensitivity)
{
    RoomPairContext context;
    context.actor.player.level = LEVEL_IMPL;
    SET_BIT(context.victim.specials2.act, MOB_ISNPC);
    context.victim.player.short_descr = const_cast<char*>("Legolas");
    char lower_case[] = "Legolas ob 73";
    do_wizset(&context.actor, lower_case, nullptr, 0, 0);
    EXPECT_EQ(SET_OB(&context.victim), 73);
    char upper_case[] = "Legolas OB 81";
    do_wizset(&context.actor, upper_case, nullptr, 0, 0);
    EXPECT_EQ(SET_OB(&context.victim), 81);
    char mixed_case[] = "Legolas oB 92";
    do_wizset(&context.actor, mixed_case, nullptr, 0, 0);
    EXPECT_EQ(SET_OB(&context.victim), 92);
}
```

- [x] Run ActWizPlayerAdmin.DoWizsetMatchesObWithoutCaseSensitivity before implementation; require failure for the lowercase lookup.
- [x] Change only do_wizset's field search from strncmp to strn_cmp (upstream 8bd82c94); retain abbreviation matching, table ordering and permission checks. Add braces to the edited loop. Do not change the unrelated do_show field lookup.

```cpp
for (l = 0; *(fields[l].cmd) != '\n'; l++) {
    if (!strn_cmp(field, fields[l].cmd, strlen(field))) {
        break;
    }
}
```

The existing index l belongs to the enclosing function; apply the global meaningful-name rule to a new field_index local and assign l after the search, avoiding a mass rename of untouched switch uses.

- [x] Run ActWizPlayerAdmin.* plus the existing LoadRoomRider.Wizset* cases to pin lookup/permission/file behavior.

## B4: bounded long descriptions

**Files:** src/persist/db_players.cpp; src/tests/db_loader_tests.cpp.
**Interfaces:** existing load_char_from_text(char*, std::string_view, char_file_u*).

- [x] Add the long terminated description with a following scalar field:

```cpp
TEST(DbLoader, TruncatesLongDescriptionAndContinuesAtTheNextField)
{
    ScopedPlayerTableEntry player_table_entry;
    char player_name[] = "aragorn";
    char_file_u stored_character {};
    std::string player_text = "#player\nname        aragorn\ndescription \n";
    player_text.append(600, 'x');
    player_text += "~\nlevel       27\nend\n";
    EXPECT_GE(load_char_from_text(player_name, player_text, &stored_character), 0);
    EXPECT_EQ(std::string(stored_character.description), std::string(511, 'x'));
    EXPECT_EQ(stored_character.level, 27);
}
```

- [x] Require the new test to fail before the change; retain DbLoader.RejectsMalformedPlayerTextWithoutLongStringTerminator as the malformed-input discriminator.
- [x] In KEY_LONG_STR, terminate element immediately after its bounded copy, consume remaining input until '~' or input_end, fail if input_end is reached, then consume the terminator/newlines. Upstream 26c0df11 supplies the algorithm; retain modern std::format diagnostics.

```cpp
element[tmp1] = '\0';
while (position < input_end && *position != '~') {
    ++position;
}
if (position >= input_end) {
    log(std::format("load_player_from_text: malformed long string for {}", name));
    return -1;
}
```

- [x] Run DbLoader.*. Verify 511 stored bytes, a terminator, and the following level 27; do not relax missing-terminator validation.

## Build and verification commands

Before code, configure/build the native baseline and run full CTest. For red/green cycles, build after test edits and filter the existing binary via CTest:

```sh
cd src
cmake --build --preset macos-arm64 -j4
ctest --preset macos-arm64 -R 'SkillTimerTest|GetNextCommand|ActWizPlayerAdmin.DoWizsetMatchesOb|DbLoader.TruncatesLongDescription' --output-on-failure
```

After the complete B batch, run these from their indicated directories; capture logs and actual counts. These are required tests, not prefilled results:

```sh
# src/
ctest --preset macos-arm64 --output-on-failure
cmake --preset macos-arm64-asan
cmake --build --preset macos-arm64-asan -j4
ctest --preset macos-arm64-asan --output-on-failure
# repository root
python3 tools/location_read_census.py --check
python3 tools/room_resolve_census.py --check
python3 tools/string_view_census.py --check
scripts/boot-golden.sh --native build/macos-arm64/ageland verify
docker compose run --rm --pull never rots64 bash -lc 'cd /rots/src && cmake --preset linux-x64 && cmake --build --preset linux-x64 -j"$(nproc)" && ctest --preset linux-x64'
scripts/boot-golden.sh --service rots64 verify
```

The loader is on the login path, so also run the local account smoke flow using the repository's documented native/proxy setup. Do not copy a binary over another writer's active binary. If a required tool or runtime fixture is unavailable, record the exact gap and keep the batch unverified; do not declare it done from focused tests alone.

## Review and manual/external work

Inline plan review checked source signatures, fixture ownership and known characterization expectations. Execution corrected two fixture details missed by that review: NPC short_descr for the wizard success message, and column-12 scalar values in legacy player text. The snippets above include those corrections. Independent read-only code review and reinspection are complete for batch B. The nested unterminated-block regression and explicit-return style findings are closed. No remaining actionable source findings were reported; acceptance still requires the recorded execution gates below. No production access or external service change is part of this batch. The parent migration still owes its later slices and finalization gates.

## Execution evidence, 2026-09-07

Batch B implementation remains uncommitted on fix/task-025-summon-dark-ok at c88186b5. Four source fixes stay in their existing owners, with ten new tests and one rewritten timer characterization. No interfaces, dependencies, build lists, resolver counts or golden files changed.

Regression sequence: two timer failures; two nested-boundary failures; lowercase/mixed-case OB failures with uppercase positive control; oversized-description rejection after the fixed-column fixture correction. The additional nested unterminated-block test reproduced the original SIGSEGV before the final traversal fix. Exact-capacity (511-byte) description and overlong missing-terminator controls pass before and after. The wizard success path initially crashed because its NPC fixture lacked short_descr; that fixture was repaired before the production lookup change and its intended failures were re-observed. No production change was made for the fixture crash.

Native baseline: 1,989 tests discovered; the four AcceptPathTest cases failed at bind/listen/connect with errno 1 inside the sandbox and passed when rerun with local-socket permission. Final native build and full CTest pass with 1,999 discovered / 1,923 passed / 76 skipped / zero failures. Native boot golden matches. All three census --check commands exit 0. The existing account_smoke.py flow passes against a private runtime-data copy and the new native binary; The smoke test used the copied data and a symlink to the native build; it did not replace bin/ageland or use the checkout runtime-data files.

Final Linux build/full CTest: 1,999 discovered / 1,921 passed / 78 skipped / zero failures (191.15 s). Final native ASan+UBSan build/full CTest: 1,999 discovered / 1,923 passed / 76 skipped / zero failures (221.85 s). Linux boot golden matches. All commands exited 0. Both hosts retain platform/legacy-fixture skips; no skipped case is reported as executed. i386 and the six remote CI jobs remain whole-port finalization gates, not completed by batch B.

Local evidence logs (not tracked artifacts): /tmp/task015-red-tests.log, /tmp/task015-wizset-red.log, /tmp/task015-loader-red.log, /tmp/task015-nested-red.log; final builds and suites in /tmp/task015-final-native-build.log, /tmp/task015-final-native-ctest.log, /tmp/task015-final-asan-build.log, /tmp/task015-final-asan-ctest.log and /tmp/task015-final-linux.log; boot results in /tmp/task015-native-boot.log and /tmp/task015-linux-boot.log; smoke in /tmp/task015-account-smoke.log; census logs /tmp/task015-{string-view,location-read,room-resolve}-census.log. These local logs are session evidence, not permanent repository dependencies. Counts/outcomes above are the durable record.

Final independent review reinspection reported no remaining actionable source findings. git -c core.whitespace=cr-at-eol diff --check passes (script.cpp preserves its existing CRLF convention). The four release commits 1b2a06bb, 84462052, 8bd82c94 and 26c0df11 are ported locally with the behavior/tests/gates above. TASK-015 remains In Progress because the other release slices and U1-U4 are not yet implemented.
