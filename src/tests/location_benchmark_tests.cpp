// location_benchmark_tests.cpp

// LS-3b T3 (the perf harness + pre-swap baseline capture). Correctness-only
// coverage for src/entity/location_benchmark.cpp's locbench::run_benchmark()
// -- matching src/tests/save_benchmark_tests.cpp's precedent exactly: this
// suite asserts row SHAPE and CHECKSUM correctness, never a duration. Timing
// numbers are informational; the tree's standing posture (save_benchmark.cpp,
// scripts/combat-golden.sh's capture-only rung) is that timing never gates
// ctest.
//
// Every occupancy point below is placed into its OWN room out of a small
// ScopedTestWorld -- the established multi-room test-world idiom (test_world.h's
// own class comment cites mage_tests.cpp's 33-room static as precedent) --
// rather than a hand-rolled resolver stub, because run_benchmark() spreads
// M2's lookup population across ALL of the arm's rooms by id, which needs a
// resolver that maps distinct ids to distinct room_data instances (a stub
// single-room resolver, as placement_tests.cpp's CharToRoomTest suite uses,
// cannot do that).

#include "../handler.h"
#include "../location_benchmark.h"
#include "rots/core/character.h"
#include "rots/core/room.h"
#include "test_world.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

// The occupancy points every correctness test below uses: small enough to
// run fast (this is a ctest-gated correctness suite, not the baseline
// capture), but with three DISTINCT values so per-row occupancy plumbing
// bugs (e.g. a row silently reusing another row's fields) would be caught.
const std::vector<int> kTestOccupancyPoints = {1, 3, 5};
constexpr int kTestRoomIdBase = 0;
constexpr int kTestRepetitions = 5; // the wave's own process-rule floor.
constexpr int kTestLookupPopulation =
    9; // 3 rooms * 3, uneven-friendly (9 % 3 == 0 AND != room_count trivially).

// True when `stat` looks like a real timing measurement: repetitions
// matches what was requested, iterations (the batch size) is positive, and
// min <= median <= max -- the one universal invariant across every arm
// regardless of what it measures. Never inspects a nanosecond VALUE.
void ExpectPlausibleTiming(const locbench::TimingStat &stat, int expected_repetitions,
                           int expected_iterations) {
    EXPECT_EQ(stat.repetitions, expected_repetitions);
    EXPECT_EQ(stat.iterations, expected_iterations);
    EXPECT_GE(stat.ns_total_min, 0);
    EXPECT_LE(stat.ns_total_min, stat.ns_total_median);
    EXPECT_LE(stat.ns_total_median, stat.ns_total_max);
    EXPECT_GE(stat.ns_per_op_median, 0.0);
}

} // namespace

TEST(LocationBenchmark, RunBenchmarkReportsExpectedShapeChecksumsAndRestoresWorldState) {
    ScopedTestWorld world(static_cast<int>(kTestOccupancyPoints.size()));

    locbench::BenchmarkReport report;
    std::string error;
    ASSERT_TRUE(locbench::run_benchmark(kTestOccupancyPoints, kTestRoomIdBase, kTestRepetitions,
                                        kTestLookupPopulation, &report, &error))
        << error;

    EXPECT_EQ(report.representation_tag, locbench::kRepresentationTag);
    EXPECT_EQ(std::string(locbench::kRepresentationTag), "intrusive-dual-store-pre-swap");

    // --- M0: mutation ---
    ASSERT_EQ(report.mutation.size(), kTestOccupancyPoints.size());
    for (std::size_t i = 0; i < kTestOccupancyPoints.size(); ++i) {
        const int occupancy = kTestOccupancyPoints[i];
        const locbench::MutationRow &row = report.mutation[i];
        EXPECT_EQ(row.occupancy, occupancy);

        // tail_insert / unlink_*: one op measured per repetition.
        ExpectPlausibleTiming(row.tail_insert, kTestRepetitions, 1);
        ExpectPlausibleTiming(row.unlink_head, kTestRepetitions, 1);
        ExpectPlausibleTiming(row.unlink_middle, kTestRepetitions, 1);
        ExpectPlausibleTiming(row.unlink_tail, kTestRepetitions, 1);
        // place_then_remove_cycle: occupancy inserts + occupancy removes.
        ExpectPlausibleTiming(row.place_then_remove_cycle, kTestRepetitions, occupancy * 2);
    }

    // --- M1: iteration ---
    ASSERT_EQ(report.iteration.size(), kTestOccupancyPoints.size());
    for (std::size_t i = 0; i < kTestOccupancyPoints.size(); ++i) {
        const int occupancy = kTestOccupancyPoints[i];
        const locbench::IterationRow &row = report.iteration[i];
        EXPECT_EQ(row.occupancy, occupancy);

        // Host-independent, exact: abs_number is stamped 0..occupancy-1.
        const int64_t expected_payload_checksum =
            static_cast<int64_t>(occupancy) * (occupancy - 1) / 2;
        EXPECT_EQ(row.payload_checksum, expected_payload_checksum);
        // Host/run dependent (raw addresses) -- only non-zero is assertable,
        // and only because occupancy > 0 in every row here.
        EXPECT_GT(row.pointer_checksum, 0u);

        const int inner_repeat = std::max(1, 200 / std::max(occupancy, 1));
        ExpectPlausibleTiming(row.pointer_walk, kTestRepetitions, occupancy * inner_repeat);
        ExpectPlausibleTiming(row.payload_walk, kTestRepetitions, occupancy * inner_repeat);
    }

    // --- M2: lookup ---
    ASSERT_EQ(report.lookup.size(), 1u);
    const locbench::LookupRow &lookup_row = report.lookup[0];
    EXPECT_EQ(lookup_row.character_count, kTestLookupPopulation);
    EXPECT_EQ(lookup_row.room_count, static_cast<int>(kTestOccupancyPoints.size()));

    // Independently replay the documented round-robin assignment (character
    // i -> room_ids[i % room_count]) to compute the expected location
    // checksum WITHOUT going through run_benchmark()'s own internals.
    int64_t expected_location_checksum = 0;
    for (int i = 0; i < kTestLookupPopulation; ++i) {
        expected_location_checksum +=
            kTestRoomIdBase + (i % static_cast<int>(kTestOccupancyPoints.size()));
    }
    EXPECT_EQ(lookup_row.location_checksum, expected_location_checksum);
    EXPECT_EQ(lookup_row.room_of_correct_count, kTestLookupPopulation);

    ExpectPlausibleTiming(lookup_row.location_of_walk, kTestRepetitions, kTestLookupPopulation);
    ExpectPlausibleTiming(lookup_row.room_of_walk, kTestRepetitions, kTestLookupPopulation);

    // --- fixture hygiene: run_benchmark() must restore every room it used
    // to empty (the monolithic single-process runner's own leak-catching
    // rule) ---
    for (std::size_t i = 0; i < kTestOccupancyPoints.size(); ++i) {
        const int room_id = kTestRoomIdBase + static_cast<int>(i);
        EXPECT_EQ(rots::entity::first_occupant(room_by_id_total(room_id)), nullptr)
            << "room " << room_id << " left non-empty after run_benchmark()";
    }
}

