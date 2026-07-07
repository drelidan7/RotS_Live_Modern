#ifndef ACCOUNT_MANAGEMENT_ASSETS_H
#define ACCOUNT_MANAGEMENT_ASSETS_H

#include "account_management_types.h"
#include "objects_json.h"

namespace account {

bool write_account_character_file(const std::string& root_directory, const std::string& account_name, const char_file_u& stored_character, std::string* error_message = nullptr);
bool write_linked_character_file(const std::string& root_directory, const std::string& character_name, const char_file_u& stored_character, std::string* error_message = nullptr);
bool read_account_character_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, char_file_u* stored_character, std::string* error_message = nullptr);
bool inspect_account_character_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, bool* exists, std::string* error_message = nullptr);
bool account_character_file_exists(const std::string& root_directory, const std::string& account_name, const std::string& character_name, std::string* error_message = nullptr);
bool remove_account_character_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, std::string* error_message = nullptr);

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
bool write_account_object_data(const std::string& root_directory, const std::string& account_name, const std::string& character_name, const objects_json::ObjectSaveData& object_data, std::string* error_message = nullptr);
bool write_default_account_object_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, std::string* error_message = nullptr);
// JSON-native sibling of write_account_object_data: mirrors an in-memory
// ObjectSaveData straight to a *linked* character's account JSON object file
// (resolving the owning account first). Added for Phase 2a Task 2 so the
// primary (objsave.cpp) writer never has to touch object_save_data_to_binary
// just to reuse this path.
bool write_linked_character_object_json_file(const std::string& root_directory, const std::string& character_name, const objects_json::ObjectSaveData& object_data, std::string* error_message = nullptr);
bool read_account_object_data(const std::string& root_directory, const std::string& account_name, const std::string& character_name, objects_json::ObjectSaveData* data, std::string* error_message = nullptr);
bool inspect_account_object_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, bool* exists, std::string* error_message = nullptr);
bool account_object_file_exists(const std::string& root_directory, const std::string& account_name, const std::string& character_name, std::string* error_message = nullptr);
bool remove_account_object_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, std::string* error_message = nullptr);

bool write_account_exploit_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, const std::vector<exploit_record>& records, std::string* error_message = nullptr);
bool write_default_account_exploit_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, std::string* error_message = nullptr);
bool write_linked_character_exploit_file(const std::string& root_directory, const std::string& character_name, const std::vector<exploit_record>& records, std::string* error_message = nullptr);
bool read_account_exploit_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, std::vector<exploit_record>* records, std::string* error_message = nullptr);
bool inspect_account_exploit_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, bool* exists, std::string* error_message = nullptr);
bool account_exploit_file_exists(const std::string& root_directory, const std::string& account_name, const std::string& character_name, std::string* error_message = nullptr);
bool remove_account_exploit_file(const std::string& root_directory, const std::string& account_name, const std::string& character_name, std::string* error_message = nullptr);

bool clear_account_character_runtime_support_files(const std::string& root_directory, const std::string& character_name, std::string* error_message = nullptr);
bool decode_snapshot_content(const LegacyAssetSnapshot& snapshot, std::string* contents, std::string* error_message = nullptr);

} // namespace account

#endif
