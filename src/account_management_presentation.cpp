namespace {

std::string format_account_timestamp(long timestamp)
{
    if (timestamp <= 0)
        return "Never";

    // Compare in long long: casting time_t's max down to long truncates to -1 on
    // Windows LLP64 (long = 4 bytes, time_t = 8), which made every positive
    // timestamp "Invalid" (Phase 3 Task 6). Both operands fit in long long on
    // every supported platform.
    if (static_cast<long long>(timestamp) > static_cast<long long>(std::numeric_limits<time_t>::max()))
        return "Invalid";

    time_t raw_time = static_cast<time_t>(timestamp);
    struct tm broken_down_time {};
    // gmtime_r is POSIX-only; MSVC's CRT spells the same reentrant conversion
    // gmtime_s, with the argument order swapped and an errno_t (0 = success)
    // return instead of a struct tm* (Phase 3 Task 6).
#if defined PREDEF_PLATFORM_WINDOWS
    if (gmtime_s(&broken_down_time, &raw_time) != 0)
        return "Invalid";
#else
    if (gmtime_r(&raw_time, &broken_down_time) == nullptr)
        return "Invalid";
#endif

    char buffer[64];
    size_t formatted_length = strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &broken_down_time);
    if (formatted_length == 0)
        return "Invalid";

    return std::string(buffer) + " UTC";
}

} // namespace

std::string format_account_character_prompt(const std::string& root_directory, const AccountData& account)
{
    std::ostringstream output;
    output << "\n\rLinked characters for your account:\n\r";
    output << format_account_character_short_roster(root_directory, account);
    output << "\n\r0) Back to Account Menu.\n\r";
    output << "\n\rCharacter number: ";
    return output.str();
}

std::string format_account_character_list(const std::string& root_directory, const AccountData& account)
{
    if (account.characters.empty())
        return "\n\rNo linked characters yet.\n\r";

    std::ostringstream output;
    output << "\n\rLinked characters:\n\r";
    output << format_account_character_short_roster(root_directory, account);
    return output.str();
}

std::string format_account_summary(const AccountData& account)
{
    std::ostringstream output;
    output << "Account email: " << account.normalized_email << "\n\r";
    output << "Internal name: " << account.account_name << "\n\r";
    output << "Email verified: " << (account.email_verified ? "yes" : "no") << "\n\r";
    if (account.email_verified) {
        output << "Verified by: " << account.email_verified_by << "\n\r";
        output << "Verified at: " << format_account_timestamp(account.email_verified_at) << "\n\r";
    } else if (!account.verification_code_hash.empty()) {
        output << "Verification code sent at: " << format_account_timestamp(account.verification_code_sent_at) << "\n\r";
        output << "Verification code expires at: " << format_account_timestamp(account.verification_code_expires_at) << "\n\r";
        output << "Verification attempts: " << account.verification_attempt_count << "\n\r";
    }
    output << "Blocked: " << (account.blocked ? "yes" : "no") << "\n\r";
    if (account.blocked) {
        output << "Blocked by: " << account.blocked_by << "\n\r";
        output << "Block reason: " << account.block_reason << "\n\r";
        output << "Blocked at: " << format_account_timestamp(account.blocked_at) << "\n\r";
    }
    output << "Created: " << format_account_timestamp(account.created_at) << "\n\r";
    output << "Updated: " << format_account_timestamp(account.updated_at) << "\n\r";
    if (!account.password_reset_by.empty()) {
        output << "Password reset by: " << account.password_reset_by << "\n\r";
        output << "Password reset at: " << format_account_timestamp(account.password_reset_at) << "\n\r";
    }
    output << "Characters (" << account.characters.size() << "): ";
    if (account.characters.empty()) {
        output << "(none)";
    } else {
        for (size_t index = 0; index < account.characters.size(); ++index) {
            if (index > 0)
                output << ", ";
            output << account.characters[index];
        }
    }
    output << "\n\r";
    return output.str();
}
