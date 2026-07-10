#pragma once

// safe_template.h -- validate a printf-style template that comes from world/builder
// data (a mob's death_cry2, a shop's message fields, etc.) against an expected
// conversion signature BEFORE expanding it, so a malformed template (wrong
// specifier, wrong conversion count, or a %n) can never read past the caller's
// fixed argument list. A well-formed template expands byte-identically to the
// sprintf/snprintf call it replaces; a malformed one falls back to a fixed
// caller-supplied string and logs once via mudlog.
//
// This validator deliberately only understands bare "%s" conversions and literal
// "%%" -- every current call site (death_cry2, all shop message fields) is a
// %s-only template. Extend Conv (and the scanner in safe_template.cpp) only when
// a real call site needs a new specifier; do not widen the accept-list ahead of
// need.

#include <initializer_list>
#include <string>
#include <string_view>

namespace safe_template
{
    // A single required conversion in a template's expected signature. String is
    // the only kind any current call site needs.
    enum class Conv
    {
        String
    };

    // Validates that `tmpl`'s ordered run of conversions (ignoring literal "%%"
    // and any non-conversion text) is EXACTLY `expected` -- same count, same
    // order, no extra conversions, and no unsupported specifier (%d, %n, %5s,
    // %-s, etc. all count as unsupported, since this validator only ever accepts
    // bare %s). `args` supplies the replacement text for each accepted %s, in
    // order; its size must equal `expected`'s or the check fails.
    //
    // On a signature match, returns `tmpl` with each %s substituted by the
    // matching `args` entry and each %% collapsed to a literal '%' -- byte-
    // identical to what sprintf(tmpl, args...) would have produced. On any
    // mismatch (including a null `tmpl`), logs one mudlog line tagged with
    // `context` and returns `fallback` unexpanded.
    std::string expand_checked(const char* tmpl,
        std::initializer_list<Conv> expected,
        std::initializer_list<std::string_view> args,
        std::string_view fallback,
        std::string_view context);
}
