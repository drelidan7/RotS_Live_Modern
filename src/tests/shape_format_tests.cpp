#include "../protos.h"
#include "../structs.h"
#include "../utils.h"
#include "../zone.h"
#include "test_platform_compat.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// Characterization/round-trip tests for Phase 4 Wave 2 Task 3 (std::format
// population on the shape* world-editor tools), covering shapemdl.cpp first.
// Per the task's binding pattern, these pin the CURRENT byte-for-byte output
// -- they must (and do, confirmed via `git stash` against the pre-conversion
// tree) pass unmodified, then stay green after the sprintf/strcpy sites are
// converted to std::format/std::string composition.

extern int load_mudlle(char_data* ch, char* arg);
extern int save_mudlle(struct char_data* ch);
extern void show_mudlle(struct char_data* ch);
extern void clear_char(struct char_data* ch, int mode);
extern int num_of_programs;

namespace {

// Sandboxes each test in a scratch directory (removed on destruction) --
// mirrors objsave_json_tests.cpp's TemporaryDirectory.
class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        char path_template[] = "/tmp/rots-shape-format-XXXXXX";
        char* created_path = rots_mkdtemp(path_template);
        EXPECT_NE(created_path, nullptr);
        if (created_path)
            m_path = created_path;
    }

    ~TemporaryDirectory()
    {
        if (!m_path.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(m_path, ec);
        }
    }

    const std::string& path() const { return m_path; }

private:
    std::string m_path;
};

// load_mudlle/save_mudlle resolve "world/mdl/<zone>.mdl" relative to the
// process cwd (SHAPE_MDL_DIR/SHAPE_MDL_BACKDIR) -- mirrors
// objsave_json_tests.cpp's ScopedWorkingDirectory.
class ScopedWorkingDirectory {
public:
    explicit ScopedWorkingDirectory(const std::string& path)
    {
        std::error_code ec;
        m_original_path = std::filesystem::current_path(ec);
        EXPECT_FALSE(ec);

        std::filesystem::current_path(path, ec);
        EXPECT_FALSE(ec) << "Expected current_path(" << path << ") to succeed.";
    }

    ~ScopedWorkingDirectory()
    {
        if (!m_original_path.empty()) {
            std::error_code ec;
            std::filesystem::current_path(m_original_path, ec);
            EXPECT_FALSE(ec);
        }
    }

private:
    std::filesystem::path m_original_path;
};

// real_program()'s loop is `for (tmp = 0; tmp <= num_of_programs; tmp++)`
// over the (here, un-booted) mobile_program_zone[] table; num_of_programs =
// -1 makes the loop body never execute and the "not found" branch return 0
// immediately, without touching the table -- same trick shapemob_tests.cpp
// uses for the sibling mob-implement path.
// get_permission() (shapemob.cpp) -- reached via load_mudlle's permission
// lookup -- scans zone_table[0..MAX_ZONES) unconditionally, regardless of
// whether the world was ever booted. In this standalone test process
// zone_table is still its default-constructed nullptr, so the scan
// dereferences a null pointer; a real boot always populates it first, so
// this is a test-fixture gap, not a product bug this task should touch.
// Stubbing a zero-initialized array (every entry's `.number` is 0, which
// never matches the vnum/100 zone numbers these tests use) lets the scan
// run to completion (the "outside any zone" branch) exactly like a real
// zone-less vnum would.
struct ShapeMudlleGlobalsGuard {
    int saved_num_of_programs = num_of_programs;
    zone_data* saved_zone_table = zone_table;
    int saved_top_of_zone_table = top_of_zone_table;
    std::vector<zone_data> stub_zone_table = std::vector<zone_data>(MAX_ZONES);

    ShapeMudlleGlobalsGuard()
    {
        zone_table = stub_zone_table.data();
        top_of_zone_table = 0;
    }

    ~ShapeMudlleGlobalsGuard()
    {
        num_of_programs = saved_num_of_programs;
        zone_table = saved_zone_table;
        top_of_zone_table = saved_top_of_zone_table;
    }
};

