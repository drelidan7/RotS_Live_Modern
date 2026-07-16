#include "../objects_json.h"
#include "legacy_rent_fixture.h"
#include "rots/persist/file_formats.h"
#include "rots/core/character.h"
#include "rots/core/types.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

// Characterization tests for the legacy 32-bit on-disk `.obj` (rent) layout,
// documented in docs/data-formats/object-rent-files.md and defined by
// structs.h's `rent_info` / `obj_file_elem` / `follower_file_elem`. These
// tests build byte buffers the way the CURRENT (32-bit container) native
// struct layout lays them out -- i.e. exactly what the historical fwrite()
// wrote to disk -- and decode them through the public objects_json API.
//
// This is the safety net for Task 1's rewrite of the binary decoders from a
// whole-struct memcpy (`read_pod<rent_info>` etc., ABI-dependent) to explicit
// little-endian offset reads (ABI-portable): these tests must pass BEFORE and
// AFTER that rewrite with byte-identical results. The GoldenRoundTripsByteStable
// test additionally freezes one such buffer to disk so the reference bytes
// outlive the 32-bit toolchain.
//
// The byte-buffer builders (make_rent_info, make_obj_file_elem,
// build_full_fixture_bytes, ...) now live in legacy_rent_fixture.h (Phase 2a
// Task 3), shared with convert_plrobjs_tests.cpp's corrupt/valid legacy-file
// fixtures, instead of being duplicated here.

using legacy_rent_fixture::append_pod;
using legacy_rent_fixture::build_full_fixture_bytes;
using legacy_rent_fixture::build_full_fixture_bytes_with_alias;
using legacy_rent_fixture::expect_full_fixture_decoded;
using legacy_rent_fixture::make_follower_terminator;
using legacy_rent_fixture::make_obj_file_elem;
using legacy_rent_fixture::make_rent_info;
using legacy_rent_fixture::make_sentinel_obj_file_elem;

namespace {

// ROTS_GOLDEN_DIR (set by src/CMakeLists.txt on the ageland_tests target) anchors
// this to an absolute path so the compare works under ctest, whose
// gtest_discover_tests runs the binary with WORKING_DIRECTORY at the repo root
// rather than src/tests/. The src/tests/Makefile build doesn't define it, so it
// falls back to the plain relative path it has always used (cwd is src/tests/
// there). Mirrors characterization_json_tests.cpp / characterization_combat_tests.cpp.
#ifdef ROTS_GOLDEN_DIR
const char* const kGoldenPath = ROTS_GOLDEN_DIR "/legacy_rent_fixture.bin";
#else
const char* const kGoldenPath = "goldens/legacy_rent_fixture.bin";
#endif

std::string read_binary_file(const char* path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream contents;
    contents << in.rdbuf();
    return contents.str();
}

} // namespace

TEST(ObjectsJsonLayout, DecodesRentObjectBoardAliasAndFollowerRecordsFieldForField)
{
    if (sizeof(long) != 4)
        GTEST_SKIP() << "legacy fixtures encode the 32-bit ABI; run in the i386 container";

    const std::string bytes = build_full_fixture_bytes();

    objects_json::ObjectSaveData data;
    std::string error;
    ASSERT_TRUE(objects_json::object_save_data_from_binary(bytes, &data, &error)) << error;
    expect_full_fixture_decoded(data);
}

