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

namespace safe_template {
// A single required conversion in a template's expected signature. String is
// the only kind any current call site needs.
enum class Conv { String };

// Validates that `tmpl`'s ordered run of conversions (ignoring literal "%%"
// and any non-conversion text) is a PREFIX of `expected` -- same order, and
// no MORE conversions than `expected` describes, with no unsupported
// specifier (%d, %n, %5s, %-s, a dangling trailing %, etc. all count as
// unsupported, since this validator only ever accepts bare %s). A template
// with FEWER conversions than `expected` is accepted: the surplus trailing
// args are ignored, exactly as sprintf ignores surplus varargs -- live world
// data depends on this (a one-%s message_sell at a two-arg call site, a
// fully-literal death_cry2 at a two-%s herald call site). `args` supplies the
// replacement text; its size must equal `expected`'s (the call site pairs the
// two) or the check fails.
//
// On a signature match, returns `tmpl` with each %s substituted by the
// matching leading `args` entry and each %% collapsed to a literal '%' --
// byte-identical to what sprintf(tmpl, args...) would have produced,
// including the ignored-surplus-args case. On any mismatch (including a null
// `tmpl`), logs one mudlog line tagged with `context` and returns `fallback`
// unexpanded.
std::string expand_checked(const char *tmpl, std::initializer_list<Conv> expected,
                           std::initializer_list<std::string_view> args, std::string_view fallback,
                           std::string_view context);

// Convenience entry point for the "exactly one bare %s consuming one
// argument" shape -- script.cpp's five SCRIPT_DO_SAY-family sites
// (SCRIPT_DO_SAY, SCRIPT_DO_YELL, SCRIPT_SEND_TO_CHAR, SCRIPT_SEND_TO_ROOM,
// SCRIPT_SEND_TO_ROOM_X), each of which replaces a
// `sprintf(output, curr->text, txt1)`. Delegates to expand_checked() for
// the actual scan/validate/substitute -- same signature-match contract,
// no duplicated logic; this only adds a one-arg-shaped call surface.
//
// death_cry2/shop.cpp's existing expand_checked() call sites always pass
// an already-non-null std::string_view (GET_NAME()/money_message()
// results). script.cpp's `txt1` comes from get_text_param(), which can
// legitimately return a null char* (an unset SCRIPT_PARAM_STRn, or a
// SCRIPT_PARAM_CHn_NAME whose target character pointer is null) --
// constructing std::string_view(nullptr) is undefined behavior. A null
// `arg` is guarded with nz() (utils.h) here before it ever reaches
// expand_checked(): the template is still treated as well-formed and
// substitutes the literal "(null)" for the missing text -- byte-identical
// to what glibc's sprintf("%s", NULL) printed on the old path, per the
// depot-wide nz() convention -- rather than being rejected or hitting UB.
std::string expand_checked_one(const char *tmpl, const char *arg, std::string_view fallback,
                               std::string_view context);
} // namespace safe_template
