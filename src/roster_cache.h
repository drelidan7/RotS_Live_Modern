#ifndef ROSTER_CACHE_H
#define ROSTER_CACHE_H

#include "rots/core/types.h"

struct char_file_u;

#include <string>
#include <string_view>

namespace roster_cache {

/// Owns the persisted fields used to render, sort and filter one character roster row.
/// Display names remain account-owned; side membership derives from the persisted race.
struct RosterSummary {
    // Persisted level used for display and ordering.
    unsigned char level = 0;
    // Persisted race used for display, side grouping and profession adjustments.
    unsigned char race = 0;
    // False when the character file could not be read or parsed. The roster renders those as
    // "[ ?? ???]"; caching the failure stops the most useless rows being the most expensive.
    bool readable = false;
    // Persisted profession coefficient clamped to the square_root[] domain 0..170
    // before storage; used for display, filtering and profession ordering.
    short prof_coof[MAX_PROFS + 1] = { 0 };
};

/// Copies a character summary to the output. A null output returns false; an unreadable
/// backing file returns true with readable=false. Inputs stop at their first null byte;
/// character keys are normalized and owned. Disabled mode reads through on every call.
bool get(std::string_view root_directory, std::string_view account_name,
    std::string_view character_name, RosterSummary* out_summary);

// Drops the entry for exactly one character. Called from the write_account_character_file
// chokepoint. Deliberately NOT a global flush: character saves are frequent (autosave), so a
// coarse flush like account_cache's would keep this cache permanently cold and useless.
void invalidate_character(std::string_view root_directory, std::string_view character_name);

// Empties the map. Call in test-fixture SetUp() for isolation.
void clear();

// Whether get() memoizes. Default OFF so the test binary and non-server callers keep exact
// uncached behavior; the live server calls set_enabled(true) once at boot.
void set_enabled(bool enabled);
/// Returns whether reads currently memoize summaries.
bool is_enabled();

// Signature of the on-disk reader get() delegates to on a miss.
using ReaderFn = bool (*)(std::string_view, std::string_view, std::string_view,
    char_file_u*, std::string*);

// Test-only seam: override the backing reader. Pass nullptr to restore the real
// account::read_account_character_file. Changing the reader clears cached summaries.
// The callback must not retain input views; this seam and cache are single-threaded.
void set_backing_reader_for_testing(ReaderFn reader);

} // namespace roster_cache

#endif
