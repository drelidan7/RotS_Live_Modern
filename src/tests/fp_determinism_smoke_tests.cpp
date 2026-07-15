#include <bit>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <gtest/gtest.h>

// Permanent toolchain tripwire (Phase 1: FP determinism). The game's
// combat/stat math is + - * / sqrt in double; these must be bit-identical
// across i386(SSE)/x64/arm64/MSVC. This test fails if a build path regresses
// to x87 extended precision, enables -ffast-math/-Ofast, or re-enables FMA
// contraction (the project's shared policy is -ffp-contract=off on
// GNU-family targets and /fp:precise on MSVC -- see src/fp_policy.h and
// src/CMakeLists.txt). Keep this test forever. See docs/BUILD.md "FP
// determinism".
//
// Every arithmetic pin below is CHARACTERIZATION style: it hardcodes the
// exact IEEE-754 double bit pattern observed on a conforming build, rather
// than re-deriving an expected value from the same arithmetic (which would
// only prove the compiler is self-consistent, not that it matches IEEE
// double semantics). These bit patterns are IEEE-754 double representations
// of ordinary decimal arithmetic; they must be identical on every conforming
// platform (i386-SSE/x64/arm64/MSVC) -- a cross-platform mismatch is a
// determinism finding, not a constant to "fix".
//
// The inputs are `volatile` so the compiler cannot constant-fold the whole
// expression at compile time (which would evaluate it on the host's own FP
// path instead of the target's runtime FP unit) and, for the FMA-sensitive
// chain, so the multiply/add aren't optimized away independent of
// -ffp-contract.

TEST(FpDeterminismSmoke, IeeeDoubleEvaluation) {
    // 0 == IEEE double (SSE/ARM). x87 == 2. -1 == indeterminate (skip).
#if defined(FLT_EVAL_METHOD)
    if (FLT_EVAL_METHOD >= 0) {
        EXPECT_EQ(FLT_EVAL_METHOD, 0)
            << "Non-IEEE float evaluation (x87/extended precision) detected.";
    }
#endif
}

TEST(FpDeterminismSmoke, SqrtIsCorrectlyRoundedIeeeDouble) {
    // sqrt is IEEE-correctly-rounded on every conforming platform; this is
    // the standard IEEE-754 double bit pattern for sqrt(2.0).
    volatile double one = 1.0;
    const double s = std::sqrt(2.0 * one);
    EXPECT_EQ(std::bit_cast<std::uint64_t>(s), std::uint64_t{0x3FF6A09E667F3BCDULL});
}

TEST(FpDeterminismSmoke, MulAddChainIsStable) {
    // a * 7 + 2 with a = 1/3: a short +-*/ chain pinned to its exact IEEE
    // double bits.
    volatile double a = 1.0 / 3.0;
    volatile double seven = 7.0;
    volatile double two = 2.0;
    const double b = a * seven + two;
    EXPECT_EQ(std::bit_cast<std::uint64_t>(b), std::uint64_t{0x4011555555555555ULL});
}

TEST(FpDeterminismSmoke, FmaContractionShiftsCancellationChain) {
    // x * y + z is chosen so a fused multiply-add (single rounding of the
    // full product-plus-addend) produces a materially different result from
    // two separately rounded operations (multiply, then add), because the
    // addend nearly cancels the product. Under the project's
    // -ffp-contract=off / /fp:precise policy this must land on 2.0 exactly;
    // if a build regresses to fused contraction, this chain resolves to
    // ~2.9802322387695312 instead -- a large, easy-to-see divergence rather
    // than a last-bit rounding difference.
    volatile double x = 100000000.00000001;
    volatile double y = 100000000.00000001;
    volatile double z = -10000000000000000.0;
    const double r = x * y + z;
    EXPECT_EQ(std::bit_cast<std::uint64_t>(r), std::uint64_t{0x4000000000000000ULL});
}