TEST(LocationBenchmark, RunBenchmarkRejectsUsageErrors) {
    locbench::BenchmarkReport report;
    std::string error;

    EXPECT_FALSE(locbench::run_benchmark({}, 0, 5, 1, &report, &error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(locbench::run_benchmark({1, 0}, 0, 5, 2, &report, &error));
    EXPECT_FALSE(error.empty());

    error.clear();
    EXPECT_FALSE(locbench::run_benchmark({1, 3}, 0, 5, /*lookup_population=*/1, &report, &error));
    EXPECT_FALSE(error.empty());
}

TEST(LocationBenchmark, FormatReportEmitsRepresentationTagAndRowCounts) {
    ScopedTestWorld world(static_cast<int>(kTestOccupancyPoints.size()));

    locbench::BenchmarkReport report;
    std::string error;
    ASSERT_TRUE(locbench::run_benchmark(kTestOccupancyPoints, kTestRoomIdBase, kTestRepetitions,
                                        kTestLookupPopulation, &report, &error))
        << error;

    const std::string text = locbench::format_report(report);
    EXPECT_NE(text.find("representation_tag=intrusive-dual-store-pre-swap"), std::string::npos);
    EXPECT_NE(text.find("mutation.count=3"), std::string::npos);
    EXPECT_NE(text.find("iteration.count=3"), std::string::npos);
    EXPECT_NE(text.find("lookup.count=1"), std::string::npos);
    EXPECT_NE(text.find("mutation[0].occupancy=1"), std::string::npos);
    EXPECT_NE(text.find("iteration[2].occupancy=5"), std::string::npos);
}

// Env-gated report dump for the T3 baseline capture (census D Sec5.2(b)'s
// own recommendation: "a small main() target, or the gtest with a
// --gtest_filter + an env-gated report dump"). Skipped by default so `ctest`/
// the monolithic runner never pay for the larger population; the controller
// runs it explicitly (ROTS_LOCBENCH_DUMP=1 + --gtest_filter) to capture
// .superpowers/sdd/ls3b-perf-baseline.md. Still asserts run_benchmark()'s
// return value when it does run -- a broken build should fail LOUD even
// here -- but never asserts a duration.
TEST(LocationBenchmark, DumpsReportWhenRequestedByEnvironment) {
    if (std::getenv("ROTS_LOCBENCH_DUMP") == nullptr) {
        GTEST_SKIP() << "set ROTS_LOCBENCH_DUMP=1 to capture the LS-3b perf baseline report";
    }

    const std::vector<int> occupancy_points = {1, 8, 64};
    ScopedTestWorld world(static_cast<int>(occupancy_points.size()));

    locbench::BenchmarkReport report;
    std::string error;
    ASSERT_TRUE(locbench::run_benchmark(occupancy_points, /*room_id_base=*/0, /*repetitions=*/9,
                                        /*lookup_population=*/256, &report, &error))
        << error;

    std::printf("%s", locbench::format_report(report).c_str());
}
