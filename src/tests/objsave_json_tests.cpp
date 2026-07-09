#include "../objects_json.h"
#include "test_platform_compat.h"
#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits.h>
#include <string>

// Declared in objsave.cpp (new in this task): the single JSON serialization
// point (temp-file + rename atomicity) and the shared bucket-path helper it
// uses to locate <name>.objs.json.
bool write_player_objects_json(const char* player_name, const objects_json::ObjectSaveData& data, std::string* error);
std::string player_objects_json_path(const char* player_name);

namespace {

// The writer resolves paths relative to the process cwd (mirroring the
// legacy .obj writer), so these tests sandbox themselves in a scratch
// directory with the one bucket ("F-J", for the "json*char" test names used
// below) pre-created, the same way db_loader_tests.cpp/
// interpre_account_menu_tests.cpp already do for plrobjs/accounts fixtures.
class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        char path_template[] = "/tmp/rots-objsave-json-XXXXXX";
        char* created_path = rots_mkdtemp(path_template);
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

class ScopedWorkingDirectory {
public:
    // std::filesystem::current_path() is both the getter and (with a path argument)
    // the setter -- a direct, portable stand-in for the getcwd()/chdir() pair (Phase 3
    // Task 5/6: POSIX-ism cleanup for MSVC bring-up; <unistd.h> -- getcwd()/chdir()'s
    // POSIX home -- doesn't exist on Windows).
    explicit ScopedWorkingDirectory(const std::string& path)
    {
        std::error_code ec;
        m_original_path = std::filesystem::current_path(ec);
        EXPECT_FALSE(ec) << "Expected current_path() to report this test process's working directory.";

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

objects_json::ObjectSaveData make_save_data()
{
    objects_json::ObjectSaveData data;
    data.rent.time = 1700000000;
    data.rent.rentcode = 1;
    data.rent.nitems = 1;
    objects_json::ObjectRecord record {};
    record.item_number = 3001;
    record.wear_pos = 5;
    record.bitvector = 0x1234;
    data.objects.push_back(record);
    return data;
}

} // namespace

TEST(ObjsaveJson, WritesJsonFileAndRoundTrips)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ASSERT_TRUE(std::filesystem::create_directory("plrobjs"));
    ASSERT_TRUE(std::filesystem::create_directory("plrobjs/F-J"));

    objects_json::ObjectSaveData data = make_save_data();
    std::string error;
    ASSERT_TRUE(write_player_objects_json("jsontestchar", data, &error)) << error;

    std::string path = player_objects_json_path("jsontestchar");
    std::ifstream in(path.c_str());
    ASSERT_TRUE(in.good()) << "expected JSON file at " << path;
    std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    objects_json::ObjectSaveData parsed;
    ASSERT_TRUE(objects_json::deserialize_objects_from_json(contents, &parsed, &error)) << error;
    EXPECT_EQ(data.rent.time, parsed.rent.time);
    ASSERT_EQ(1u, parsed.objects.size());
    EXPECT_EQ(3001, parsed.objects[0].item_number);

    // No temp file left behind after a successful rename.
    std::ifstream leftover_temp((path + ".tmp").c_str());
    EXPECT_FALSE(leftover_temp.good());
}

TEST(ObjsaveJson, PathHelperUsesBucketConventionAndJsonExtension)
{
    // Same first-letter bucket scheme as the legacy .obj writer (Crash_get_filename),
    // but with the new .objs.json extension and no dependency on a live character.
    EXPECT_EQ("plrobjs/A-E/alice.objs.json", player_objects_json_path("Alice"));
    EXPECT_EQ("plrobjs/F-J/frodo.objs.json", player_objects_json_path("FRODO"));
    EXPECT_EQ("plrobjs/K-O/nazgul.objs.json", player_objects_json_path("nazgul"));
    EXPECT_EQ("plrobjs/P-T/sauron.objs.json", player_objects_json_path("Sauron"));
    EXPECT_EQ("plrobjs/U-Z/witchking.objs.json", player_objects_json_path("witchking"));
}

TEST(ObjsaveJson, OverwriteReplacesPreviousContentAtomically)
{
    TemporaryDirectory temp_directory;
    ScopedWorkingDirectory working_directory(temp_directory.path());
    ASSERT_TRUE(std::filesystem::create_directory("plrobjs"));
    ASSERT_TRUE(std::filesystem::create_directory("plrobjs/F-J"));

    objects_json::ObjectSaveData first = make_save_data();
    std::string error;
    ASSERT_TRUE(write_player_objects_json("jsonoverwritechar", first, &error)) << error;

    objects_json::ObjectSaveData second = make_save_data();
    second.rent.time = 1800000000;
    second.objects.clear();
    ASSERT_TRUE(write_player_objects_json("jsonoverwritechar", second, &error)) << error;

    std::string path = player_objects_json_path("jsonoverwritechar");
    std::ifstream in(path.c_str());
    ASSERT_TRUE(in.good());
    std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    objects_json::ObjectSaveData parsed;
    ASSERT_TRUE(objects_json::deserialize_objects_from_json(contents, &parsed, &error)) << error;
    EXPECT_EQ(1800000000, parsed.rent.time);
    EXPECT_EQ(0u, parsed.objects.size());
}
