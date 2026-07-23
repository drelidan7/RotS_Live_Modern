#pragma once

// Shared primitive for the Corrupt Legacy File Recovery plan
// (docs/superpowers/plans/2026-07-07-corrupt-legacy-recovery.md). Header-only
// (not duplicated per-TU like the small file-I/O helpers in
// convert_exploits.cpp/convert_plrobjs.cpp) because the sanitization policy
// itself is LOCKED by that plan: a single implementation shared by both
// recovery families (exploits' chtime/chVictimName, plrobjs' alias keyword)
// means the policy can't silently drift between them.
//
// Locked policy: for a fixed-width string field with no NUL byte anywhere
// within its declared width, keep the longest prefix of printable ASCII
// (0x20-0x7E) starting at byte 0, capped at width-1 (so the sanitized value
// always leaves room for a NUL terminator when the fixed-width field is
// later re-encoded). If byte 0 itself is non-printable, the sanitized field
// is the empty string. Fields that DO have a NUL within their width are left
// alone by recovery entirely -- the existing strict codecs already handle
// those losslessly (they read up to the NUL, exactly like a C string).

#include <cstddef>
#include <cstring>
#include <string>

namespace legacy_salvage {

/// Returns whether a byte is within the printable seven-bit ASCII range.
inline bool is_printable_ascii(unsigned char byte)
{
    return byte >= 0x20 && byte <= 0x7E;
}

/// Reports whether a fixed-width binary field contains no null byte in its complete declared range.
///
/// The pointer and explicit width intentionally remain a binary-data contract: embedded and trailing
/// null bytes participate in the result and must not be truncated through a textual view.
// True iff `field` (exactly `width` bytes) has no NUL byte anywhere within
// its declared width -- i.e. is a candidate for sanitize_fixed_width_field.
// A field that DOES have an in-width NUL is not touched by recovery: the
// existing strict codecs already read it correctly (bounded by that NUL),
// so re-sanitizing it here would only risk changing already-good data.
inline bool fixed_width_field_has_no_nul(const char* field, size_t width)
{
    return std::memchr(field, '\0', width) == nullptr;
}

/// Recovers the printable prefix of a complete fixed-width binary field.
///
/// The explicit width remains authoritative so recovery can distinguish unterminated legacy storage
/// from normal text; callers must provide readable storage for the full declared range.
// Applies the locked sanitization policy to a fixed-width field already
// known (via fixed_width_field_has_no_nul) to have no NUL within `width`.
// Safe to call even if that precondition doesn't hold -- it just stops at
// the first embedded NUL like any printable-ASCII scan would -- but callers
// should gate on fixed_width_field_has_no_nul first so NUL-terminated
// fields are left byte-for-byte untouched instead of being re-derived.
inline std::string sanitize_fixed_width_field(const char* field, size_t width)
{
    if (width == 0)
        return std::string();

    const size_t cap = width - 1;
    size_t length = 0;
    while (length < cap && is_printable_ascii(static_cast<unsigned char>(field[length])))
        ++length;

    return std::string(field, length);
}

} // namespace legacy_salvage
