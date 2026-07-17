// rots_convert_equivalence_tests.cc -- proves spec Sec4b's claim that the
// standalone `rots_convert` executable's conversion output is byte-identical
// to the in-MUD conversion path, for every playable RACE_* value (db.cpp-
// split Task 4; docs/superpowers/plans/2026-07-17-db-split-and-rots-convert.md
// Task 4 + its binding addendum from Task 3's review adjudication).
//
// DESIGN (the plan's "strongest portable design" option): this is a gtest TU
// compiled into ageland_tests, which links the REAL handler.cpp/profs.cpp/
// utility.cpp/consts.cpp (ROTS_SERVER_SOURCES) -- never convert_stubs.cpp,
// which only convert_main.cpp's own CMake target compiles (see
// CMakeLists.txt's rots_convert comment). So a load->store->save round trip
// run IN THIS PROCESS exercises the exact in-MUD implementations of the ~14
// functions that, before the Task 4b relocation, convert_stubs.cpp substituted (affect_modify,
// recalc_abilities, get_race_perception, class_HP, and kin -- see
// convert_stubs.cpp's own section comments for the full list and each
// substitution's justification). This test then ALSO shells out to the real,
// separately-built `rots_convert` BINARY (a second executable, its own
// main(), linking convert_stubs.cpp instead) against a byte-identical copy of
// the same input fixture, and compares the two resulting on-disk pfiles.
// Nothing here re-implements or approximates either conversion path; both
// sides call the production load_char()/store_to_char()/save_char() trio
// (mirroring convert_main.cpp's convert_one_character() exactly -- see
// run_reference_conversion() below), so a divergence here can only come from
// convert_stubs.cpp's substitutes genuinely disagreeing with their real
// counterparts (exactly the class of bug Task 3's review found and fixed --
// see db-task-3-report.md's C1).
//
// RACE COVERAGE (binding addendum): Task 3's C1 finding was
// convert_stub_get_race_perception() silently diverging from the real
// get_race_perception() (utility.cpp) on six race values, invisible to a
// "raceless" functional smoke (5292/5589 converted counts are insensitive to
// a wrong-but-still-a-number derived stat) -- see db-task-3-report.md's "Note
// for Task 4" section, which explicitly names Dwarf/Wood-elf/Hobbit/
// High-elf/Orc/Undead/Troll as the races that slipped through. kRaceCoverage
// below therefore covers EVERY RACE_* constant character.h declares, not
// just the ten chargen-selectable ones (interpre.cpp's CON_QRACE menu):
// the twelve under character.h's "/* Races for PCS */" comment (including
// RACE_GOD -- Immortals are GET_RACE()==RACE_GOD player characters, see
// AGENTS.md's "first character is promoted to a level-100 Implementor" note
// -- and RACE_HIGH, admin/reward-granted rather than chargen-selectable) PLUS
// the four "/* Races used for NPCs */" constants (RACE_EASTERLING/RACE_HARAD/
// RACE_UNDEAD/RACE_TROLL). The latter four are included deliberately: a
// player's char_file_u::race is a plain persisted byte, and every race-keyed
// derived-stat table this test exercises (get_race_perception/
// get_race_weight/get_race_height/race_affect[]) switches on all sixteen
// RACE_* values with no IS_NPC gate -- exactly the surface C1 slipped through,
// and exactly the surface RACE_UNDEAD/RACE_TROLL were flagged as diverging or
// matching on in that report's table.
//
// TIME_PHASE (binding addendum): convert_stubs.cpp's get_current_time_phase()
// always returns 0 (a deliberately non-reproducible live-server-uptime
// field -- see that function's own header comment); a live server's `pulse`
// global would return something else. This test does NOT need any special
// exclusion/normalization logic for affected_type::time_phase: the legacy
// text-pfile format's "affect" line (write_player_text(), db_players.cpp)
// serializes only {type, duration, modifier, location, bitvector} --
// time_phase is a pure in-memory/runtime field, never persisted in either
// format (grep confirms no "time_phase" token anywhere under
// src/character_json.cpp or src/rots/ persist headers). kRaceCoverage's
// kAffectBearingCase proves this directly: it seeds a fixture with one active
// affected_type entry (APPLY_STR -- deliberately NOT APPLY_SPELL, the one
// documented, disclosed gap in convert_stubs.cpp's affect_modify() copy) so
// store_to_char() genuinely calls affect_to_char()->affect_modify() on both
// sides, and the two resulting pfiles still match byte-for-byte because the
// only field that could differ (time_phase) never reaches the comparison
// surface in the first place.
//
// WALL-CLOCK FIELDS (a related, but distinct, normalization): char_to_store()
// (db_players.cpp) unconditionally stamps `st->last_logon = time(0)` and
// advances `st->played` by a real time(0) delta on every save -- a property
// of ANY load->store->save round trip (live login or batch converter alike),
// not a rots_convert-specific approximation. Two runs of the SAME conversion
// executed a heartbeat apart in wall time legitimately produce different
// "played"/"last_logon" text lines. expect_pfiles_equivalent() below excludes
// exactly these two field lines (by exact key match, not a blanket skip) from
// the byte comparison, and documents why at the exclusion site.
//
// SCRIPT-CTEST COMPANION: the plan's simpler-alternative branch requires "a
// cheap script CTest that runs the real binary on at least 2 fixtures
// end-to-end" when a gtest-only design is chosen. This file's
// ConvertEquivalence/PerRace.* suite already runs the real, separately-built
// rots_convert BINARY (not merely in-process code) against kRaceCoverage's
// 17 fixtures via CTest (gtest_discover_tests registers each as its own
// CTest case) -- comfortably exceeding that bar without a second script.

