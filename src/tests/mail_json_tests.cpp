#include "../mail.h"
#include "test_platform_compat.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace mail_json {
bool write_json_text_for_testing(std::string_view path, std::string_view contents, std::string* error_message);
}

// TDD coverage for Phase 2a Task 5: mail persistence as JSON, plus the
// one-time legacy-block-file boot converter.
//
// The legacy `lib/misc/plrmail` on-disk layout is a chain of fixed 100-byte
// blocks. A block's leading 4-byte little-endian `block_type` is -1 (header,
// starts a message), -2 (last block of a chain), -3 (deleted -- silently
// skipped, never acted on), or a byte offset (a multiple of 100) linking to
// the next block. A header block is: block_type(4) + next_block(4) +
// from[16] + to[16] + mail_time(4) + txt[56] = 100 bytes exactly. A data
// (continuation) block is: block_type(4) + txt[96] = 100 bytes exactly.
// Both text fields are always NUL-terminated within their fixed size (the
// legacy writer memset the whole block to 0 first, then strncpy'd content
// in, then explicitly forced a NUL at the last usable byte regardless).
//
// All sizes below are hardcoded literals -- NOT derived from sizeof(long)
// or any macro in mail.h -- per the controller correction on this task:
// mail.h's constants describe the *live* JSON format, and must never
// silently reinterpret the frozen historical on-disk bytes. This was
// verified byte-for-byte against the real, 171,400-byte
// lib/misc/plrmail (1,714 blocks; see docs/superpowers/sdd/p2a-task-5-report.md
// for the hexdump walkthrough) before any decoder code was written.

