#include "../player_file_finalize.h"
#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <stdio.h>
#include <string>
#include <string_view>

namespace {

void write_file(const char* path, const char* content) {
    // "wb": the byte-identical assertions below compare against exactly the bytes
    // written here; text mode on Windows would expand "\n" to "\r\n" (Phase 3 Task 6).
    FILE* f = fopen(path, "wb");
    ASSERT_NE(f, nullptr);
    fputs(content, f);
    fclose(f);
}

std::string read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        return std::string("<<missing:") + path + ">>";
    }
    std::string out;
    char buf[1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        out.append(buf, n);
    }
    fclose(f);
    return out;
}

// Portable stand-in for the old opendir/readdir/closedir loop (Phase 3 Task 5:
// POSIX-ism cleanup for MSVC bring-up) -- std::filesystem::directory_iterator walks
// the directory on every platform; error_code overload keeps a missing directory a
// -1 return (matching the historical opendir()==nullptr short-circuit) instead of a
// thrown exception.
int count_files(const char* dir) {
    std::error_code ec;
    std::filesystem::directory_iterator it(dir, ec);
    if (ec) {
        return -1;
    }
    int count = 0;
    for (const auto& entry : it) {
        if (entry.path().filename().string()[0] != '.') {
            count++;
        }
    }
    return count;
}

} // namespace

// Both finalizers must: (1) write byte-identical output, (2) delete every stale "<base>."
// file via the dot-anchored glob while leaving a different player's "<base>name.*" file
// untouched (the bob. vs bobby. guard), and (3) honor move-vs-copy (rename consumes its
// scratch; cp leaves it).
TEST(PlayerFinalize, ByteIdenticalAndSingleFile) {
    const char* legacy_dir = "pf_test_legacy";
    const char* new_dir = "pf_test_new";
    std::filesystem::create_directory(legacy_dir);
    std::filesystem::create_directory(new_dir);

    write_file("pf_test_legacy/probe.stale", "OLD");
    write_file("pf_test_new/probe.stale", "OLD");
    write_file("pf_test_legacy/probe.42.1.99.0.0", "OLD2");
    write_file("pf_test_new/probe.42.1.99.0.0", "OLD2");
    write_file("pf_test_legacy/probename.7.1.124.0.0", "KEEP");
    write_file("pf_test_new/probename.7.1.124.0.0", "KEEP");

    write_file("pf_test_legacy_scratch", "PLAYER-BYTES-V1\n");
    write_file("pf_test_new_scratch", "PLAYER-BYTES-V1\n");

    bool ok_legacy = finalize_player_file_legacy("pf_test_legacy_scratch", "pf_test_legacy/probe",
                                                 "pf_test_legacy/probe.50.1.123.0.0");
    bool ok_new = finalize_player_file_rename("pf_test_new_scratch", "pf_test_new", "probe",
                                              "pf_test_new/probe.50.1.123.0.0");
    EXPECT_TRUE(ok_legacy);
    EXPECT_TRUE(ok_new);

    EXPECT_EQ(read_file("pf_test_new/probe.50.1.123.0.0"), "PLAYER-BYTES-V1\n");
    EXPECT_EQ(read_file("pf_test_legacy/probe.50.1.123.0.0"),
              read_file("pf_test_new/probe.50.1.123.0.0"));

    EXPECT_FALSE(std::filesystem::exists("pf_test_legacy/probe.stale"));
    EXPECT_FALSE(std::filesystem::exists("pf_test_new/probe.stale"));
    EXPECT_FALSE(std::filesystem::exists("pf_test_legacy/probe.42.1.99.0.0"));
    EXPECT_FALSE(std::filesystem::exists("pf_test_new/probe.42.1.99.0.0"));
    EXPECT_TRUE(std::filesystem::exists("pf_test_legacy/probename.7.1.124.0.0"));
    EXPECT_TRUE(std::filesystem::exists("pf_test_new/probename.7.1.124.0.0"));
    EXPECT_EQ(count_files(legacy_dir), 2);
    EXPECT_EQ(count_files(new_dir), 2);

    EXPECT_FALSE(std::filesystem::exists("pf_test_new_scratch"));
    EXPECT_TRUE(std::filesystem::exists("pf_test_legacy_scratch"));

    std::filesystem::remove("pf_test_legacy/probe.50.1.123.0.0");
    std::filesystem::remove("pf_test_new/probe.50.1.123.0.0");
    std::filesystem::remove("pf_test_legacy/probename.7.1.124.0.0");
    std::filesystem::remove("pf_test_new/probename.7.1.124.0.0");
    std::filesystem::remove("pf_test_legacy_scratch");
    std::filesystem::remove("pf_test_new_scratch");
    std::filesystem::remove(legacy_dir);
    std::filesystem::remove(new_dir);
}

TEST(PlayerFinalize, AcceptsBoundedPathsAndTruncatesEmbeddedNullSuffixes)
{
    const std::array<char, 26> scratch_storage {
        'p', 'f', '_', 'b', 'o', 'u', 'n', 'd', 'e', 'd', '_', 's', 'c', 'r', 'a', 't', 'c', 'h',
        'X', 'X', 'X', 'X', 'X', 'X', 'X', 'X'
    };
    const std::string_view scratch_path(scratch_storage.data(), 18);
    constexpr std::string_view directory_path("pf_bounded\0ignored", 19);
    constexpr std::string_view base_name("probe\0ignored", 13);
    constexpr std::string_view versioned_path("pf_bounded/probe.1\0ignored", 27);

    std::filesystem::create_directory("pf_bounded");
    write_file("pf_bounded_scratch", "PLAYER-BYTES-V2\n");

    ASSERT_TRUE(finalize_player_file_rename(
        scratch_path, directory_path, base_name, versioned_path));
    EXPECT_EQ(read_file("pf_bounded/probe.1"), "PLAYER-BYTES-V2\n");

    std::filesystem::remove("pf_bounded/probe.1");
    std::filesystem::remove("pf_bounded");
}