#include "../db.h"
#include "../handler.h"
#include "../utils.h"
#include "rots/core/character.h" // RACE_*/SEX_*/LANG_*/MOB_VOID/PROF_* constants.
#include "rots/core/descriptor.h"
#include "rots/persist/file_formats.h"
#include "test_platform_compat.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// db_players.cpp/entity_lifecycle.cpp/convert_main.cpp all declare their own
// file-local externs for these rather than reaching them through a header
// (see e.g. convert_main.cpp:114-121, interpre.cpp:71/81) -- matching that
// established convention here rather than adding a new db.h-wide
// declaration.
extern struct player_index_element *player_table;
extern int top_of_p_table;
extern void build_player_index(void);
extern void clear_char(struct char_data *ch, int mode);
extern void save_player(struct char_data *ch, int load_room, int index_pos);
extern void store_to_char(struct char_file_u *st, struct char_data *ch);

namespace {

// --- Small fixtures, duplicated per this repo's established one-fixture-
// per-test-file convention (see db_loader_tests.cpp's identically named
// helpers; TemporaryDirectory/ScopedWorkingDirectory/ScopedPlayerTableReset
// each have internal linkage there too, so they cannot be shared directly).

class TemporaryDirectory {
  public:
    TemporaryDirectory() {
        char path_template[] = "/tmp/rots-convert-equiv-XXXXXX";
        char *created_path = rots_mkdtemp(path_template);
        EXPECT_NE(created_path, nullptr);
        if (created_path)
            m_path = created_path;
    }

    ~TemporaryDirectory() {
        if (!m_path.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(m_path, ec);
        }
    }

    const std::string &path() const { return m_path; }

  private:
    std::string m_path;
};

class ScopedWorkingDirectory {
  public:
    explicit ScopedWorkingDirectory(const std::string &path) {
        std::error_code ec;
        m_original_path = std::filesystem::current_path(ec);
        EXPECT_FALSE(ec);
        std::filesystem::current_path(path, ec);
        EXPECT_FALSE(ec) << "Expected current_path(" << path << ") to succeed.";
    }

    ~ScopedWorkingDirectory() {
        if (!m_original_path.empty()) {
            std::error_code ec;
            std::filesystem::current_path(m_original_path, ec);
            EXPECT_FALSE(ec);
        }
    }

