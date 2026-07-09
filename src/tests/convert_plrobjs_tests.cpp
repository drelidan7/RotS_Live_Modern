#include "../convert_plrobjs.h"
#include "../objects_json.h"
#include "legacy_rent_fixture.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

// TDD coverage for the plrobjs conversion sweep (Phase 2a Task 3). Uses
// legacy_rent_fixture.h's byte builders (shared with objects_json_layout_tests.cpp)
// to synthesize legacy .obj files on disk, exactly the way Task 1's decoder
// characterization tests do, but here feeding real files through
// convert_all_legacy_plrobjs instead of calling the decoder directly.

namespace {

// Sandboxes each test in a scratch directory under /tmp (removed on
// destruction), the same TemporaryDirectory pattern already used by
// account_management_tests.cpp / db_loader_tests.cpp / objsave_json_tests.cpp
// / act_wiz_tests.cpp / interpre_account_menu_tests.cpp for filesystem
// fixtures -- convert_all_legacy_plrobjs takes an explicit root path, so no
// chdir is needed here (unlike objsave_json_tests.cpp, which writes via a
// cwd-relative path).
class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        char path_template[] = "/tmp/rots-convert-plrobjs-XXXXXX";
        char* created_path = mkdtemp(path_template);
        EXPECT_NE(created_path, nullptr);
        if (created_path)
            m_path = created_path;
    }

    ~TemporaryDirectory()
    {
        if (!m_path.empty()) {
            const std::string command = "rm -rf '" + m_path + "'";
            std::system(command.c_str());
        }
    }

    const std::string& path() const { return m_path; }

private:
    std::string m_path;
};

bool file_exists(const std::string& path)
{
    return std::filesystem::exists(path.c_str());
}

void write_file(const std::string& path, const std::string& contents)
{
    std::ofstream out(path, std::ios::binary);
    ASSERT_TRUE(out.good()) << "failed to open " << path << " for write";
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    ASSERT_TRUE(out.good()) << "failed writing " << path;
}

std::string read_file(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream contents;
    contents << in.rdbuf();
    return contents.str();
}

} // namespace

TEST(ConvertPlrobjs, ConvertsValidFileAndSkipsCorruptFileLeavingItUntouched)
{
    if (sizeof(long) != 4)
        GTEST_SKIP() << "legacy fixtures encode the 32-bit ABI; run in the i386 container";

    TemporaryDirectory root;

    const std::string valid_bytes = legacy_rent_fixture::build_full_fixture_bytes();
    const std::string valid_path = root.path() + "/amanya.obj";
    write_file(valid_path, valid_bytes);

    // Truncated well before even a full rent_info header -- guaranteed decode
    // failure, distinct from the "missing follower section" tolerance case
    // objects_json_layout_tests.cpp already covers.
    const std::string corrupt_bytes = valid_bytes.substr(0, 10);
    const std::string corrupt_path = root.path() + "/corrupt.obj";
    write_file(corrupt_path, corrupt_bytes);

    std::string report;
    const int converted = convert_all_legacy_plrobjs(root.path().c_str(), /*delete_after=*/false, &report);

    EXPECT_EQ(1, converted);

    // Valid file: legacy renamed to .migrated (never deleted), JSON written,
    // and the JSON decodes back to exactly what the legacy bytes decoded to.
    EXPECT_FALSE(file_exists(valid_path)) << "legacy .obj should have been renamed away";
    EXPECT_TRUE(file_exists(valid_path + ".migrated"));
    const std::string json_path = root.path() + "/amanya.objs.json";
    ASSERT_TRUE(file_exists(json_path));

    objects_json::ObjectSaveData original_decode;
    std::string decode_error;
    bool accepted_missing_follower_section = false;
    ASSERT_TRUE(objects_json::legacy_object_save_data_from_binary(valid_bytes, &original_decode, &accepted_missing_follower_section, &decode_error)) << decode_error;

    objects_json::ObjectSaveData from_json;
    std::string json_decode_error;
    ASSERT_TRUE(objects_json::deserialize_objects_from_json(read_file(json_path), &from_json, &json_decode_error)) << json_decode_error;
    EXPECT_TRUE(objects_json::object_save_data_equal(original_decode, from_json));

    // Corrupt file: left exactly as found -- not renamed, not deleted, no
    // JSON counterpart produced -- and named in the report as a skip.
    ASSERT_TRUE(file_exists(corrupt_path));
    EXPECT_EQ(corrupt_bytes, read_file(corrupt_path));
    EXPECT_FALSE(file_exists(corrupt_path + ".migrated"));
    EXPECT_FALSE(file_exists(root.path() + "/corrupt.objs.json"));

    EXPECT_NE(report.find("CONVERTED"), std::string::npos);
    EXPECT_NE(report.find("amanya.obj"), std::string::npos);
    EXPECT_NE(report.find("SKIP"), std::string::npos);
    EXPECT_NE(report.find("corrupt.obj"), std::string::npos);
}

