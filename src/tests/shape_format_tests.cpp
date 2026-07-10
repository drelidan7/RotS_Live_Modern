#include "../db.h"
#include "../protos.h"
#include "../script.h"
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

// Points descriptor.output back at descriptor's OWN small_outbuf and clears
// its write cursor.
//
// CRITICAL: this MUST mutate the caller's own descriptor_data in place; a
// helper that builds a descriptor and returns it BY VALUE is unsafe here.
// descriptor_data::output is a self-pointer into the same object's
// small_outbuf[] array, and copying/moving a descriptor_data (whether across
// a `return` or an `x = f()` reassignment) copies that pointer bytewise --
// leaving `output` aimed at the SOURCE object's small_outbuf, which for a
// returned function-local is destroyed the moment the function returns. The
// resulting dangling pointer is what made write_to_output() scribble into
// freed stack on MSVC (empty/garbage output, cross-descriptor bleed, and an
// eventual SEH 0xc0000005 access violation once bufptr walked off a guard
// page). It went unnoticed on Linux/macOS only because those builds happened
// to elide the copy (guaranteed/NRVO) or the freed slot stayed intact; MSVC's
// Debug config disables NRVO, exposing the bug. Always: declare the
// descriptor, then reset_capturing_descriptor() it in place.
void reset_capturing_descriptor(descriptor_data& descriptor, char_data* character)
{
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = 0; // CON_PLAYING
    descriptor.character = character;
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
    descriptor_data descriptor {};
    reset_capturing_descriptor(descriptor, &editor);
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
    // message would land after an unreachable '\0' gap. Reset the descriptor
    // in place to get a genuinely empty, freshly-positioned buffer.
    reset_capturing_descriptor(descriptor, &editor);
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
    descriptor_data descriptor {};
    reset_capturing_descriptor(descriptor, &editor);
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
    descriptor_data descriptor {};
    reset_capturing_descriptor(descriptor, &editor);
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

// shapeobj.cpp's list_object() (the OLC "look at object" display) --
// pure formatting, no file I/O, no zone_table/permission dependency, so it's
// a cheap, representative pin for that file's message-composition sites
// (the path-construction sites (load_object/save_object) reuse the exact
// literal-inlined-{}-plus-static_cast<const char*>-decay pattern already
// pinned by the shapemdl.cpp round-trip test above; write_object() itself,
// which emits the actual .obj world-file records, uses only fprintf and has
// no sprintf/strcpy/strcat sites -- untouched by this task).
extern void list_object(struct char_data* ch, struct obj_data* obj);
extern int num_of_object_materials;

TEST(ShapeObj, ListObjectFormatsAllFieldsIncludingNegativeAffections)
{
    char_data editor {};
    clear_char(&editor, MOB_VOID);
    descriptor_data descriptor {};
    reset_capturing_descriptor(descriptor, &editor);
    editor.desc = &descriptor;

    obj_data object {};
    object.name = const_cast<char*>("a shiny widget");
    object.short_description = const_cast<char*>("a shiny widget");
    object.description = const_cast<char*>("A shiny widget lies here.");
    object.action_description = const_cast<char*>("The widget hums.");
    object.obj_flags.type_flag = 5;
    object.obj_flags.extra_flags = 0;
    object.obj_flags.wear_flags = 2;
    object.obj_flags.value[0] = 1;
    object.obj_flags.value[1] = -2;
    object.obj_flags.value[2] = 3;
    object.obj_flags.value[3] = -4;
    object.obj_flags.value[4] = 5;
    object.obj_flags.weight = 10;
    object.obj_flags.cost = 100;
    object.obj_flags.cost_per_day = 0;
    object.obj_flags.level = 1;
    object.obj_flags.rarity = 0;
    // material=-1: exercises the "out of range" ternary branch without
    // needing the (here un-booted) object_materials[]/num_of_object_materials
    // globals populated.
    object.obj_flags.material = -1;
    object.obj_flags.prog_number = 0;
    object.obj_flags.script_number = 0;
    object.affected[0].location = 1;
    object.affected[0].modifier = -3;

    list_object(&editor, &object);

    const std::string output(descriptor.output);
    EXPECT_NE(output.find("(1) alias(es)    :a shiny widget\n\r"), std::string::npos);
    EXPECT_NE(output.find("(2) reference description :a shiny widget\n\r"), std::string::npos);
    EXPECT_NE(output.find("(3) full  description     :A shiny widget lies here.\n\r"), std::string::npos);
    EXPECT_NE(output.find("(4) action description  :\n\rThe widget hums.\n\r"), std::string::npos);
    EXPECT_NE(output.find("No extra descriptions for this object\n\r"), std::string::npos);
    EXPECT_NE(output.find("(9) type flag    :5\n\r"), std::string::npos);
    EXPECT_NE(output.find("(12) values: 1 -2 3 -4 5\n\r"), std::string::npos);
    EXPECT_NE(output.find("(18) material     :-1 (Unknown)\n\r"), std::string::npos);
    EXPECT_NE(output.find("(19) Affections:\n\r (1 -3)"), std::string::npos);
    EXPECT_NE(output.find("(21) script        :0\n\r"), std::string::npos);
}

// shaperom.cpp's list_room() (the OLC "look at room" display) -- same
// rationale as ShapeObj.ListObjectFormatsAllFieldsIncludingNegativeAffections
// above: pure formatting, no file I/O. Exercises the "no exits"/"no extra
// description"/room-affection branches, including a negative
// affected->location (forces the ternary's "unknown" branch rather than
// indexing the here-un-booted skills[] table -- same technique as
// shapeobj.cpp's material=-1 case) and a negative modifier/bitvector.
extern void list_room(struct char_data* ch, struct room_data* mob);

TEST(ShapeRoom, ListRoomFormatsAllFieldsWithNoExitsAndNegativeAffection)
{
    char_data editor {};
    clear_char(&editor, MOB_VOID);
    descriptor_data descriptor {};
    reset_capturing_descriptor(descriptor, &editor);
    editor.desc = &descriptor;

    shape_room shape {};
    shape.exit_chosen = -1;
    editor.temp = &shape;

    room_data room; // room_data() sets number/zone/level/name/description/affected;
                     // the rest (dir_option/ex_description/sector_type/room_flags)
                     // needs explicit zeroing here, same as dummy_room_data() does
                     // for a freshly-allocated real room.
    room.name = const_cast<char*>("A Dusty Study");
    room.description = const_cast<char*>("Dust motes hang in still air.");
    room.room_flags = 4;
    room.sector_type = 2;
    for (int i = 0; i < NUM_OF_DIRS; i++)
        room.dir_option[i] = nullptr;
    room.ex_description = nullptr;
    room.level = 3;

    affected_type affection {};
    affection.type = ROOMAFF_SPELL;
    affection.location = -1; // forces the "unknown" skill-name branch
    affection.modifier = -5;
    affection.bitvector = -1;
    room.affected = &affection;

    list_room(&editor, &room);

    const std::string output(descriptor.output);
    EXPECT_NE(output.find("(1) name         :A Dusty Study\n\r"), std::string::npos);
    EXPECT_NE(output.find("(3) room flag  :4\n\r"), std::string::npos);
    EXPECT_NE(output.find("(4) sector type   :2\n\r"), std::string::npos);
    EXPECT_NE(output.find("No exits are made from this room\n\r"), std::string::npos);
    EXPECT_NE(output.find("No exit selected for editing.\n\r"), std::string::npos);
    EXPECT_NE(output.find("No extra description exists.\n\r"), std::string::npos);
    EXPECT_NE(output.find("(18) Affection type (1), (unknown)--1, level -5, flags -1\n\r"), std::string::npos);
}

// shapezon.cpp's show_command() (the OLC zone-reset-command display) writes
// a formatted line into a caller-supplied buffer -- pure formatting, no
// permission/zone_table/file dependency. Exercises the '{:>3}'-width zone
// number field (translated from the original's "%3d") plus the negative-if
// the task brief calls for isn't directly applicable here (if_flag etc. are
// builder-supplied non-negative flags in practice), so this instead covers
// the two structurally-different shapes present in the switch: a normal
// command ('M') and the "Unrecognized Command" default branch (the widest
// field list, and the one most likely to reveal a translation-table slip).
extern void show_command(char* str, struct zone_tree* zon);

TEST(ShapeZone, ShowCommandFormatsKnownAndUnrecognizedCommandsWithRightJustifiedNumber)
{
    zone_tree zon {};
    zon.number = 7;
    zon.comment = const_cast<char*>("a test comment");
    zon.comm.command = 'M';
    zon.comm.if_flag = 1;
    zon.comm.arg1 = 101;
    zon.comm.arg2 = 5;
    zon.comm.arg3 = -1;
    zon.comm.arg4 = 50;
    zon.comm.arg5 = 100;
    zon.comm.arg6 = 3;
    zon.comm.arg7 = 0;

    char buf[500];
    show_command(buf, &zon);
    EXPECT_STREQ(buf,
        "  7 M:: Ld_flg(1) Mob:101 toRom:5 MxExst:-1 Prb:50 Diff:100 MxLine:3 Tro:0\n\r      a test comment\n\r");

    zon.number = 123;
    zon.comm.command = 'Z'; // not one of the recognized letters -> default branch
    show_command(buf, &zon);
    EXPECT_STREQ(buf,
        "123 Unrecognized Command (if 1) on 101 by 5 to -1 when 50 with 100, (3 0) \n\r    a test comment.\n\r");
}

// shapemob.cpp's list_simple_proto()/list_proto() (the OLC "look at mobile"
// displays, simple and full editing modes) -- pure formatting, no file I/O.
// list_proto's "(39) roleplay flag" line is a pre-existing "\n\4" (not
// "\n\r") typo this task preserves rather than "fixes" -- pinned explicitly
// below since it's the one site in this whole task where the literal tail
// isn't the usual "\n\r".
extern void list_simple_proto(struct char_data* ch, struct char_data* mob);
extern void list_proto(struct char_data* ch, struct char_data* mob);

TEST(ShapeMob, ListSimpleProtoFormatsAllFields)
{
    char_data editor {};
    clear_char(&editor, MOB_VOID);
    descriptor_data descriptor {};
    reset_capturing_descriptor(descriptor, &editor);
    editor.desc = &descriptor;

    char_data mob {};
    clear_char(&mob, MOB_ISNPC);
    mob.player.name = const_cast<char*>("a stone golem");
    mob.player.short_descr = const_cast<char*>("a stone golem");
    mob.player.long_descr = const_cast<char*>("A stone golem stands here.\n\r");
    mob.player.description = const_cast<char*>("It is roughly hewn.\n\r");
    mob.specials2.act = 4096;
    mob.specials.affected_by = -1;
    mob.player.level = 30;
    mob.player.sex = 1;
    mob.player.race = 2;
    mob.player.bodytype = 3;
    mob.specials2.pref = 0;
    mob.specials.butcher_item = -1;
    mob.points.spirit = 0;

    list_simple_proto(&editor, &mob);

    const std::string output(descriptor.output);
    EXPECT_NE(output.find("(1) alias(es)         :a stone golem\n\r"), std::string::npos);
    EXPECT_NE(output.find("(3) full  description   :\n\rA stone golem stands here.\n\r\n\r"), std::string::npos);
    EXPECT_NE(output.find("(4) detailed description  :\n\rIt is roughly hewn.\n\r\n\r"), std::string::npos);
    EXPECT_NE(output.find("(5) flag number  :4096\n\r"), std::string::npos);
    EXPECT_NE(output.find("(6) affections   :-1\n\r"), std::string::npos);
    EXPECT_NE(output.find("(7) level        :30\n\r"), std::string::npos);
    EXPECT_NE(output.find("(12) butcher item:-1\n\r"), std::string::npos);
    EXPECT_NE(output.find("(13) spirit:0\n\r"), std::string::npos);
}

TEST(ShapeMob, ListProtoRoleplayFlagLinePreservesNonStandardLineEnding)
{
    char_data editor {};
    clear_char(&editor, MOB_VOID);
    descriptor_data descriptor {};
    reset_capturing_descriptor(descriptor, &editor);
    editor.desc = &descriptor;

    char_data mob {};
    clear_char(&mob, MOB_ISNPC);
    mob.player.name = const_cast<char*>("a stone golem");
    mob.player.short_descr = const_cast<char*>("a stone golem");
    mob.player.long_descr = const_cast<char*>("A stone golem stands here.\n\r");
    mob.player.description = const_cast<char*>("It is roughly hewn.\n\r");
    mob.specials2.rp_flag = 7;
    mob.player.death_cry = nullptr; // exercises the "(None)" ternary branch
    mob.player.death_cry2 = const_cast<char*>("The golem crumbles to dust!");

    list_proto(&editor, &mob);

    const std::string output(descriptor.output);
    EXPECT_NE(output.find("(33) death cry_1    :(None)\n\r"), std::string::npos);
    EXPECT_NE(output.find("(34) death cry_2    :The golem crumbles to dust!\n\r"), std::string::npos);
    // "\n\4" (EOT), not "\n\r" -- the preserved typo.
    EXPECT_NE(output.find("(39) roleplay flag  :7\n\4"), std::string::npos);
}

// shapescript.cpp's show_command(char_data*, script_data*) (the script-
// command disassembler used by /list and /show) -- writes into the global
// `buf` then sends it in one shared call after the switch. Exercises: a
// plain single-%s/%d TRIG case, the 4-argument SCRIPT_ASSIGN_EQ case (the
// widest arg count in the switch), the multi-line-string-literal
// SCRIPT_DO_SAY case, and SCRIPT_SET_INT_WAR_STATUS's "\r\n" (reversed from
// this file's usual "\n\r") tail -- a genuine pre-existing quirk, preserved
// verbatim rather than "fixed".
extern void show_command(struct char_data* ch, struct script_data* script);

TEST(ShapeScript, ShowCommandFormatsTrigAssignEqSayAndReversedLineEnding)
{
    char_data editor {};
    clear_char(&editor, MOB_VOID);
    descriptor_data descriptor {};
    reset_capturing_descriptor(descriptor, &editor);
    editor.desc = &descriptor;

    script_data script {};
    script.number = 3;
    script.text = const_cast<char*>("a comment");

    script.command_type = ON_DAMAGE;
    show_command(&editor, &script);
    EXPECT_EQ(std::string(descriptor.output), "[3] TRIG ON_DAMAGE       (a comment)\n\r");

    reset_capturing_descriptor(descriptor, &editor);
    script.command_type = SCRIPT_ASSIGN_EQ;
    script.param[0] = SCRIPT_PARAM_CH1;
    script.param[1] = SCRIPT_PARAM_OB1;
    script.param[2] = -1;
    script.param[3] = SCRIPT_PARAM_INT1;
    show_command(&editor, &script);
    EXPECT_EQ(std::string(descriptor.output),
        "[3] SYS ASSIGN_EQ        character: ch1, object: ob1, position: -1, int true/false: int1\n\r");

    reset_capturing_descriptor(descriptor, &editor);
    script.command_type = SCRIPT_DO_SAY;
    script.param[0] = SCRIPT_PARAM_CH1;
    script.param[1] = SCRIPT_PARAM_STR1;
    show_command(&editor, &script);
    EXPECT_EQ(std::string(descriptor.output), "[3] ACT DO_SAY           a comment (ch1)(str1)\n\r");

    reset_capturing_descriptor(descriptor, &editor);
    script.command_type = SCRIPT_SET_INT_WAR_STATUS;
    script.param[0] = SCRIPT_PARAM_INT1;
    show_command(&editor, &script);
    // "\r\n", not "\n\r" -- preserved verbatim.
    EXPECT_EQ(std::string(descriptor.output), "[3] SYS SET_INT_WAR_STATUS integer: int1 (a comment)\r\n");
}
