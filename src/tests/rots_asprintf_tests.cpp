#include "../platform_compat.h"
#include "../mob_csv_extract.h"
#include "test_platform_compat.h"
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <string_view>

// rots_asprintf is a portable drop-in for the POSIX/BSD/glibc asprintf() extension,
// which MSVC's CRT does not provide (Phase 3 Task 5: MSVC bring-up). These pin its
// asprintf(3)-compatible ownership contract (malloc'd *out on success, caller frees;
// nullptr + -1 on failure) so the mechanical asprintf-call-site replacement across the
// game sources (act_info.cpp, fight.cpp, spec_pro.cpp, pkill.cpp, utility.cpp) is
// behavior-preserving.

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        char path_template[] = "/tmp/rots-platform-compat-XXXXXX";
        char* created_path = rots_mkdtemp(path_template);
        EXPECT_NE(created_path, nullptr);
        if (created_path != nullptr) {
            path_ = created_path;
        }
    }

    ~TemporaryDirectory()
    {
        std::error_code cleanup_error;
        std::filesystem::remove_all(path_, cleanup_error);
    }

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    // Identifies the test-owned directory removed when the fixture leaves scope.
    std::filesystem::path path_;
};

void close_file(FILE* file)
{
    if (file != nullptr) {
        std::fclose(file);
    }
}

using ScopedFile = std::unique_ptr<FILE, decltype(&close_file)>;

} // namespace

TEST(RotsAsprintf, FormatsSimpleString) {
    char* out = nullptr;
    int written = rots_asprintf(&out, "%s", "hello");

    ASSERT_NE(out, nullptr);
    EXPECT_EQ(written, 5);
    EXPECT_STREQ(out, "hello");
    std::free(out);
}

TEST(RotsAsprintf, FormatsMixedArguments) {
    char* out = nullptr;
    int written = rots_asprintf(&out, "%d%s", 21, "st");

    ASSERT_NE(out, nullptr);
    EXPECT_EQ(written, 4);
    EXPECT_STREQ(out, "21st");
    std::free(out);
}

TEST(RotsAsprintf, ProducesEmptyStringForEmptyFormat) {
    char* out = nullptr;
    int written = rots_asprintf(&out, "%s", "");

    ASSERT_NE(out, nullptr);
    EXPECT_EQ(written, 0);
    EXPECT_STREQ(out, "");
    std::free(out);
}

TEST(RotsAsprintf, HandlesLongOutputPastAnySmallStackBuffer) {
    // Exercises the two-pass (size-then-allocate) path with output well past any
    // plausible fixed-size scratch buffer, guarding against an off-by-one in the
    // vsnprintf-size-then-malloc sizing.
    std::string expected(2048, 'x');
    char* out = nullptr;
    int written = rots_asprintf(&out, "%s", expected.c_str());

    ASSERT_NE(out, nullptr);
    EXPECT_EQ(written, static_cast<int>(expected.size()));
    EXPECT_EQ(std::strlen(out), expected.size());
    EXPECT_EQ(expected, out);
    std::free(out);
}

TEST(RotsAsprintf, NullTerminatesExactlyAtWrittenLength) {
    char* out = nullptr;
    int written = rots_asprintf(&out, "%s-%s", "abc", "de");

    ASSERT_NE(out, nullptr);
    EXPECT_EQ(written, 6);
    EXPECT_EQ(out[written], '\0');
    std::free(out);
}

TEST(RotsAsprintf, PlatformPathsAcceptBoundedAndEmbeddedNullViews)
{
    TemporaryDirectory temporary_directory;
    ASSERT_FALSE(temporary_directory.path().empty());

    const std::string source_path = (temporary_directory.path() / "source").string();
    const std::string destination_path = (temporary_directory.path() / "destination").string();
    ScopedFile source_file(std::fopen(source_path.c_str(), "wb"), &close_file);
    ASSERT_NE(source_file, nullptr);
    source_file.reset();

    std::string source_storage = source_path + "ignored-without-a-terminator";
    const std::string_view bounded_source(source_storage.data(), source_path.size());
    const std::string embedded_destination = destination_path + std::string("\0ignored", 8);
    EXPECT_EQ(rots_rename_replace(
                  bounded_source, std::string_view(embedded_destination)),
        0);
    EXPECT_TRUE(std::filesystem::exists(destination_path));

    std::string destination_storage = destination_path + "ignored-without-a-terminator";
    const std::string_view bounded_destination(destination_storage.data(), destination_path.size());
    EXPECT_EQ(rots_remove(bounded_destination), 0);
    EXPECT_FALSE(std::filesystem::exists(destination_path));

}

TEST(MobCsv, WritesOnlyTheBoundedTextualPrefix)
{
    mob_csv_extract exporter;
    ScopedFile output_file(std::tmpfile(), &close_file);
    ASSERT_NE(output_file, nullptr);
    exporter.file = output_file.get();

    const std::string text_storage("alpha,beta\0ignored", 18);
    exporter.write_to_file(nullptr, std::string_view(text_storage));
    ASSERT_EQ(std::fflush(exporter.file), 0);
    ASSERT_EQ(std::fseek(exporter.file, 0, SEEK_SET), 0);

    char contents[32] {};
    const std::size_t bytes_read = std::fread(contents, sizeof(char), sizeof(contents), exporter.file);
    EXPECT_EQ(std::string_view(contents, bytes_read), "alpha,beta");
    output_file.release();
    exporter.close_file(nullptr);
    EXPECT_EQ(exporter.file, nullptr);
}
