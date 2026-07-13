#ifndef ACCOUNT_MANAGEMENT_MIGRATION_H
#define ACCOUNT_MANAGEMENT_MIGRATION_H

#include "account_management_types.h"

#include <string_view>

namespace account {

/// Migrates legacy assets selected by bounded root, account, and character names.
bool migrate_legacy_character_by_name(std::string_view root_directory, std::string_view account_name, std::string_view character_name, long migrated_at, CharacterMigrationData* migration, std::string* error_message = nullptr);
/// Reads migration state selected by first-null-normalized bounded names.
bool read_character_migration(std::string_view root_directory, std::string_view account_name, std::string_view character_name, CharacterMigrationData* migration, std::string* error_message = nullptr);
/// Ensures migration state exists for the normalized bounded account-character selection.
bool ensure_character_migration(std::string_view root_directory, std::string_view account_name, std::string_view character_name, long migrated_at, CharacterMigrationData* migration, std::string* error_message = nullptr);
/// Restores migration assets only when bounded expected identities match the snapshot.
bool restore_character_migration(std::string_view root_directory, std::string_view expected_account_name, std::string_view expected_character_name, const CharacterMigrationData& migration, std::string* error_message = nullptr);
/// Clears runtime support files after validating bounded expected migration identities.
bool clear_character_runtime_support_files_for_account_play(std::string_view root_directory, std::string_view expected_account_name, std::string_view expected_character_name, const CharacterMigrationData& migration, std::string* error_message = nullptr);
/// Refreshes a migration snapshot for a bounded linked-character name.
bool refresh_linked_character_snapshot(std::string_view root_directory, std::string_view character_name, long migrated_at, CharacterMigrationData* migration, std::string* error_message = nullptr);

} // namespace account

#endif
