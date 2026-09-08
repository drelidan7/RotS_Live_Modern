#ifndef ACCOUNT_MANAGEMENT_TYPES_H
#define ACCOUNT_MANAGEMENT_TYPES_H

#include "db.h"

#include <string>
#include <vector>

namespace account {

/// Ordering of the account character roster; equal keys retain account order.
enum class RosterSort {
    Account, ///< Preserve the account's insertion order.
    Name, ///< Alphabetical character name.
    Level, ///< Highest level first.
    Race, ///< Ascending persisted race index.
    Side, ///< Gods, Lights, Darks, Third Side, then unavailable.
};

/// Show characters whose highest derived profession matches; ties match each tied filter.
enum class RosterFilter {
    None, ///< Include every linked character.
    Warrior, ///< Highest warrior coefficient.
    Ranger, ///< Highest ranger coefficient.
    Mystic, ///< Highest cleric coefficient.
    Mage, ///< Highest mage coefficient.
};

static constexpr int ACCOUNT_SCHEMA_VERSION = 1;
static constexpr int MIN_PASSWORD_LENGTH = 8;
static constexpr int MIN_ACCOUNT_NAME_LENGTH = 3;
static constexpr int MAX_ACCOUNT_NAME_LENGTH = 20;
static constexpr long EMAIL_VERIFICATION_WINDOW_SECONDS = 15 * 60;
static constexpr long EMAIL_VERIFICATION_RESEND_COOLDOWN_SECONDS = 60;
static constexpr int MAX_EMAIL_VERIFICATION_ATTEMPTS = 5;

/// Maximum printable host length recorded for a failed login.
static constexpr int MAX_FAILED_LOGIN_HOST_LENGTH = 49;
/// Recovery code lifetime, in seconds.
static constexpr long PASSWORD_RESET_WINDOW_SECONDS = EMAIL_VERIFICATION_WINDOW_SECONDS;
/// Minimum interval between recovery code sends, in seconds.
static constexpr long PASSWORD_RESET_RESEND_COOLDOWN_SECONDS = EMAIL_VERIFICATION_RESEND_COOLDOWN_SECONDS;
/// Wrong-code attempts allowed before persistent invalidation.
static constexpr int MAX_PASSWORD_RESET_ATTEMPTS = MAX_EMAIL_VERIFICATION_ATTEMPTS;

/// Owns persisted account identity, credentials, recovery policy and roster preferences.
struct AccountData {
    struct CharacterLinkReference {
        std::string character_name;
        std::string character_path;
        std::string object_path;
        std::string exploits_path;
    };

    int version = ACCOUNT_SCHEMA_VERSION;
    std::string account_name;
    std::string normalized_email;
    std::string password_hash;
    std::string password_salt;
    std::vector<std::string> characters;
    std::vector<CharacterLinkReference> character_links;
    // Persisted sort preference; empty selects account insertion order.
    std::string roster_sort;
    bool email_verified = false;
    std::string email_verified_by;
    long email_verified_at = 0;
    std::string verification_code_hash;
    long verification_code_sent_at = 0;
    long verification_code_expires_at = 0;
    int verification_attempt_count = 0;
    long verification_last_attempt_at = 0;
    bool blocked = false;
    std::string block_reason;
    std::string blocked_by;
    long blocked_at = 0;
    long created_at = 0;
    long updated_at = 0;
    long password_reset_at = 0;
    std::string password_reset_by;
    // Number of rejected logins since the last successful authentication.
    int failed_login_count = 0;
    // Unix time of the most recent rejected login.
    long failed_login_last_at = 0;
    // Printable, bounded host text for the login notice.
    std::string failed_login_last_host;
    // Cryptographic hash of the pending recovery code; never plaintext.
    std::string password_reset_code_hash;
    // Unix issue time also enforces resend cooldown after attempt exhaustion.
    long password_reset_code_sent_at = 0;
    // Unix expiration time of the pending recovery code.
    long password_reset_code_expires_at = 0;
    // Persisted number of rejected guesses for the pending recovery code.
    int password_reset_attempt_count = 0;
};

struct LegacyAssetSnapshot {
    std::string source_path;
    std::string encoding;
    std::string content;
    bool present = false;
};

struct CharacterMigrationData {
    int version = ACCOUNT_SCHEMA_VERSION;
    std::string account_name;
    std::string character_name;
    long migrated_at = 0;
    LegacyAssetSnapshot player_file;
    LegacyAssetSnapshot object_file;
    LegacyAssetSnapshot exploits_file;
};

} // namespace account

#endif
