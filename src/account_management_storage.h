#ifndef ACCOUNT_MANAGEMENT_STORAGE_H
#define ACCOUNT_MANAGEMENT_STORAGE_H

#include "account_management_types.h"

#include <string_view>

namespace account {

/// Returns the storage bucket for a first-null-normalized bounded identity.
std::string account_bucket_for_name(std::string_view name);
/// Builds the legacy player path from bounded root and character inputs.
std::string legacy_player_file_path(std::string_view root_directory, std::string_view character_name);
/// Builds the legacy object path from bounded root and character inputs.
std::string legacy_object_file_path(std::string_view root_directory, std::string_view character_name);
/// Builds the legacy exploit path from bounded root and character inputs.
std::string legacy_exploits_file_path(std::string_view root_directory, std::string_view character_name);
/// Builds the account JSON path from bounded root and email-or-storage-key inputs.
std::string account_file_path(std::string_view root_directory, std::string_view email_or_storage_key);
/// Builds the owning account-character directory path from bounded text inputs.
std::string account_character_directory(std::string_view root_directory, std::string_view account_name, std::string_view character_name);
/// Builds the migration snapshot path from bounded account-character inputs.
std::string account_character_snapshot_path(std::string_view root_directory, std::string_view account_name, std::string_view character_name);
/// Builds the character JSON path from bounded account-character inputs.
std::string account_character_player_path(std::string_view root_directory, std::string_view account_name, std::string_view character_name);
/// Builds the object JSON path from bounded account-character inputs.
std::string account_character_object_path(std::string_view root_directory, std::string_view account_name, std::string_view character_name);
/// Builds the exploit JSON path from bounded account-character inputs.
std::string account_character_exploits_path(std::string_view root_directory, std::string_view account_name, std::string_view character_name);

std::string serialize_account_to_json(const AccountData& account);
bool deserialize_account_from_json(const std::string& json, AccountData* account, std::string* error_message = nullptr);

/// Persists an account beneath a first-null-normalized bounded storage root.
bool write_account_file(std::string_view root_directory, const AccountData& account, std::string* error_message = nullptr);
/// Reads an account selected by bounded root and account-name inputs.
bool read_account_file(std::string_view root_directory, std::string_view account_name, AccountData* account, std::string* error_message = nullptr);
// Uncached on-disk read (the real scan). read_account_file delegates here when the cache is disabled,
// and it is the cache's backing resolver on a miss. Call directly to bypass the cache.
/// Performs an uncached account read selected by bounded root and account-name inputs.
bool read_account_file_uncached(std::string_view root_directory, std::string_view account_name, AccountData* account, std::string* error_message = nullptr);
/// Reads an account selected by bounded root and email inputs.
bool read_account_file_by_email(std::string_view root_directory, std::string_view email, AccountData* account, std::string* error_message = nullptr);
/// Reads an account selected by a bounded name-or-email identifier.
bool read_account_file_by_identifier(std::string_view root_directory, std::string_view identifier, AccountData* account, std::string* error_message = nullptr);

std::string serialize_character_migration_to_json(const CharacterMigrationData& migration);
bool deserialize_character_migration_from_json(const std::string& json, CharacterMigrationData* migration, std::string* error_message = nullptr);

// Read an entire text file into *contents (POSIX-backed). Exposed for stage-timing the
// LOAD pipeline's file-read step.
bool read_text_file(const std::string& path, std::string* contents, std::string* error_message);

// Atomic write: temp(path+".tmp") -> fwrite -> rename. Exposed for stage-timing the SAVE
// pipeline's disk-write step against a throwaway path.
bool write_text_file_atomically(const std::string& path, const std::string& text,
                                std::string* error_message);

} // namespace account

#endif