  private:
    std::filesystem::path m_original_path;
};

// Saves/restores player_table + top_of_p_table around a build_player_index()
// call in this process, releasing whatever build_player_index() allocated
// (create_entry()'s CREATE()-based .name strings + the table itself) before
// restoring -- mirrors db_loader_tests.cpp's ScopedPlayerTableReset exactly.
class ScopedPlayerTableReset {
  public:
    ScopedPlayerTableReset()
        : m_previous_player_table(player_table), m_previous_top_of_p_table(top_of_p_table) {
        player_table = nullptr;
        top_of_p_table = -1;
    }

    ~ScopedPlayerTableReset() {
        if (player_table != nullptr) {
            for (int index = 0; index <= top_of_p_table; ++index)
                RELEASE(player_table[index].name);
            RELEASE(player_table);
        }
        player_table = m_previous_player_table;
        top_of_p_table = m_previous_top_of_p_table;
    }

  private:
    player_index_element *m_previous_player_table;
    int m_previous_top_of_p_table;
};

std::string read_file_contents(const std::string &path) {
    FILE *file = std::fopen(path.c_str(), "rb");
    EXPECT_NE(file, nullptr) << "could not open " << path;
    if (file == nullptr)
        return "";

    std::string contents;
    char buffer[1024];
    while (true) {
        const size_t bytes_read = std::fread(buffer, sizeof(char), sizeof(buffer), file);
        if (bytes_read > 0)
            contents.append(buffer, bytes_read);
        if (bytes_read < sizeof(buffer)) {
            EXPECT_EQ(std::ferror(file), 0);
            break;
        }
    }
    std::fclose(file);
    return contents;
}

void write_file(const std::string &path, const std::string &contents) {
    FILE *file = std::fopen(path.c_str(), "wb");
    ASSERT_NE(file, nullptr) << "could not create " << path;
    ASSERT_EQ(std::fwrite(contents.data(), sizeof(char), contents.size(), file), contents.size());
    ASSERT_EQ(std::fclose(file), 0);
}

void create_player_bucket_directories(const std::string &root) {
    for (const char *bucket : {"A-E", "F-J", "K-O", "P-T", "U-Z"}) {
        std::error_code ec;
        std::filesystem::create_directories(root + "/players/" + bucket, ec);
        ASSERT_FALSE(ec) << "could not create " << root << "/players/" << bucket;
    }
}

// Locates the single output file (name a live/rebuilt bucket dir holds after
// a save) whose name starts with "<name_lowercase>." -- mirrors
// db_loader_tests.cpp's count_versioned_files_for()'s naming convention
// (save_player()'s atomic finalize prunes stale siblings, so exactly one
// file should match).
struct BucketFile {
    std::string filename; // basename only.
    std::string text;
};

BucketFile read_single_bucket_file_for(const std::string &bucket_directory,
                                       const std::string &name_lowercase) {
    namespace fs = std::filesystem;
    const std::string prefix = name_lowercase + ".";

    std::error_code ec;
    fs::directory_iterator it(bucket_directory, ec);
    const fs::directory_iterator end;

    BucketFile result;
    int matches = 0;
    for (; !ec && it != end; it.increment(ec)) {
        const std::string filename = it->path().filename().string();
        if (filename.size() >= prefix.size() &&
            std::string_view(filename).substr(0, prefix.size()) == prefix) {
            result.filename = filename;
            result.text = read_file_contents(bucket_directory + "/" + filename);
            ++matches;
        }
    }
    EXPECT_EQ(matches, 1) << "expected exactly one output file for '" << name_lowercase
                          << "' under " << bucket_directory;
    return result;
}

// One coverage case: a RACE_* value, its symbolic name (used as the gtest
// instance label), a distinct fixture character name (all start with 'a' so
// get_char_directory()'s bucket-by-first-letter convention always lands in
// players/A-E/, matching create_player_bucket_directories() above), and
// whether to seed one active affected_type entry (the addendum's "one
// documented affect-bearing case").
struct RaceCoverageCase {
    int race;
    const char *label;
    const char *name;
    bool with_affect;
};

// See this file's header comment for why all sixteen RACE_* constants (not
// just the ten chargen-selectable ones) are covered, and why the affect-
// bearing case exists.
constexpr RaceCoverageCase kRaceCoverage[] = {
    // character.h's "/* Races for PCS */" block.
    {RACE_GOD, "RACE_GOD", "aqcgod", false},
    {RACE_HUMAN, "RACE_HUMAN", "aqchuman", false},
    {RACE_DWARF, "RACE_DWARF", "aqcdwarf", false},
    {RACE_WOOD, "RACE_WOOD", "aqcwood", false},
    {RACE_HOBBIT, "RACE_HOBBIT", "aqchobbit", false},
    {RACE_HIGH, "RACE_HIGH", "aqchigh", false},
    {RACE_BEORNING, "RACE_BEORNING", "aqcbeorn", false},
    {RACE_URUK, "RACE_URUK", "aqcuruk", false},
    {RACE_ORC, "RACE_ORC", "aqcorc", false},
    {RACE_MAGUS, "RACE_MAGUS", "aqcmagus", false},
    {RACE_OLOGHAI, "RACE_OLOGHAI", "aqcolog", false},
    {RACE_HARADRIM, "RACE_HARADRIM", "aqchradr", false},
    // character.h's "/* Races used for NPCs */" block -- see header comment.
    {RACE_EASTERLING, "RACE_EASTERLING", "aqceast", false},
    {RACE_HARAD, "RACE_HARAD", "aqcharad", false},
    {RACE_UNDEAD, "RACE_UNDEAD", "aqcundd", false},
    {RACE_TROLL, "RACE_TROLL", "aqctroll", false},
    // The addendum's one documented affect-bearing case (RACE_HUMAN, chosen
    // arbitrarily -- the point is the active affect, not the race).
    {RACE_HUMAN, "RACE_HUMAN_WITH_AFFECT", "aqcaffct", true},
};

// Builds a minimal, valid char_file_u for `race`/`name`, closely modeled on
// db_loader_tests.cpp's make_stored_character() (same field set, same
// values) so this fixture exercises the identical store_to_char()/
// char_to_store() code paths that file's own round-trip tests already cover
// -- only race/name/affect vary between coverage cases, isolating the
// race-keyed derived-stat tables (get_race_perception/class_HP/
// recalc_abilities/race_affect[]) as the sole source of any divergence.
char_file_u make_stored_character_for_race(int race, const char *name, bool with_affect) {
    char_file_u stored_character{};
    std::snprintf(stored_character.name, sizeof(stored_character.name), "%s", name);
    std::snprintf(stored_character.title, sizeof(stored_character.title), "%s", "the Fixture");
    std::snprintf(stored_character.description, sizeof(stored_character.description), "%s",
                  "A conversion-equivalence fixture character.");
    stored_character.sex = SEX_MALE;
    stored_character.race = static_cast<byte>(race);
    stored_character.bodytype = 1;
    stored_character.level = 12;
    stored_character.language = LANG_HUMAN;
    stored_character.birth = 1700000000;
    stored_character.played = 456;
    stored_character.weight = 190;
    stored_character.height = 72;
    stored_character.hometown = 7;
    stored_character.last_logon = 1700000100;
    stored_character.points.gold = 1234;
    stored_character.points.exp = 5678;
    stored_character.specials2.idnum = 4242;
    stored_character.specials2.load_room = 3001;
    stored_character.specials2.act = 0;
    stored_character.specials2.pref = 1L << 5;
    stored_character.specials2.tactics = TACTICS_BERSERK;
    stored_character.specials2.shooting = SHOOTING_FAST;
    stored_character.specials2.casting = CASTING_SLOW;
    stored_character.specials2.two_handed = 1;
    stored_character.profs.prof_level[PROF_WARRIOR] = 12;
    stored_character.profs.prof_coof[PROF_WARRIOR] = 34;

    if (with_affect) {
        // `type` is pure metadata here -- affect_modify() dispatches on
        // `location`, never `type` (see handler.cpp/convert_stubs.cpp's
        // signature: `affect_modify(char_data*, byte loc, int mod, long
        // bitv, char add, sh_int counter)`). APPLY_STR is deliberately NOT
        // APPLY_SPELL -- the one documented, disclosed gap in
        // the real affect_modify() (single definition in entity_lifecycle.cpp; under
        // rots_convert its APPLY_SPELL case silently self-skips via the
        // null-spell_pointer guard against the names-only skills[] stub table,
        // where a live server would dispatch the spell function) -- so this
        // exercises a location
        // both sides implement identically.
        stored_character.affected[0].type = 1;
        stored_character.affected[0].duration = 10;
        stored_character.affected[0].modifier = 5;
        stored_character.affected[0].location = APPLY_STR;
        stored_character.affected[0].bitvector = 0;
    }

    return stored_character;
}

// Runs store_to_char()+save_player() once (real functions, this TU/binary)
// to produce the shared INPUT pfile text+filename both roots start from --
// called exactly ONCE per race (not once per root): char_to_store() stamps
// `last_logon`/`played` from a real time(0) call, so generating "the same"
// input twice, seconds apart, would NOT byte-match; copying one generated
// result into both roots (see the caller) sidesteps that entirely.
BucketFile generate_input_pfile(const std::string &generation_root,
                                const char_file_u &stored_character) {
    ScopedWorkingDirectory working_directory(generation_root);
    ScopedPlayerTableReset player_table_reset;

    player_table = new player_index_element[1]{};
    top_of_p_table = 0;
    player_table[0].name = strdup(stored_character.name);
    player_table[0].level = stored_character.level;
    player_table[0].race = stored_character.race;
    player_table[0].idnum = stored_character.specials2.idnum;
    player_table[0].log_time = stored_character.last_logon;
    player_table[0].flags = stored_character.specials2.act;

    char_data *character;
    CREATE1(character, char_data);
    clear_char(character, MOB_VOID);

    char_file_u mutable_store = stored_character;
    store_to_char(&mutable_store, character);

    descriptor_data descriptor{};
    std::snprintf(descriptor.pwd, sizeof(descriptor.pwd), "%s", "EquivFix1");
    std::snprintf(descriptor.host, sizeof(descriptor.host), "%s", "equiv-host");
    character->desc = &descriptor;

    save_player(character, stored_character.specials2.load_room, 0);
    const std::string generated_path = player_table[0].ch_file;

    const std::string player_text = read_file_contents(generated_path);
    const std::size_t slash = generated_path.rfind('/');
    const std::string filename =
        (slash == std::string::npos) ? generated_path : generated_path.substr(slash + 1);

    free_char(character);
    free(player_table[0].name);
    delete[] player_table;
    player_table = nullptr; // ScopedPlayerTableReset's destructor no-ops on nullptr, see below.
    top_of_p_table = -1;

    return BucketFile{filename, player_text};
}

// Mirrors convert_main.cpp's convert_one_character() (see that file's header
// comment for the load-room / descriptor pwd+host rationale, duplicated
// verbatim here) -- but runs INSIDE ageland_tests, against the REAL
// handler.cpp/profs.cpp/utility.cpp/consts.cpp this TU links, in contrast to
// the rots_convert BINARY (invoked out-of-process below), which links
// convert_stubs.cpp's substitutes for those same symbols instead. This is
// "the in-MUD conversion path" this suite proves rots_convert matches.
BucketFile run_reference_conversion(const std::string &root_directory, const char *name) {
    ScopedWorkingDirectory working_directory(root_directory);
    ScopedPlayerTableReset player_table_reset;

    build_player_index();

    int index = -1;
    for (int i = 0; i <= top_of_p_table; ++i) {
        if (player_table[i].name != nullptr && str_cmp(player_table[i].name, name) == 0) {
            index = i;
            break;
        }
    }
    EXPECT_GE(index, 0) << "fixture '" << name << "' missing from reference player_table";
    if (index < 0)
        return {};

    char_file_u loaded_record{};
    const int load_result = load_char(player_table[index].name, &loaded_record);
    EXPECT_GE(load_result, 0) << "reference load_char('" << name << "') failed";

    char_data_ptr character = make_char_data(MOB_VOID);

    descriptor_data fake_descriptor{};
    std::strncpy(fake_descriptor.pwd, loaded_record.pwd, MAX_PWD_LENGTH);
    fake_descriptor.pwd[MAX_PWD_LENGTH] = '\0';
    std::strncpy(fake_descriptor.host, loaded_record.host, HOST_LEN);
    fake_descriptor.host[HOST_LEN] = '\0';
    character->desc = &fake_descriptor;

    store_to_char(&loaded_record, character.get());
    save_char(character.get(), character->specials2.load_room, 0);
    character->desc = nullptr;

    return read_single_bucket_file_for("players/A-E", name);
}

// True for a text-pfile line this comparison must NOT hold to byte identity
// -- see this file's header comment ("WALL-CLOCK FIELDS") for the full
// rationale. Matched by exact field-name prefix (each write_player_text()
// key is followed by aligning spaces before its value, e.g. "played      %d"
// -- see db_players.cpp's fprintf list), so this cannot accidentally match
// an unrelated key.
bool is_wall_clock_field_line(const std::string &line) {
    return line.rfind("played ", 0) == 0 || line.rfind("last_logon ", 0) == 0;
}

std::vector<std::string> split_lines(const std::string &text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t newline = text.find('\n', start);
        std::string line = (newline == std::string::npos) ? text.substr(start)
                                                          : text.substr(start, newline - start);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(std::move(line));
        if (newline == std::string::npos)
            break;
        start = newline + 1;
    }
    return lines;
}

