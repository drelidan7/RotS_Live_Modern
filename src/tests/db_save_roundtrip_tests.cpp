// db_save_roundtrip_tests.cpp: pins the on-disk bytes write_player_text() (db.cpp,
// currently starting at line 2840) produces for the live player-save TEXT format --
// the format save_player/save_char still use for every character that isn't linked to
// an account (see docs/data-formats/player-save.md). This is an on-disk FORMAT pin, not
// a terminal-output pin: byte drift here is player-data corruption, not cosmetics.
//
// Wave 4 Task 3 (D2) converts the 18 sprintf/strcpy-family composition sites in
// save_player/save_char (db.cpp region :2851-3170) to std::format, but touches none of
// write_player_text's fprintf calls (left untouched by design -- see the wave's global
// constraints) and only the two struct-field copies inside write_player_text itself
// (chd.pwd/chd.host, both catalogued as parser/copy skips, not conversion sites). So
// this suite's expected result is trivial identity: the transform should not move this
// needle at all. It exists as the empirical proof of that claim and as a tripwire
// against any accidental collateral edit (a stray reflow, a misplaced line) near the
// highest-stakes region in the wave.
//
// Hash procedure (see SavePlayerRoundTrip.PrintsStableFormatHash below): run this suite
// against UNCHANGED source and record the FNV-1a64 value it prints to stdout; apply the
// D2 transform; rebuild and rerun; the hash MUST be byte-identical. Any diff is a STOP
// per the wave's global constraints. The task report records the actual before/after
// values captured this way. Both the hash and the round-trip equality check below
// normalize away write_player_text's two wall-clock-derived fields first (see
// normalize_time_variant_fields) -- without that, comparing raw bytes across two
// SEPARATE PROCESS RUNS (exactly what the pre-/post-transform gate does) would show a
// spurious diff on `last_logon` alone, purely because real time advanced between runs,
// even though nothing in write_player_text's own composition changed.

#include "../db.h"
#include "../structs.h"
#include "../utils.h"
#include "test_char_cleanup.h"
#include "test_platform_compat.h"
#include "test_world.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

// Fixture char_file_u whose fields flow straight through store_to_char ->
// char_to_store -> write_player_text with no derived/recomputed value in between (see
// db.cpp's store_to_char:2450 and char_to_store:2561), so their exact serialized text
// is safe to pin as a landmark below. Values are deliberately distinctive (unlikely to
// collide with any other numeric field's default) and chosen to dodge the two
// known non-passthrough traps in this path: weight > 200 (store_to_char overrides
// <=200 with get_race_weight()) and tmpabilities/constabilities.str = 100 (the
// standing Wave 4 fixture rule -- an unset str divides by zero in x86 strength-derived
// math reachable from store_to_char's recalc_skills()/affect_total() calls).
char_file_u make_round_trip_fixture()
{
    char_file_u stored_character {};
    std::snprintf(stored_character.name, sizeof(stored_character.name), "%s", "rndtripchr");
    std::snprintf(stored_character.title, sizeof(stored_character.title), "%s", "the Round-Trip Fixture");
    std::snprintf(stored_character.description, sizeof(stored_character.description), "%s",
        "A deterministic fixture character used to pin write_player_text on-disk bytes (Wave 4 Task 3).");
    stored_character.sex = SEX_MALE;
    stored_character.prof = PROF_WARRIOR;
    stored_character.race = RACE_HUMAN;
    stored_character.bodytype = 1;
    stored_character.level = 47;
    stored_character.language = LANG_HUMAN;
    stored_character.birth = 1700000000;
    stored_character.played = 4242;
    stored_character.weight = 250; // >200: bypasses store_to_char's get_race_weight() override
    stored_character.height = 71;
    stored_character.hometown = 3;
    stored_character.last_logon = 1700005000;
    stored_character.points.gold = 918273;
    stored_character.points.exp = 445566;
    stored_character.specials2.idnum = 918273;
    stored_character.specials2.alignment = -250;
    stored_character.specials2.pref = 1L << 4;
    stored_character.specials2.tactics = TACTICS_BERSERK;
    stored_character.specials2.shooting = SHOOTING_FAST;
    stored_character.specials2.casting = CASTING_SLOW;
    stored_character.specials2.two_handed = 1;
    stored_character.tmpabilities.str = 100; // mandatory div-by-zero guard (Wave 4 fixture rule)
    stored_character.constabilities.str = 100;
    stored_character.profs.colors[COLOR_MAGIC] = CBMAG;
    stored_character.profs.prof_level[PROF_WARRIOR] = 47;
    stored_character.profs.prof_coof[PROF_WARRIOR] = 88;
    return stored_character;
}