TEST(ObjectsJsonLayout, LegacyDecoderToleratesMissingFollowerSectionAndSetsFlag)
{
    if (sizeof(long) != 4)
        GTEST_SKIP() << "legacy fixtures encode the 32-bit ABI; run in the i386 container";

    std::string bytes = build_full_fixture_bytes();
    // Trim everything from the follower section onward: rent(48) + 2 items +
    // sentinel (3 * 56) + board (MAX_MAXBOARD * 2) + one alias record
    // (20 keyword + 4 length + 8 command) + alias terminator (20).
    const size_t header_len = sizeof(rent_info) + 3 * sizeof(obj_file_elem)
        + static_cast<size_t>(MAX_MAXBOARD) * sizeof(sh_int) + 20 + sizeof(int) + 8 + 20;
    ASSERT_LT(header_len, bytes.size());
    bytes.resize(header_len);

    // The strict (non-legacy) decoder must still reject a file that ends
    // before the follower section -- only legacy_object_save_data_from_binary
    // tolerates this, for RENT_TIMEDOUT-style saves predating follower persistence.
    objects_json::ObjectSaveData strict_result;
    std::string strict_error;
    EXPECT_FALSE(objects_json::object_save_data_from_binary(bytes, &strict_result, &strict_error));
    EXPECT_NE(strict_error.find("Truncated objects data while reading follower record"), std::string::npos);

    objects_json::ObjectSaveData data;
    std::string error;
    bool accepted_missing_follower_section = false;
    ASSERT_TRUE(objects_json::legacy_object_save_data_from_binary(bytes, &data, &accepted_missing_follower_section, &error)) << error;
    EXPECT_TRUE(accepted_missing_follower_section);
    EXPECT_TRUE(data.followers.empty());
    EXPECT_EQ(1234567890, data.rent.time);
    ASSERT_EQ(2u, data.objects.size());
    EXPECT_EQ(3001, data.objects[0].item_number);
    ASSERT_EQ(1u, data.aliases.size());
    EXPECT_EQ("assist", data.aliases[0].keyword);
}

TEST(ObjectsJsonLayout, DecodesOldFormatItemNumberFromDeprecatedField)
{
    if (sizeof(long) != 4)
        GTEST_SKIP() << "legacy fixtures encode the 32-bit ABI; run in the i386 container";

    // Pre-widening saves stored the vnum in item_number_deprecated (sh_int)
    // and left the later-added, widened `item_number` (originally spare2) as
    // stack garbage. The decoder detects this by item_number_deprecated !=
    // DEPRECATED_ID_VALUE and falls back to it (Crash_load / objsave.cpp).
    std::string bytes;
    append_pod(&bytes, make_rent_info());

    obj_file_elem old_format_elem = make_obj_file_elem(0, WEAR_HEAD);
    old_format_elem.item_number_deprecated = 1500; // real vnum, old on-disk format
    old_format_elem.item_number = 0xBAD; // garbage the decoder must ignore
    append_pod(&bytes, old_format_elem);
    append_pod(&bytes, make_sentinel_obj_file_elem());

    for (int index = 0; index < MAX_MAXBOARD; ++index) {
        sh_int point = 0;
        append_pod(&bytes, point);
    }
    char alias_terminator[20] {};
    append_pod(&bytes, alias_terminator);
    append_pod(&bytes, make_follower_terminator());

    objects_json::ObjectSaveData data;
    std::string error;
    ASSERT_TRUE(objects_json::object_save_data_from_binary(bytes, &data, &error)) << error;
    ASSERT_EQ(1u, data.objects.size());
    EXPECT_EQ(1500, data.objects[0].item_number);
}

