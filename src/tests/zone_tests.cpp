#include "../db.h"
#include "../zone.h"
#include "test_platform_compat.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <string>

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
// that happens to run later in the same gtest binary.
class ScopedZoneTable {
public:
    ScopedZoneTable()
        : m_previous_zone_table(zone_table)
        , m_previous_top_of_zone_table(top_of_zone_table)
    {
        zone_table = new zone_data[1] {};
    }

    ~ScopedZoneTable()
    {
        delete[] zone_table;
        zone_table = m_previous_zone_table;
        top_of_zone_table = m_previous_top_of_zone_table;
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
    // symbol/x/y/level, top, lifespan, reset_mode), then one 'M' (load
    // mobile) command. arg3 (max-existing, per the M:: print layout in
    // shapezon.cpp) carries the -1 sentinel under test; the surrounding
    // fields are all distinct in-range values so a field-order mixup would
    // also fail this test, not just the sentinel itself.
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
        "M 0 30 1200 -1 100 100 5 3 test mob load, -1 = no max-existing cap\n"
        "S\n");

    FILE* zone_file = fopen(zone_file_path.c_str(), "r");
    ASSERT_NE(zone_file, nullptr);
    load_zones(zone_file);
    fclose(zone_file);

    ASSERT_EQ(zone_table[0].cmdno, 1);
    EXPECT_EQ(zone_table[0].cmd[0].command, 'M');
    EXPECT_EQ(zone_table[0].cmd[0].if_flag, 0);
    EXPECT_EQ(zone_table[0].cmd[0].arg1, 30);
    EXPECT_EQ(zone_table[0].cmd[0].arg2, 1200);
    EXPECT_EQ(zone_table[0].cmd[0].arg3, -1); // pre-fix: 65535
    EXPECT_EQ(zone_table[0].cmd[0].arg4, 100);
    EXPECT_EQ(zone_table[0].cmd[0].arg5, 100);
    EXPECT_EQ(zone_table[0].cmd[0].arg6, 5);
    EXPECT_EQ(zone_table[0].cmd[0].arg7, 3);
}
