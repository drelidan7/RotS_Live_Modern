#ifndef OBJECTS_JSON_H
#define OBJECTS_JSON_H

#include "rots/core/types.h" /* For MAX_OBJ_AFFECT, MAX_MAXBOARD */

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace objects_json {

static constexpr int OBJECTS_SCHEMA_VERSION = 1;

struct ObjectAffectData {
    int location = 0;
    int modifier = 0;
};

struct ObjectRecord {
    int item_number = 0;
    std::array<int, 5> values {};
    int extra_flags = 0;
    int weight = 0;
    int timer = 0;
    long bitvector = 0;
    std::array<ObjectAffectData, MAX_OBJ_AFFECT> affects {};
    int wear_pos = 0;
    int loaded_by = 0;
};

struct RentData {
    int time = 0;
    int rentcode = 0;
    int net_cost_per_hour = 0;
    int gold = 0;
    int nitems = 0;
    int spare0 = 0;
    int spare1 = 0;
    int spare2 = 0;
    int spare3 = 0;
    int spare4 = 0;
    int spare5 = 0;
    int spare6 = 0;
    int spare7 = 0;
};

struct AliasData {
    std::string keyword;
    std::string command;
};

struct FollowerData {
    int fol_vnum = 0;
    int mount_vnum = 0;
    int wimpy = 0;
    int exp = 0;
    int flag_config = 0;
    int spare1 = 0;
    int spare2 = 0;
    std::vector<ObjectRecord> objects;
};

struct ObjectSaveData {
    int version = OBJECTS_SCHEMA_VERSION;
    RentData rent;
    std::vector<ObjectRecord> objects;
    std::array<int, MAX_MAXBOARD> board_points {};
    std::vector<AliasData> aliases;
    std::vector<FollowerData> followers;
};

bool object_save_data_from_binary(const std::string& bytes, ObjectSaveData* data, std::string* error_message = nullptr);
bool legacy_object_save_data_from_binary(
    const std::string& bytes, ObjectSaveData* data, bool* accepted_missing_follower_section = nullptr, std::string* error_message = nullptr);

// Corrupt Legacy File Recovery (2026-07-07): a lenient, lossy structural
// decode for `.obj` rent files that fail even legacy_object_save_data_from_binary
// (empty, truncated, or otherwise garbled). Parses as much as is genuinely
// salvageable from the front of `bytes` and stops at the first
// truncated/invalid section rather than failing the whole file:
//
//   - rent header (48 bytes): required. Returns false (data untouched, an
//     error set) if fewer than 48 bytes are available at all -- "salvage
//     requires at least a valid rent header."
//   - top-level object records: kept one obj_file_elem at a time while a
//     complete 56-byte record is present; stops at the first
//     truncated/incomplete trailing record (that partial record's bytes are
//     dropped, counted in `*dropped_partial_record_count`, not an error).
//   - board points / aliases / followers: each subsequent section is
//     included ONLY if it parses fully intact (through its own terminator);
//     an incomplete section is dropped in its entirety (not partially
//     included), and no later section is attempted. Alias keywords are
//     sanitized per legacy_salvage::sanitize_fixed_width_field when (and
//     only when) they have no NUL within their 20-byte width, matching the
//     same policy exploits recovery applies to chtime/chVictimName.
//
// Trailing bytes after a fully-intact parse are silently ignored (unlike the
// strict decoders, which treat them as a hard error) -- recovery only ever
// adds tolerance, never new rejection modes.
//
// Returns true whenever a rent header was present (even if everything after
// it had to be dropped) so the caller can distinguish "no header, nothing to
// salvage" from "header salvaged, rest was empty/garbage."
// `dropped_partial_record_count`, if non-null, is set to the number of
// trailing partial top-level object records discarded (0 or 1; recovery
// stops at the first one).
bool recover_object_save_data_from_binary(
    const std::string& bytes, ObjectSaveData* data, int* dropped_partial_record_count = nullptr, std::string* error_message = nullptr);

bool object_save_data_to_binary(const ObjectSaveData& data, std::string* bytes, std::string* error_message = nullptr);
std::string serialize_objects_to_json(const ObjectSaveData& data);
/// Deserializes a bounded object-save JSON document, stopping at its first embedded null byte.
bool deserialize_objects_from_json(std::string_view json, ObjectSaveData *data,
                                   std::string *error_message = nullptr);

// Field-for-field structural equality (not a re-serialization/string compare):
// used by the plrobjs conversion sweep (convert_plrobjs.cpp) to verify a
// freshly-written .objs.json decodes back to the exact same data as the
// legacy .obj it was converted from, before the legacy file is ever touched.
bool object_save_data_equal(const ObjectSaveData& a, const ObjectSaveData& b);

} // namespace objects_json

#endif
