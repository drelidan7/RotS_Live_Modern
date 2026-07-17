#include "objects_json.h"
#include "json_utils.h"
#include "legacy_salvage.h"
#include "text_view.h"
#include "rots/persist/file_formats.h"
#include "rots/core/types.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <format>
#include <iterator>
#include <limits>
#include <sstream>

namespace objects_json {
namespace {

    void set_error(std::string* error_message, std::string_view message)
    {
        if (error_message)
            error_message->assign(rots::text::truncate_at_null(message));
    }

    // read_pod remains valid for single scalars (sh_int board points, the alias
    // command-length int) and fixed char arrays (the 20-byte alias keyword
    // buffers): those have no compiler-dependent padding or width, unlike
    // rent_info / obj_file_elem / follower_file_elem below, which mix `long`
    // (changes size on LP64) with structure padding baked into the on-disk
    // format. Those three are decoded by the explicit-offset readers instead.
    template <typename T>
    bool read_pod(const std::string &bytes, size_t *offset, T *value, std::string *error_message,
                  std::string_view label) {
        label = rots::text::truncate_at_null(label);
        if (offset == nullptr || value == nullptr) {
            set_error(error_message, "Binary parse output parameter must not be null.");
            return false;
        }

        if (*offset + sizeof(T) > bytes.size()) {
            set_error(error_message, std::string("Truncated objects data while reading ") +
                                         std::string(label) + ".");
            return false;
        }

        std::memcpy(value, bytes.data() + *offset, sizeof(T));
        *offset += sizeof(T);
        return true;
    }

    // ------------------------------------------------------------------
    // Explicit-offset little-endian readers for the legacy 32-bit on-disk
    // layout of rent_info / obj_file_elem / follower_file_elem. These
    // deliberately do NOT memcpy into the native structs: on a 64-bit build
    // sizeof(long) and struct padding change, but the bytes in lib/plrobjs
    // were written by the 32-bit game and never change. Offsets and sizes
    // are from docs/data-formats/object-rent-files.md, cross-checked against
    // structs.h and confirmed with a container-side sizeof/offsetof probe.
    // ------------------------------------------------------------------

    constexpr size_t kRentInfoDiskSize = 48;
    constexpr size_t kObjFileElemDiskSize = 56;
    constexpr size_t kFollowerFileElemDiskSize = 28;

    // These native-struct sizes only match the documented on-disk layout on
    // the 32-bit build (4-byte long, default alignment/padding); a 64-bit
    // build changes sizeof(long) and would trip this guard, so it is
    // compiled away there rather than firing a false alarm — the explicit
    // offsets above are what actually decode the on-disk bytes on any ABI.
#if !defined(__LP64__) && !defined(_WIN64)
    static_assert(sizeof(rent_info) == kRentInfoDiskSize, "rent_info on-disk layout doc drift");
    static_assert(sizeof(obj_file_elem) == kObjFileElemDiskSize, "obj_file_elem on-disk layout doc drift");
    static_assert(sizeof(follower_file_elem) == kFollowerFileElemDiskSize, "follower_file_elem on-disk layout doc drift");
#endif

    uint32_t read_u32le(const std::string& bytes, size_t offset)
    {
        return static_cast<uint32_t>(static_cast<unsigned char>(bytes[offset]))
            | (static_cast<uint32_t>(static_cast<unsigned char>(bytes[offset + 1])) << 8)
            | (static_cast<uint32_t>(static_cast<unsigned char>(bytes[offset + 2])) << 16)
            | (static_cast<uint32_t>(static_cast<unsigned char>(bytes[offset + 3])) << 24);
    }

    int32_t read_s32le(const std::string& bytes, size_t offset)
    {
        return static_cast<int32_t>(read_u32le(bytes, offset));
    }

    int16_t read_s16le(const std::string& bytes, size_t offset)
    {
        return static_cast<int16_t>(static_cast<uint16_t>(static_cast<unsigned char>(bytes[offset]))
            | (static_cast<uint16_t>(static_cast<unsigned char>(bytes[offset + 1])) << 8));
    }

    bool check_bounds(const std::string &bytes, size_t offset, size_t length,
                      std::string *error_message, std::string_view label) {
        label = rots::text::truncate_at_null(label);
        if (offset + length > bytes.size()) {
            set_error(error_message, std::string("Truncated objects data while reading ") +
                                         std::string(label) + ".");
            return false;
        }
        return true;
    }

    // Portable in-memory mirror of `rent_info` (structs.h), populated by
    // explicit-offset reads instead of a struct-shaped memcpy.
    struct DecodedRentInfo {
        int32_t time = 0;
        int32_t rentcode = 0;
        int32_t net_cost_per_hour = 0;
        int32_t gold = 0;
        int32_t nitems = 0;
        int16_t spare0 = 0;
        int16_t spare1 = 0;
        int16_t spare2 = 0;
        int32_t spare3 = 0;
        int32_t spare4 = 0;
        int32_t spare5 = 0;
        int32_t spare6 = 0;
        int32_t spare7 = 0;
    };

    // Offsets: docs/data-formats/object-rent-files.md "rent_info" table.
    bool read_rent_info(const std::string& bytes, size_t* offset, DecodedRentInfo* rent, std::string* error_message)
    {
        if (!check_bounds(bytes, *offset, kRentInfoDiskSize, error_message, "rent data"))
            return false;

        const size_t base = *offset;
        rent->time = read_s32le(bytes, base + 0);
        rent->rentcode = read_s32le(bytes, base + 4);
        rent->net_cost_per_hour = read_s32le(bytes, base + 8);
        rent->gold = read_s32le(bytes, base + 12);
        rent->nitems = read_s32le(bytes, base + 16);
        rent->spare0 = read_s16le(bytes, base + 20);
        rent->spare1 = read_s16le(bytes, base + 22);
        rent->spare2 = read_s16le(bytes, base + 24);
        // Offsets 26-27: 2 bytes of compiler padding aligning spare3; not read.
        rent->spare3 = read_s32le(bytes, base + 28);
        rent->spare4 = read_s32le(bytes, base + 32);
        rent->spare5 = read_s32le(bytes, base + 36);
        rent->spare6 = read_s32le(bytes, base + 40);
        rent->spare7 = read_s32le(bytes, base + 44);
        *offset += kRentInfoDiskSize;
        return true;
    }

    // Portable in-memory mirror of `obj_file_elem` (structs.h). `bitvector`
    // is read as a 32-bit value: the documented on-disk field is the 32-bit
    // build's 4-byte `long`, not whatever width `long` has on the reading ABI.
    struct DecodedObjFileElem {
        int16_t item_number_deprecated = 0;
        int16_t value[5] = { 0, 0, 0, 0, 0 };
        int32_t extra_flags = 0;
        int32_t weight = 0;
        int32_t timer = 0;
        int32_t bitvector = 0;
        struct {
            uint8_t location = 0;
            int32_t modifier = 0;
        } affected[MAX_OBJ_AFFECT];
        int16_t wear_pos = 0;
        int32_t loaded_by = 0;
        int32_t item_number = 0;
    };