// Line-for-line comparison, excluding exactly the wall-clock fields
// documented above -- every other line (including every race-derived stat:
// tmpstats/permstats/tmpabil/permabil/ENE_regen/perception/willpower/
// resistances/vulnerabilities, plus password/host/affect lines) must match
// exactly.
void expect_pfiles_equivalent(const BucketFile &reference, const BucketFile &actual,
                              const std::string &context) {
    const std::vector<std::string> reference_lines = split_lines(reference.text);
    const std::vector<std::string> actual_lines = split_lines(actual.text);
    ASSERT_EQ(reference_lines.size(), actual_lines.size())
        << context << ": pfile line counts differ";

    for (std::size_t i = 0; i < reference_lines.size(); ++i) {
        if (is_wall_clock_field_line(reference_lines[i])) {
            EXPECT_TRUE(is_wall_clock_field_line(actual_lines[i]))
                << context << ": line " << i << " -- expected a wall-clock field on both sides";
            continue;
        }
        EXPECT_EQ(reference_lines[i], actual_lines[i]) << context << ": line " << i << " diverged";
    }
}

// Resolves the real rots_convert executable's path. CMake defines
// ROTS_CONVERT_EXECUTABLE (via $<TARGET_FILE:rots_convert>, see
// CMakeLists.txt) when this target is built through the CMake ageland_tests
// target -- rots_convert is a CMake-only target this wave (not part of the
// flat src/Makefile / src/tests/Makefile build, see CMakeLists.txt's
// rots_convert comment), so a flat-`make tests` build never defines this
// macro; ROTS_CONVERT_EXECUTABLE the environment variable is the fallback
// for that case (or any manual invocation) -- an absolute path is required
// either way, since this suite chdir()s during the reference-conversion
// step. Neither present (or the path not existing on disk) means this
// suite skips cleanly rather than failing the whole build.
std::string resolve_rots_convert_executable() {
    std::string candidate;
#if defined(ROTS_CONVERT_EXECUTABLE)
    candidate = ROTS_CONVERT_EXECUTABLE;
#endif
    if (candidate.empty()) {
        if (const char *env = std::getenv("ROTS_CONVERT_EXECUTABLE"))
            candidate = env;
    }
    if (candidate.empty())
        return {};

    std::error_code ec;
    if (!std::filesystem::exists(candidate, ec) || ec)
        return {};
    return candidate;
}

} // namespace

