#ifndef ACCOUNT_MANAGEMENT_PRESENTATION_H
#define ACCOUNT_MANAGEMENT_PRESENTATION_H

#include "account_management_types.h"

#include <string_view>

namespace account {

/// Formats a bounded character name for display after first-null normalization.
std::string format_character_name_for_display(std::string_view character_name);
/// Builds the linked-character prompt using a nonretained bounded storage root.
std::string format_account_character_prompt(std::string_view root_directory, const AccountData& account, RosterSort sort, RosterFilter filter);
/// Builds the linked-character list using a nonretained bounded storage root.
std::string format_account_character_list(std::string_view root_directory, const AccountData& account, RosterSort sort);
/// Formats the failure banner, or empty text when no rejected logins are recorded.
std::string format_account_login_failure_notice(const AccountData& account);
std::string format_account_summary(const AccountData& account);

} // namespace account

#endif