    // Offsets: docs/data-formats/object-rent-files.md "obj_file_elem" table.
    bool read_obj_file_elem(const std::string &bytes, size_t *offset, DecodedObjFileElem *elem,
                            std::string *error_message, std::string_view label) {
        label = rots::text::truncate_at_null(label);
        if (!check_bounds(bytes, *offset, kObjFileElemDiskSize, error_message, label))
            return false;

        const size_t base = *offset;
        elem->item_number_deprecated = read_s16le(bytes, base + 0);
        for (size_t index = 0; index < 5; ++index)
            elem->value[index] = read_s16le(bytes, base + 2 + index * 2);
        elem->extra_flags = read_s32le(bytes, base + 12);
        elem->weight = read_s32le(bytes, base + 16);
        elem->timer = read_s32le(bytes, base + 20);
        elem->bitvector = read_s32le(bytes, base + 24);
        for (size_t index = 0; index < MAX_OBJ_AFFECT; ++index) {
            const size_t affect_base = base + 28 + index * 8;
            elem->affected[index].location = static_cast<uint8_t>(bytes[affect_base]);
            // Bytes affect_base+1..+3: 3 bytes of compiler padding; not read.
            elem->affected[index].modifier = read_s32le(bytes, affect_base + 4);
        }
        elem->wear_pos = read_s16le(bytes, base + 44);
        // Bytes base+46..+47: 2 bytes of compiler padding aligning loaded_by; not read.
        elem->loaded_by = read_s32le(bytes, base + 48);
        elem->item_number = read_s32le(bytes, base + 52);
        *offset += kObjFileElemDiskSize;
        return true;
    }

    // Portable in-memory mirror of `follower_file_elem` (structs.h) — seven
    // ints with no padding, but still read explicitly for ABI portability
    // and consistency with the two structs above.
    struct DecodedFollowerFileElem {
        int32_t fol_vnum = 0;
        int32_t mount_vnum = 0;
        int32_t wimpy = 0;
        int32_t exp = 0;
        int32_t flag_config = 0;
        int32_t spare1 = 0;
        int32_t spare2 = 0;
    };

    // Offsets: docs/data-formats/object-rent-files.md "follower_file_elem" table.
    bool read_follower_file_elem(const std::string& bytes, size_t* offset, DecodedFollowerFileElem* follower, std::string* error_message)
    {
        if (!check_bounds(bytes, *offset, kFollowerFileElemDiskSize, error_message, "follower record"))
            return false;

        const size_t base = *offset;
        follower->fol_vnum = read_s32le(bytes, base + 0);
        follower->mount_vnum = read_s32le(bytes, base + 4);
        follower->wimpy = read_s32le(bytes, base + 8);
        follower->exp = read_s32le(bytes, base + 12);
        follower->flag_config = read_s32le(bytes, base + 16);
        follower->spare1 = read_s32le(bytes, base + 20);
        follower->spare2 = read_s32le(bytes, base + 24);
        *offset += kFollowerFileElemDiskSize;
        return true;
    }

    // Reads one obj_file_elem record and converts it to an ObjectRecord,
    // applying the legacy item-number fallback (old-format saves stored the
    // vnum in the narrower item_number_deprecated field and left the later,
    // widened item_number field as stack garbage). Sets *is_sentinel and
    // leaves *record untouched when the record is the -17 list terminator.
    bool read_object_record_or_sentinel(const std::string &bytes, size_t *offset,
                                        ObjectRecord *record, bool *is_sentinel,
                                        std::string *error_message, std::string_view label) {
        label = rots::text::truncate_at_null(label);
        DecodedObjFileElem raw_object {};
        if (!read_obj_file_elem(bytes, offset, &raw_object, error_message, label))
            return false;

        if (raw_object.item_number_deprecated != DEPRECATED_ID_VALUE)
            raw_object.item_number = raw_object.item_number_deprecated;

        if (raw_object.item_number == SENTINEL_ITEM_ID_VALUE) {
            *is_sentinel = true;
            return true;
        }

        *is_sentinel = false;
        record->item_number = raw_object.item_number;
        for (size_t index = 0; index < record->values.size(); ++index)
            record->values[index] = raw_object.value[index];
        record->extra_flags = raw_object.extra_flags;
        record->weight = raw_object.weight;
        record->timer = raw_object.timer;
        record->bitvector = raw_object.bitvector;
        for (size_t index = 0; index < record->affects.size(); ++index) {
            record->affects[index].location = raw_object.affected[index].location;
            record->affects[index].modifier = raw_object.affected[index].modifier;
        }
        record->wear_pos = raw_object.wear_pos;
        record->loaded_by = raw_object.loaded_by;
        return true;
    }

    template <typename T>
    void append_pod(std::string* bytes, const T& value)
    {
        bytes->append(reinterpret_cast<const char*>(&value), sizeof(T));
    }

    template <typename TargetType>
    bool validate_narrowed_range(long long value, std::string_view field_name,
                                 std::string *error_message) {
        field_name = rots::text::truncate_at_null(field_name);
        if (value < static_cast<long long>(std::numeric_limits<TargetType>::min())
            || value > static_cast<long long>(std::numeric_limits<TargetType>::max())) {
            set_error(error_message, std::string(field_name) + " is outside the supported storage range.");
            return false;
        }

        return true;
    }

    bool parse_exact_integer_array(json_utils::JsonReader* reader, size_t expected_size, std::vector<long>* values, std::string* error_message)
    {
        std::vector<long> parsed_values;
        if (!reader->parse_array([&parsed_values](json_utils::JsonReader* nested_reader, std::string* nested_error_message) {
                long value = 0;
                if (!nested_reader->parse_long(&value, nested_error_message))
                    return false;
                parsed_values.push_back(value);
                return true;
            }, error_message)) {
            return false;
        }

        if (parsed_values.size() != expected_size) {
            set_error(error_message, "Objects JSON array length did not match the expected size.");
            return false;
        }

        *values = std::move(parsed_values);
        set_error(error_message, "");
        return true;
    }

    bool parse_object_affect(json_utils::JsonReader* reader, ObjectAffectData* affect, std::string* error_message)
    {
        if (affect == nullptr) {
            set_error(error_message, "Object affect output parameter must not be null.");
            return false;
        }

        ObjectAffectData parsed_affect;
        bool saw_location = false;
        bool saw_modifier = false;
        if (!reader->parse_object([&parsed_affect, &saw_location, &saw_modifier](std::string_view key, json_utils::JsonReader* nested_reader, std::string* nested_error_message) {
                if (key == "location")
                    return saw_location = true, nested_reader->parse_integer(&parsed_affect.location, nested_error_message);
                if (key == "modifier")
                    return saw_modifier = true, nested_reader->parse_integer(&parsed_affect.modifier, nested_error_message);
                return nested_reader->skip_value(nested_error_message);
            }, error_message)) {
            return false;
        }

        if (!saw_location || !saw_modifier) {
            set_error(error_message, "Object affect record was missing one or more required fields.");
            return false;
        }

        *affect = parsed_affect;
        set_error(error_message, "");
        return true;
    }

