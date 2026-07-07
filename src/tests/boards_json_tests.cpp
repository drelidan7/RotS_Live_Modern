#include "../boards.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>

// TDD coverage for Phase 2a Task 4: board persistence as JSON, plus the
// one-time legacy-file boot converter.
//
// The legacy `<boardfile>.boa` on-disk layout is: a header of two ints
// (num_of_msgs, last_message), followed by num_of_msgs records of the
// native `board_msginfo` struct (28 bytes on the 32-bit build: slot_num,
// msg_num, a `char*` heading pointer, level, post_time, heading_len,
// message_len -- all 4-byte fields, no padding), each immediately followed
// by heading_len bytes of heading text and (iff message_len > 0) message_len
// bytes of message text, both including their own trailing NUL byte. This
// was verified against a hexdump of a real board file (lib/boards/boa15.boa)
// before writing any decoder code -- see
// docs/superpowers/sdd/p2a-task-4-report.md for the byte-level walkthrough.
//
// The fixture builder below uses the REAL native `board_msginfo` struct
// (append_pod) so it reproduces the exact historical on-disk bytes on this
// 32-bit build, matching Task 1/3's established fixture convention
// (legacy_rent_fixture.h). The heading pointer field is filled with an
// obviously-garbage, non-null value to prove the decoder truly discards it
// rather than dereferencing it.

namespace {

template <typename T>
void append_pod(std::string* bytes, const T& value)
{
    bytes->append(reinterpret_cast<const char*>(&value), sizeof(T));
}

void append_i32(std::string* bytes, int value)
{
    append_pod(bytes, value);
}

// Appends one on-disk message record: the 28-byte board_msginfo struct plus
// its adjacent heading/message text, exactly as save_board() writes it
// (heading_len/message_len are strlen()+1, i.e. text includes its own
// trailing NUL; message_len == 0 means no message body at all).
void append_record(std::string* bytes, int slot_num, int msg_num, int level, int post_time,
    const std::string& heading, bool has_message, const std::string& message)
{
    board_msginfo record {};
    record.slot_num = slot_num;
    record.msg_num = msg_num;
    // Garbage, deliberately non-null and never a valid pointer this process
    // owns -- proves the decoder reads past this field and discards it
    // rather than dereferencing it.
    record.heading = reinterpret_cast<char*>(0xdeadbeef);
    record.level = level;
    record.post_time = post_time;
    record.heading_len = static_cast<int>(heading.size()) + 1;
    record.message_len = has_message ? static_cast<int>(message.size()) + 1 : 0;

    append_pod(bytes, record);
    bytes->append(heading);
    bytes->append(1, '\0');
    if (has_message) {
        bytes->append(message);
        bytes->append(1, '\0');
    }
}

std::string build_two_message_board_bytes()
{
    std::string bytes;
    append_i32(&bytes, 2); // num_of_msgs
    append_i32(&bytes, 7); // last_message
    append_record(&bytes, /*slot_num=*/483, /*msg_num=*/1, /*level=*/98, /*post_time=*/878116136,
        "Sat Nov  8 (Zenith)     :: zone layout", /*has_message=*/true,
        "\n\r105\n\r\n\r    -  \n\r     -\n\r     -\n\r     -\n\r     -\n\r     ---\n\r\n\r");
    append_record(&bytes, /*slot_num=*/484, /*msg_num=*/2, /*level=*/98, /*post_time=*/878200000,
        "A heading with no body yet", /*has_message=*/false, "");
    return bytes;
}

class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        char path_template[] = "/tmp/rots-boards-json-XXXXXX";
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

#ifdef ROTS_GOLDEN_DIR
const char* const kGoldenPath = ROTS_GOLDEN_DIR "/legacy_board_fixture.bin";
#else
const char* const kGoldenPath = "goldens/legacy_board_fixture.bin";
#endif

} // namespace

TEST(BoardsJson, NativeRecordStructIs28BytesOn32Bit)
{
    // Pins the assumption the decoder's explicit-offset reads are built on --
    // confirmed against a real board file's hexdump before any decoder code
    // was written (see the file header comment above). board_msginfo's
    // native layout only matches that 32-bit on-disk size on the 32-bit ABI
    // (a 64-bit build changes pointer/long width); pre-existing gap noticed
    // and closed incidentally while chasing full linux-x64 green for Phase
    // 2b Task 1 -- unrelated to the account-staged binary bridge itself.
    if (sizeof(long) != 4)
        GTEST_SKIP() << "board_msginfo's native size only matches the 32-bit on-disk layout; run in the i386 container";

    ASSERT_EQ(28u, sizeof(board_msginfo));
}

