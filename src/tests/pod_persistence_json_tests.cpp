#include "../account_management.h"
#include "../db.h"
#include "../exploits_json.h"
#include "../pkill.h"
#include "test_platform_compat.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace pkill_json {
bool write_json_text_for_testing(std::string_view path, std::string_view contents, std::string* error_message);
}

namespace crime_json {
bool write_json_text_for_testing(std::string_view path, std::string_view contents, std::string* error_message);
}

// Phase 2a Task 6: the "low-risk POD trio" -- pkill records, crime records,
// and non-account exploit files -- move from raw struct-layout file I/O to
// JSON, following the mail_json/boards_json (Tasks 4/5) precedent. All
// three legacy on-disk formats are literal fwrite(&record, sizeof(record),
// 1, f) dumps of a real, already-declared struct (PKILL, crime_record_type,
// exploit_record) holding only int/sh_int/unsigned-char fields -- so unlike
// mail's/boards' hand-reconstructed historical block formats, these fixture
// builders use the REAL native structs (append_pod below), and the
// decoders use offsetof-derived offsets (pkill.cpp/db.cpp), rather than
// hand-picked literals -- both describe the exact same bytes regardless of
// which ABI (32-bit container vs any 64-bit host) compiles them.
//
// This file covers:
//   1. pkill_json (pkill.h/pkill.cpp): decode, JSON round trip, converter,
//      frozen golden.
//   2. crime_json (db.h/db.cpp): decode, JSON round trip, converter, frozen
//      golden.
//   3. The non-linked exploits runtime path (db.cpp's
//      load_exploit_records_for_character/write_exploit_record_for_character):
//      exploits_json's *existing* codec (exploit_records_from_binary/
//      exploit_records_to_binary, already covered field-by-field in
//      exploits_json_tests.cpp) is reused as-is; what's new here is db.cpp's
//      JSON-first storage + one-time legacy-conversion wiring, plus a
//      frozen golden covering that pipeline end to end.

namespace {

template <typename T>
void append_pod(std::string* bytes, const T& value)
{
    bytes->append(reinterpret_cast<const char*>(&value), sizeof(T));
}

class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        char path_template[] = "/tmp/rots-pod-persistence-XXXXXX";
        char* created_path = rots_mkdtemp(path_template);
        EXPECT_NE(created_path, nullptr);
        if (created_path)
            m_path = created_path;
    }

    ~TemporaryDirectory()
    {
        if (!m_path.empty()) {
            // std::filesystem::remove_all, not system("rm -rf ..."): portable to
            // Windows (cmd.exe has no rm), and already the pattern
            // account_management_tests.cpp's fixture uses (Phase 3 Task 5/6).
            std::error_code ec;
            std::filesystem::remove_all(m_path, ec);
        }
    }

    const std::string& path() const { return m_path; }

private:
    std::string m_path;
};

void write_file(const std::string& path, const std::string& contents)
{
    FILE* file = fopen(path.c_str(), "wb");
    ASSERT_NE(file, nullptr);
    ASSERT_EQ(fwrite(contents.data(), sizeof(char), contents.size(), file), contents.size());
    ASSERT_EQ(fclose(file), 0);
}

