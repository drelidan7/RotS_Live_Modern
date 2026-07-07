#include "../objects_json.h"

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

template <typename T>
void append_pod(std::string* bytes, const T& value)
{
    bytes->append(reinterpret_cast<const char*>(&value), sizeof(T));
}

// ---------------------------------------------------------------------------
// Builders below intentionally use the REAL native structs (not guessed
// stand-ins) so that, on the 32-bit build, `append_pod` reproduces the exact
// historical on-disk bytes: rent_info (structs.h ~1929), obj_file_elem
// (structs.h ~1905), follower_file_elem (structs.h ~1889), obj_affected_type
// (structs.h ~454). Every field each decoder populates gets a distinct value
// so a swapped/misread offset shows up as a wrong assertion rather than a
// coincidental match.
// ---------------------------------------------------------------------------

rent_info make_rent_info()
{
    rent_info rent {};
    rent.time = 1234567890;
    rent.rentcode = RENT_CAMP; // = 3
    rent.net_cost_per_hour = 250;
    rent.gold = 10000;
    rent.nitems = 2; // documented as unused/garbage; still round-tripped verbatim
    rent.spare0 = 11;
    rent.spare1 = 22;
    rent.spare2 = 33;
    rent.spare3 = 44;
    rent.spare4 = 55;
    rent.spare5 = 66;
    rent.spare6 = 77;
    rent.spare7 = 88;
    return rent;
}

obj_file_elem make_obj_file_elem(int item_number, int wear_pos_value)
{
    obj_file_elem elem {};
    elem.item_number_deprecated = DEPRECATED_ID_VALUE;
    elem.value[0] = 11;
    elem.value[1] = 22;
    elem.value[2] = 33;
    elem.value[3] = 44;
    elem.value[4] = 55;
    elem.extra_flags = 0x00F0;
    elem.weight = 7;
    elem.timer = 99;
    elem.bitvector = 0x12345678L; // the `long` that breaks on LP64
    elem.affected[0].location = APPLY_STR;
    elem.affected[0].modifier = 3;
    elem.affected[1].location = APPLY_OB;
    elem.affected[1].modifier = -4;
    elem.wear_pos = static_cast<sh_int>(wear_pos_value);
    elem.loaded_by = 777;
    elem.item_number = item_number;
    return elem;
}

obj_file_elem make_sentinel_obj_file_elem()
{
    obj_file_elem elem {};
    elem.item_number_deprecated = DEPRECATED_ID_VALUE;
    elem.item_number = SENTINEL_ITEM_ID_VALUE;
    return elem;
}

follower_file_elem make_follower_file_elem()
{
    follower_file_elem follower {};
    follower.fol_vnum = 4001;
    follower.mount_vnum = 4002;
    follower.wimpy = 12;
    follower.exp = 345678;
    follower.flag_config = 2;
    follower.spare1 = 9;
    follower.spare2 = 10;
    return follower;
}

follower_file_elem make_follower_terminator()
{
    follower_file_elem follower {};
    follower.fol_vnum = SENTINEL_ITEM_ID_VALUE;
    return follower;
}

// Builds the full historical byte layout: rent header, two carried items, the
// item-list/alias sentinel, an (undersized-tolerant but here full-size) board
// array, one alias plus its terminator, one follower with one equipped item,
// and the follower-list terminator. Mirrors Crash_rentsave's write order
// (docs/data-formats/object-rent-files.md "File layout").
std::string build_full_fixture_bytes()
{
    std::string bytes;
    append_pod(&bytes, make_rent_info());
    append_pod(&bytes, make_obj_file_elem(3001, WEAR_HEAD));
    append_pod(&bytes, make_obj_file_elem(3002, MAX_WEAR));
    append_pod(&bytes, make_sentinel_obj_file_elem());

    for (int index = 0; index < MAX_MAXBOARD; ++index) {
        sh_int point = static_cast<sh_int>(100 + index);
        append_pod(&bytes, point);
    }

    char alias_keyword[20] {};
    std::strcpy(alias_keyword, "assist");
    append_pod(&bytes, alias_keyword);
    const int command_length = 8;
    append_pod(&bytes, command_length);
    bytes.append("kill orc");

    char alias_terminator[20] {};
    append_pod(&bytes, alias_terminator);

    append_pod(&bytes, make_follower_file_elem());
    append_pod(&bytes, make_obj_file_elem(2200, WEAR_BODY));
    append_pod(&bytes, make_sentinel_obj_file_elem());
    append_pod(&bytes, make_follower_terminator());

    return bytes;
}

