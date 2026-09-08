#ifndef ACCOUNT_MANAGEMENT_IDENTITY_H
#define ACCOUNT_MANAGEMENT_IDENTITY_H

#include "account_management_types.h"

#include <string_view>

namespace account {

/// Returns a trimmed lowercase account name, stopping at the first null byte.
std::string normalize_account_name(std::string_view account_name);
/// Returns a trimmed lowercase email address, stopping at the first null byte.
std::string normalize_email(std::string_view email);

/// Validates a bounded account name under the account identity policy.
bool is_valid_account_name(std::string_view account_name, std::string* error_message = nullptr);
/// Validates a bounded email address under the account identity policy.
bool is_valid_email(std::string_view email, std::string* error_message = nullptr);
/// Validates a bounded password under the account credential policy.
bool is_valid_password(std::string_view password, std::string* error_message = nullptr);

/// Generates owning password hash and salt outputs from a nonretained bounded password.
bool generate_password_credentials(std::string_view password, std::string* password_hash, std::string* password_salt, std::string* error_message = nullptr);
/// Verifies a bounded password against a bounded stored hash at the cryptographic boundary.
bool verify_password(std::string_view password, std::string_view password_hash);
/// Initializes owning account state from normalized bounded identity and credential inputs.
bool initialize_new_account(std::string_view account_name, std::string_view email, std::string_view password, long created_at, AccountData* account, std::string* error_message = nullptr);
/// Adds an owning normalized copy of a bounded character name to an account.
bool add_character_to_account(AccountData* account, std::string_view character_name, std::string* error_message = nullptr);
/// Returns whether an account owns the normalized bounded character name.
bool account_has_character(const AccountData& account, std::string_view character_name);
/// Selects a numeric row using the active filter; exact names use the sorted 200-row unfiltered roster.
bool select_linked_character(std::string_view root_directory, const AccountData& account, std::string_view character_name, RosterSort sort, RosterFilter filter, std::string* normalized_character_name, std::string* error_message = nullptr);
bool prepare_email_verification_code(AccountData* account, long sent_at, std::string* verification_code, std::string* error_message = nullptr);
/// Confirms a bounded verification code and copies bounded audit identity into account state.
bool confirm_email_verification_code(AccountData* account, std::string_view verification_code, std::string_view verified_by, long verified_at, std::string* error_message = nullptr);
/// Marks an account verified and retains an owning first-null-normalized audit identity.
void verify_email(AccountData* account, std::string_view verified_by, long verified_at);
void unverify_email(AccountData* account);
/// Blocks an account while copying bounded actor and reason text into owning audit fields.
void block_account(AccountData* account, std::string_view blocked_by, std::string_view block_reason, long blocked_at);
void unblock_account(AccountData* account);
/// Resets account credentials and copies the bounded reset actor into owning audit state.
bool reset_account_password(AccountData* account, std::string_view new_password, std::string_view reset_by, long reset_at, std::string* error_message = nullptr);

/// Creates and persists an account from bounded root, identity, and credential inputs.
bool create_account(std::string_view root_directory, std::string_view account_name, std::string_view email, std::string_view password, long created_at, AccountData* account, std::string* error_message = nullptr);
/// Creates and persists an account using a bounded email-derived account identity.
bool create_account_for_email(std::string_view root_directory, std::string_view email, std::string_view password, long created_at, AccountData* account, std::string* error_message = nullptr);
/// Authenticates a bounded account name and password without retaining either view.
bool authenticate_account(std::string_view root_directory, std::string_view account_name, std::string_view password, AccountData* account, std::string* error_message = nullptr);
/// Authenticates a bounded email and password without retaining either view.
bool authenticate_account_by_email(std::string_view root_directory, std::string_view email, std::string_view password, AccountData* account, std::string* error_message = nullptr);
// Note a rejected account-login password on the stored account so the owner can be told about it
// the next time they get in. Unknown or malformed addresses are a silent no-op (returning true and
// writing nothing), so a failed attempt never reveals whether an address has an account behind it.
bool record_account_login_failure(std::string_view root_directory, std::string_view email, std::string_view host, long attempted_at, std::string* out_error_message = nullptr);
// Clear the failure tally after a successful login. Writes nothing when there is nothing to clear.
bool clear_account_login_failures(std::string_view root_directory, std::string_view account_name, std::string* out_error_message = nullptr);
// Begin a forgot-password reset for the account at this address. Malformed addresses, addresses
// with no account, and repeat requests inside the resend cooldown all return true having written
// and mailed nothing -- a failed request must never reveal whether an account exists.
// *out_code_expires_at is always sent_at + PASSWORD_RESET_WINDOW_SECONDS: a synthetic value derived
// only from the caller's clock, never from stored state. The caller stamps a visible connection
// deadline from it, so anything account-dependent here would leak through how long the connection
// lived. Inside the cooldown the pending code may therefore expire slightly before the deadline.
// True means no operational failure, including unknown-address/cooldown no-ops; it does not
// prove mail delivery. The result must never change the visible response or deadline.
bool start_password_reset(std::string_view root_directory, std::string_view email, long sent_at, long* out_code_expires_at, std::string* out_error_message = nullptr);
// Check a reset code WITHOUT consuming it, so the player learns a code is wrong at the code prompt
// rather than after typing a new password twice. A wrong code costs an attempt exactly as the
// completing call would; a correct one changes nothing at all. Stored expiry equality is accepted;
// a descriptor absolute deadline closes on equality independently.
bool verify_password_reset_code(std::string_view root_directory, std::string_view email, std::string_view reset_code, long attempted_at, std::string* out_error_message = nullptr);

// Finish a forgot-password reset. Fails for an unknown address, a missing or expired code, a wrong
// code, or a password the policy rejects. The fifth wrong code clears the stored code so a
// reconnect cannot resume against it. On success the reset state is cleared, the address is marked
// verified (mailbox control was just proven) and the failed-login tally is reset.
bool complete_password_reset(std::string_view root_directory, std::string_view email, std::string_view reset_code, std::string_view new_password, long reset_at, AccountData* out_account, std::string* out_error_message = nullptr);

/// Starts verification for a bounded account identity and persists the updated account.
bool start_email_verification(std::string_view root_directory, std::string_view account_name, long sent_at, AccountData* account, std::string* error_message = nullptr);
/// Completes verification using bounded account, code, and audit-actor inputs.
bool complete_email_verification(std::string_view root_directory, std::string_view account_name, std::string_view verification_code, std::string_view verified_by, long verified_at, AccountData* account, std::string* error_message = nullptr);
/// Resolves the owning account for a bounded character name, copying the result to output storage.
bool find_linked_character_owner_account(std::string_view root_directory, std::string_view character_name, std::string* owner_account_name, std::string* error_message = nullptr);
// Uncached owner resolution (the real scan). find_linked_character_owner_account delegates here when
// the cache is disabled, and it is the owner cache's backing resolver on a miss.
/// Performs uncached owner resolution for bounded root and character inputs.
bool find_linked_character_owner_account_uncached(std::string_view root_directory, std::string_view character_name, std::string* owner_account_name, std::string* error_message = nullptr);
/// Administratively links a bounded character name to a bounded account identity.
bool admin_link_character(std::string_view root_directory, std::string_view account_name, std::string_view character_name, long updated_at, AccountData* account, std::string* error_message = nullptr);
/// Administratively links and migrates a bounded account-character selection.
bool admin_link_and_migrate_character(std::string_view root_directory, std::string_view account_name, std::string_view character_name, long updated_at, AccountData* account, CharacterMigrationData* migration, std::string* error_message = nullptr);
/// Administratively verifies an account and copies the bounded audit actor.
bool admin_verify_email(std::string_view root_directory, std::string_view account_name, std::string_view verified_by, long verified_at, AccountData* account, std::string* error_message = nullptr);
/// Administratively removes verification from a bounded account identity.
bool admin_unverify_email(std::string_view root_directory, std::string_view account_name, long updated_at, AccountData* account, std::string* error_message = nullptr);
/// Administratively blocks an account with bounded actor and reason text.
bool admin_block_account(std::string_view root_directory, std::string_view account_name, std::string_view blocked_by, std::string_view block_reason, long blocked_at, AccountData* account, std::string* error_message = nullptr);
/// Administratively unblocks a bounded account identity.
bool admin_unblock_account(std::string_view root_directory, std::string_view account_name, long updated_at, AccountData* account, std::string* error_message = nullptr);
/// Administratively resets credentials using bounded password and audit-actor inputs.
bool admin_reset_password(std::string_view root_directory, std::string_view account_name, std::string_view new_password, std::string_view reset_by, long reset_at, AccountData* account, std::string* error_message = nullptr);
/// Deletes an owned character. False means unlink did not commit; true includes logged residual
/// staged-file cleanup failures after commit. Callers must retire their index on true.
bool admin_delete_linked_character(std::string_view root_directory, std::string_view account_name, std::string_view character_name, long updated_at, AccountData* account, std::string* error_message = nullptr);
/// Authenticates, links, and migrates a bounded account-character selection.
bool link_and_migrate_character(std::string_view root_directory, std::string_view account_name, std::string_view password, std::string_view character_name, long updated_at, AccountData* account, CharacterMigrationData* migration, std::string* error_message = nullptr);

} // namespace account

#endif