// Populates `character` (a caller-owned, already value-initialized char_data) from
// `fixture` via the production clear_char()+store_to_char() pair -- the same sequence
// db_loader_tests.cpp's write_valid_legacy_player_file() helper uses to reach
// save_player -- minus the player_table/bucket-directory plumbing that only
// save_player's filename composition needs; write_player_text() takes an explicit
// scratch path and doesn't touch player_table at all. Wires `descriptor` (password/
// host) the way write_player_text reads them (ch->desc->pwd / ch->desc->host,
// db.cpp:2854-2855); `descriptor` must outlive `character`'s use.
void build_round_trip_character(char_data& character, descriptor_data& descriptor, const char_file_u& fixture)
{
    clear_char(&character, MOB_VOID);
    char_file_u mutable_fixture = fixture;
    store_to_char(&mutable_fixture, &character);

    std::snprintf(descriptor.pwd, sizeof(descriptor.pwd), "%s", "RndTrPw1");
    std::snprintf(descriptor.host, sizeof(descriptor.host), "%s", "roundtrip.test.host");
    character.desc = &descriptor;
}

std::string read_entire_file(const std::string& path)
{
    FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr)
        return {};

    std::string contents;
    char buffer[1024];
    size_t bytes_read;
    while ((bytes_read = std::fread(buffer, sizeof(char), sizeof(buffer), file)) > 0)
        contents.append(buffer, bytes_read);
    std::fclose(file);
    return contents;
}

// Blanks the value on write_player_text's two wall-clock-derived lines ("played" and
// "last_logon", db.cpp's char_to_store folds time(0) into both -- see char_to_store:
// "st->played += (long)(time(0) - ch->player.time.logon); st->last_logon = time(0);")
// to a fixed placeholder, so byte comparisons reflect the FORMAT write_player_text
// produces, not the moment it happened to run. `last_logon` is guaranteed to differ
// between two separate process invocations; `played` is stable in practice (the two
// time(0) calls this fixture triggers land microseconds apart, so their delta is
// almost always 0) but isn't provably so, so it's normalized defensively too.
std::string normalize_time_variant_fields(std::string text)
{
    static const char* const kVariantPrefixes[] = { "played      ", "last_logon  " };
    for (const char* prefix : kVariantPrefixes) {
        const std::size_t prefix_position = text.find(prefix);
        if (prefix_position == std::string::npos)
            continue;
        const std::size_t value_start = prefix_position + std::strlen(prefix);
        const std::size_t value_end = text.find('\n', value_start);
        if (value_end == std::string::npos)
            continue;
        text.replace(value_start, value_end - value_start, "0");
    }
    return text;
}

