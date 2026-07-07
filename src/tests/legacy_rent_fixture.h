#pragma once

// Shared synthesized-legacy-`.obj`-byte-buffer helpers, extracted (Phase 2a
// Task 3) from objects_json_layout_tests.cpp so more than one test binary can
// build the same characterization fixtures without duplicating the byte
// layout. See objects_json_layout_tests.cpp's file header for the full
// rationale: these builders use the REAL native structs (rent_info,
// obj_file_elem, follower_file_elem) so append_pod reproduces the exact
// historical on-disk bytes on the 32-bit build these tests run under.
//
// Everything here is `inline` (not wrapped in an anonymous namespace) because
// this header is included by multiple test translation units
// (objects_json_layout_tests.cpp, convert_plrobjs_tests.cpp); anonymous-
// namespace functions would each get their own internal-linkage copy per TU,
// which works but trips -Wunused-function in whichever TU doesn't call every
// helper. `inline` gives one true definition across TUs instead.

#include "../objects_json.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

namespace legacy_rent_fixture {

template <typename T>
inline void append_pod(std::string* bytes, const T& value)
{
    bytes->append(reinterpret_cast<const char*>(&value), sizeof(T));
}

inline rent_info make_rent_info()
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

inline obj_file_elem make_obj_file_elem(int item_number, int wear_pos_value)
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

inline obj_file_elem make_sentinel_obj_file_elem()
{
    obj_file_elem elem {};
    elem.item_number_deprecated = DEPRECATED_ID_VALUE;
    elem.item_number = SENTINEL_ITEM_ID_VALUE;
    return elem;
}

inline follower_file_elem make_follower_file_elem()
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

inline follower_file_elem make_follower_terminator()
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
inline std::string build_full_fixture_bytes()
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

inline void expect_object_record_matches(const objects_json::ObjectRecord& record, int expected_item_number, int expected_wear_pos)
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

inline void expect_full_fixture_decoded(const objects_json::ObjectSaveData& data)
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

} // namespace legacy_rent_fixture
