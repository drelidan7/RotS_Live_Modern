#ifndef ACCOUNT_MANAGEMENT_H
#define ACCOUNT_MANAGEMENT_H

#include "account_management_assets.h"
#include "account_management_identity.h"
#include "account_management_migration.h"
#include "account_management_presentation.h"
#include "account_management_storage.h"
#include "account_management_types.h"

namespace account {

/// Returns account.characters positions after stable sorting, filtering and the 200-row cap.
std::vector<size_t> ordered_roster_indices(std::string_view root_directory,
    const AccountData& account, RosterSort sort, RosterFilter filter);
/// Returns the persisted sort spelling; Account is the empty string.
const char* roster_sort_to_string(RosterSort sort);
/// Parses a bounded first-null spelling; invalid input leaves output unchanged.
bool roster_sort_from_string(std::string_view value, RosterSort* out_sort);

} // namespace account

#endif
