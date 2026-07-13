#ifndef PLAYER_FILE_FINALIZE_H
#define PLAYER_FILE_FINALIZE_H

#include <string_view>

// Crash-safe finalize primitives for the legacy (non-account) player-file save path.
// Portable standard C++ (std::filesystem with non-throwing error_code).

// Historical A/B oracle: system("rm <base>.*") then system("cp scratch versioned").
// Kept ONLY to prove byte/stale-file equivalence against the rename path in tests.
// NOT for production use.
/// Finalizes a legacy player file through the historical shell oracle using normalized bounded paths.
bool finalize_player_file_legacy(std::string_view scratch_path, std::string_view base_path,
                                 std::string_view versioned_path);

// Crash-safe finalize: atomically rename scratch -> versioned FIRST, then remove every
// OTHER "<base_name>." entry in dir_path (dot-anchored, so "bob." never matches "bobby.").
// Return contract: false BEFORE the rename means nothing changed (old save intact); false
// AFTER the rename means the new file IS written and only stale cleanup failed.
/// Atomically publishes a bounded scratch path and removes stale siblings without retaining views.
bool finalize_player_file_rename(std::string_view scratch_path, std::string_view dir_path,
                                 std::string_view base_name, std::string_view versioned_path);

#endif // PLAYER_FILE_FINALIZE_H