// A tiny, fully-specified, dependency-free 64-bit hash (FNV-1a) used only to print a
// short fingerprint of the serialized bytes for the before/after diff described in the
// file header comment. Deliberately NOT std::hash<std::string> (its result is
// implementation-defined per the standard, so equality across two separate builds --
// even on the same machine/toolchain -- is not something the standard promises) and
// NOT a cryptographic hash.
std::uint64_t fnv1a64(const std::string& data)
{
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (unsigned char byte : data) {
        hash ^= byte;
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

} // namespace

// Pins the on-disk text player-save format byte-for-byte. write_player_text()
// exists precisely to serialize to a caller-chosen scratch path without touching live
// data; a fixed fixture character must serialize to IDENTICAL bytes (modulo the two
// wall-clock fields normalize_time_variant_fields blanks -- see its comment) across two
// back-to-back calls, and every landmark line below is a field that flows straight
// through store_to_char -> char_to_store -> write_player_text with no derived/
// recomputed value, so its exact text is safe to pin.
TEST(SavePlayerRoundTrip, FixtureCharacterSerializesToStableBytes)
{
    ScopedTestWorld test_world;

    char path_template[] = "/tmp/rots-db-save-roundtrip-XXXXXX";
    char* created_path = rots_mkdtemp(path_template);
    ASSERT_NE(created_path, nullptr);
    const std::string temp_dir = created_path;
    const std::string scratch = temp_dir + "/roundtrip-scratch";

    const char_file_u fixture = make_round_trip_fixture();
    char_data character {};
    descriptor_data descriptor {};
    build_round_trip_character(character, descriptor, fixture);
    // Releases character.profs/skills/knowledge (clear_char() heap
    // allocations, via build_round_trip_character's clear_char() call) at
    // scope exit (Phase 5 T6 leak sweep).
    ScopedClearCharFields character_cleanup { character };
    // Releases character.player.title/description/name
    // (store_to_char() heap allocations, via build_round_trip_character's
    // store_to_char() call) at scope exit (Phase 5 T6 leak sweep).
    ScopedStoreToCharFields character_store_cleanup { character };

    ASSERT_TRUE(write_player_text(&character, /*load_room=*/9001, scratch.c_str()));
    const std::string first = read_entire_file(scratch);
    ASSERT_TRUE(write_player_text(&character, /*load_room=*/9001, scratch.c_str()));
    const std::string second = read_entire_file(scratch);

    EXPECT_EQ(normalize_time_variant_fields(first), normalize_time_variant_fields(second));
    EXPECT_FALSE(first.empty());

    EXPECT_NE(first.find("#player\n"), std::string::npos);
    EXPECT_NE(first.find("version     1\n"), std::string::npos);
    EXPECT_NE(first.find("name        rndtripchr\n"), std::string::npos);
    EXPECT_NE(first.find("sex         1\n"), std::string::npos);
    EXPECT_NE(first.find("prof        4\n"), std::string::npos);
    EXPECT_NE(first.find("race        1\n"), std::string::npos);
    EXPECT_NE(first.find("bodytype    1\n"), std::string::npos);
    EXPECT_NE(first.find("level       47\n"), std::string::npos);
    EXPECT_NE(first.find("language    122\n"), std::string::npos);
    EXPECT_NE(first.find("weight      250\n"), std::string::npos);
    EXPECT_NE(first.find("height      71\n"), std::string::npos);
    EXPECT_NE(first.find("title       the Round-Trip Fixture\n"), std::string::npos);
    EXPECT_NE(first.find("hometown    3\n"), std::string::npos);
    EXPECT_NE(first.find("idnum       918273\n"), std::string::npos);
    EXPECT_NE(first.find("load_room   9001\n"), std::string::npos);
    EXPECT_NE(first.find("alignment   -250\n"), std::string::npos);
    EXPECT_NE(first.find("A deterministic fixture character used to pin write_player_text on-disk bytes"), std::string::npos);
    EXPECT_NE(first.find("end\n"), std::string::npos);

    std::filesystem::remove(scratch);
    std::filesystem::remove(temp_dir);
}

// Prints (not just asserts) a short fingerprint of the SAME fixture's serialized bytes
// so a human/CI log can diff the pre-transform and post-transform values per the file
// header comment's hash procedure -- the practical gate Task 3's brief calls for in
// place of a checked-in golden file for this on-disk format.
TEST(SavePlayerRoundTrip, PrintsStableFormatHash)
{
    ScopedTestWorld test_world;

    char path_template[] = "/tmp/rots-db-save-roundtrip-hash-XXXXXX";
    char* created_path = rots_mkdtemp(path_template);
    ASSERT_NE(created_path, nullptr);
    const std::string temp_dir = created_path;
    const std::string scratch = temp_dir + "/roundtrip-scratch-hash";

    const char_file_u fixture = make_round_trip_fixture();
    char_data character {};
    descriptor_data descriptor {};
    build_round_trip_character(character, descriptor, fixture);
    // See the FixtureCharacterSerializesToStableBytes comment above (Phase 5
    // T6 leak sweep).
    ScopedClearCharFields character_cleanup { character };
    // Releases character.player.title/description/name
    // (store_to_char() heap allocations, via build_round_trip_character's
    // store_to_char() call) at scope exit (Phase 5 T6 leak sweep).
    ScopedStoreToCharFields character_store_cleanup { character };

    ASSERT_TRUE(write_player_text(&character, /*load_room=*/9001, scratch.c_str()));
    static std::string pre_transform_bytes = normalize_time_variant_fields(read_entire_file(scratch));
    const std::uint64_t hash = fnv1a64(pre_transform_bytes);

    std::cout << "[SavePlayerRoundTrip] write_player_text FNV-1a64 = 0x" << std::hex << hash
              << std::dec << " (" << pre_transform_bytes.size() << " bytes)" << std::endl;

    EXPECT_FALSE(pre_transform_bytes.empty());

    std::filesystem::remove(scratch);
    std::filesystem::remove(temp_dir);
}
