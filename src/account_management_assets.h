#ifndef ACCOUNT_MANAGEMENT_ASSETS_H
#define ACCOUNT_MANAGEMENT_ASSETS_H

#include "account_management_types.h"
#include "objects_json.h"

#include <string_view>

namespace account {

/// Writes an account-owned character record using first-null-normalized borrowed root and account names.
bool write_account_character_file(std::string_view root_directory, std::string_view account_name, const char_file_u& stored_character, std::string* error_message = nullptr);
/// Writes a linked character record after resolving the bounded character name to its owning account.
bool write_linked_character_file(std::string_view root_directory, std::string_view character_name, const char_file_u& stored_character, std::string* error_message = nullptr);
/// Reads an account-owned character record selected by bounded account and character names.
bool read_account_character_file(std::string_view root_directory, std::string_view account_name, std::string_view character_name, char_file_u* stored_character, std::string* error_message = nullptr);
/// Reports whether an account-owned character record exists without retaining borrowed text.
bool inspect_account_character_file(std::string_view root_directory, std::string_view account_name, std::string_view character_name, bool* exists, std::string* error_message = nullptr);
/// Returns whether an account-owned character record exists for the normalized text inputs.
bool account_character_file_exists(std::string_view root_directory, std::string_view account_name, std::string_view character_name, std::string* error_message = nullptr);
/// Removes an account-owned character record selected by normalized borrowed text.
bool remove_account_character_file(std::string_view root_directory, std::string_view account_name, std::string_view character_name, std::string* error_message = nullptr);

// Struct-level object-file API (Phase 2b Task 1). The account-staging path
// used to bounce an in-memory ObjectSaveData through object_save_data_to_binary
// (a native-struct encode) just so callers could hand back "bytes" -- on a
// 64-bit build that encode no longer matches the fixed-width on-disk layout
// the portable decoders expect, corrupting the round trip. Passing
// ObjectSaveData straight through removes that binary hop entirely; JSON is
// the only on-disk representation these functions touch. (The byte-vector
// write_account_object_file/read_account_object_file this replaces are gone,
// not deprecated -- see the account_management_assets.cpp history for that
// implementation if it's ever needed again.)
/// Writes JSON-native object data for an account character selected by bounded names.
bool write_account_object_data(std::string_view root_directory, std::string_view account_name, std::string_view character_name, const objects_json::ObjectSaveData& object_data, std::string* error_message = nullptr);
/// Creates the default object file for an account character selected by bounded names.
bool write_default_account_object_file(std::string_view root_directory, std::string_view account_name, std::string_view character_name, std::string* error_message = nullptr);
// JSON-native sibling of write_account_object_data: mirrors an in-memory
// ObjectSaveData straight to a *linked* character's account JSON object file
// (resolving the owning account first). Added for Phase 2a Task 2 so the
// primary (objsave.cpp) writer never has to touch object_save_data_to_binary
// just to reuse this path.
/// Writes JSON-native object data after resolving a bounded linked-character name.
bool write_linked_character_object_json_file(std::string_view root_directory, std::string_view character_name, const objects_json::ObjectSaveData& object_data, std::string* error_message = nullptr);
/// Reads JSON-native object data for the normalized account-character selection.
bool read_account_object_data(std::string_view root_directory, std::string_view account_name, std::string_view character_name, objects_json::ObjectSaveData* data, std::string* error_message = nullptr);
/// Reports object-file presence for the normalized account-character selection.
bool inspect_account_object_file(std::string_view root_directory, std::string_view account_name, std::string_view character_name, bool* exists, std::string* error_message = nullptr);
/// Returns whether object data exists for the normalized account-character selection.
bool account_object_file_exists(std::string_view root_directory, std::string_view account_name, std::string_view character_name, std::string* error_message = nullptr);
/// Removes object data for the normalized account-character selection.
bool remove_account_object_file(std::string_view root_directory, std::string_view account_name, std::string_view character_name, std::string* error_message = nullptr);

/// Writes exploit history for an account character selected by bounded names.
bool write_account_exploit_file(std::string_view root_directory, std::string_view account_name, std::string_view character_name, const std::vector<exploit_record>& records, std::string* error_message = nullptr);
/// Creates an empty exploit-history file for an account character selected by bounded names.
bool write_default_account_exploit_file(std::string_view root_directory, std::string_view account_name, std::string_view character_name, std::string* error_message = nullptr);
/// Writes exploit history after resolving a bounded linked-character name.
bool write_linked_character_exploit_file(std::string_view root_directory, std::string_view character_name, const std::vector<exploit_record>& records, std::string* error_message = nullptr);
/// Reads exploit history for the normalized account-character selection.
bool read_account_exploit_file(std::string_view root_directory, std::string_view account_name, std::string_view character_name, std::vector<exploit_record>* records, std::string* error_message = nullptr);
/// Reports exploit-file presence for the normalized account-character selection.
bool inspect_account_exploit_file(std::string_view root_directory, std::string_view account_name, std::string_view character_name, bool* exists, std::string* error_message = nullptr);
/// Returns whether exploit history exists for the normalized account-character selection.
bool account_exploit_file_exists(std::string_view root_directory, std::string_view account_name, std::string_view character_name, std::string* error_message = nullptr);
/// Removes exploit history for the normalized account-character selection.
bool remove_account_exploit_file(std::string_view root_directory, std::string_view account_name, std::string_view character_name, std::string* error_message = nullptr);

/// Clears runtime support files for a bounded linked-character name before account-backed play.
bool clear_account_character_runtime_support_files(std::string_view root_directory, std::string_view character_name, std::string* error_message = nullptr);
bool decode_snapshot_content(const LegacyAssetSnapshot& snapshot, std::string* contents, std::string* error_message = nullptr);

} // namespace account

#endif