class ConvertEquivalence : public ::testing::TestWithParam<RaceCoverageCase> {
  protected:
    void SetUp() override {
        rots_convert_path_ = resolve_rots_convert_executable();
        if (rots_convert_path_.empty()) {
            GTEST_SKIP() << "rots_convert executable not available in this build (it is a "
                            "CMake-only target -- see CMakeLists.txt's rots_convert comment -- "
                            "and ROTS_CONVERT_EXECUTABLE is neither compiled in nor set in the "
                            "environment); skipping conversion-equivalence coverage here.";
        }
    }

    std::string rots_convert_path_;
};

// See this file's header comment for the full design rationale. Each
// instance: (1) builds one fixture character for its RACE_*/affect
// combination, (2) generates its INPUT pfile once via the real
// store_to_char()+save_player() path, (3) copies that byte-identical input
// into a "reference" root and a "convert" root, (4) runs the in-MUD
// conversion path in-process against the reference root, (5) shells out to
// the real, separately-built rots_convert binary against the convert root,
// and (6) compares the two resulting pfiles line-for-line (wall-clock fields
// excluded, see expect_pfiles_equivalent()).
TEST_P(ConvertEquivalence, PerRaceLegacyPfileMatchesInMudConversion) {
    const RaceCoverageCase &test_case = GetParam();

    TemporaryDirectory generation_root;
    TemporaryDirectory reference_root;
    TemporaryDirectory convert_root;
    create_player_bucket_directories(generation_root.path());
    create_player_bucket_directories(reference_root.path());
    create_player_bucket_directories(convert_root.path());

    const char_file_u stored_character =
        make_stored_character_for_race(test_case.race, test_case.name, test_case.with_affect);
    const BucketFile input_fixture = generate_input_pfile(generation_root.path(), stored_character);
    ASSERT_FALSE(input_fixture.filename.empty())
        << "input fixture generation failed for " << test_case.label;

    write_file(reference_root.path() + "/players/A-E/" + input_fixture.filename,
               input_fixture.text);
    write_file(convert_root.path() + "/players/A-E/" + input_fixture.filename, input_fixture.text);

    const BucketFile reference_output =
        run_reference_conversion(reference_root.path(), test_case.name);
    ASSERT_FALSE(reference_output.filename.empty())
        << "reference (in-MUD) conversion produced no output for " << test_case.label;

    // rots_convert_path_ and convert_root.path() are both already absolute
    // (TemporaryDirectory's rots_mkdtemp() and CMake's $<TARGET_FILE:...>
    // both return absolute paths), so this command is independent of this
    // process's current directory.
    const std::string log_path = convert_root.path() + "/rots_convert.log";
    const std::string command = "\"" + rots_convert_path_ + "\" --lib \"" + convert_root.path() +
                                "\" > \"" + log_path + "\" 2>&1";
    const int exit_code = std::system(command.c_str());
    EXPECT_EQ(exit_code, 0) << "rots_convert exited non-zero for " << test_case.label << "; log:\n"
                            << read_file_contents(log_path);

    const BucketFile actual_output =
        read_single_bucket_file_for(convert_root.path() + "/players/A-E", test_case.name);

    expect_pfiles_equivalent(reference_output, actual_output, test_case.label);
}

INSTANTIATE_TEST_SUITE_P(PerRace, ConvertEquivalence, ::testing::ValuesIn(kRaceCoverage),
                         [](const ::testing::TestParamInfo<RaceCoverageCase> &info) {
                             return std::string(info.param.label);
                         });