TEST(ConvertPlrobjs, RecursesIntoBucketSubdirectoriesLikeRealPlrobjsLayout)
{
    if (sizeof(long) != 4)
        GTEST_SKIP() << "legacy fixtures encode the 32-bit ABI; run in the i386 container";

    TemporaryDirectory root;

    // Mirrors the real lib/plrobjs/ layout: bucket subdirectories one level
    // down, plus some files directly at the root (lib/plrobjs/*.obj in the
    // live tree has exactly this shape).
    ASSERT_TRUE(std::filesystem::create_directory((root.path() + "/A-E").c_str()));
    const std::string bucketed_path = root.path() + "/A-E/aramir.obj";
    write_file(bucketed_path, legacy_rent_fixture::build_full_fixture_bytes());

    const std::string root_level_path = root.path() + "/loose.obj";
    write_file(root_level_path, legacy_rent_fixture::build_full_fixture_bytes());

    std::string report;
    const int converted = convert_all_legacy_plrobjs(root.path().c_str(), /*delete_after=*/false, &report);

    EXPECT_EQ(2, converted);
    EXPECT_TRUE(file_exists(bucketed_path + ".migrated"));
    EXPECT_TRUE(file_exists(root.path() + "/A-E/aramir.objs.json"));
    EXPECT_TRUE(file_exists(root_level_path + ".migrated"));
    EXPECT_TRUE(file_exists(root.path() + "/loose.objs.json"));
}

TEST(ConvertPlrobjs, DeleteAfterDefaultsFalseAndOnlyRemovesMigratedFilesWhenRequested)
{
    if (sizeof(long) != 4)
        GTEST_SKIP() << "legacy fixtures encode the 32-bit ABI; run in the i386 container";

    {
        TemporaryDirectory root;
        const std::string obj_path = root.path() + "/keepsit.obj";
        write_file(obj_path, legacy_rent_fixture::build_full_fixture_bytes());

        std::string report;
        const int converted = convert_all_legacy_plrobjs(root.path().c_str(), /*delete_after=*/false, &report);
        EXPECT_EQ(1, converted);
        EXPECT_TRUE(file_exists(obj_path + ".migrated")) << "default delete_after=false must leave .obj.migrated in place";
    }

    {
        TemporaryDirectory root;
        const std::string obj_path = root.path() + "/removesit.obj";
        write_file(obj_path, legacy_rent_fixture::build_full_fixture_bytes());

        std::string report;
        const int converted = convert_all_legacy_plrobjs(root.path().c_str(), /*delete_after=*/true, &report);
        EXPECT_EQ(1, converted);
        EXPECT_FALSE(file_exists(obj_path + ".migrated")) << "delete_after=true must remove the .obj.migrated it just created";
        EXPECT_TRUE(file_exists(root.path() + "/removesit.objs.json")) << "the converted JSON itself must survive delete_after";
        EXPECT_NE(report.find("DELETED"), std::string::npos);
    }
}

