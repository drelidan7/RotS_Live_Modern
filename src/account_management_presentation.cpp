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

std::string format_account_character_prompt(std::string_view root_directory, const AccountData& account)
{
    std::string output;
    output.append("\n\rLinked characters for your account:\n\r");
    output.append(format_account_character_short_roster(root_directory, account));
    output.append("\n\r0) Back to Account Menu.\n\r");
    output.append("\n\rCharacter number: ");
    return output;
}

std::string format_account_character_list(std::string_view root_directory, const AccountData& account)
{
    if (account.characters.empty())
        return "\n\rNo linked characters yet.\n\r";

    std::string output;
    output.append("\n\rLinked characters:\n\r");
    output.append(format_account_character_short_roster(root_directory, account));
    return output;
}

std::string format_account_summary(const AccountData& account)
{
    std::string output;
    std::format_to(std::back_inserter(output), "Account email: {}\n\r", account.normalized_email);
    std::format_to(std::back_inserter(output), "Internal name: {}\n\r", account.account_name);
    std::format_to(std::back_inserter(output), "Email verified: {}\n\r", (account.email_verified ? "yes" : "no"));
    if (account.email_verified) {
        std::format_to(std::back_inserter(output), "Verified by: {}\n\r", account.email_verified_by);
        std::format_to(std::back_inserter(output), "Verified at: {}\n\r", format_account_timestamp(account.email_verified_at));
    } else if (!account.verification_code_hash.empty()) {
        std::format_to(std::back_inserter(output), "Verification code sent at: {}\n\r", format_account_timestamp(account.verification_code_sent_at));
        std::format_to(std::back_inserter(output), "Verification code expires at: {}\n\r", format_account_timestamp(account.verification_code_expires_at));
        std::format_to(std::back_inserter(output), "Verification attempts: {}\n\r", account.verification_attempt_count);
    }
    std::format_to(std::back_inserter(output), "Blocked: {}\n\r", (account.blocked ? "yes" : "no"));
    if (account.blocked) {
        std::format_to(std::back_inserter(output), "Blocked by: {}\n\r", account.blocked_by);
        std::format_to(std::back_inserter(output), "Block reason: {}\n\r", account.block_reason);
        std::format_to(std::back_inserter(output), "Blocked at: {}\n\r", format_account_timestamp(account.blocked_at));
    }
    std::format_to(std::back_inserter(output), "Created: {}\n\r", format_account_timestamp(account.created_at));
    std::format_to(std::back_inserter(output), "Updated: {}\n\r", format_account_timestamp(account.updated_at));
    if (!account.password_reset_by.empty()) {
        std::format_to(std::back_inserter(output), "Password reset by: {}\n\r", account.password_reset_by);
        std::format_to(std::back_inserter(output), "Password reset at: {}\n\r", format_account_timestamp(account.password_reset_at));
    }
    std::format_to(std::back_inserter(output), "Characters ({}): ", account.characters.size());
    if (account.characters.empty()) {
        output.append("(none)");
    } else {
        for (size_t index = 0; index < account.characters.size(); ++index) {
            if (index > 0)
                output.append(", ");
            output.append(account.characters[index]);
        }
    }
    output.append("\n\r");
    return output;
}