std::string read_file_contents(const std::string& path)
{
    FILE* file = std::fopen(path.c_str(), "rb");
    EXPECT_NE(file, nullptr);
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

bool path_exists(const std::string& path)
{
    return std::filesystem::exists(path);
}

TEST(PkillJson, AtomicTextWriterAcceptsBoundedInputAndStopsAtEmbeddedNull)
{
    TemporaryDirectory temporary_directory;
    const std::string output_path = temporary_directory.path() + "/pkill.json";
    std::string storage = "{\"pkill\":1}";
    storage.push_back('\0');
    storage += "ignored";
    const std::string_view bounded_text(storage.data(), storage.size());
    std::string error_message;

    ASSERT_TRUE(pkill_json::write_json_text_for_testing(output_path, bounded_text, &error_message))
        << error_message;
    EXPECT_EQ(read_file_contents(output_path), "{\"pkill\":1}");
}

TEST(CrimeJson, AtomicTextWriterAcceptsBoundedInputAndStopsAtEmbeddedNull)
{
    TemporaryDirectory temporary_directory;
    const std::string output_path = temporary_directory.path() + "/crime.json";
    std::string storage = "{\"crime\":1}";
    storage.push_back('\0');
    storage += "ignored";
    const std::string_view bounded_text(storage.data(), storage.size());
    std::string error_message;

    ASSERT_TRUE(crime_json::write_json_text_for_testing(output_path, bounded_text, &error_message))
        << error_message;
    EXPECT_EQ(read_file_contents(output_path), "{\"crime\":1}");
}

// ROTS_GOLDEN_DIR (set by src/CMakeLists.txt on the ageland_tests target)
// anchors this to an absolute path, matching mail_json_tests.cpp/
// boards_json_tests.cpp/objects_json_layout_tests.cpp -- the src/tests/
// Makefile build doesn't define it, so this falls back to the plain
// relative path (cwd is src/tests/ there).
#ifdef ROTS_GOLDEN_DIR
const char* const kPkillGoldenPath = ROTS_GOLDEN_DIR "/legacy_pkill_fixture.bin";
const char* const kCrimeGoldenPath = ROTS_GOLDEN_DIR "/legacy_crime_fixture.bin";
const char* const kExploitsGoldenPath = ROTS_GOLDEN_DIR "/legacy_exploits_fixture.bin";
#else
const char* const kPkillGoldenPath = "goldens/legacy_pkill_fixture.bin";
const char* const kCrimeGoldenPath = "goldens/legacy_crime_fixture.bin";
const char* const kExploitsGoldenPath = "goldens/legacy_exploits_fixture.bin";
#endif

// ---------------------------------------------------------------------------
// pkill_json
// ---------------------------------------------------------------------------

PKILL make_pkill(int kill_time, int killer, int victim, unsigned char killer_level, unsigned char victim_level, int killer_points, int victim_points)
{
    PKILL record {};
    record.kill_time = kill_time;
    record.killer = killer;
    record.victim = victim;
    record.killer_level = killer_level;
    record.victim_level = victim_level;
    record.killer_points = killer_points;
    record.victim_points = victim_points;
    return record;
}

std::string build_pkill_fixture_bytes()
{
    std::string bytes;
    // A level-0 killer/victim edge case (killer_level 0), a max-unsigned-
    // char level (255), and a negative victim_points value (legacy stores
    // victim points as negative, per pkill.h's comment).
    append_pod(&bytes, make_pkill(1700000000, 101, 202, 50, 45, 12000, -12000));
    append_pod(&bytes, make_pkill(1700003600, 303, 404, 0, 255, 0, 0));
    return bytes;
}

TEST(PkillJson, DecodesLegacyRecordsFieldForField)
{
    if (sizeof(long) != 4)
        GTEST_SKIP() << "legacy fixtures encode the 32-bit ABI; run in the i386 container";

    const std::string bytes = build_pkill_fixture_bytes();

    std::vector<PKILL> records;
    std::string error;
    ASSERT_TRUE(pkill_json::legacy_pkill_file_from_binary(bytes, &records, &error)) << error;
    ASSERT_EQ(records.size(), 2u);

    EXPECT_EQ(records[0].kill_time, 1700000000);
    EXPECT_EQ(records[0].killer, 101);
    EXPECT_EQ(records[0].victim, 202);
    EXPECT_EQ(records[0].killer_level, 50);
    EXPECT_EQ(records[0].victim_level, 45);
    EXPECT_EQ(records[0].killer_points, 12000);
    EXPECT_EQ(records[0].victim_points, -12000);

    EXPECT_EQ(records[1].killer_level, 0);
    EXPECT_EQ(records[1].victim_level, 255);
}

TEST(PkillJson, BinaryDecoderReadsFullPayloadPastEmbeddedNullBytes) {
    if (sizeof(long) != 4) {
        GTEST_SKIP() << "legacy player-kill fixtures require the 32-bit ABI";
    }

    const std::string bytes = build_pkill_fixture_bytes();
    const std::size_t first_null_offset = bytes.find('\0');
    ASSERT_NE(first_null_offset, std::string::npos);
    ASSERT_LT(first_null_offset, bytes.size() - 1);

    std::vector<PKILL> parsed;
    std::string error_message;
    ASSERT_TRUE(pkill_json::legacy_pkill_file_from_binary(bytes, &parsed, &error_message))
        << error_message;
    ASSERT_EQ(parsed.size(), 2u);
    EXPECT_EQ(parsed[1].victim_level, 255);
}

TEST(PkillJson, RejectsSizeNotMultipleOfRecordSize)
{
    if (sizeof(long) != 4)
        GTEST_SKIP() << "legacy fixtures encode the 32-bit ABI; run in the i386 container";

    std::string bytes = build_pkill_fixture_bytes();
    bytes.push_back('\x01'); // one stray trailing byte

    std::vector<PKILL> records;
    std::string error;
    EXPECT_FALSE(pkill_json::legacy_pkill_file_from_binary(bytes, &records, &error));
    EXPECT_NE(error.find("not a multiple"), std::string::npos);
}

TEST(PkillJson, JsonRoundTripPreservesAllFields)
{
    if (sizeof(long) != 4)
        GTEST_SKIP() << "legacy fixtures encode the 32-bit ABI; run in the i386 container";

    std::vector<PKILL> records;
    std::string error;
    ASSERT_TRUE(pkill_json::legacy_pkill_file_from_binary(build_pkill_fixture_bytes(), &records, &error)) << error;

    pkill_json::PkillStoreData store;
    store.records = records;
    const std::string json = pkill_json::serialize_pkill_to_json(store);

    pkill_json::PkillStoreData parsed;
    ASSERT_TRUE(pkill_json::deserialize_pkill_from_json(json, &parsed, &error)) << error;
    ASSERT_TRUE(pkill_json::pkill_records_equal(records, parsed.records));
}

TEST(PkillJson, DeserializeAcceptsBoundedTextAndStopsAtEmbeddedNull) {
    pkill_json::PkillStoreData original;
    original.records.push_back(make_pkill(1700000000, 101, 202, 50, 45, 12000, -12000));
    const std::string json = pkill_json::serialize_pkill_to_json(original);
    std::string bounded_storage = json + "ignored";
    std::string embedded_null_storage = json + std::string("\0ignored", 8);

    for (const std::string_view json_view :
         {std::string_view(bounded_storage.data(), json.size()),
          std::string_view(embedded_null_storage.data(), embedded_null_storage.size())}) {
        pkill_json::PkillStoreData parsed;
        std::string error_message;
        ASSERT_TRUE(pkill_json::deserialize_pkill_from_json(json_view, &parsed, &error_message))
            << error_message;
        EXPECT_TRUE(pkill_json::pkill_records_equal(original.records, parsed.records));
    }
}

TEST(PkillJson, RejectsOutOfRangeLevelInJson)
{
    const std::string json =
        "{\n"
        "  \"version\": 1,\n"
        "  \"records\": [\n"
        "    { \"kill_time\": 1, \"killer\": 2, \"victim\": 3, \"killer_level\": 999, \"victim_level\": 1, \"killer_points\": 1, \"victim_points\": -1 }\n"
        "  ]\n"
        "}\n";

    pkill_json::PkillStoreData parsed;
    std::string error;
    EXPECT_FALSE(pkill_json::deserialize_pkill_from_json(json, &parsed, &error));
    EXPECT_NE(error.find("[0, 255]"), std::string::npos);
}

TEST(PkillJson, ConvertLegacyFileWritesJsonVerifiesAndRenamesLegacyToMigrated)
{
    if (sizeof(long) != 4)
        GTEST_SKIP() << "legacy fixtures encode the 32-bit ABI; run in the i386 container";

    TemporaryDirectory temp_directory;
    const std::string legacy_path = temp_directory.path() + "/pklist";
    write_file(legacy_path, build_pkill_fixture_bytes());

    std::string error;
    ASSERT_TRUE(pkill_json::convert_legacy_pkill_file(legacy_path.c_str(), &error)) << error;
    EXPECT_TRUE(error.empty());

    EXPECT_FALSE(path_exists(legacy_path));
    EXPECT_TRUE(path_exists(legacy_path + ".migrated"));
    EXPECT_EQ(read_file_contents(legacy_path + ".migrated"), build_pkill_fixture_bytes());

    const std::string json_path = pkill_json::pkill_json_path(legacy_path);
    ASSERT_TRUE(path_exists(json_path));

    pkill_json::PkillStoreData parsed;
    ASSERT_TRUE(pkill_json::deserialize_pkill_from_json(read_file_contents(json_path), &parsed, &error)) << error;

    std::vector<PKILL> expected;
    ASSERT_TRUE(pkill_json::legacy_pkill_file_from_binary(build_pkill_fixture_bytes(), &expected, &error)) << error;
    EXPECT_TRUE(pkill_json::pkill_records_equal(expected, parsed.records));
}

TEST(PkillJson, ConvertLegacyFileFailsCleanlyWhenFileMissing)
{
    TemporaryDirectory temp_directory;
    const std::string legacy_path = temp_directory.path() + "/pklist";

    std::string error;
    EXPECT_FALSE(pkill_json::convert_legacy_pkill_file(legacy_path.c_str(), &error));
    EXPECT_FALSE(error.empty());
}

// Freezes build_pkill_fixture_bytes()'s output as a permanent on-disk
// reference: regenerate with
//   UPDATE_GOLDENS=1 ./bin/tests --gtest_filter=PkillJson.GoldenRoundTripsByteStable
// (run from src/tests/ on the 32-bit build only -- it encodes the
// historical 32-bit compiler layout, including any struct padding).
TEST(PkillJson, GoldenRoundTripsByteStable)
{
    if (sizeof(long) != 4)
        GTEST_SKIP() << "legacy fixtures encode the 32-bit ABI; run in the i386 container";

    const std::string bytes = build_pkill_fixture_bytes();

    if (std::getenv("UPDATE_GOLDENS") != nullptr) {
        ASSERT_EQ(sizeof(long), 4u) << "refusing to regenerate a 32-bit-ABI golden on a non-32-bit host; run in the i386 container";
        std::ofstream out(kPkillGoldenPath, std::ios::binary);
        out << bytes;
        ASSERT_TRUE(out.good()) << "failed to write golden " << kPkillGoldenPath;
        GTEST_SKIP() << "Golden written to " << kPkillGoldenPath << "; rerun without UPDATE_GOLDENS to verify.";
    }

    const std::string golden = read_file_contents(kPkillGoldenPath);
    ASSERT_FALSE(golden.empty()) << "run GoldenRoundTripsByteStable with UPDATE_GOLDENS=1 first, from src/tests/";
    ASSERT_EQ(golden, bytes) << "legacy pkill on-disk layout drifted -- this must never change; "
                                "if it did on purpose, rerun with UPDATE_GOLDENS=1 and commit.";

    std::vector<PKILL> records;
    std::string error;
    ASSERT_TRUE(pkill_json::legacy_pkill_file_from_binary(golden, &records, &error)) << error;
    ASSERT_EQ(records.size(), 2u);
    EXPECT_EQ(records[0].kill_time, 1700000000);
    EXPECT_EQ(records[1].victim_level, 255);
}

// ---------------------------------------------------------------------------
// crime_json
// ---------------------------------------------------------------------------

crime_record_type make_crime(int crime_time, sh_int criminal, sh_int victim, int crime, sh_int witness, sh_int witness_type)
{
    crime_record_type record {};
    record.crime_time = crime_time;
    record.criminal = criminal;
    record.victim = victim;
    record.crime = crime;
    record.witness = witness;
    record.witness_type = witness_type;
    return record;
}

std::string build_crime_fixture_bytes()
{
    std::string bytes;
    // Negative idnum-like values are never expected in practice, but
    // sh_int is signed -- exercise the sign-extension path along with an
    // ordinary positive-idnum record.
    append_pod(&bytes, make_crime(1700000000, 11, 22, 3, 33, 1));
    append_pod(&bytes, make_crime(1700003600, -1, -2, 0, -3, 0));
    return bytes;
}

TEST(CrimeJson, DecodesLegacyRecordsFieldForField)
{
    if (sizeof(long) != 4)
        GTEST_SKIP() << "legacy fixtures encode the 32-bit ABI; run in the i386 container";

    const std::string bytes = build_crime_fixture_bytes();

    std::vector<crime_record_type> records;
    std::string error;
    ASSERT_TRUE(crime_json::legacy_crime_file_from_binary(bytes, &records, &error)) << error;
    ASSERT_EQ(records.size(), 2u);

    EXPECT_EQ(records[0].crime_time, 1700000000);
    EXPECT_EQ(records[0].criminal, 11);
    EXPECT_EQ(records[0].victim, 22);
    EXPECT_EQ(records[0].crime, 3);
    EXPECT_EQ(records[0].witness, 33);
    EXPECT_EQ(records[0].witness_type, 1);

    EXPECT_EQ(records[1].criminal, -1);
    EXPECT_EQ(records[1].victim, -2);
    EXPECT_EQ(records[1].witness, -3);
}

TEST(CrimeJson, BinaryDecoderReadsFullPayloadPastEmbeddedNullBytes) {
    if (sizeof(long) != 4) {
        GTEST_SKIP() << "legacy crime fixtures require the 32-bit ABI";
    }

    const std::string bytes = build_crime_fixture_bytes();
    const std::size_t first_null_offset = bytes.find('\0');
    ASSERT_NE(first_null_offset, std::string::npos);
    ASSERT_LT(first_null_offset, bytes.size() - 1);

    std::vector<crime_record_type> parsed;
    std::string error_message;
    ASSERT_TRUE(crime_json::legacy_crime_file_from_binary(bytes, &parsed, &error_message))
        << error_message;
    ASSERT_EQ(parsed.size(), 2u);
    EXPECT_EQ(parsed[1].witness_type, 2);
}

TEST(CrimeJson, RejectsSizeNotMultipleOfRecordSize)
{
    if (sizeof(long) != 4)
        GTEST_SKIP() << "legacy fixtures encode the 32-bit ABI; run in the i386 container";

    std::string bytes = build_crime_fixture_bytes();
    bytes.push_back('\x01');

    std::vector<crime_record_type> records;
    std::string error;
    EXPECT_FALSE(crime_json::legacy_crime_file_from_binary(bytes, &records, &error));
    EXPECT_NE(error.find("not a multiple"), std::string::npos);
}

TEST(CrimeJson, JsonRoundTripPreservesAllFields)
{
    if (sizeof(long) != 4)
        GTEST_SKIP() << "legacy fixtures encode the 32-bit ABI; run in the i386 container";

    std::vector<crime_record_type> records;
    std::string error;
    ASSERT_TRUE(crime_json::legacy_crime_file_from_binary(build_crime_fixture_bytes(), &records, &error)) << error;

    crime_json::CrimeStoreData store;
    store.records = records;
    const std::string json = crime_json::serialize_crime_to_json(store);

    crime_json::CrimeStoreData parsed;
    ASSERT_TRUE(crime_json::deserialize_crime_from_json(json, &parsed, &error)) << error;
    ASSERT_TRUE(crime_json::crime_records_equal(records, parsed.records));
}

TEST(CrimeJson, DeserializeAcceptsBoundedTextAndStopsAtEmbeddedNull) {
    crime_json::CrimeStoreData original;
    original.records.push_back(make_crime(1700000000, 101, 202, 3, 303, 1));
    const std::string json = crime_json::serialize_crime_to_json(original);
    std::string bounded_storage = json + "ignored";
    std::string embedded_null_storage = json + std::string("\0ignored", 8);

    for (const std::string_view json_view :
         {std::string_view(bounded_storage.data(), json.size()),
          std::string_view(embedded_null_storage.data(), embedded_null_storage.size())}) {
        crime_json::CrimeStoreData parsed;
        std::string error_message;
        ASSERT_TRUE(crime_json::deserialize_crime_from_json(json_view, &parsed, &error_message))
            << error_message;
        EXPECT_TRUE(crime_json::crime_records_equal(original.records, parsed.records));
    }
}

TEST(CrimeJson, RejectsOutOfRangeFieldInJson)
{
    const std::string json =
        "{\n"
        "  \"version\": 1,\n"
        "  \"records\": [\n"
        "    { \"crime_time\": 1, \"criminal\": 999999, \"victim\": 2, \"crime\": 3, \"witness\": 4, \"witness_type\": 0 }\n"
        "  ]\n"
        "}\n";

    crime_json::CrimeStoreData parsed;
    std::string error;
    EXPECT_FALSE(crime_json::deserialize_crime_from_json(json, &parsed, &error));
    EXPECT_NE(error.find("signed 16-bit"), std::string::npos);
}

TEST(CrimeJson, ConvertLegacyFileWritesJsonVerifiesAndRenamesLegacyToMigrated)
{
    if (sizeof(long) != 4)
        GTEST_SKIP() << "legacy fixtures encode the 32-bit ABI; run in the i386 container";

    TemporaryDirectory temp_directory;
    const std::string legacy_path = temp_directory.path() + "/crimelist";
    write_file(legacy_path, build_crime_fixture_bytes());

    std::string error;
    ASSERT_TRUE(crime_json::convert_legacy_crime_file(legacy_path.c_str(), &error)) << error;
    EXPECT_TRUE(error.empty());

    EXPECT_FALSE(path_exists(legacy_path));
    EXPECT_TRUE(path_exists(legacy_path + ".migrated"));
    EXPECT_EQ(read_file_contents(legacy_path + ".migrated"), build_crime_fixture_bytes());

    const std::string json_path = crime_json::crime_json_path(legacy_path);
    ASSERT_TRUE(path_exists(json_path));

    crime_json::CrimeStoreData parsed;
    ASSERT_TRUE(crime_json::deserialize_crime_from_json(read_file_contents(json_path), &parsed, &error)) << error;

    std::vector<crime_record_type> expected;
    ASSERT_TRUE(crime_json::legacy_crime_file_from_binary(build_crime_fixture_bytes(), &expected, &error)) << error;
    EXPECT_TRUE(crime_json::crime_records_equal(expected, parsed.records));
}

TEST(CrimeJson, ConvertLegacyFileFailsCleanlyWhenFileMissing)
{
    TemporaryDirectory temp_directory;
    const std::string legacy_path = temp_directory.path() + "/crimelist";

    std::string error;
    EXPECT_FALSE(crime_json::convert_legacy_crime_file(legacy_path.c_str(), &error));
    EXPECT_FALSE(error.empty());
}

// Phase 2a final-review Important 2: mirrors pkill_update_file's
// (pkill.cpp) fail-closed overwrite guard, which add_crime/forget_crimes
// (db.cpp) now call before persisting -- a malformed on-disk store must
// never be silently crushed by whatever's in memory.
TEST(CrimeJson, RefusesToOverwriteMalformedOnDiskStore)
{
    TemporaryDirectory temp_directory;
    const std::string json_path = temp_directory.path() + "/crimelist.json";
    write_file(json_path, "{ this is not valid json");

    std::string error;
    EXPECT_FALSE(crime_json::crime_store_safe_to_overwrite(json_path, &error));
    EXPECT_FALSE(error.empty());
}

TEST(CrimeJson, SafeToOverwriteWhenStoreAbsentOrValid)
{
    if (sizeof(long) != 4)
        GTEST_SKIP() << "legacy fixtures encode the 32-bit ABI; run in the i386 container";

    TemporaryDirectory temp_directory;
    const std::string json_path = temp_directory.path() + "/crimelist.json";

    std::string error;
    EXPECT_TRUE(crime_json::crime_store_safe_to_overwrite(json_path, &error)) << error;

    std::vector<crime_record_type> records;
    ASSERT_TRUE(crime_json::legacy_crime_file_from_binary(build_crime_fixture_bytes(), &records, &error)) << error;
    crime_json::CrimeStoreData store;
    store.records = records;
    write_file(json_path, crime_json::serialize_crime_to_json(store));
    EXPECT_TRUE(crime_json::crime_store_safe_to_overwrite(json_path, &error)) << error;
}

TEST(CrimeJson, GoldenRoundTripsByteStable)
{
    if (sizeof(long) != 4)
        GTEST_SKIP() << "legacy fixtures encode the 32-bit ABI; run in the i386 container";

    const std::string bytes = build_crime_fixture_bytes();

    if (std::getenv("UPDATE_GOLDENS") != nullptr) {
        ASSERT_EQ(sizeof(long), 4u) << "refusing to regenerate a 32-bit-ABI golden on a non-32-bit host; run in the i386 container";
        std::ofstream out(kCrimeGoldenPath, std::ios::binary);
        out << bytes;
        ASSERT_TRUE(out.good()) << "failed to write golden " << kCrimeGoldenPath;
        GTEST_SKIP() << "Golden written to " << kCrimeGoldenPath << "; rerun without UPDATE_GOLDENS to verify.";
    }

    const std::string golden = read_file_contents(kCrimeGoldenPath);
    ASSERT_FALSE(golden.empty()) << "run GoldenRoundTripsByteStable with UPDATE_GOLDENS=1 first, from src/tests/";
    ASSERT_EQ(golden, bytes) << "legacy crime on-disk layout drifted -- this must never change; "
                                "if it did on purpose, rerun with UPDATE_GOLDENS=1 and commit.";

    std::vector<crime_record_type> records;
    std::string error;
    ASSERT_TRUE(crime_json::legacy_crime_file_from_binary(golden, &records, &error)) << error;
    ASSERT_EQ(records.size(), 2u);
    EXPECT_EQ(records[0].crime_time, 1700000000);
    EXPECT_EQ(records[1].criminal, -1);
}

// ---------------------------------------------------------------------------
// Non-account exploits runtime path (db.cpp): exploits_json's own binary/
// JSON codec is exercised field-by-field in exploits_json_tests.cpp
// already; what's covered here is db.cpp's storage wiring -- JSON-first
// load with a one-time legacy-binary conversion fallback -- for
// non-account-linked characters (account-linked behavior is covered
// separately in db_loader_tests.cpp and is untouched by this task).
// ---------------------------------------------------------------------------

exploit_record make_exploit(int type, const char* timestamp, sh_int victim_id, const char* victim_name, int victim_level, int killer_level, int int_param)
{
    exploit_record record {};
    record.type = type;
    std::snprintf(record.chtime, sizeof(record.chtime), "%s", timestamp);
    record.shintVictimID = victim_id;
    std::snprintf(record.chVictimName, sizeof(record.chVictimName), "%s", victim_name);
    record.iVictimLevel = victim_level;
    record.iKillerLevel = killer_level;
    record.iIntParam = int_param;
    return record;
}

std::string build_exploits_fixture_bytes()
{
    std::vector<exploit_record> records;
    records.push_back(make_exploit(EXPLOIT_LEVEL, "Mon Jan  1 00:00:00 2024", 42, "level", 10, 0, 20));
    records.push_back(make_exploit(EXPLOIT_ACHIEVEMENT, "Tue Jan  2 00:00:00 2024", 43, "Won a battle", 11, 0, 0));

    std::string bytes;
    std::string error;
    EXPECT_TRUE(exploits_json::exploit_records_to_binary(records, &bytes, &error)) << error;
    return bytes;
}

TEST(ExploitsRuntimeJson, LoadsAndConvertsLegacyRuntimeFileToJsonAndMigratesLegacy)
{
    if (sizeof(long) != 4)
        GTEST_SKIP() << "legacy fixtures encode the 32-bit ABI; run in the i386 container";

    TemporaryDirectory temp_directory;
    ASSERT_TRUE(std::filesystem::create_directory((temp_directory.path() + "/exploits").c_str()));
    ASSERT_TRUE(std::filesystem::create_directory((temp_directory.path() + "/exploits/A-E").c_str()));

    const std::string legacy_path = account::legacy_exploits_file_path(temp_directory.path(), "aragorn");
    write_file(legacy_path, build_exploits_fixture_bytes());

    std::vector<exploit_record> records;
    std::string error;
    ASSERT_TRUE(load_exploit_records_for_character(temp_directory.path(), "aragorn", &records, &error)) << error;
    ASSERT_EQ(records.size(), 2u);
    EXPECT_EQ(records[0].type, EXPLOIT_LEVEL);
    EXPECT_STREQ(records[1].chVictimName, "Won a battle");

    // Legacy binary retired, JSON store now authoritative.
    EXPECT_FALSE(path_exists(legacy_path));
    EXPECT_TRUE(path_exists(legacy_path + ".migrated"));
    EXPECT_EQ(read_file_contents(legacy_path + ".migrated"), build_exploits_fixture_bytes());
    ASSERT_TRUE(path_exists(legacy_path + ".json"));

    exploits_json::ExploitHistoryData history;
    ASSERT_TRUE(exploits_json::deserialize_exploits_from_json(read_file_contents(legacy_path + ".json"), &history, &error)) << error;
    ASSERT_EQ(history.records.size(), 2u);
    EXPECT_EQ(history.records[0].type, EXPLOIT_LEVEL);

    // A second load reads straight from the JSON store -- no re-conversion,
    // same results.
    std::vector<exploit_record> reloaded;
    ASSERT_TRUE(load_exploit_records_for_character(temp_directory.path(), "aragorn", &reloaded, &error)) << error;
    ASSERT_EQ(reloaded.size(), 2u);
    EXPECT_EQ(reloaded[0].type, records[0].type);
}

TEST(ExploitsRuntimeJson, WritesNewRecordsDirectlyAsJsonWithNoLegacyBinaryFile)
{
    TemporaryDirectory temp_directory;
    ASSERT_TRUE(std::filesystem::create_directory((temp_directory.path() + "/exploits").c_str()));
    ASSERT_TRUE(std::filesystem::create_directory((temp_directory.path() + "/exploits/K-O").c_str())); // "legolas" buckets to K-O

    const exploit_record new_record = make_exploit(EXPLOIT_ACHIEVEMENT, "Wed Jan  3 00:00:00 2024", 1, "first blood", 5, 0, 0);
    std::string error;
    ASSERT_TRUE(write_exploit_record_for_character(temp_directory.path(), "legolas", new_record, &error)) << error;

    const std::string legacy_path = account::legacy_exploits_file_path(temp_directory.path(), "legolas");
    EXPECT_FALSE(path_exists(legacy_path));
    ASSERT_TRUE(path_exists(legacy_path + ".json"));

    std::vector<exploit_record> records;
    ASSERT_TRUE(load_exploit_records_for_character(temp_directory.path(), "legolas", &records, &error)) << error;
    ASSERT_EQ(records.size(), 1u);
    EXPECT_STREQ(records[0].chVictimName, "first blood");
}

TEST(ExploitsRuntimeJson, FailsClosedOnMalformedRuntimeJsonWithoutDestroyingIt)
{
    TemporaryDirectory temp_directory;
    ASSERT_TRUE(std::filesystem::create_directory((temp_directory.path() + "/exploits").c_str()));
    ASSERT_TRUE(std::filesystem::create_directory((temp_directory.path() + "/exploits/A-E").c_str()));

    const std::string json_path = account::legacy_exploits_file_path(temp_directory.path(), "aragorn") + ".json";
    write_file(json_path, "{bad-json");

    std::vector<exploit_record> records;
    std::string error;
    EXPECT_FALSE(load_exploit_records_for_character(temp_directory.path(), "aragorn", &records, &error));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(path_exists(json_path)) << "Failing closed on malformed runtime exploit JSON must not destroy it.";
}

// Freezes build_exploits_fixture_bytes()'s output (itself produced via
// exploits_json::exploit_records_to_binary, the existing, already-tested
// codec this task reuses as-is) as a permanent on-disk reference, exercised
// end to end through the db.cpp integration above. Regenerate with
//   UPDATE_GOLDENS=1 ./bin/tests --gtest_filter=ExploitsRuntimeJson.GoldenRoundTripsByteStable
TEST(ExploitsRuntimeJson, GoldenRoundTripsByteStable)
{
    if (sizeof(long) != 4)
        GTEST_SKIP() << "legacy fixtures encode the 32-bit ABI; run in the i386 container";

    const std::string bytes = build_exploits_fixture_bytes();

    if (std::getenv("UPDATE_GOLDENS") != nullptr) {
        ASSERT_EQ(sizeof(long), 4u) << "refusing to regenerate a 32-bit-ABI golden on a non-32-bit host; run in the i386 container";
        std::ofstream out(kExploitsGoldenPath, std::ios::binary);
        out << bytes;
        ASSERT_TRUE(out.good()) << "failed to write golden " << kExploitsGoldenPath;
        GTEST_SKIP() << "Golden written to " << kExploitsGoldenPath << "; rerun without UPDATE_GOLDENS to verify.";
    }

    const std::string golden = read_file_contents(kExploitsGoldenPath);
    ASSERT_FALSE(golden.empty()) << "run GoldenRoundTripsByteStable with UPDATE_GOLDENS=1 first, from src/tests/";
    ASSERT_EQ(golden, bytes) << "legacy exploit_record on-disk layout drifted -- this must never change; "
                                 "if it did on purpose, rerun with UPDATE_GOLDENS=1 and commit.";

    TemporaryDirectory temp_directory;
    ASSERT_TRUE(std::filesystem::create_directory((temp_directory.path() + "/exploits").c_str()));
    ASSERT_TRUE(std::filesystem::create_directory((temp_directory.path() + "/exploits/A-E").c_str()));
    const std::string legacy_path = account::legacy_exploits_file_path(temp_directory.path(), "aragorn");
    write_file(legacy_path, golden);

    std::vector<exploit_record> records;
    std::string error;
    ASSERT_TRUE(load_exploit_records_for_character(temp_directory.path(), "aragorn", &records, &error)) << error;
    ASSERT_EQ(records.size(), 2u);
    EXPECT_EQ(records[0].type, EXPLOIT_LEVEL);
    EXPECT_EQ(records[1].type, EXPLOIT_ACHIEVEMENT);
    EXPECT_TRUE(path_exists(legacy_path + ".migrated"));
}

// Phase 2b Task 2: direct field-by-field check that the explicit-offset
// decoder (exploits_json.cpp's exploit_records_from_binary) reads the frozen
// on-disk fixture correctly -- independent of GoldenRoundTripsByteStable
// above, which only round-trips through the encoder and checks `type`.
// Every field of both records is checked here against the values known to
// have produced this exact golden (build_exploits_fixture_bytes()).
TEST(ExploitsRuntimeJson, GoldenFixtureDecodesToKnownFieldValuesExactly)
{
    if (sizeof(long) != 4)
        GTEST_SKIP() << "legacy fixtures encode the 32-bit ABI; run in the i386 container";

    const std::string golden = read_file_contents(kExploitsGoldenPath);
    ASSERT_FALSE(golden.empty()) << "run GoldenRoundTripsByteStable with UPDATE_GOLDENS=1 first, from src/tests/";

    std::vector<exploit_record> records;
    std::string error;
    ASSERT_TRUE(exploits_json::exploit_records_from_binary(golden, &records, &error)) << error;
    ASSERT_EQ(records.size(), 2u);

    EXPECT_EQ(records[0].type, EXPLOIT_LEVEL);
    EXPECT_STREQ(records[0].chtime, "Mon Jan  1 00:00:00 2024");
    EXPECT_EQ(records[0].shintVictimID, 42);
    EXPECT_STREQ(records[0].chVictimName, "level");
    EXPECT_EQ(records[0].iVictimLevel, 10);
    EXPECT_EQ(records[0].iKillerLevel, 0);
    EXPECT_EQ(records[0].iIntParam, 20);

    EXPECT_EQ(records[1].type, EXPLOIT_ACHIEVEMENT);
    EXPECT_STREQ(records[1].chtime, "Tue Jan  2 00:00:00 2024");
    EXPECT_EQ(records[1].shintVictimID, 43);
    EXPECT_STREQ(records[1].chVictimName, "Won a battle");
    EXPECT_EQ(records[1].iVictimLevel, 11);
    EXPECT_EQ(records[1].iKillerLevel, 0);
    EXPECT_EQ(records[1].iIntParam, 0);
}

} // namespace
