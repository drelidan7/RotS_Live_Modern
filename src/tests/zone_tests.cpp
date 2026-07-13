#include "../db.h"
#include "../utils.h"
#include "../zone.h"
#include "test_platform_compat.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <string>

// TESTING seam in zone.cpp (same convention as fight.cpp's
// reset_perform_violence_timing_for_testing): resets the static cursor
// load_zones() advances on every call, so each test starts filling
// zone_table at slot 0 instead of wherever a previous load_zones() call
// (same test under --gtest_repeat, or another test) left it.
void reset_zone_load_cursor_for_testing();

namespace {

void write_file(const std::string& path, const std::string& contents)
{
    FILE* file = fopen(path.c_str(), "wb");
    ASSERT_NE(file, nullptr);
    ASSERT_EQ(fwrite(contents.data(), sizeof(char), contents.size(), file), contents.size());
    ASSERT_EQ(fclose(file), 0);
}

class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        char path_template[] = "/tmp/rots-zone-XXXXXX";
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

// RAII fixture: gives load_zones() one fresh zone_table slot to write into
// and restores the previous global zone_table/top_of_zone_table afterward,
// so this test doesn't leak process-global zone state into any other test
// that happens to run later in the same gtest binary. Also resets zone.cpp's
// static zone_load_cursor on BOTH construction and destruction: load_zones()
// increments it every call, and without the reset a second run of any
// load_zones()-calling test (--gtest_repeat, or a future sibling test) would
// index past this fixture's 1-element array — a heap overflow, not just
// stale state.
class ScopedZoneTable {
public:
    ScopedZoneTable()
        : m_previous_zone_table(zone_table)
        , m_previous_top_of_zone_table(top_of_zone_table)
    {
        reset_zone_load_cursor_for_testing();
        zone_table = new zone_data[1] {};
    }

    ~ScopedZoneTable()
    {
        // Releases each populated entry's load_zones()-allocated fields
        // (name/description/map via fread_string(), cmd via CREATE(), owners
        // via a CREATE1()'d linked list) before the array itself is deleted --
        // matches the per-field RELEASE() idiom shapezon.cpp's zone editor
        // already uses when replacing a zone_table entry's contents (Phase 5
        // T6 leak sweep). The fixture's own constructor only ever allocates
        // a single slot (`new zone_data[1]`), so top_of_zone_table (0 if
        // load_zones() populated it, unmodified otherwise) is exactly the
        // valid index range to walk here.
        if (top_of_zone_table >= 0) {
            RELEASE(zone_table[0].name);
            RELEASE(zone_table[0].description);
            RELEASE(zone_table[0].map);
            RELEASE(zone_table[0].cmd);
            struct owner_list* owner = zone_table[0].owners;
            while (owner) {
                struct owner_list* next_owner = owner->next;
                RELEASE(owner);
                owner = next_owner;
            }
        }
        delete[] zone_table;
        zone_table = m_previous_zone_table;
        top_of_zone_table = m_previous_top_of_zone_table;
        reset_zone_load_cursor_for_testing();
    }

private:
    // Snapshot of the real (or previously-set) global table/top-index, restored
    // on destruction so this fixture's substitute table can't outlive the test.
    zone_data* m_previous_zone_table;
    int m_previous_top_of_zone_table;
};

} // namespace