// Corrupt Legacy File Recovery (2026-07-07), Step 1 fixture matrix, rent
// files: (a) truncated mid-record -- salvage the header plus every complete
// leading object record, drop the trailing partial one.
TEST(ConvertPlrobjs, RecoverySalvagesHeaderAndCompleteLeadingObjectRecordsWhenTruncatedMidRecord)
{
    if (sizeof(long) != 4)
        GTEST_SKIP() << "legacy fixtures encode the 32-bit ABI; run in the i386 container";

    TemporaryDirectory root;

    const std::string full_bytes = legacy_rent_fixture::build_full_fixture_bytes();
    // rent header (48) + 2 real items (56 each) + 30 of the would-be
    // sentinel record's 56 bytes -- a genuine truncated-mid-record cut,
    // never reaching the sentinel that would end the object list cleanly.
    const size_t truncated_length = sizeof(rent_info) + 2 * sizeof(obj_file_elem) + 30;
    ASSERT_LT(truncated_length, full_bytes.size());
    const std::string truncated_bytes = full_bytes.substr(0, truncated_length);

    const std::string legacy_path = root.path() + "/midrecord.obj";
    write_file(legacy_path, truncated_bytes);

    // Sanity: strict conversion must reject this file untouched -- otherwise
    // this fixture isn't exercising the recovery path.
    std::string strict_report;
    EXPECT_EQ(0, convert_all_legacy_plrobjs(root.path().c_str(), /*delete_after=*/false, &strict_report));
    ASSERT_TRUE(file_exists(legacy_path));

    std::string report;
    const int salvaged = recover_all_legacy_plrobjs(root.path().c_str(), &report);

    EXPECT_EQ(1, salvaged);
    EXPECT_FALSE(file_exists(legacy_path)) << "legacy .obj should have been renamed away";
    const std::string salvaged_from_path = legacy_path + ".salvaged-from";
    ASSERT_TRUE(file_exists(salvaged_from_path)) << "original must be preserved, never deleted";
    EXPECT_EQ(truncated_bytes, read_file(salvaged_from_path)) << "the preserved original must be byte-identical to what was on disk";

    const std::string json_path = root.path() + "/midrecord.objs.json";
    ASSERT_TRUE(file_exists(json_path));

    objects_json::ObjectSaveData salvaged_data;
    std::string decode_error;
    ASSERT_TRUE(objects_json::deserialize_objects_from_json(read_file(json_path), &salvaged_data, &decode_error)) << decode_error;

    EXPECT_EQ(1234567890, salvaged_data.rent.time);
    ASSERT_EQ(2u, salvaged_data.objects.size()) << "both complete leading records must be kept";
    legacy_rent_fixture::expect_object_record_matches(salvaged_data.objects[0], 3001, WEAR_HEAD);
    legacy_rent_fixture::expect_object_record_matches(salvaged_data.objects[1], 3002, MAX_WEAR);

    // The object list never reached a sentinel, so nothing after it is
    // salvageable -- board/alias/follower sections must all be empty/default,
    // not half-included.
    for (int index = 0; index < MAX_MAXBOARD; ++index)
        EXPECT_EQ(0, salvaged_data.board_points[index]);
    EXPECT_TRUE(salvaged_data.aliases.empty());
    EXPECT_TRUE(salvaged_data.followers.empty());

    EXPECT_NE(report.find("SALVAGED"), std::string::npos);
    EXPECT_NE(report.find("2 top-level object(s)"), std::string::npos);
    EXPECT_NE(report.find("1 partial trailing object record(s) dropped"), std::string::npos);
}

// (b) empty file: nothing to salvage -- report-only, untouched.
TEST(ConvertPlrobjs, RecoveryLeavesEmptyFileUntouchedAndReportsIt)
{
    TemporaryDirectory root;

    const std::string legacy_path = root.path() + "/empty.obj";
    write_file(legacy_path, "");

    std::string report;
    const int salvaged = recover_all_legacy_plrobjs(root.path().c_str(), &report);

    EXPECT_EQ(0, salvaged);
    ASSERT_TRUE(file_exists(legacy_path));
    EXPECT_EQ("", read_file(legacy_path));
    EXPECT_FALSE(file_exists(legacy_path + ".salvaged-from"));
    EXPECT_FALSE(file_exists(root.path() + "/empty.objs.json"));
    EXPECT_NE(report.find("UNSALVAGEABLE"), std::string::npos);
    EXPECT_NE(report.find("empty file"), std::string::npos);
}

// (c) garbage header: fewer than 48 bytes present at all -- "salvage
// requires at least a valid rent header" -- untouched, reported (distinct
// message from the empty-file case).
TEST(ConvertPlrobjs, RecoveryLeavesGarbageHeaderUntouchedAndReportsIt)
{
    TemporaryDirectory root;

    const std::string garbage_bytes(10, '\xAB'); // nonzero, but far short of the 48-byte rent_info header
    const std::string legacy_path = root.path() + "/garbageheader.obj";
    write_file(legacy_path, garbage_bytes);

    std::string report;
    const int salvaged = recover_all_legacy_plrobjs(root.path().c_str(), &report);

    EXPECT_EQ(0, salvaged);
    ASSERT_TRUE(file_exists(legacy_path));
    EXPECT_EQ(garbage_bytes, read_file(legacy_path));
    EXPECT_FALSE(file_exists(legacy_path + ".salvaged-from"));
    EXPECT_FALSE(file_exists(root.path() + "/garbageheader.objs.json"));
    EXPECT_NE(report.find("UNSALVAGEABLE"), std::string::npos);
    EXPECT_NE(report.find("No valid rent header"), std::string::npos);
}

