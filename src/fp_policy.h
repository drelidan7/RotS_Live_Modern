#ifndef FP_POLICY_H
#define FP_POLICY_H

// Guards the cross-platform floating-point determinism contract (Phase 1;
// see docs/BUILD.md "FP determinism"). Included by the combat/stat/HP
// translation units so a regression fails the SHIPPING build, not just the
// tests. The deterministic subset is + - * / sqrt, double-only.
#include <cfloat>

// FLT_EVAL_METHOD == 0 means IEEE double evaluation (SSE / ARM). 2 == x87
// 80-bit extended precision, 1 == double-extended -- both round intermediates
// differently across platforms and are banned. -1 == indeterminate: cannot
// check at compile time, the fp_determinism smoke test backstops it.
#if defined(FLT_EVAL_METHOD) && FLT_EVAL_METHOD > 0
#error "Non-IEEE float evaluation (x87/extended precision) breaks cross-platform \
determinism. Build with -msse2 -mfpmath=sse (GNU x86). See docs/BUILD.md."
#endif

#ifdef __FAST_MATH__
#error "-ffast-math / -Ofast breaks cross-platform FP determinism. See docs/BUILD.md."
#endif

#include <cmath>

// Combat-math double-interior boundary (fp-interiors wave, 2026-07). Every core-combat formula
// computes in double and lands its result in its unchanged int destination through exactly this
// call -- one uniform std::lround (round-half-away-from-zero), no exception table (storage stays
// int, so display/cost/death logic only ever sees ints). See docs/BUILD.md "FP determinism" and
// docs/superpowers/specs/2026-07-22-fp-interiors-design.md. Grep 'to_game_int' for every boundary;
// no bare lround/cast is allowed at a converted call site.
namespace rots::fp {
inline int to_game_int(double value)
{
    return static_cast<int>(std::lround(value));
}
}

#endif // FP_POLICY_H