// Pins that a zone-command argument written as "-1" in a .zon file (the
// world-builder sentinel meaning "no cap" / "none" / NOWHERE, depending on
// the field -- see the census in docs/superpowers/sdd reports for the real
// occurrences, which land in the K/P/L commands' object- and room-vnum
// slots) round-trips through load_zones() as the signed value -1, not the
// unsigned-truncation artifact 65535.
//
// Before the fix, zone.cpp's load_zones() read these fields with fscanf's
// "%hd" directly into `int*` destinations (struct reset_com's if_flag/
// arg1..arg7 are all declared `int`, not `short`, in db.h). fscanf("%hd", ...)
// only ever writes the pointed-at location's low 2 bytes, so a caller-
// supplied `int*` gets treated as a `short*`: on this little-endian target,
// with the field already zeroed, writing the 16-bit pattern for -1 leaves
// the high 2 bytes at 0, producing 0x0000FFFF == 65535 in the full 32-bit
// int -- this test's EXPECT_EQ(..., -1) FAILS pre-fix (reads 65535) and
// PASSES post-fix (%hd -> %d, matching the already-%d write side in
// shapezon.cpp and the declared field type).
TEST(ZoneLoad, NegativeOneArgumentParsesAsSignedNegativeOne)
{
    ScopedZoneTable zone_table_guard;
    TemporaryDirectory temp_directory;

    const std::string zone_file_path = temp_directory.path() + "/1.zon";
    // Minimal zone-file snippet covering every header field load_zones reads
    // before the command list (name/description/map, an empty owner list,
    // symbol/x/y/level, top, lifespan, reset_mode), then one command per
    // fixed fscanf call site so all three are pinned:
    //   - 'M' (load mobile): call site 1 (the always-read if_flag/arg1..arg5
    //     block) via arg3 = -1 (max-existing, per the M:: print layout in
    //     shapezon.cpp), AND call site 2 (the M/N/X/H/E/K/Q arg6/arg7 read)
    //     via arg7 = -1 (the census found real negative arg6/arg7 values in
    //     113.zon/116.zon K commands, same read path).
    //   - 'P' (put object in object): call site 3 (the P-only arg6 read) via
    //     arg6 = -1; its arg1/arg3 = -1 mirror the census's real
    //     "P 1 -1 7628 -1 0 100 2" lines (NOWHERE room / no sub-container).
    // The surrounding fields are all distinct in-range values so a
    // field-order mixup would also fail this test, not just the sentinels.
    write_file(zone_file_path,
        "#1\n"
        "TestZone~\n"
        "Test zone for the -1 sentinel parse regression.~\n"
        "~\n"
        "0\n"
        "X 0 0 0\n"
        "5\n"
        "10\n"
        "2\n"
        "M 0 30 1200 -1 100 100 5 -1 test mob load, -1 = no cap / no trophy line\n"
        "P 1 -1 7628 -1 0 100 -1 put paper in last_obj pouch, -1 = no max-in-obj\n"
        "S\n");

    FILE* zone_file = fopen(zone_file_path.c_str(), "r");
    ASSERT_NE(zone_file, nullptr);
    load_zones(zone_file);
    fclose(zone_file);

    ASSERT_EQ(zone_table[0].cmdno, 2);

    // Call sites 1 + 2 ('M'): base block and the two-extra-args read.
    EXPECT_EQ(zone_table[0].cmd[0].command, 'M');
    EXPECT_EQ(zone_table[0].cmd[0].if_flag, 0);
    EXPECT_EQ(zone_table[0].cmd[0].arg1, 30);
    EXPECT_EQ(zone_table[0].cmd[0].arg2, 1200);
    EXPECT_EQ(zone_table[0].cmd[0].arg3, -1); // call site 1; pre-fix: 65535
    EXPECT_EQ(zone_table[0].cmd[0].arg4, 100);
    EXPECT_EQ(zone_table[0].cmd[0].arg5, 100);
    EXPECT_EQ(zone_table[0].cmd[0].arg6, 5);
    EXPECT_EQ(zone_table[0].cmd[0].arg7, -1); // call site 2; pre-fix: 65535

    // Call sites 1 + 3 ('P'): base block again and the one-extra-arg read.
    EXPECT_EQ(zone_table[0].cmd[1].command, 'P');
    EXPECT_EQ(zone_table[0].cmd[1].if_flag, 1);
    EXPECT_EQ(zone_table[0].cmd[1].arg1, -1); // call site 1; pre-fix: 65535
    EXPECT_EQ(zone_table[0].cmd[1].arg2, 7628);
    EXPECT_EQ(zone_table[0].cmd[1].arg3, -1); // call site 1; pre-fix: 65535
    EXPECT_EQ(zone_table[0].cmd[1].arg4, 0);
    EXPECT_EQ(zone_table[0].cmd[1].arg5, 100);
    EXPECT_EQ(zone_table[0].cmd[1].arg6, -1); // call site 3; pre-fix: 65535
}
