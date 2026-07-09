#include "../boards.h"
#include "../db.h"
#include "../pkill.h"
#include "../structs.h"

#include <gtest/gtest.h>

#include <cstdio>

// Phase 3 Task 5 (MSVC bring-up): the LLP64 fixture-guard trap flagged in the Phase 3
// plan is that `sizeof(long) != 4` is used across this test suite as a GTEST_SKIP
// condition for legacy-fixture tests built on native structs -- and Windows x64 is
// LLP64 (long stays 4 bytes there, unlike Linux/macOS x86-64's LP64), so that guard
// does NOT skip on Windows. Whether that is actually safe depends on whether each
// fixture struct's real size on Windows x64 still matches the frozen 32-bit on-disk
// size the legacy binary decoders were built against.
//
// This test runs UNCONDITIONALLY on every platform/preset (no skip) and prints the
// actual sizes so a CI log from the windows-msvc job is hard, quotable evidence
// instead of pure reasoning-from-the-standard. See the reasoning this pins down,
// worked through per-field, in boards_json_tests.cpp's file-header comment.
TEST(Llp64Probe, ReportsFixtureStructAndScalarSizes) {
    std::printf("[llp64-probe] sizeof(long)=%zu sizeof(void*)=%zu\n", sizeof(long), sizeof(void*));
    std::printf("[llp64-probe] sizeof(rent_info)=%zu (frozen 32-bit: 48)\n", sizeof(rent_info));
    std::printf("[llp64-probe] sizeof(obj_file_elem)=%zu (frozen 32-bit: 56)\n", sizeof(obj_file_elem));
    std::printf("[llp64-probe] sizeof(follower_file_elem)=%zu (frozen 32-bit: 28)\n", sizeof(follower_file_elem));
    std::printf("[llp64-probe] sizeof(exploit_record)=%zu (frozen 32-bit: 80)\n", sizeof(exploit_record));
    std::printf("[llp64-probe] sizeof(board_msginfo)=%zu (frozen 32-bit: 28)\n", sizeof(board_msginfo));
    std::printf("[llp64-probe] sizeof(PKILL)=%zu (frozen 32-bit: 24)\n", sizeof(PKILL));

    // rent_info/follower_file_elem/exploit_record/PKILL carry no `long` or pointer
    // members at all (only int/short/char[]), so their size is identical on every
    // ABI this project targets -- ILP32, LP64, and LLP64 alike (docs/data-formats/
    // object-rent-files.md's field-by-field byte-offset derivation confirms
    // rent_info/obj_file_elem/follower_file_elem's 32-bit sizes; exploit_record's 80
    // is likewise confirmed in .superpowers/sdd/p2b-task-2-report.md). Pinned here
    // unconditionally (not a GTEST_SKIP-guarded ASSERT) as a portability invariant:
    // if this ever fails on any platform, one of these structs grew a long/pointer
    // member and the corresponding legacy-fixture guard needs re-auditing.
    EXPECT_EQ(sizeof(rent_info), 48u);
    EXPECT_EQ(sizeof(follower_file_elem), 28u);
    EXPECT_EQ(sizeof(exploit_record), 80u);
    EXPECT_EQ(sizeof(PKILL), 24u);

    // obj_file_elem has exactly one width-variable member (`long bitvector`), so its
    // size matches the frozen 32-bit layout wherever `long` is 4 bytes -- true on
    // ILP32 (32-bit Linux) and LLP64 (Windows x64) alike, false only on LP64
    // (64-bit Linux/macOS), which is why its GTEST_SKIP guards stay `sizeof(long) !=
    // 4` unchanged (no pointer-width extension needed).
    if (sizeof(long) == 4) {
        EXPECT_EQ(sizeof(obj_file_elem), 56u);
    }

    // board_msginfo has a `char* heading` pointer member in addition to five ints --
    // its size instead tracks pointer width, which diverges from `long` width
    // specifically on LLP64 (Windows x64: 4-byte long, 8-byte pointer). This is the
    // one guard in the suite (boards_json_tests.cpp) extended to also check
    // `sizeof(void*) != 4`.
    if (sizeof(long) == 4 && sizeof(void*) == 4) {
        EXPECT_EQ(sizeof(board_msginfo), 28u);
    }
}