    bool parse_object_record(json_utils::JsonReader* reader, ObjectRecord* record, std::string* error_message)
    {
        if (record == nullptr) {
            set_error(error_message, "Object record output parameter must not be null.");
            return false;
        }

        ObjectRecord parsed_record;
        bool saw_item_number = false;
        bool saw_values = false;
        bool saw_extra_flags = false;
        bool saw_weight = false;
        bool saw_timer = false;
        bool saw_bitvector = false;
        bool saw_wear_pos = false;
        bool saw_loaded_by = false;
        bool saw_affects = false;
        if (!reader->parse_object([&parsed_record, &saw_item_number, &saw_values, &saw_extra_flags, &saw_weight, &saw_timer, &saw_bitvector, &saw_wear_pos, &saw_loaded_by, &saw_affects](std::string_view key, json_utils::JsonReader* nested_reader, std::string* nested_error_message) {
                if (key == "item_number")
                    return saw_item_number = true, nested_reader->parse_integer(&parsed_record.item_number, nested_error_message);
                if (key == "values") {
                    saw_values = true;
                    std::vector<long> values;
                    if (!parse_exact_integer_array(nested_reader, parsed_record.values.size(), &values, nested_error_message))
                        return false;
                    for (size_t index = 0; index < values.size(); ++index)
                        parsed_record.values[index] = static_cast<int>(values[index]);
                    return true;
                }
                if (key == "extra_flags")
                    return saw_extra_flags = true, nested_reader->parse_integer(&parsed_record.extra_flags, nested_error_message);
                if (key == "weight")
                    return saw_weight = true, nested_reader->parse_integer(&parsed_record.weight, nested_error_message);
                if (key == "timer")
                    return saw_timer = true, nested_reader->parse_integer(&parsed_record.timer, nested_error_message);
                if (key == "bitvector")
                    return saw_bitvector = true, nested_reader->parse_long(&parsed_record.bitvector, nested_error_message);
                if (key == "wear_pos")
                    return saw_wear_pos = true, nested_reader->parse_integer(&parsed_record.wear_pos, nested_error_message);
                if (key == "loaded_by")
                    return saw_loaded_by = true, nested_reader->parse_integer(&parsed_record.loaded_by, nested_error_message);
                if (key == "affects") {
                    saw_affects = true;
                    std::vector<ObjectAffectData> affects;
                    if (!nested_reader->parse_array([&affects](json_utils::JsonReader* affect_reader, std::string* affect_error_message) {
                            ObjectAffectData affect;
                            if (!parse_object_affect(affect_reader, &affect, affect_error_message))
                                return false;
                            affects.push_back(affect);
                            return true;
                        }, nested_error_message)) {
                        return false;
                    }

                    if (affects.size() != parsed_record.affects.size()) {
                        set_error(nested_error_message, "Object affect array length did not match MAX_OBJ_AFFECT.");
                        return false;
                    }

                    for (size_t index = 0; index < affects.size(); ++index)
                        parsed_record.affects[index] = affects[index];
                    return true;
                }
                return nested_reader->skip_value(nested_error_message);
            }, error_message)) {
            return false;
        }

        if (!saw_item_number || !saw_values || !saw_extra_flags || !saw_weight || !saw_timer || !saw_bitvector || !saw_wear_pos || !saw_loaded_by || !saw_affects) {
            set_error(error_message, "Object record was missing one or more required fields.");
            return false;
        }

        *record = parsed_record;
        set_error(error_message, "");
        return true;
    }

    bool parse_alias_record(json_utils::JsonReader* reader, AliasData* alias, std::string* error_message)
    {
        AliasData parsed_alias;
        bool saw_keyword = false;
        bool saw_command = false;
        if (!reader->parse_object([&parsed_alias, &saw_keyword, &saw_command](std::string_view key, json_utils::JsonReader* nested_reader, std::string* nested_error_message) {
                if (key == "keyword")
                    return saw_keyword = true, nested_reader->parse_string(&parsed_alias.keyword, nested_error_message);
                if (key == "command")
                    return saw_command = true, nested_reader->parse_string(&parsed_alias.command, nested_error_message);
                return nested_reader->skip_value(nested_error_message);
            }, error_message)) {
            return false;
        }

        if (!saw_keyword || !saw_command) {
            set_error(error_message, "Alias record was missing one or more required fields.");
            return false;
        }

        // Legacy alias_list::keyword is char[20] with no guaranteed NUL
        // terminator: a keyword that fills all 20 bytes leaves no room for a
        // trailing NUL (docs/data-formats/object-rent-files.md "Alias on-disk
        // format"), and object_save_data_from_binary's std::find(...,'\0')
        // then decodes the full 20 bytes as the keyword (objects_json.cpp
        // ~line 586). serialize_objects_to_json writes such a keyword
        // unconditionally, so JSON must accept it back losslessly -- reject
        // only keywords that could never have come from that 20-byte field.
        if (parsed_alias.keyword.size() > 20) {
            set_error(error_message, "Alias keyword must not exceed 20 characters.");
            return false;
        }

        *alias = std::move(parsed_alias);
        set_error(error_message, "");
        return true;
    }

    bool parse_follower_record(json_utils::JsonReader* reader, FollowerData* follower, std::string* error_message)
    {
        FollowerData parsed_follower;
        bool saw_fol_vnum = false;
        bool saw_mount_vnum = false;
        bool saw_wimpy = false;
        bool saw_exp = false;
        bool saw_flag_config = false;
        bool saw_spare1 = false;
        bool saw_spare2 = false;
        bool saw_objects = false;
        if (!reader->parse_object([&parsed_follower, &saw_fol_vnum, &saw_mount_vnum, &saw_wimpy, &saw_exp, &saw_flag_config, &saw_spare1, &saw_spare2, &saw_objects](std::string_view key, json_utils::JsonReader* nested_reader, std::string* nested_error_message) {
                if (key == "fol_vnum")
                    return saw_fol_vnum = true, nested_reader->parse_integer(&parsed_follower.fol_vnum, nested_error_message);
                if (key == "mount_vnum")
                    return saw_mount_vnum = true, nested_reader->parse_integer(&parsed_follower.mount_vnum, nested_error_message);
                if (key == "wimpy")
                    return saw_wimpy = true, nested_reader->parse_integer(&parsed_follower.wimpy, nested_error_message);
                if (key == "exp")
                    return saw_exp = true, nested_reader->parse_integer(&parsed_follower.exp, nested_error_message);
                if (key == "flag_config")
                    return saw_flag_config = true, nested_reader->parse_integer(&parsed_follower.flag_config, nested_error_message);
                if (key == "spare1")
                    return saw_spare1 = true, nested_reader->parse_integer(&parsed_follower.spare1, nested_error_message);
                if (key == "spare2")
                    return saw_spare2 = true, nested_reader->parse_integer(&parsed_follower.spare2, nested_error_message);
                if (key == "objects") {
                    saw_objects = true;
                    std::vector<ObjectRecord> objects;
                    if (!nested_reader->parse_array([&objects](json_utils::JsonReader* object_reader, std::string* object_error_message) {
                            ObjectRecord object_record;
                            if (!parse_object_record(object_reader, &object_record, object_error_message))
                                return false;
                            objects.push_back(object_record);
                            return true;
                        }, nested_error_message)) {
                        return false;
                    }
                    parsed_follower.objects = std::move(objects);
                    return true;
                }
                return nested_reader->skip_value(nested_error_message);
            }, error_message)) {
            return false;
        }

        if (!saw_fol_vnum || !saw_mount_vnum || !saw_wimpy || !saw_exp || !saw_flag_config || !saw_spare1 || !saw_spare2 || !saw_objects) {
            set_error(error_message, "Follower record was missing one or more required fields.");
            return false;
        }

        *follower = std::move(parsed_follower);
        set_error(error_message, "");
        return true;
    }