// (d) valid file: recovery mode must REFUSE it -- recovery only ever runs on
// files strict conversion rejects, so lossless conversion can never degrade
// to lossy salvage. This is THE load-bearing invariant for this feature.
TEST(ConvertPlrobjs, RecoveryRefusesFileThatStrictConversionWouldAlreadyAccept)
{
    if (sizeof(long) != 4)
        GTEST_SKIP() << "legacy fixtures encode the 32-bit ABI; run in the i386 container";

    TemporaryDirectory root;

    const std::string valid_bytes = legacy_rent_fixture::build_full_fixture_bytes();
    const std::string legacy_path = root.path() + "/wholesome.obj";
    write_file(legacy_path, valid_bytes);

    std::string report;
    const int salvaged = recover_all_legacy_plrobjs(root.path().c_str(), &report);

    EXPECT_EQ(0, salvaged);
    ASSERT_TRUE(file_exists(legacy_path)) << "recovery must never touch a file strict conversion would accept";
    EXPECT_EQ(valid_bytes, read_file(legacy_path));
    EXPECT_FALSE(file_exists(legacy_path + ".salvaged-from"));
    EXPECT_FALSE(file_exists(root.path() + "/wholesome.objs.json"));
    EXPECT_NE(report.find("REFUSED"), std::string::npos);
    EXPECT_NE(report.find("wholesome.obj"), std::string::npos);
}

// Board+alias sections are fully intact (and the alias section's no-NUL
// 20-byte keyword is sanitized per the locked policy) even though the
// follower section trails off mid-header -- "sections included only if
// fully intact" is per-section, not all-or-nothing for the whole file.
TEST(ConvertPlrobjs, RecoveryKeepsIntactBoardAndSanitizedAliasSectionsWhileDroppingIncompleteFollowerSection)
{
    if (sizeof(long) != 4)
        GTEST_SKIP() << "legacy fixtures encode the 32-bit ABI; run in the i386 container";

    TemporaryDirectory root;

    const std::string full_length_keyword(20, 'k'); // exactly 20 bytes, no NUL anywhere
    const std::string full_bytes = legacy_rent_fixture::build_full_fixture_bytes_with_alias(full_length_keyword, "kill orc");

    // Keep everything through the alias section's terminator, then replace
    // the (complete) follower section with a short garbage tail -- too few
    // bytes for even one follower_file_elem header. Strict conversion's
    // "missing follower section" tolerance only covers EOF landing exactly
    // at the start of the follower section (zero trailing bytes); a nonzero
    // but incomplete trailing chunk is a genuine decode failure for strict.
    const size_t prefix_length = sizeof(rent_info) + 3 * sizeof(obj_file_elem)
        + static_cast<size_t>(MAX_MAXBOARD) * sizeof(sh_int) + 20 + sizeof(int) + 8 /* "kill orc" */ + 20;
    ASSERT_LT(prefix_length, full_bytes.size());
    const std::string truncated_bytes = full_bytes.substr(0, prefix_length) + std::string(10, '\x7F');

    const std::string legacy_path = root.path() + "/partialfollower.obj";
    write_file(legacy_path, truncated_bytes);

    std::string strict_report;
    EXPECT_EQ(0, convert_all_legacy_plrobjs(root.path().c_str(), /*delete_after=*/false, &strict_report));
    ASSERT_TRUE(file_exists(legacy_path));

    std::string report;
    const int salvaged = recover_all_legacy_plrobjs(root.path().c_str(), &report);

    EXPECT_EQ(1, salvaged);
    const std::string json_path = root.path() + "/partialfollower.objs.json";
    ASSERT_TRUE(file_exists(json_path));

    objects_json::ObjectSaveData salvaged_data;
    std::string decode_error;
    ASSERT_TRUE(objects_json::deserialize_objects_from_json(read_file(json_path), &salvaged_data, &decode_error)) << decode_error;

    ASSERT_EQ(2u, salvaged_data.objects.size());
    for (int index = 0; index < MAX_MAXBOARD; ++index)
        EXPECT_EQ(100 + index, salvaged_data.board_points[index]);

    ASSERT_EQ(1u, salvaged_data.aliases.size());
    EXPECT_EQ(std::string(19, 'k'), salvaged_data.aliases[0].keyword) << "20-byte no-NUL keyword sanitized to the 19-byte printable prefix";
    EXPECT_EQ("kill orc", salvaged_data.aliases[0].command);

    EXPECT_TRUE(salvaged_data.followers.empty()) << "the incomplete follower section must be dropped wholesale, not half-included";

    EXPECT_NE(report.find("1 alias(es)"), std::string::npos);
    EXPECT_NE(report.find("0 follower(s)"), std::string::npos);
}

TEST(ConvertPlrobjs, IgnoresNonObjFilesAndEmptyRoot)
{
    TemporaryDirectory root;
    write_file(root.path() + "/somebody.bak", "not a rent file");
    write_file(root.path() + "/somebody.cheat", "not a rent file either");

    std::string report;
    const int converted = convert_all_legacy_plrobjs(root.path().c_str(), /*delete_after=*/false, &report);

    EXPECT_EQ(0, converted);
    EXPECT_TRUE(file_exists(root.path() + "/somebody.bak"));
    EXPECT_TRUE(file_exists(root.path() + "/somebody.cheat"));
}