std::string read_file(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream contents;
    contents << in.rdbuf();
    return contents.str();
}

void write_file(const std::string& path, const std::string& contents)
{
    std::ofstream out(path, std::ios::binary);
    ASSERT_TRUE(out.good()) << "failed to open " << path << " for write";
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

descriptor_data make_capturing_descriptor(char_data* character)
{
    descriptor_data descriptor {};
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = 0; // CON_PLAYING
    descriptor.character = character;
    return descriptor;
}

} // namespace

// The real-world .mdl format (confirmed against lib/world/mdl/11.mdl):
// "#<vnum>  <comment>\n<program body...>\n#99999\n" -- load_mudlle scans for
// the requested vnum's header line, slurps everything up to the next '#'
// line as the program body, then save_mudlle splices the (possibly edited)
// body back between the same two '#' lines. Loading then immediately saving
// back, with no edits, must reproduce the exact original bytes -- this is
// the round-trip pin for the shapemdl.cpp world-file writer (the site the
// task brief flags as the boot-golden risk: any byte drift in this output
// would change the .mdl files the game reads on boot).
TEST(ShapeMudlle, LoadThenSaveRoundTripsMdlFileByteForByte)
{
    ShapeMudlleGlobalsGuard globals_guard;
    num_of_programs = -1;

    TemporaryDirectory root;
    std::filesystem::create_directories(root.path() + "/world/mdl/oldmdls");

    const std::string original
        = "#1101  Test program\n"
          "2VI1.0m3.0m,1.0m$01105K,3.0m \\ Just a testing program.. nothing special\n"
          "`Testing more`s\n"
          ".\n"
          "#99999\n";
    write_file(root.path() + "/world/mdl/11.mdl", original);

    ScopedWorkingDirectory cwd_guard(root.path());

    char_data editor {};
    clear_char(&editor, MOB_VOID);
    descriptor_data descriptor = make_capturing_descriptor(&editor);
    editor.desc = &descriptor;

    shape_mudlle mudlle {};
    mudlle.prog_num = -1;
    editor.temp = &mudlle;

    char arg[] = "1101";
    int loaded = load_mudlle(&editor, arg);
    ASSERT_EQ(loaded, 1101);
    ASSERT_NE(mudlle.txt, nullptr);
    EXPECT_STREQ(mudlle.txt,
        "2VI1.0m3.0m,1.0m$01105K,3.0m \\ Just a testing program.. nothing special\n"
        "`Testing more`s\n"
        ".\n");
    EXPECT_STREQ(mudlle.f_from, "world/mdl/11.mdl");
    EXPECT_STREQ(mudlle.f_old, "world/mdl/oldmdls/11.mdl");
    EXPECT_NE(std::string(descriptor.output).find("You uploaded the special program #1101.\n\r"),
        std::string::npos);

    // load_mudlle's get_permission() lookup found no matching zone in the
    // (empty) stub zone_table, so it just granted the real "outside any
    // zone -> limited permission" outcome (permission=0). save_mudlle itself
    // gates on `SHAPE_MUDLLE(ch)->permission`, which is orthogonal to this
    // test's actual target (the world-file writer's byte output) -- force
    // it to 1, exactly as a real builder with zone access would have.
    mudlle.permission = 1;

    // write_to_output() (comm.cpp) tracks the next-write position via
    // bufptr/bufspace, not strlen(output) -- resetting only output[0]
    // leaves bufptr pointing past the just-cleared byte, so the next
    // message would land after an unreachable '\0' gap. Re-run the same
    // setup make_capturing_descriptor() uses to get a genuinely empty,
    // freshly-positioned buffer.
    descriptor = make_capturing_descriptor(&editor);
    int saved = save_mudlle(&editor);
    EXPECT_EQ(saved, 1101);
    EXPECT_NE(std::string(descriptor.output).find("Saved succesfully.\n\r"), std::string::npos);

    // save_mudlle does NOT reproduce the original file byte-for-byte -- two
    // pre-existing quirks in code this task's conversion doesn't touch the
    // control flow of, preserved exactly rather than "fixed": (1) the
    // replacement header line is built from scratch as "#<prog_num>\n",
    // discarding the original's trailing comment text ("  Test program");
    // (2) the write loop's
    // `while (!feof(ofp))` never re-checks feof() until AFTER an already-
    // failed fgets() on the stale buffer, so the final "#99999\n" sentinel
    // line gets written twice. Both are exercised here rather than
    // "corrected", per this task's preserve-observed-behavior mandate.
    const std::string expected_after_save
        = "#1101\n"
          "2VI1.0m3.0m,1.0m$01105K,3.0m \\ Just a testing program.. nothing special\n"
          "`Testing more`s\n"
          ".\n"
          "#99999\n"
          "#99999\n";
    const std::string round_tripped = read_file(root.path() + "/world/mdl/11.mdl");
    EXPECT_EQ(round_tripped, expected_after_save);

    // f_old (world/mdl/oldmdls/11.mdl) is the pre-save backup copy_file made
    // of the ORIGINAL (pre-save) file content -- this one IS byte-identical,
    // since copy_file runs before any rewriting.
    EXPECT_EQ(read_file(root.path() + "/world/mdl/oldmdls/11.mdl"), original);

    RELEASE(mudlle.txt);
}