    void write_object_record(std::string &output, const ObjectRecord &record,
                             std::string_view indent) {
        indent = rots::text::truncate_at_null(indent);
        output.append(indent);
        output.append("{\n");
        output.append(indent);
        std::format_to(std::back_inserter(output), "  \"item_number\": {},\n", record.item_number);
        output.append(indent);
        output.append("  \"values\": [");
        for (size_t index = 0; index < record.values.size(); ++index) {
            if (index > 0)
                output.append(", ");
            std::format_to(std::back_inserter(output), "{}", record.values[index]);
        }
        output.append("],\n");
        output.append(indent);
        std::format_to(std::back_inserter(output), "  \"extra_flags\": {},\n", record.extra_flags);
        output.append(indent);
        std::format_to(std::back_inserter(output), "  \"weight\": {},\n", record.weight);
        output.append(indent);
        std::format_to(std::back_inserter(output), "  \"timer\": {},\n", record.timer);
        output.append(indent);
        std::format_to(std::back_inserter(output), "  \"bitvector\": {},\n", record.bitvector);
        output.append(indent);
        std::format_to(std::back_inserter(output), "  \"wear_pos\": {},\n", record.wear_pos);
        output.append(indent);
        std::format_to(std::back_inserter(output), "  \"loaded_by\": {},\n", record.loaded_by);
        output.append(indent);
        output.append("  \"affects\": [\n");
        for (size_t index = 0; index < record.affects.size(); ++index) {
            const ObjectAffectData& affect = record.affects[index];
            output.append(indent);
            output.append("    {\"location\": ");
            std::format_to(std::back_inserter(output), "{}, \"modifier\": {}", affect.location, affect.modifier);
            output.append("}");
            if (index + 1 < record.affects.size())
                output.append(",");
            output.append("\n");
        }
        output.append(indent);
        output.append("  ]\n");
        output.append(indent);
        output.append("}");
    }

bool object_save_data_from_binary_impl(
    const std::string& bytes, ObjectSaveData* data, bool allow_missing_follower_section, bool* accepted_missing_follower_section, std::string* error_message)
{
    if (accepted_missing_follower_section != nullptr)
        *accepted_missing_follower_section = false;

    if (data == nullptr) {
        set_error(error_message, "Objects data output parameter must not be null.");
        return false;
    }

    ObjectSaveData parsed_data;
    size_t offset = 0;

    DecodedRentInfo raw_rent {};
    if (!read_rent_info(bytes, &offset, &raw_rent, error_message))
        return false;

    parsed_data.rent.time = raw_rent.time;
    parsed_data.rent.rentcode = raw_rent.rentcode;
    parsed_data.rent.net_cost_per_hour = raw_rent.net_cost_per_hour;
    parsed_data.rent.gold = raw_rent.gold;
    parsed_data.rent.nitems = raw_rent.nitems;
    parsed_data.rent.spare0 = raw_rent.spare0;
    parsed_data.rent.spare1 = raw_rent.spare1;
    parsed_data.rent.spare2 = raw_rent.spare2;
    parsed_data.rent.spare3 = raw_rent.spare3;
    parsed_data.rent.spare4 = raw_rent.spare4;
    parsed_data.rent.spare5 = raw_rent.spare5;
    parsed_data.rent.spare6 = raw_rent.spare6;
    parsed_data.rent.spare7 = raw_rent.spare7;

    while (true) {
        ObjectRecord record;
        bool is_sentinel = false;
        if (!read_object_record_or_sentinel(bytes, &offset, &record, &is_sentinel, error_message, "top-level object record"))
            return false;

        if (is_sentinel)
            break;

        parsed_data.objects.push_back(std::move(record));
    }

    for (size_t index = 0; index < parsed_data.board_points.size(); ++index) {
        sh_int point = 0;
        if (!read_pod(bytes, &offset, &point, error_message, "board point"))
            return false;
        parsed_data.board_points[index] = point;
    }

    while (true) {
        char keyword_bytes[20] {};
        if (!read_pod(bytes, &offset, &keyword_bytes, error_message, "alias keyword"))
            return false;

        if (keyword_bytes[0] == '\0')
            break;

        int command_length = 0;
        if (!read_pod(bytes, &offset, &command_length, error_message, "alias command length"))
            return false;

        if (command_length < 0 || offset + static_cast<size_t>(command_length) > bytes.size()) {
            set_error(error_message, "Alias command length is malformed.");
            return false;
        }

        AliasData alias;
        alias.keyword.assign(keyword_bytes, std::find(keyword_bytes, keyword_bytes + sizeof(keyword_bytes), '\0'));
        alias.command.assign(bytes.data() + offset, static_cast<size_t>(command_length));
        offset += static_cast<size_t>(command_length);
        parsed_data.aliases.push_back(std::move(alias));
    }

    bool saw_follower_section = false;
    while (true) {
        DecodedFollowerFileElem raw_follower {};
        if (offset == bytes.size() && allow_missing_follower_section && !saw_follower_section) {
            if (accepted_missing_follower_section != nullptr)
                *accepted_missing_follower_section = true;
            break;
        }
        if (!read_follower_file_elem(bytes, &offset, &raw_follower, error_message))
            return false;
        saw_follower_section = true;

        if (raw_follower.fol_vnum == SENTINEL_ITEM_ID_VALUE)
            break;

        FollowerData follower;
        follower.fol_vnum = raw_follower.fol_vnum;
        follower.mount_vnum = raw_follower.mount_vnum;
        follower.wimpy = raw_follower.wimpy;
        follower.exp = raw_follower.exp;
        follower.flag_config = raw_follower.flag_config;
        follower.spare1 = raw_follower.spare1;
        follower.spare2 = raw_follower.spare2;

        while (true) {
            ObjectRecord record;
            bool is_sentinel = false;
            if (!read_object_record_or_sentinel(bytes, &offset, &record, &is_sentinel, error_message, "follower object record"))
                return false;

            if (is_sentinel)
                break;

            follower.objects.push_back(std::move(record));
        }

        parsed_data.followers.push_back(std::move(follower));
    }

    if (offset != bytes.size()) {
        set_error(error_message, "Objects binary data had unexpected trailing bytes.");
        return false;
    }

    *data = std::move(parsed_data);
    set_error(error_message, "");
    return true;
}

// Attempts to read one complete object list (repeated obj_file_elem records
// through the -17 sentinel) starting at `*offset`, entirely within bounds.
// On success, advances `*offset` past the sentinel and returns the records
// read (possibly empty). On any truncation/failure, `*offset` is left
// UNCHANGED (the caller's local scan offset, not the real cursor) and false
// is returned -- used by recovery's "fully intact or dropped wholesale"
// sections (a follower's own object list).
bool try_read_complete_object_list(const std::string &bytes, size_t *offset,
                                   std::vector<ObjectRecord> *records, std::string_view label) {
    label = rots::text::truncate_at_null(label);
    size_t local_offset = *offset;
    std::vector<ObjectRecord> parsed_records;
    while (true) {
        ObjectRecord record;
        bool is_sentinel = false;
        if (!read_object_record_or_sentinel(bytes, &local_offset, &record, &is_sentinel, nullptr, label))
            return false;
        if (is_sentinel)
            break;
        parsed_records.push_back(std::move(record));
    }
    *offset = local_offset;
    *records = std::move(parsed_records);
    return true;
}

// Corrupt Legacy File Recovery (2026-07-07): lenient structural salvage --
// see the full contract in objects_json.h. Lives in this anonymous
// namespace (not object_save_data_from_binary_impl's single linear pass)
// because unlike the strict decoders, each section here can independently
// stop the parse without failing the whole file.
bool recover_object_save_data_from_binary_impl(
    const std::string& bytes, ObjectSaveData* data, int* dropped_partial_record_count, std::string* error_message)
{
    if (dropped_partial_record_count != nullptr)
        *dropped_partial_record_count = 0;

    if (data == nullptr) {
        set_error(error_message, "Objects data output parameter must not be null.");
        return false;
    }

    if (bytes.size() < kRentInfoDiskSize) {
        set_error(error_message, "No valid rent header: fewer than 48 bytes available.");
        return false;
    }

    ObjectSaveData parsed_data;
    size_t offset = 0;

    DecodedRentInfo raw_rent {};
    if (!read_rent_info(bytes, &offset, &raw_rent, error_message)) {
        // Unreachable given the size check above, but defensive: a header
        // that can't even be read is not a valid header.
        return false;
    }
    parsed_data.rent.time = raw_rent.time;
    parsed_data.rent.rentcode = raw_rent.rentcode;
    parsed_data.rent.net_cost_per_hour = raw_rent.net_cost_per_hour;
    parsed_data.rent.gold = raw_rent.gold;
    parsed_data.rent.nitems = raw_rent.nitems;
    parsed_data.rent.spare0 = raw_rent.spare0;
    parsed_data.rent.spare1 = raw_rent.spare1;
    parsed_data.rent.spare2 = raw_rent.spare2;
    parsed_data.rent.spare3 = raw_rent.spare3;
    parsed_data.rent.spare4 = raw_rent.spare4;
    parsed_data.rent.spare5 = raw_rent.spare5;
    parsed_data.rent.spare6 = raw_rent.spare6;
    parsed_data.rent.spare7 = raw_rent.spare7;

    // Top-level object records: keep every COMPLETE record while there is
    // room for one; a trailing partial record's bytes are dropped (counted,
    // not fatal) and salvage stops there.
    bool object_list_ended_cleanly = false;
    while (true) {
        if (offset + kObjFileElemDiskSize > bytes.size()) {
            if (offset != bytes.size() && dropped_partial_record_count != nullptr)
                ++(*dropped_partial_record_count);
            offset = bytes.size();
            break;
        }
        ObjectRecord record;
        bool is_sentinel = false;
        if (!read_object_record_or_sentinel(bytes, &offset, &record, &is_sentinel, nullptr, "top-level object record")) {
            // Unreachable given the bounds check just above; defensive stop.
            break;
        }
        if (is_sentinel) {
            object_list_ended_cleanly = true;
            break;
        }
        parsed_data.objects.push_back(std::move(record));
    }

    // Everything after the object list is only included if the list itself
    // ended cleanly (real sentinel, not "ran out of bytes") AND each
    // subsequent section parses fully intact -- a partial section is
    // dropped wholesale rather than half-included, and salvage stops there.
    if (!object_list_ended_cleanly) {
        *data = std::move(parsed_data);
        set_error(error_message, "");
        return true;
    }

    const size_t board_bytes = parsed_data.board_points.size() * sizeof(sh_int);
    if (offset + board_bytes > bytes.size()) {
        *data = std::move(parsed_data);
        set_error(error_message, "");
        return true;
    }
    for (size_t index = 0; index < parsed_data.board_points.size(); ++index) {
        sh_int point = 0;
        read_pod(bytes, &offset, &point, nullptr, "board point"); // cannot fail: bounds already checked above
        parsed_data.board_points[index] = point;
    }

    // Aliases: fully intact only if the whole list (through its 20-byte
    // all-zero terminator) is present without truncation. Sanitize each
    // keyword per the locked policy iff it has no NUL within its 20-byte
    // width (same treatment exploits recovery applies to chtime/chVictimName).
    std::vector<AliasData> parsed_aliases;
    size_t alias_scan_offset = offset;
    bool alias_section_intact = true;
    while (true) {
        char keyword_bytes[20] {};
        if (!read_pod(bytes, &alias_scan_offset, &keyword_bytes, nullptr, "alias keyword")) {
            alias_section_intact = false;
            break;
        }
        if (keyword_bytes[0] == '\0')
            break; // clean terminator

        int command_length = 0;
        if (!read_pod(bytes, &alias_scan_offset, &command_length, nullptr, "alias command length")) {
            alias_section_intact = false;
            break;
        }
        if (command_length < 0 || alias_scan_offset + static_cast<size_t>(command_length) > bytes.size()) {
            alias_section_intact = false;
            break;
        }

        AliasData alias;
        if (legacy_salvage::fixed_width_field_has_no_nul(keyword_bytes, sizeof(keyword_bytes)))
            alias.keyword = legacy_salvage::sanitize_fixed_width_field(keyword_bytes, sizeof(keyword_bytes));
        else
            alias.keyword.assign(keyword_bytes, std::find(keyword_bytes, keyword_bytes + sizeof(keyword_bytes), '\0'));
        alias.command.assign(bytes.data() + alias_scan_offset, static_cast<size_t>(command_length));
        alias_scan_offset += static_cast<size_t>(command_length);
        parsed_aliases.push_back(std::move(alias));
    }

    if (!alias_section_intact) {
        *data = std::move(parsed_data);
        set_error(error_message, "");
        return true;
    }
    offset = alias_scan_offset;
    parsed_data.aliases = std::move(parsed_aliases);

    // Followers: fully intact only if the whole follower section (each
    // follower's fixed header plus its own complete object list, repeated
    // through the top-level follower-list terminator) is present. As with
    // the strict legacy decoder, a completely absent follower section (EOF
    // right here, before any follower has been read) is tolerated, not an
    // error -- older saves predate follower persistence.
    std::vector<FollowerData> parsed_followers;
    size_t follower_scan_offset = offset;
    bool follower_section_intact = true;
    while (true) {
        if (follower_scan_offset == bytes.size() && parsed_followers.empty())
            break; // legacy tolerance: no follower section at all

        DecodedFollowerFileElem raw_follower {};
        if (!read_follower_file_elem(bytes, &follower_scan_offset, &raw_follower, nullptr)) {
            follower_section_intact = false;
            break;
        }
        if (raw_follower.fol_vnum == SENTINEL_ITEM_ID_VALUE)
            break; // clean end of follower list

        FollowerData follower;
        follower.fol_vnum = raw_follower.fol_vnum;
        follower.mount_vnum = raw_follower.mount_vnum;
        follower.wimpy = raw_follower.wimpy;
        follower.exp = raw_follower.exp;
        follower.flag_config = raw_follower.flag_config;
        follower.spare1 = raw_follower.spare1;
        follower.spare2 = raw_follower.spare2;

        std::vector<ObjectRecord> follower_objects;
        if (!try_read_complete_object_list(bytes, &follower_scan_offset, &follower_objects, "follower object record")) {
            follower_section_intact = false;
            break;
        }
        follower.objects = std::move(follower_objects);
        parsed_followers.push_back(std::move(follower));
    }

    if (follower_section_intact)
        parsed_data.followers = std::move(parsed_followers);
    // else: leave parsed_data.followers empty -- a partial follower section
    // is dropped wholesale, same as an incomplete alias/board section above.

    *data = std::move(parsed_data);
    set_error(error_message, "");
    return true;
}

} // namespace

#ifdef TESTING
std::string format_object_record_for_testing(std::string_view indent)
{
    std::string output;
    write_object_record(output, ObjectRecord {}, indent);
    return output;
}
#endif

bool object_save_data_from_binary(const std::string& bytes, ObjectSaveData* data, std::string* error_message)
{
    return object_save_data_from_binary_impl(bytes, data, false, nullptr, error_message);
}

bool legacy_object_save_data_from_binary(
    const std::string& bytes, ObjectSaveData* data, bool* accepted_missing_follower_section, std::string* error_message)
{
    return object_save_data_from_binary_impl(bytes, data, true, accepted_missing_follower_section, error_message);
}

bool recover_object_save_data_from_binary(
    const std::string& bytes, ObjectSaveData* data, int* dropped_partial_record_count, std::string* error_message)
{
    return recover_object_save_data_from_binary_impl(bytes, data, dropped_partial_record_count, error_message);
}

bool object_save_data_to_binary(const ObjectSaveData& data, std::string* bytes, std::string* error_message)
{
    if (bytes == nullptr) {
        set_error(error_message, "Objects binary output parameter must not be null.");
        return false;
    }

    std::string serialized_bytes;

    rent_info raw_rent {};
    raw_rent.time = data.rent.time;
    raw_rent.rentcode = data.rent.rentcode;
    raw_rent.net_cost_per_hour = data.rent.net_cost_per_hour;
    raw_rent.gold = data.rent.gold;
    raw_rent.nitems = data.rent.nitems;
    if (!validate_narrowed_range<sh_int>(data.rent.spare0, "rent.spare0", error_message)
        || !validate_narrowed_range<sh_int>(data.rent.spare1, "rent.spare1", error_message)
        || !validate_narrowed_range<sh_int>(data.rent.spare2, "rent.spare2", error_message)) {
        return false;
    }
    raw_rent.spare0 = static_cast<sh_int>(data.rent.spare0);
    raw_rent.spare1 = static_cast<sh_int>(data.rent.spare1);
    raw_rent.spare2 = static_cast<sh_int>(data.rent.spare2);
    raw_rent.spare3 = data.rent.spare3;
    raw_rent.spare4 = data.rent.spare4;
    raw_rent.spare5 = data.rent.spare5;
    raw_rent.spare6 = data.rent.spare6;
    raw_rent.spare7 = data.rent.spare7;
    append_pod(&serialized_bytes, raw_rent);

    auto append_object_record = [&serialized_bytes, error_message](const ObjectRecord& record) {
        obj_file_elem raw_object {};
        raw_object.item_number_deprecated = DEPRECATED_ID_VALUE;
        raw_object.item_number = record.item_number;
        for (size_t index = 0; index < record.values.size(); ++index) {
            if (!validate_narrowed_range<sh_int>(record.values[index], "object.value", error_message))
                return false;
            raw_object.value[index] = static_cast<sh_int>(record.values[index]);
        }
        raw_object.extra_flags = record.extra_flags;
        raw_object.weight = record.weight;
        raw_object.timer = record.timer;
        raw_object.bitvector = record.bitvector;
        for (size_t index = 0; index < record.affects.size(); ++index) {
            if (!validate_narrowed_range<unsigned char>(record.affects[index].location, "object.affects.location", error_message))
                return false;
            raw_object.affected[index].location = static_cast<byte>(record.affects[index].location);
            raw_object.affected[index].modifier = record.affects[index].modifier;
        }
        if (!validate_narrowed_range<sh_int>(record.wear_pos, "object.wear_pos", error_message))
            return false;
        raw_object.wear_pos = static_cast<sh_int>(record.wear_pos);
        raw_object.loaded_by = record.loaded_by;
        append_pod(&serialized_bytes, raw_object);
        return true;
    };

    for (const ObjectRecord& record : data.objects) {
        if (!append_object_record(record))
            return false;
    }

    obj_file_elem object_sentinel {};
    object_sentinel.item_number_deprecated = DEPRECATED_ID_VALUE;
    object_sentinel.item_number = SENTINEL_ITEM_ID_VALUE;
    append_pod(&serialized_bytes, object_sentinel);

    for (int board_point : data.board_points) {
        if (!validate_narrowed_range<sh_int>(board_point, "board_point", error_message))
            return false;
        const sh_int narrowed_board_point = static_cast<sh_int>(board_point);
        append_pod(&serialized_bytes, narrowed_board_point);
    }

    for (const AliasData& alias : data.aliases) {
        // Mirrors the read-side cap in parse_alias_record: a 20-byte keyword
        // with no embedded NUL is a legitimate legacy value (see comment
        // there), so only reject what could never fit in the on-disk
        // char[20] field. When alias.keyword.size() == 20, keyword_bytes
        // below is fully overwritten with no trailing NUL -- exactly what
        // Crash_alias_save wrote for such a keyword.
        if (alias.keyword.size() > 20) {
            set_error(error_message, "Alias keyword must not exceed 20 characters.");
            return false;
        }

        char keyword_bytes[20] {};
        std::memcpy(keyword_bytes, alias.keyword.data(), alias.keyword.size());
        append_pod(&serialized_bytes, keyword_bytes);

        const int command_length = static_cast<int>(alias.command.size());
        append_pod(&serialized_bytes, command_length);
        serialized_bytes.append(alias.command);
    }

    char alias_terminator[20] {};
    append_pod(&serialized_bytes, alias_terminator);

    for (const FollowerData& follower : data.followers) {
        follower_file_elem raw_follower {};
        raw_follower.fol_vnum = follower.fol_vnum;
        raw_follower.mount_vnum = follower.mount_vnum;
        raw_follower.wimpy = follower.wimpy;
        raw_follower.exp = follower.exp;
        raw_follower.flag_config = follower.flag_config;
        raw_follower.spare1 = follower.spare1;
        raw_follower.spare2 = follower.spare2;
        append_pod(&serialized_bytes, raw_follower);

        for (const ObjectRecord& record : follower.objects) {
            if (!append_object_record(record))
                return false;
        }

        append_pod(&serialized_bytes, object_sentinel);
    }

    follower_file_elem follower_sentinel {};
    follower_sentinel.fol_vnum = SENTINEL_ITEM_ID_VALUE;
    append_pod(&serialized_bytes, follower_sentinel);

    *bytes = std::move(serialized_bytes);
    set_error(error_message, "");
    return true;
}

std::string serialize_objects_to_json(const ObjectSaveData& data)
{
    std::string output;
    output.append("{\n");
    std::format_to(std::back_inserter(output), "  \"version\": {},\n", data.version);
    output.append("  \"rent\": {\n");
    std::format_to(std::back_inserter(output), "    \"time\": {},\n", data.rent.time);
    std::format_to(std::back_inserter(output), "    \"rentcode\": {},\n", data.rent.rentcode);
    std::format_to(std::back_inserter(output), "    \"net_cost_per_hour\": {},\n", data.rent.net_cost_per_hour);
    std::format_to(std::back_inserter(output), "    \"gold\": {},\n", data.rent.gold);
    std::format_to(std::back_inserter(output), "    \"nitems\": {},\n", data.rent.nitems);
    std::format_to(std::back_inserter(output), "    \"spare0\": {},\n", data.rent.spare0);
    std::format_to(std::back_inserter(output), "    \"spare1\": {},\n", data.rent.spare1);
    std::format_to(std::back_inserter(output), "    \"spare2\": {},\n", data.rent.spare2);
    std::format_to(std::back_inserter(output), "    \"spare3\": {},\n", data.rent.spare3);
    std::format_to(std::back_inserter(output), "    \"spare4\": {},\n", data.rent.spare4);
    std::format_to(std::back_inserter(output), "    \"spare5\": {},\n", data.rent.spare5);
    std::format_to(std::back_inserter(output), "    \"spare6\": {},\n", data.rent.spare6);
    std::format_to(std::back_inserter(output), "    \"spare7\": {}\n", data.rent.spare7);
    output.append("  },\n");
    output.append("  \"objects\": [\n");
    for (size_t index = 0; index < data.objects.size(); ++index) {
        write_object_record(output, data.objects[index], "    ");
        if (index + 1 < data.objects.size())
            output.append(",");
        output.append("\n");
    }
    output.append("  ],\n");
    output.append("  \"board_points\": [");
    for (size_t index = 0; index < data.board_points.size(); ++index) {
        if (index > 0)
            output.append(", ");
        std::format_to(std::back_inserter(output), "{}", data.board_points[index]);
    }
    output.append("],\n");
    output.append("  \"aliases\": [\n");
    for (size_t index = 0; index < data.aliases.size(); ++index) {
        const AliasData& alias = data.aliases[index];
        output.append("    {\"keyword\": \"");
        output.append(json_utils::escape_json_string(alias.keyword));
        output.append("\", \"command\": \"");
        output.append(json_utils::escape_json_string(alias.command));
        output.append("\"}");
        if (index + 1 < data.aliases.size())
            output.append(",");
        output.append("\n");
    }
    output.append("  ],\n");
    output.append("  \"followers\": [\n");
    for (size_t index = 0; index < data.followers.size(); ++index) {
        const FollowerData& follower = data.followers[index];
        output.append("    {\n");
        std::format_to(std::back_inserter(output), "      \"fol_vnum\": {},\n", follower.fol_vnum);
        std::format_to(std::back_inserter(output), "      \"mount_vnum\": {},\n", follower.mount_vnum);
        std::format_to(std::back_inserter(output), "      \"wimpy\": {},\n", follower.wimpy);
        std::format_to(std::back_inserter(output), "      \"exp\": {},\n", follower.exp);
        std::format_to(std::back_inserter(output), "      \"flag_config\": {},\n", follower.flag_config);
        std::format_to(std::back_inserter(output), "      \"spare1\": {},\n", follower.spare1);
        std::format_to(std::back_inserter(output), "      \"spare2\": {},\n", follower.spare2);
        output.append("      \"objects\": [\n");
        for (size_t object_index = 0; object_index < follower.objects.size(); ++object_index) {
            write_object_record(output, follower.objects[object_index], "        ");
            if (object_index + 1 < follower.objects.size())
                output.append(",");
            output.append("\n");
        }
        output.append("      ]\n");
        output.append("    }");
        if (index + 1 < data.followers.size())
            output.append(",");
        output.append("\n");
    }
    output.append("  ]\n");
    output.append("}\n");
    return output;
}

bool deserialize_objects_from_json(std::string_view json, ObjectSaveData *data,
                                   std::string *error_message) {
    if (data == nullptr) {
        set_error(error_message, "Objects data output parameter must not be null.");
        return false;
    }

    ObjectSaveData parsed_data;
    bool saw_rent = false;
    bool saw_objects = false;
    bool saw_board_points = false;
    bool saw_aliases = false;
    bool saw_followers = false;
    json_utils::JsonReader reader(json);
    if (!reader.parse_root_object([&parsed_data, &saw_rent, &saw_objects, &saw_board_points, &saw_aliases, &saw_followers](std::string_view key, json_utils::JsonReader* nested_reader, std::string* nested_error_message) {
            if (key == "version")
                return nested_reader->parse_integer(&parsed_data.version, nested_error_message);
            if (key == "rent") {
                saw_rent = true;
                return nested_reader->parse_object([&parsed_data](std::string_view rent_key, json_utils::JsonReader* rent_reader, std::string* rent_error_message) {
                    if (rent_key == "time")
                        return rent_reader->parse_integer(&parsed_data.rent.time, rent_error_message);
                    if (rent_key == "rentcode")
                        return rent_reader->parse_integer(&parsed_data.rent.rentcode, rent_error_message);
                    if (rent_key == "net_cost_per_hour")
                        return rent_reader->parse_integer(&parsed_data.rent.net_cost_per_hour, rent_error_message);
                    if (rent_key == "gold")
                        return rent_reader->parse_integer(&parsed_data.rent.gold, rent_error_message);
                    if (rent_key == "nitems")
                        return rent_reader->parse_integer(&parsed_data.rent.nitems, rent_error_message);
                    if (rent_key == "spare0")
                        return rent_reader->parse_integer(&parsed_data.rent.spare0, rent_error_message);
                    if (rent_key == "spare1")
                        return rent_reader->parse_integer(&parsed_data.rent.spare1, rent_error_message);
                    if (rent_key == "spare2")
                        return rent_reader->parse_integer(&parsed_data.rent.spare2, rent_error_message);
                    if (rent_key == "spare3")
                        return rent_reader->parse_integer(&parsed_data.rent.spare3, rent_error_message);
                    if (rent_key == "spare4")
                        return rent_reader->parse_integer(&parsed_data.rent.spare4, rent_error_message);
                    if (rent_key == "spare5")
                        return rent_reader->parse_integer(&parsed_data.rent.spare5, rent_error_message);
                    if (rent_key == "spare6")
                        return rent_reader->parse_integer(&parsed_data.rent.spare6, rent_error_message);
                    if (rent_key == "spare7")
                        return rent_reader->parse_integer(&parsed_data.rent.spare7, rent_error_message);
                    return rent_reader->skip_value(rent_error_message);
                }, nested_error_message);
            }
            if (key == "objects") {
                saw_objects = true;
                std::vector<ObjectRecord> objects;
                if (!nested_reader->parse_array([&objects](json_utils::JsonReader* object_reader, std::string* object_error_message) {
                        ObjectRecord record;
                        if (!parse_object_record(object_reader, &record, object_error_message))
                            return false;
                        objects.push_back(record);
                        return true;
                    }, nested_error_message)) {
                    return false;
                }
                parsed_data.objects = std::move(objects);
                return true;
            }
            if (key == "board_points") {
                saw_board_points = true;
                std::vector<long> board_points;
                if (!parse_exact_integer_array(nested_reader, parsed_data.board_points.size(), &board_points, nested_error_message))
                    return false;
                for (size_t index = 0; index < board_points.size(); ++index)
                    parsed_data.board_points[index] = static_cast<int>(board_points[index]);
                return true;
            }
            if (key == "aliases") {
                saw_aliases = true;
                std::vector<AliasData> aliases;
                if (!nested_reader->parse_array([&aliases](json_utils::JsonReader* alias_reader, std::string* alias_error_message) {
                        AliasData alias;
                        if (!parse_alias_record(alias_reader, &alias, alias_error_message))
                            return false;
                        aliases.push_back(std::move(alias));
                        return true;
                    }, nested_error_message)) {
                    return false;
                }
                parsed_data.aliases = std::move(aliases);
                return true;
            }
            if (key == "followers") {
                saw_followers = true;
                std::vector<FollowerData> followers;
                if (!nested_reader->parse_array([&followers](json_utils::JsonReader* follower_reader, std::string* follower_error_message) {
                        FollowerData follower;
                        if (!parse_follower_record(follower_reader, &follower, follower_error_message))
                            return false;
                        followers.push_back(std::move(follower));
                        return true;
                    }, nested_error_message)) {
                    return false;
                }
                parsed_data.followers = std::move(followers);
                return true;
            }
            return nested_reader->skip_value(nested_error_message);
        }, error_message)) {
        return false;
    }

    if (parsed_data.version != OBJECTS_SCHEMA_VERSION) {
        set_error(error_message, "Unsupported objects JSON schema version.");
        return false;
    }

    if (!saw_rent || !saw_objects || !saw_board_points || !saw_aliases || !saw_followers) {
        set_error(error_message, "Objects JSON was missing one or more required sections.");
        return false;
    }

    *data = std::move(parsed_data);
    set_error(error_message, "");
    return true;
}

namespace {

bool object_affect_data_equal(const ObjectAffectData& a, const ObjectAffectData& b)
{
    return a.location == b.location && a.modifier == b.modifier;
}

bool object_record_equal(const ObjectRecord& a, const ObjectRecord& b)
{
    return a.item_number == b.item_number && a.values == b.values && a.extra_flags == b.extra_flags
        && a.weight == b.weight && a.timer == b.timer && a.bitvector == b.bitvector
        && std::equal(a.affects.begin(), a.affects.end(), b.affects.begin(), object_affect_data_equal)
        && a.wear_pos == b.wear_pos && a.loaded_by == b.loaded_by;
}

bool object_record_vector_equal(const std::vector<ObjectRecord>& a, const std::vector<ObjectRecord>& b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), object_record_equal);
}

