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

#endif // FP_POLICY_H