void expect_object_record_matches(const objects_json::ObjectRecord& record, int expected_item_number, int expected_wear_pos)
{
    EXPECT_EQ(expected_item_number, record.item_number);
    ASSERT_EQ(5u, record.values.size());
    EXPECT_EQ(11, record.values[0]);
    EXPECT_EQ(22, record.values[1]);
    EXPECT_EQ(33, record.values[2]);
    EXPECT_EQ(44, record.values[3]);
    EXPECT_EQ(55, record.values[4]);
    EXPECT_EQ(0x00F0, record.extra_flags);
    EXPECT_EQ(7, record.weight);
    EXPECT_EQ(99, record.timer);
    EXPECT_EQ(0x12345678L, record.bitvector);
    ASSERT_EQ(2u, record.affects.size());
    EXPECT_EQ(APPLY_STR, record.affects[0].location);
    EXPECT_EQ(3, record.affects[0].modifier);
    EXPECT_EQ(APPLY_OB, record.affects[1].location);
    EXPECT_EQ(-4, record.affects[1].modifier);
    EXPECT_EQ(expected_wear_pos, record.wear_pos);
    EXPECT_EQ(777, record.loaded_by);
}

void expect_full_fixture_decoded(const objects_json::ObjectSaveData& data)
{
    EXPECT_EQ(1234567890, data.rent.time);
    EXPECT_EQ(RENT_CAMP, data.rent.rentcode);
    EXPECT_EQ(250, data.rent.net_cost_per_hour);
    EXPECT_EQ(10000, data.rent.gold);
    EXPECT_EQ(2, data.rent.nitems);
    EXPECT_EQ(11, data.rent.spare0);
    EXPECT_EQ(22, data.rent.spare1);
    EXPECT_EQ(33, data.rent.spare2);
    EXPECT_EQ(44, data.rent.spare3);
    EXPECT_EQ(55, data.rent.spare4);
    EXPECT_EQ(66, data.rent.spare5);
    EXPECT_EQ(77, data.rent.spare6);
    EXPECT_EQ(88, data.rent.spare7);

    ASSERT_EQ(2u, data.objects.size());
    expect_object_record_matches(data.objects[0], 3001, WEAR_HEAD);
    expect_object_record_matches(data.objects[1], 3002, MAX_WEAR);

    ASSERT_EQ(static_cast<size_t>(MAX_MAXBOARD), data.board_points.size());
    for (int index = 0; index < MAX_MAXBOARD; ++index)
        EXPECT_EQ(100 + index, data.board_points[index]);

    ASSERT_EQ(1u, data.aliases.size());
    EXPECT_EQ("assist", data.aliases[0].keyword);
    EXPECT_EQ("kill orc", data.aliases[0].command);

    ASSERT_EQ(1u, data.followers.size());
    const objects_json::FollowerData& follower = data.followers[0];
    EXPECT_EQ(4001, follower.fol_vnum);
    EXPECT_EQ(4002, follower.mount_vnum);
    EXPECT_EQ(12, follower.wimpy);
    EXPECT_EQ(345678, follower.exp);
    EXPECT_EQ(2, follower.flag_config);
    EXPECT_EQ(9, follower.spare1);
    EXPECT_EQ(10, follower.spare2);
    ASSERT_EQ(1u, follower.objects.size());
    expect_object_record_matches(follower.objects[0], 2200, WEAR_BODY);
}

} // namespace

TEST(ObjectsJsonLayout, DecodesRentObjectBoardAliasAndFollowerRecordsFieldForField)
{
    const std::string bytes = build_full_fixture_bytes();

    objects_json::ObjectSaveData data;
    std::string error;
    ASSERT_TRUE(objects_json::object_save_data_from_binary(bytes, &data, &error)) << error;
    expect_full_fixture_decoded(data);
}

TEST(ObjectsJsonLayout, LegacyDecoderToleratesMissingFollowerSectionAndSetsFlag)
{
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
    const std::string bytes = build_full_fixture_bytes();

    if (std::getenv("UPDATE_GOLDENS") != nullptr) {
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
