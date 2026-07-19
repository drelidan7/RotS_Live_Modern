#pragma once
// rots/platform/numbered_name.h — NumberedName (combat-seed Task 3):
// extracted verbatim from handler.h (placement-seam Task 4's
// parse_numbered_name()/get_char() deferral riders). parse_numbered_name()
// moves to rots_util.cpp (rots_platform, L0) in this same task, which needs
// this type visible without including handler.h -- rots_platform links only
// rots_build_flags and must not pull in the app-tier / rots_core include
// surface (see PlatformLayerAcyclicity in CMakeLists.txt; rots_platform's
// target_include_directories exposes only platform/include, the mirror image
// of rots/platform/log.h's existing L0-visibility precedent). handler.h now
// compatibility-includes this header in the struct's former place, so every
// existing caller still sees the identical spelling/layout.

#include <string_view>

// Non-mutating replacement for get_number's "N.keyword" prefix parse.
struct NumberedName {
    // Requested match ordinal from the "N." prefix: 0 = malformed prefix
    // (legacy no-match), 1 = no prefix present.
    int match_number;
    // Keyword after the prefix (whole input when no prefix); borrows the
    // caller's storage, bounded and first-null-normalized.
    std::string_view name;
};