TEST(BoardsJson, DecodesLegacyRecordsFieldForFieldAndDiscardsHeadingPointer)
{
    if (sizeof(long) != 4)
        GTEST_SKIP() << "legacy fixtures encode the 32-bit ABI; run in the i386 container";

    const std::string bytes = build_two_message_board_bytes();

    boards_json::BoardSaveData data;
    std::string error;
    ASSERT_TRUE(boards_json::legacy_board_file_from_binary(bytes, &data, &error)) << error;

    EXPECT_EQ(7, data.last_message);
    ASSERT_EQ(2u, data.messages.size());

    const boards_json::BoardMessageData& first = data.messages[0];
    EXPECT_EQ(483, first.slot_num);
    EXPECT_EQ(1, first.msg_num);
    EXPECT_EQ(98, first.level);
    EXPECT_EQ(878116136, first.post_time);
    EXPECT_EQ("Sat Nov  8 (Zenith)     :: zone layout", first.heading);
    EXPECT_TRUE(first.has_message);
    EXPECT_EQ("\n\r105\n\r\n\r    -  \n\r     -\n\r     -\n\r     -\n\r     -\n\r     ---\n\r\n\r", first.message);

    const boards_json::BoardMessageData& second = data.messages[1];
    EXPECT_EQ(484, second.slot_num);
    EXPECT_EQ(2, second.msg_num);
    EXPECT_EQ("A heading with no body yet", second.heading);
    EXPECT_FALSE(second.has_message);
    EXPECT_EQ("", second.message);
}

TEST(BoardsJson, RejectsTruncatedFile)
{
    if (sizeof(long) != 4)
        GTEST_SKIP() << "legacy fixtures encode the 32-bit ABI; run in the i386 container";

    std::string bytes = build_two_message_board_bytes();
    bytes.resize(10); // well before even the first full record

    boards_json::BoardSaveData data;
    std::string error;
    EXPECT_FALSE(boards_json::legacy_board_file_from_binary(bytes, &data, &error));
    EXPECT_FALSE(error.empty());
}

TEST(BoardsJson, RejectsZeroHeadingLenAsCorrupt)
{
    if (sizeof(long) != 4)
        GTEST_SKIP() << "legacy fixtures encode the 32-bit ABI; run in the i386 container";

    // Matches the legacy loader's own corruption check: heading_len == 0 was
    // treated as "Board file corrupt!(load) Resetting." -- never a valid
    // on-disk state (save_board only ever writes heading_len == 0 when
    // MSG_HEADING is null, and load_board refuses to load that).
    std::string bytes;
    append_i32(&bytes, 1);
    append_i32(&bytes, 1);
    board_msginfo record {};
    record.slot_num = 1;
    record.msg_num = 1;
    record.heading = reinterpret_cast<char*>(0xdeadbeef);
    record.level = 1;
    record.post_time = 1;
    record.heading_len = 0;
    record.message_len = 0;
    append_pod(&bytes, record);

    boards_json::BoardSaveData data;
    std::string error;
    EXPECT_FALSE(boards_json::legacy_board_file_from_binary(bytes, &data, &error));
    EXPECT_NE(error.find("heading_len"), std::string::npos);
}

TEST(BoardsJson, JsonRoundTripPreservesAllFieldsIncludingAbsentMessage)
{
    if (sizeof(long) != 4)
        GTEST_SKIP() << "legacy fixtures encode the 32-bit ABI; run in the i386 container";

    const std::string bytes = build_two_message_board_bytes();

    boards_json::BoardSaveData decoded;
    std::string decode_error;
    ASSERT_TRUE(boards_json::legacy_board_file_from_binary(bytes, &decoded, &decode_error)) << decode_error;

    const std::string json = boards_json::serialize_board_to_json(decoded);

    boards_json::BoardSaveData roundtripped;
    std::string json_error;
    ASSERT_TRUE(boards_json::deserialize_board_from_json(json, &roundtripped, &json_error)) << json_error;

    EXPECT_TRUE(boards_json::board_save_data_equal(decoded, roundtripped));
}

TEST(BoardsJson, DeserializeRejectsInconsistentHasMessageFalseWithNonEmptyMessage)
{
    const std::string json = R"({
  "version": 1,
  "last_message": 1,
  "messages": [
    {
      "slot_num": 1,
      "msg_num": 1,
      "heading": "h",
      "level": 1,
      "post_time": 1,
      "has_message": false,
      "message": "should not be here"
    }
  ]
})";

    boards_json::BoardSaveData data;
    std::string error;
    EXPECT_FALSE(boards_json::deserialize_board_from_json(json, &data, &error));
}