// Characterizes the "keyword fills all 20 bytes, no embedded NUL" edge case
// documented in object-rent-files.md's "Alias on-disk format" / "Latent bug"
// notes: alias_list::keyword is char[20] with no guaranteed NUL terminator,
// so a legacy keyword exactly 20 bytes long decodes as a full 20-character
// std::string (object_save_data_from_binary's std::find(...,'\0') finds
// nothing and keeps every byte). serialize_objects_to_json writes such a
// keyword unconditionally, so the JSON round trip must accept it back
// losslessly, and object_save_data_to_binary (the test/shim-only
// re-encoder) must reproduce the original 20-byte, no-trailing-NUL layout.
TEST(ObjectsJsonLayout, TwentyByteAliasKeywordWithNoEmbeddedNulRoundTripsLosslessly)
{
    if (sizeof(long) != 4)
        GTEST_SKIP() << "legacy fixtures encode the 32-bit ABI; run in the i386 container";

    const std::string full_length_keyword(20, 'k'); // exactly 20 bytes, no NUL anywhere
    const std::string bytes = build_full_fixture_bytes_with_alias(full_length_keyword, "kill orc");

    objects_json::ObjectSaveData decoded;
    std::string decode_error;
    ASSERT_TRUE(objects_json::object_save_data_from_binary(bytes, &decoded, &decode_error)) << decode_error;
    ASSERT_EQ(1u, decoded.aliases.size());
    EXPECT_EQ(20u, decoded.aliases[0].keyword.size());
    EXPECT_EQ(full_length_keyword, decoded.aliases[0].keyword);
    EXPECT_EQ("kill orc", decoded.aliases[0].command);

    const std::string json = objects_json::serialize_objects_to_json(decoded);

    objects_json::ObjectSaveData roundtripped;
    std::string json_error;
    ASSERT_TRUE(objects_json::deserialize_objects_from_json(json, &roundtripped, &json_error)) << json_error;
    ASSERT_EQ(1u, roundtripped.aliases.size());
    EXPECT_EQ(full_length_keyword, roundtripped.aliases[0].keyword);
    EXPECT_EQ(decoded.aliases[0].command, roundtripped.aliases[0].command);

    // The binary re-encoder must also accept the 20-byte keyword and lay it
    // out exactly like the original legacy file: all 20 bytes, no trailing NUL.
    std::string reencoded_bytes;
    std::string reencode_error;
    ASSERT_TRUE(objects_json::object_save_data_to_binary(roundtripped, &reencoded_bytes, &reencode_error)) << reencode_error;

    const size_t alias_keyword_offset = sizeof(rent_info) + 3 * sizeof(obj_file_elem)
        + static_cast<size_t>(MAX_MAXBOARD) * sizeof(sh_int);
    ASSERT_LE(alias_keyword_offset + 20, reencoded_bytes.size());
    EXPECT_EQ(full_length_keyword, reencoded_bytes.substr(alias_keyword_offset, 20));
}

// Freezes build_full_fixture_bytes()'s output as a permanent on-disk
// reference: regenerate with
//   UPDATE_GOLDENS=1 ./bin/tests --gtest_filter=ObjectsJsonLayout.GoldenRoundTripsByteStable
// (run from src/tests/ on the 32-bit build only -- it encodes the historical
// 32-bit compiler layout) whenever the documented on-disk format itself
// changes (it must not, for existing saves to keep loading). Comparing
// against a committed golden means these bytes remain the reference even
// after the 32-bit toolchain (and thus the ability to regenerate them from
// the native structs) is retired.
TEST(ObjectsJsonLayout, GoldenRoundTripsByteStable)
{
    if (sizeof(long) != 4)
        GTEST_SKIP() << "legacy fixtures encode the 32-bit ABI; run in the i386 container";

    const std::string bytes = build_full_fixture_bytes();

    if (std::getenv("UPDATE_GOLDENS") != nullptr) {
        ASSERT_EQ(sizeof(long), 4u) << "refusing to regenerate a 32-bit-ABI golden on a non-32-bit host; run in the i386 container";
        std::ofstream out(kGoldenPath, std::ios::binary);
        out << bytes;
        ASSERT_TRUE(out.good()) << "failed to write golden " << kGoldenPath;
        GTEST_SKIP() << "Golden written to " << kGoldenPath << "; rerun without UPDATE_GOLDENS to verify.";
    }

    const std::string golden = read_binary_file(kGoldenPath);
    ASSERT_FALSE(golden.empty()) << "run GoldenRoundTripsByteStable with UPDATE_GOLDENS=1 first, from src/tests/";
    ASSERT_EQ(golden, bytes) << "legacy .obj on-disk layout drifted -- this must never change; "
                                 "if it did on purpose, rerun with UPDATE_GOLDENS=1 and commit.";

    objects_json::ObjectSaveData data;
    std::string error;
    ASSERT_TRUE(objects_json::object_save_data_from_binary(golden, &data, &error)) << error;
    expect_full_fixture_decoded(data);
}
