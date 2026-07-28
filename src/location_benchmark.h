#ifndef LOCATION_BENCHMARK_H
#define LOCATION_BENCHMARK_H

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Consumer-free perf harness for the LS-3b store swap (T3; the wave's
// spec-mandated "occupant-iteration microbenchmark vs the intrusive list,
// plus the boot-time delta"). Design corrected per
// .superpowers/sdd/ls3b-census-review.md finding F14 against the draft in
// .superpowers/sdd/ls3b-census-d.md Sec5.2:
//   (1) the draft's single measured arm accumulated a PAYLOAD dereference
//       (c->abs_number) in both the walk-under-test and its vector control,
//       which swamps the traversal-cost signal the harness exists to see.
//       Every walk below has a PRIMARY row that accumulates the POINTER
//       VALUE (so traversal is the measured work) and a labelled SECONDARY
//       "payload" row that keeps the realistic dereference for completeness.
//   (2) the draft had no mutation arm at all, though census C's own K12
//       finding (char_to_room() is an O(n) tail-walk insert in
//       entity/placement.cpp; detach_char_from_room()'s non-head unlink is
//       an O(n) predecessor scan) is the entire reason a swapped
//       representation might legitimately win or lose on this axis. M0
//       below is that arm.
//   (3) every report below carries a `representation_tag` so a reader is
//       never left guessing which side of the swap a number was measured
//       against (F14 point 3).
//
// Timing is NEVER a ctest gate here, matching the tree's own standing
// posture (save_benchmark.cpp / save_benchmark_tests.cpp,
// scripts/combat-golden.sh's capture-only rung): only
// location_benchmark_tests.cpp's CORRECTNESS assertions (row shapes,
// checksums, restored world state) run under ctest/ASan. The numeric
// report is a controller-run, PR-presented artifact
// (.superpowers/sdd/ls3b-perf-baseline.md is its pre-swap capture).
namespace locbench {

// Identifies the store representation a report was measured against.
// Update this string in the SAME commit that lands the store swap (T5) --
// never edit it to make an old report look newer, and never edit it
// without also re-running the baseline/comparison capture.
inline constexpr std::string_view kRepresentationTag = "private-handle-post-split";

// One timed measurement: `iterations` operations are performed inside each
// of `repetitions` independently-timed spans (repetitions clamped to >= 5
// by run_benchmark, per the wave's own process rule). ns_total_* are the
// per-span wall-clock totals across repetitions; ns_per_op_median is
// ns_total_median / iterations -- the figure the comparison thresholds
// (M1/M2 <= 1.10x, M0 <= 1.25x -- see ls3b-perf-baseline.md) are expressed
// against. MEDIAN, not mean, is the headline (rather than StageTiming's
// avg in the save_benchmark.cpp precedent) so a single QEMU/thermal outlier
// cannot skew the number the threshold compares -- the same posture
// save_benchmark.cpp's own finalize_shares() clamp records for emulation.
struct TimingStat {
    // Operations performed inside ONE timed span (the batch size).
    int iterations = 0;
    // Independently-timed spans this stat summarizes (>= 5).
    int repetitions = 0;
    long ns_total_min = 0;
    long ns_total_median = 0;
    long ns_total_max = 0;
    double ns_per_op_median = 0.0;
};

// M0 -- mutation cost at one occupancy point (F14's required addition).
// tail_insert: marginal cost of char_to_room() appending the k-th occupant
// into a room that already holds k-1 (today's O(n) tail walk).
// unlink_{head,middle,tail}: cost of detach_char_from_room() removing the
// occupant at that position out of a k-occupant chain (head is O(1) today;
// middle/tail are O(n) predecessor scans -- census C's K12).
// place_then_remove_cycle: a full k-insert-then-k-remove churn, the whole
// class K12 describes, as one measured unit.
struct MutationRow {
    int occupancy = 0;
    TimingStat tail_insert;
    TimingStat unlink_head;
    TimingStat unlink_middle;
    TimingStat unlink_tail;
    TimingStat place_then_remove_cycle;
};

// M1 -- occupant-chain iteration at one occupancy point.
// pointer_walk accumulates the pointer VALUE of each occupant visited (the
// F14-corrected primary row: traversal is the measured work, not a member
// dereference that costs the same in a vector control). payload_walk is
// the secondary, clearly-labelled row that dereferences abs_number, kept
// for completeness per F14 point 1's "run the payload variant as a
// separate, labelled row" instruction.
//
// Both checksums are computed and self-verified by run_benchmark() BEFORE
// they are ever reported (see location_benchmark.cpp) -- a walk that
// visits the wrong set, or the right set fewer times, fails run_benchmark()
// itself, not merely the timing. payload_checksum is additionally
// HOST-INDEPENDENT and computable in closed form by a caller
// (occupancy * (occupancy - 1) / 2, since abs_number is stamped 0..occupancy-1
// deterministically) -- that closed form is what
// location_benchmark_tests.cpp asserts against.
struct IterationRow {
    int occupancy = 0;
    TimingStat pointer_walk;
    TimingStat payload_walk;
    // Sum of (uintptr_t)c over one full pass of occupants(room). Host/run
    // dependent (raw addresses) -- tests can only assert non-zero.
    uint64_t pointer_checksum = 0;
    // Sum of c->abs_number over one full pass. Host-independent: equals
    // occupancy * (occupancy - 1) / 2.
    int64_t payload_checksum = 0;
};

// M2 -- location_of()/room_of() lookup cost over a population spread
// round-robin across the same rooms M0/M1 used (F14 point 2 / census D
// surprise #7: this is the swap's LARGEST call-site population -- 264
// location_of() + 279 room_of() production sites versus 70 occupants() +
// 24 first_occupant() -- and the metric nobody had budgeted for).
// Character i is placed into room_ids[i % room_ids.size()] -- a caller
// wishing to verify location_checksum independently replays that same
// round-robin assignment.
struct LookupRow {
    int character_count = 0;
    int room_count = 0;
    TimingStat location_of_walk;
    TimingStat room_of_walk;
    // Sum of location_of(ch) over the population -- host-independent given
    // the round-robin assignment documented above.
    int64_t location_checksum = 0;
    // Count of characters for which room_of(ch) == room_by_id_total() of
    // their assigned room -- should equal character_count exactly. Proves
    // room_of() resolves to the RIGHT room object, not merely A room.
    int room_of_correct_count = 0;
};

struct BenchmarkReport {
    std::string representation_tag;
    std::vector<MutationRow> mutation;
    std::vector<IterationRow> iteration;
    std::vector<LookupRow> lookup;
};

// Runs all three arms (M0/M1/M2) over `occupancy_points` (e.g. {1, 8, 64}),
// using rooms [room_id_base, room_id_base + occupancy_points.size()) --
// the CALLER must have already stood up a world covering that range (e.g.
// ScopedTestWorld(room_count)) and every room in it must start (and will
// end) EMPTY; run_benchmark() restores every room to empty before
// returning, success or failure, so a caller's world-state-restored
// assertion after this call is a meaningful leak check (the monolithic
// runner's own fixture-hygiene rule).
//
// `repetitions` is clamped up to 5 if a caller passes fewer (the wave's own
// process rule: >= 5 repetitions). `lookup_population` is M2's total
// character count and must be >= occupancy_points.size() (so every room
// gets at least one character); characters are freed before returning.
//
// Returns false (with *error set) on a usage error (empty occupancy_points,
// a non-positive occupancy, or lookup_population too small) OR if an
// internal self-check fails (a mutation primitive returned an unexpected
// result, or an iteration/lookup arm's observed checksum did not match the
// value independently computed from the pool it built) -- see
// location_benchmark.cpp's per-arm comments for exactly what each check
// covers. A benchmark run cannot otherwise fail.
bool run_benchmark(const std::vector<int> &occupancy_points, int room_id_base, int repetitions,
                   int lookup_population, BenchmarkReport *out, std::string *error);

// Machine-readable key=value block, one `key=value` per line (plus a couple
// of `#`-prefixed banner/section comments for a human skimming stdout). The
// baseline-capture step (ls3b-perf-baseline.md) is built by piping this
// output through a grep on the `key=value` lines.
std::string format_report(const BenchmarkReport &report);

} // namespace locbench

#endif // LOCATION_BENCHMARK_H