namespace {

void append_i32(std::string* bytes, int32_t value)
{
    const uint32_t unsigned_value = static_cast<uint32_t>(value);
    unsigned char encoded[4];
    encoded[0] = static_cast<unsigned char>(unsigned_value & 0xFF);
    encoded[1] = static_cast<unsigned char>((unsigned_value >> 8) & 0xFF);
    encoded[2] = static_cast<unsigned char>((unsigned_value >> 16) & 0xFF);
    encoded[3] = static_cast<unsigned char>((unsigned_value >> 24) & 0xFF);
    bytes->append(reinterpret_cast<const char*>(encoded), 4);
}

// Appends `value` followed by a NUL and enough zero padding to occupy
// exactly `field_size` bytes -- matches the legacy writer's memset-then-
// strncpy-then-force-a-trailing-NUL pattern. `value.size()` must be < field_size.
void append_fixed_field(std::string* bytes, const std::string& value, size_t field_size)
{
    ASSERT_LT(value.size(), field_size);
    bytes->append(value);
    bytes->append(field_size - value.size(), '\0');
}

void append_header_block(std::string* bytes, int32_t next_block, const std::string& from,
    const std::string& to, int32_t mail_time, const std::string& header_text)
{
    append_i32(bytes, -1); // block_type = HEADER_BLOCK
    append_i32(bytes, next_block);
    append_fixed_field(bytes, from, 16); // NAME_SIZE(15) + 1
    append_fixed_field(bytes, to, 16);
    append_i32(bytes, mail_time);
    append_fixed_field(bytes, header_text, 56); // HEADER_BLOCK_DATASIZE(55) + 1
}

void append_last_data_block(std::string* bytes, const std::string& text)
{
    append_i32(bytes, -2); // block_type = LAST_BLOCK -- this chain ends here
    append_fixed_field(bytes, text, 96); // DATA_BLOCK_DATASIZE(95) + 1
}

// Body long enough (122 chars) to require a header block (first 55 chars)
// plus exactly one continuation data block (remaining 67 chars, well under
// the 95-char data block capacity).
const char* const kChainedBody =
    "Frodo, the ring must be destroyed in the fires of Mount Doom. "
    "Trust no one but Sam and Gandalf on this journey. -- Aragorn";

// Builds a two-message legacy mail file: message A is a single header
// block; message B is a header block chained to one continuation data
// block. Message B's header sits at block 1 (offset 100) and its
// continuation sits at block 2 (offset 200) -- exercising both the
// single-block and chained-message decode paths the brief calls out.
std::string build_two_message_mail_bytes()
{
    const std::string body_b = kChainedBody;
    EXPECT_GT(body_b.size(), 55u);
    EXPECT_LE(body_b.size() - 55, 95u);

    std::string bytes;
    append_header_block(&bytes, /*next_block=*/-2, "Aragorn", "frodo", 123456789,
        "The ring is safe.\n\r");
    append_header_block(&bytes, /*next_block=*/200, "Gandalf", "frodo", 987654321,
        body_b.substr(0, 55));
    append_last_data_block(&bytes, body_b.substr(55));
    return bytes;
}

class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        char path_template[] = "/tmp/rots-mail-json-XXXXXX";
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

class ScopedWorkingDirectory {
public:
    // std::filesystem::current_path() is both the getter and (with a path argument)
    // the setter -- a direct, portable stand-in for the getcwd()/chdir() pair (Phase 3
    // Task 5/6: POSIX-ism cleanup for MSVC bring-up; <unistd.h> -- getcwd()/chdir()'s
    // POSIX home -- doesn't exist on Windows). Also sidesteps the PATH_MAX
    // include-order fragility the raw-buffer version above used to work around.
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

#ifdef ROTS_GOLDEN_DIR
const char* const kGoldenPath = ROTS_GOLDEN_DIR "/legacy_mail_fixture.bin";
#else
const char* const kGoldenPath = "goldens/legacy_mail_fixture.bin";
#endif

} // namespace

TEST(MailJson, AtomicTextWriterAcceptsBoundedInputAndStopsAtEmbeddedNull)
{
    TemporaryDirectory temporary_directory;
    const std::string output_path = temporary_directory.path() + "/mail.json";
    std::string storage = "{\"mail\":1}";
    storage.push_back('\0');
    storage += "ignored";
    const std::string_view bounded_text(storage.data(), storage.size());
    std::string error_message;

    ASSERT_TRUE(mail_json::write_json_text_for_testing(output_path, bounded_text, &error_message))
        << error_message;
    EXPECT_EQ(read_file(output_path), "{\"mail\":1}");
}

TEST(MailJson, DecodesLegacyRecordsSingleAndChainedMessages)
{
    if (sizeof(long) != 4)
        GTEST_SKIP() << "legacy fixtures encode the 32-bit ABI; run in the i386 container";

    const std::string bytes = build_two_message_mail_bytes();

    mail_json::MailStoreData data;
    std::string error;
    ASSERT_TRUE(mail_json::legacy_mail_file_from_binary(bytes, &data, &error)) << error;

    ASSERT_EQ(2u, data.messages.size());

    const mail_json::MailMessageData& single = data.messages[0];
    EXPECT_EQ("Aragorn", single.from);
    EXPECT_EQ("frodo", single.to);
    EXPECT_EQ(123456789, single.mail_time);
    EXPECT_EQ("The ring is safe.\n\r", single.body);

    const mail_json::MailMessageData& chained = data.messages[1];
    EXPECT_EQ("Gandalf", chained.from);
    EXPECT_EQ("frodo", chained.to);
    EXPECT_EQ(987654321, chained.mail_time);
    EXPECT_EQ(kChainedBody, chained.body);
}

TEST(MailJson, BinaryDecoderReadsFullPayloadPastEmbeddedNullBytes) {
    const std::string bytes = build_two_message_mail_bytes();
    const std::size_t first_null_offset = bytes.find('\0');
    ASSERT_NE(first_null_offset, std::string::npos);
    ASSERT_LT(first_null_offset, bytes.size() - 1);

    mail_json::MailStoreData parsed;
    std::string error_message;
    ASSERT_TRUE(mail_json::legacy_mail_file_from_binary(bytes, &parsed, &error_message))
        << error_message;
    ASSERT_EQ(parsed.messages.size(), 2u);
    EXPECT_EQ(parsed.messages[1].body, kChainedBody);
}

TEST(MailJson, RejectsFileSizeNotMultipleOfBlockSize)
{
    if (sizeof(long) != 4)
        GTEST_SKIP() << "legacy fixtures encode the 32-bit ABI; run in the i386 container";

    std::string bytes = build_two_message_mail_bytes();
    bytes.push_back('\0'); // 301 bytes, not a multiple of 100

    mail_json::MailStoreData data;
    std::string error;
    EXPECT_FALSE(mail_json::legacy_mail_file_from_binary(bytes, &data, &error));
    EXPECT_NE(error.find("100-byte block"), std::string::npos);
}

TEST(MailJson, RejectsTruncatedFile)
{
    if (sizeof(long) != 4)
        GTEST_SKIP() << "legacy fixtures encode the 32-bit ABI; run in the i386 container";

    std::string bytes = build_two_message_mail_bytes();
    bytes.resize(50); // well before even one full 100-byte block

    mail_json::MailStoreData data;
    std::string error;
    EXPECT_FALSE(mail_json::legacy_mail_file_from_binary(bytes, &data, &error));
    EXPECT_NE(error.find("100-byte block"), std::string::npos);
}

TEST(MailJson, RejectsChainWithInvalidLink)
{
    if (sizeof(long) != 4)
        GTEST_SKIP() << "legacy fixtures encode the 32-bit ABI; run in the i386 container";

    std::string bytes;
    append_header_block(&bytes, /*next_block=*/12345, "Gandalf", "frodo", 987654321,
        std::string(kChainedBody).substr(0, 55));

    mail_json::MailStoreData data;
    std::string error;
    EXPECT_FALSE(mail_json::legacy_mail_file_from_binary(bytes, &data, &error));
    EXPECT_NE(error.find("invalid block"), std::string::npos);
}

TEST(MailJson, RejectsChainWithCycle)
{
    if (sizeof(long) != 4)
        GTEST_SKIP() << "legacy fixtures encode the 32-bit ABI; run in the i386 container";

    // Header at block 0 links to block 1; block 1 (a data block) links
    // right back to block 0 -- a cycle that must never be walked forever.
    const std::string body_b = kChainedBody;
    std::string bytes;
    append_header_block(&bytes, /*next_block=*/100, "Gandalf", "frodo", 987654321,
        body_b.substr(0, 55));
    append_i32(&bytes, 0); // data block at offset 100 links back to offset 0
    append_fixed_field(&bytes, body_b.substr(55), 96);

    mail_json::MailStoreData data;
    std::string error;
    EXPECT_FALSE(mail_json::legacy_mail_file_from_binary(bytes, &data, &error));
    EXPECT_NE(error.find("cycle"), std::string::npos);
}

TEST(MailJson, JsonRoundTripPreservesAllFields)
{
    if (sizeof(long) != 4)
        GTEST_SKIP() << "legacy fixtures encode the 32-bit ABI; run in the i386 container";

    const std::string bytes = build_two_message_mail_bytes();

    mail_json::MailStoreData decoded;
    std::string decode_error;
    ASSERT_TRUE(mail_json::legacy_mail_file_from_binary(bytes, &decoded, &decode_error)) << decode_error;

    const std::string json = mail_json::serialize_mail_to_json(decoded);

    mail_json::MailStoreData roundtripped;
    std::string json_error;
    ASSERT_TRUE(mail_json::deserialize_mail_from_json(json, &roundtripped, &json_error)) << json_error;

    EXPECT_TRUE(mail_json::mail_store_data_equal(decoded, roundtripped));
}

TEST(MailJson, DeserializeAcceptsBoundedTextAndStopsAtEmbeddedNull) {
    mail_json::MailStoreData original;
    original.messages.push_back({"frodo", "aragorn", 1700000000, "bounded body"});
    const std::string json = mail_json::serialize_mail_to_json(original);
    std::string bounded_storage = json + "ignored";
    std::string embedded_null_storage = json + std::string("\0ignored", 8);

    for (const std::string_view json_view :
         {std::string_view(bounded_storage.data(), json.size()),
          std::string_view(embedded_null_storage.data(), embedded_null_storage.size())}) {
        mail_json::MailStoreData parsed;
        std::string error_message;
        ASSERT_TRUE(mail_json::deserialize_mail_from_json(json_view, &parsed, &error_message))
            << error_message;
        EXPECT_TRUE(mail_json::mail_store_data_equal(original, parsed));
    }
}

TEST(MailJson, DeserializeMissingMessagesFieldYieldsEmptyStore)
{
    mail_json::MailStoreData data;
    std::string error;
    ASSERT_TRUE(mail_json::deserialize_mail_from_json("{}", &data, &error)) << error;
    EXPECT_EQ(0u, data.messages.size());
}

TEST(MailJson, ConvertLegacyMailFileWritesJsonVerifiesAndRenamesLegacyToMigrated)
{
    if (sizeof(long) != 4)
        GTEST_SKIP() << "legacy fixtures encode the 32-bit ABI; run in the i386 container";

    TemporaryDirectory root;
    const std::string legacy_path = root.path() + "/plrmail";
    write_file(legacy_path, build_two_message_mail_bytes());

    std::string error;
    ASSERT_TRUE(mail_json::convert_legacy_mail_file(legacy_path.c_str(), &error)) << error;

    EXPECT_FALSE(file_exists(legacy_path)) << "legacy file should have been renamed away";
    EXPECT_TRUE(file_exists(legacy_path + ".migrated"));

    const std::string json_path = mail_json::mail_json_path(legacy_path);
    ASSERT_TRUE(file_exists(json_path));

    mail_json::MailStoreData from_json;
    std::string json_error;
    ASSERT_TRUE(mail_json::deserialize_mail_from_json(read_file(json_path), &from_json, &json_error)) << json_error;

    mail_json::MailStoreData original_decode;
    std::string decode_error;
    ASSERT_TRUE(mail_json::legacy_mail_file_from_binary(build_two_message_mail_bytes(), &original_decode, &decode_error)) << decode_error;

    EXPECT_TRUE(mail_json::mail_store_data_equal(original_decode, from_json));
}

TEST(MailJson, ConvertLegacyMailFileSkipsCorruptFileLeavingItUntouched)
{
    if (sizeof(long) != 4)
        GTEST_SKIP() << "legacy fixtures encode the 32-bit ABI; run in the i386 container";

    TemporaryDirectory root;
    const std::string legacy_path = root.path() + "/plrmail";
    const std::string corrupt_bytes = build_two_message_mail_bytes().substr(0, 50);
    write_file(legacy_path, corrupt_bytes);

    std::string error;
    EXPECT_FALSE(mail_json::convert_legacy_mail_file(legacy_path.c_str(), &error));
    EXPECT_FALSE(error.empty());

    ASSERT_TRUE(file_exists(legacy_path));
    EXPECT_EQ(corrupt_bytes, read_file(legacy_path));
    EXPECT_FALSE(file_exists(legacy_path + ".migrated"));
    EXPECT_FALSE(file_exists(mail_json::mail_json_path(legacy_path)));
}

TEST(MailJson, ConvertLegacyMailFileFailsCleanlyWhenFileMissing)
{
    std::string error;
    EXPECT_FALSE(mail_json::convert_legacy_mail_file("/tmp/rots-mail-json-nonexistent-file", &error));
    EXPECT_FALSE(error.empty());
}

// Freezes build_two_message_mail_bytes()'s output as a permanent on-disk
// reference: regenerate with
//   UPDATE_GOLDENS=1 ./bin/tests --gtest_filter=MailJson.GoldenRoundTripsByteStable
// (run from src/tests/ on the 32-bit build only -- it encodes the historical
// 32-bit compiler's block-chain layout) whenever the documented on-disk
// format itself changes (it must not, for the real lib/misc/plrmail to keep
// converting correctly).
TEST(MailJson, GoldenRoundTripsByteStable)
{
    if (sizeof(long) != 4)
        GTEST_SKIP() << "legacy fixtures encode the 32-bit ABI; run in the i386 container";

    const std::string bytes = build_two_message_mail_bytes();

    if (std::getenv("UPDATE_GOLDENS") != nullptr) {
        ASSERT_EQ(sizeof(long), 4u) << "refusing to regenerate a 32-bit-ABI golden on a non-32-bit host; run in the i386 container";
        std::ofstream out(kGoldenPath, std::ios::binary);
        out << bytes;
        ASSERT_TRUE(out.good()) << "failed to write golden " << kGoldenPath;
        GTEST_SKIP() << "Golden written to " << kGoldenPath << "; rerun without UPDATE_GOLDENS to verify.";
    }

    const std::string golden = read_file(kGoldenPath);
    ASSERT_FALSE(golden.empty()) << "run GoldenRoundTripsByteStable with UPDATE_GOLDENS=1 first, from src/tests/";
    ASSERT_EQ(golden, bytes) << "legacy plrmail on-disk layout drifted -- this must never change; "
                                "if it did on purpose, rerun with UPDATE_GOLDENS=1 and commit.";

    mail_json::MailStoreData data;
    std::string error;
    ASSERT_TRUE(mail_json::legacy_mail_file_from_binary(golden, &data, &error)) << error;
    ASSERT_EQ(2u, data.messages.size());
    EXPECT_EQ("frodo", data.messages[0].to);
}

// Exercises the real, process-global runtime API (scan_file/store_mail/
// has_mail/read_delete) end to end. Unlike the mail_json:: tests above,
// these functions are coupled to process-global state (the in-memory
// mail_index/g_mail_messages in mail.cpp) and a fixed relative path
// (MAIL_FILE = "misc/plrmail"), so this test sandboxes itself in a scratch
// working directory the same way objsave_json_tests.cpp does for
// write_player_objects_json. Being the only test in the suite that touches
// this global state, its single sequential flow is self-contained; a
// --gtest_repeat re-run within the same process would see this test's own
// leftover state, which is expected and harmless (it starts from "no mail
// for this recipient" either way, since everything gets read_delete'd away
// again by the end).
TEST(MailJson, RuntimeStoreHasMailReadDeleteLifecycle)
{
    TemporaryDirectory root;
    ScopedWorkingDirectory working_directory(root.path());
    ASSERT_TRUE(std::filesystem::create_directory("misc"));

    // Boot with neither a JSON store nor a legacy file present: scan_file
    // creates an empty JSON store and succeeds.
    ASSERT_EQ(1, scan_file());
    EXPECT_EQ(0, has_mail(const_cast<char*>("frodo")));

    char to[] = "frodo";
    char from[] = "sam";
    char body[] = "The Ring is safe with me, Mr. Frodo.\n\r";
    store_mail(to, from, body);

    EXPECT_NE(0, has_mail(const_cast<char*>("frodo")));
    EXPECT_TRUE(file_exists("misc/plrmail.json"));

    char recipient[] = "frodo";
    char recipient_formatted[] = "Frodo";
    char* letter = read_delete(recipient, recipient_formatted, /*is_good=*/1);
    ASSERT_NE(nullptr, letter);
    const std::string rendered = letter;
    free(letter);

    EXPECT_NE(rendered.find("The Ring is safe with me, Mr. Frodo."), std::string::npos);
    EXPECT_NE(rendered.find("Postal Service of Gondor"), std::string::npos);
    EXPECT_NE(rendered.find("From: sam"), std::string::npos);

    // Mail is gone after being read once.
    EXPECT_EQ(0, has_mail(const_cast<char*>("frodo")));

    // read_delete on a recipient who never had any mail at all returns null
    // (mirrors the legacy "post office spec_proc error?" guard -- has_mail
    // is always checked by callers first, so this path is only reachable
    // via a logic error, but it must fail safely rather than crash).
    char nobody[] = "nobody-at-all";
    char nobody_formatted[] = "Nobody";
    EXPECT_EQ(nullptr, read_delete(nobody, nobody_formatted, /*is_good=*/1));
}
