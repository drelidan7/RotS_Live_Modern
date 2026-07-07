#include "../convert_plrobjs.h"
#include "../objects_json.h"
#include "legacy_rent_fixture.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>

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
    struct stat info { };
    return stat(path.c_str(), &info) == 0;
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
    TemporaryDirectory root;

    // Mirrors the real lib/plrobjs/ layout: bucket subdirectories one level
    // down, plus some files directly at the root (lib/plrobjs/*.obj in the
    // live tree has exactly this shape).
    ASSERT_EQ(0, mkdir((root.path() + "/A-E").c_str(), 0755));
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
