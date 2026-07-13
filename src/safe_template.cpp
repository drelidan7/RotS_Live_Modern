#include "safe_template.h"

#include "utils.h" // vmudlog, BRF

#include <algorithm>
#include <vector>

namespace safe_template {
namespace {
// Outcome of scanning a template's '%' runs: the ordered list of
// conversions the scanner recognized (only ever Conv::String today,
// since bare %s is the only specifier this validator accepts), plus a
// `malformed` flag set the instant the scan hits anything it doesn't
// recognize as safe (a specifier other than bare %s, a dangling '%' at
// the end of the string, etc.). Once `malformed` is set, `conversions`
// is abandoned (not exhaustive) and the whole template is rejected.
struct ScanResult {
    // Conversions found, in template order; only ever Conv::String,
    // since that is the only enumerator Conv defines.
    std::vector<Conv> conversions;
    // Set as soon as the scanner sees a specifier it doesn't
    // recognize (or a truncated one); forces the fallback path.
    bool malformed = false;
};

// Scans `tmpl` for '%' runs, classifying each one. Literal "%%" is
// skipped (not a conversion); bare "%s" is recorded as Conv::String;
// anything else -- %d/%x/%n, or a flagged/width'd form like %5s -- marks
// the scan malformed and returns immediately, since this validator's
// accept-list is deliberately narrow (see safe_template.h).
ScanResult scan_conversions(std::string_view tmpl) {
    ScanResult result;
    std::size_t i = 0;
    while (i < tmpl.size()) {
        if (tmpl[i] != '%') {
            ++i;
            continue;
        }

        if (i + 1 >= tmpl.size()) {
            // Dangling '%' at the very end: no specifier follows to
            // classify, so refuse to guess.
            result.malformed = true;
            return result;
        }

        const char next = tmpl[i + 1];
        if (next == '%') {
            i += 2;
            continue;
        }
        if (next == 's') {
            result.conversions.push_back(Conv::String);
            i += 2;
            continue;
        }

        result.malformed = true;
        return result;
    }
    return result;
}

// Rebuilds `tmpl` with each validated bare %s replaced by the next
// entry of `args` (in order) and each %% collapsed to a literal '%';
// every other character is copied through unchanged. Only ever called
// once the signature has matched, which guarantees `args` holds at least
// as many entries as `tmpl` has %s conversions (it may hold MORE -- the
// surplus trailing args are simply never consumed, mirroring sprintf) --
// so `next_arg` can never run past `args.end()`. Produces byte-identical
// output to sprintf(tmpl, args...) for these %s-only templates, since
// there are no field-width/flag conversions in play.
std::string substitute(std::string_view tmpl, std::initializer_list<std::string_view> args) {
    std::string out;
    out.reserve(tmpl.size());

    const std::string_view *next_arg = args.begin();
    std::size_t i = 0;
    while (i < tmpl.size()) {
        if (tmpl[i] == '%' && i + 1 < tmpl.size()) {
            if (tmpl[i + 1] == '%') {
                out.push_back('%');
                i += 2;
                continue;
            }
            if (tmpl[i + 1] == 's') {
                out.append(*next_arg);
                ++next_arg;
                i += 2;
                continue;
            }
        }
        out.push_back(tmpl[i]);
        ++i;
    }
    return out;
}
} // namespace

std::string expand_checked(const char *tmpl, std::initializer_list<Conv> expected,
                           std::initializer_list<std::string_view> args, std::string_view fallback,
                           std::string_view context) {
    const ScanResult scan = tmpl ? scan_conversions(tmpl) : ScanResult{{}, true};

    // sprintf parity: a template may legitimately use FEWER conversions than the
    // call site supplies arguments -- the surplus trailing varargs are simply
    // ignored, which is well-defined. Live world data relies on this (e.g. a
    // one-%s message_sell string at a two-arg call site, or a fully literal
    // death_cry2 at a two-%s herald call site). So accept when the template's
    // conversions are a PREFIX of `expected` (same order, no more conversions
    // than args), binding each conversion to the matching leading arg and
    // leaving any trailing args unused. Reject only genuine hazards: an
    // unsupported specifier (scan.malformed -- covers %d/%n/%5s/%-s/dangling
    // %), MORE conversions than args, or a caller whose arg count doesn't
    // match the signature it declared.
    const bool signature_matches =
        !scan.malformed && scan.conversions.size() <= expected.size() &&
        std::equal(scan.conversions.begin(), scan.conversions.end(), expected.begin()) &&
        args.size() == expected.size();

    if (!signature_matches) {
        vmudlog(BRF, "safe_template: rejected malformed template (context=%s)",
                std::string(context).c_str());
        return std::string(fallback);
    }

    return substitute(tmpl, args);
}

std::string expand_checked_one(const char *tmpl, const char *arg, std::string_view fallback,
                               std::string_view context) {
    // Coalesce a null arg to "" here -- std::string_view(nullptr) is
    // undefined behavior, and script.cpp's txt1 (unlike death_cry2/
    // shop.cpp's always-non-null args) can legitimately be null. Once
    // coalesced, delegate to expand_checked() for the real scan/
    // validate/substitute work.
    return expand_checked(tmpl, {Conv::String}, {std::string_view(arg ? arg : "")}, fallback,
                          context);
}
} // namespace safe_template