bool rent_data_equal(const RentData& a, const RentData& b)
{
    return a.time == b.time && a.rentcode == b.rentcode && a.net_cost_per_hour == b.net_cost_per_hour
        && a.gold == b.gold && a.nitems == b.nitems && a.spare0 == b.spare0 && a.spare1 == b.spare1
        && a.spare2 == b.spare2 && a.spare3 == b.spare3 && a.spare4 == b.spare4 && a.spare5 == b.spare5
        && a.spare6 == b.spare6 && a.spare7 == b.spare7;
}

bool alias_data_equal(const AliasData& a, const AliasData& b)
{
    return a.keyword == b.keyword && a.command == b.command;
}

bool follower_data_equal(const FollowerData& a, const FollowerData& b)
{
    return a.fol_vnum == b.fol_vnum && a.mount_vnum == b.mount_vnum && a.wimpy == b.wimpy && a.exp == b.exp
        && a.flag_config == b.flag_config && a.spare1 == b.spare1 && a.spare2 == b.spare2
        && object_record_vector_equal(a.objects, b.objects);
}

} // namespace

bool object_save_data_equal(const ObjectSaveData& a, const ObjectSaveData& b)
{
    if (a.version != b.version)
        return false;
    if (!rent_data_equal(a.rent, b.rent))
        return false;
    if (!object_record_vector_equal(a.objects, b.objects))
        return false;
    if (a.board_points != b.board_points)
        return false;
    if (a.aliases.size() != b.aliases.size())
        return false;
    if (!std::equal(a.aliases.begin(), a.aliases.end(), b.aliases.begin(), alias_data_equal))
        return false;
    if (a.followers.size() != b.followers.size())
        return false;
    if (!std::equal(a.followers.begin(), a.followers.end(), b.followers.begin(), follower_data_equal))
        return false;
    return true;
}

} // namespace objects_json