TEST(ShapeMudlle, LoadOfUnknownProgramNumberCreatesNewProgramMessage)
{
    ShapeMudlleGlobalsGuard globals_guard;
    num_of_programs = -1;

    TemporaryDirectory root;
    std::filesystem::create_directories(root.path() + "/world/mdl/oldmdls");
    write_file(root.path() + "/world/mdl/11.mdl", "#99999\n");

    ScopedWorkingDirectory cwd_guard(root.path());

    char_data editor {};
    clear_char(&editor, MOB_VOID);
    descriptor_data descriptor = make_capturing_descriptor(&editor);
    editor.desc = &descriptor;

    shape_mudlle mudlle {};
    mudlle.prog_num = -1;
    editor.temp = &mudlle;

    char arg[] = "1150";
    int loaded = load_mudlle(&editor, arg);
    ASSERT_EQ(loaded, 99999);
    EXPECT_EQ(mudlle.real_num, 0);
    // num==99999 (the file has no #1150 record) takes the OUTER "new
    // program" branch, which prints "A new program.\n\r" -- the "Could not
    // find a program #{}, created it." message a few lines down is only
    // reachable from the num!=99999 ELSE branch, where isnew is
    // unconditionally set to 0 right at the top and never set to 1 again;
    // that message is unreachable dead code in both the pre- and
    // post-conversion source (not something this task touches).
    EXPECT_NE(std::string(descriptor.output).find("A new program.\n\r"), std::string::npos);

    RELEASE(mudlle.txt);
}

// show_mudlle's format ("Program #{} (real #{})...") -- exercised directly
// (no file I/O) with a negative real_num as the edge/negative-input case the
// task brief calls for. Note: the guard `!SHAPE_MUDLLE(ch)->txt ||
// !SHAPE_MUDLLE(ch)->prog_num < 0` is pre-existing operator-precedence oddity
// (`!prog_num < 0` is always false, since `!` binds first) -- not this
// task's concern to "fix"; only `txt` non-null actually gates this function.
TEST(ShapeMudlle, ShowMudlleFormatsNegativeRealNumCorrectly)
{
    char_data editor {};
    clear_char(&editor, MOB_VOID);
    descriptor_data descriptor = make_capturing_descriptor(&editor);
    editor.desc = &descriptor;

    shape_mudlle mudlle {};
    CREATE(mudlle.txt, char, strlen("Some program body\n\r") + 1);
    strcpy(mudlle.txt, "Some program body\n\r");
    mudlle.prog_num = 42;
    mudlle.real_num = -7;
    editor.temp = &mudlle;

    show_mudlle(&editor);

    EXPECT_EQ(std::string(descriptor.output),
        "Program #42 (real #-7).\n\rProgram text:\n\rSome program body\n\r\n\r");

    RELEASE(mudlle.txt);
}