TEST(BoardsJson, ConvertLegacyBoardFileWritesJsonVerifiesAndRenamesLegacyToMigrated)
{
    if (sizeof(long) != 4)
        GTEST_SKIP() << "legacy fixtures encode the 32-bit ABI; run in the i386 container";

    TemporaryDirectory root;
    const std::string legacy_path = root.path() + "/general.boa";
    write_file(legacy_path, build_two_message_board_bytes());

    std::string error;
    ASSERT_TRUE(boards_json::convert_legacy_board_file(legacy_path.c_str(), &error)) << error;

    EXPECT_FALSE(file_exists(legacy_path)) << "legacy file should have been renamed away";
    EXPECT_TRUE(file_exists(legacy_path + ".migrated"));

    const std::string json_path = boards_json::board_json_path(legacy_path);
    ASSERT_TRUE(file_exists(json_path));

    boards_json::BoardSaveData from_json;
    std::string json_error;
    ASSERT_TRUE(boards_json::deserialize_board_from_json(read_file(json_path), &from_json, &json_error)) << json_error;

    boards_json::BoardSaveData original_decode;
    std::string decode_error;
    ASSERT_TRUE(boards_json::legacy_board_file_from_binary(build_two_message_board_bytes(), &original_decode, &decode_error)) << decode_error;

    EXPECT_TRUE(boards_json::board_save_data_equal(original_decode, from_json));
}

TEST(BoardsJson, ConvertLegacyBoardFileSkipsCorruptFileLeavingItUntouched)
{
    if (sizeof(long) != 4)
        GTEST_SKIP() << "legacy fixtures encode the 32-bit ABI; run in the i386 container";

    TemporaryDirectory root;
    const std::string legacy_path = root.path() + "/corrupt.boa";
    const std::string corrupt_bytes = build_two_message_board_bytes().substr(0, 10);
    write_file(legacy_path, corrupt_bytes);

    std::string error;
    EXPECT_FALSE(boards_json::convert_legacy_board_file(legacy_path.c_str(), &error));
    EXPECT_FALSE(error.empty());

    ASSERT_TRUE(file_exists(legacy_path));
    EXPECT_EQ(corrupt_bytes, read_file(legacy_path));
    EXPECT_FALSE(file_exists(legacy_path + ".migrated"));
    EXPECT_FALSE(file_exists(boards_json::board_json_path(legacy_path)));
}

TEST(BoardsJson, ConvertLegacyBoardFileFailsCleanlyWhenFileMissing)
{
    std::string error;
    EXPECT_FALSE(boards_json::convert_legacy_board_file("/tmp/rots-boards-json-nonexistent-file.boa", &error));
    EXPECT_FALSE(error.empty());
}

// Freezes build_two_message_board_bytes()'s output as a permanent on-disk
// reference: regenerate with
//   UPDATE_GOLDENS=1 ./bin/tests --gtest_filter=BoardsJson.GoldenRoundTripsByteStable
// (run from src/tests/ on the 32-bit build only -- it encodes the historical
// 32-bit compiler's board_msginfo layout) whenever the documented on-disk
// format itself changes (it must not, for existing board files to keep
// converting correctly).
TEST(BoardsJson, GoldenRoundTripsByteStable)
{
    if (sizeof(long) != 4)
        GTEST_SKIP() << "legacy fixtures encode the 32-bit ABI; run in the i386 container";

    const std::string bytes = build_two_message_board_bytes();

    if (std::getenv("UPDATE_GOLDENS") != nullptr) {
        ASSERT_EQ(sizeof(long), 4u) << "refusing to regenerate a 32-bit-ABI golden on a non-32-bit host; run in the i386 container";
        std::ofstream out(kGoldenPath, std::ios::binary);
        out << bytes;
        ASSERT_TRUE(out.good()) << "failed to write golden " << kGoldenPath;
        GTEST_SKIP() << "Golden written to " << kGoldenPath << "; rerun without UPDATE_GOLDENS to verify.";
    }

    const std::string golden = read_file(kGoldenPath);
    ASSERT_FALSE(golden.empty()) << "run GoldenRoundTripsByteStable with UPDATE_GOLDENS=1 first, from src/tests/";
    ASSERT_EQ(golden, bytes) << "legacy .boa on-disk layout drifted -- this must never change; "
                                "if it did on purpose, rerun with UPDATE_GOLDENS=1 and commit.";

    boards_json::BoardSaveData data;
    std::string error;
    ASSERT_TRUE(boards_json::legacy_board_file_from_binary(golden, &data, &error)) << error;
    ASSERT_EQ(2u, data.messages.size());
    EXPECT_EQ("Sat Nov  8 (Zenith)     :: zone layout", data.messages[0].heading);
}
