#include "../location_benchmark.h"

#include "../handler.h"
#include "rots/core/character.h"
#include "rots/core/room.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace locbench {
namespace {

using Clock = std::chrono::steady_clock;

// Summarizes a batch of per-repetition nanosecond samples into a
// TimingStat. Empty `samples` yields an all-zero stat (defensive; every
// caller below always passes >= 1 sample).
TimingStat summarize(std::vector<long> samples, int iterations, int repetitions) {
    TimingStat stat;
    stat.iterations = iterations;
    stat.repetitions = repetitions;
    if (samples.empty()) {
        return stat;
    }
    std::sort(samples.begin(), samples.end());
    stat.ns_total_min = samples.front();
    stat.ns_total_max = samples.back();
    const std::size_t n = samples.size();
    stat.ns_total_median =
        (n % 2 == 1) ? samples[n / 2] : (samples[n / 2 - 1] + samples[n / 2]) / 2;
    stat.ns_per_op_median = (iterations > 0) ? static_cast<double>(stat.ns_total_median) /
                                                   static_cast<double>(iterations)
                                             : static_cast<double>(stat.ns_total_median);
    return stat;
}

// Times `body` `repetitions` times (each one independent span), running
// `after_each` (untimed) after every span -- the hook mutation arms use to
// restore per-repetition starting state without that restore counting
// toward the measured cost. `iterations` is the batch size (operations)
// inside ONE timed span, purely descriptive here (folded into the returned
// TimingStat, not used to loop `body` itself -- callers whose body performs
// more than one op per span pass the real count).
TimingStat time_repetitions(
    int repetitions, int iterations, const std::function<void()> &body,
    const std::function<void()> &after_each = [] {}) {
    std::vector<long> samples;
    samples.reserve(static_cast<std::size_t>(std::max(repetitions, 0)));
    for (int i = 0; i < repetitions; ++i) {
        const Clock::time_point start = Clock::now();
        body();
        const Clock::time_point end = Clock::now();
        samples.push_back(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
        after_each();
    }
    return summarize(std::move(samples), iterations, repetitions);
}

// Builds `count` NPC char_data instances in a stable-address pool
// (unique_ptr, so vector growth while building the pool never invalidates
// a pointer char_to_room()/occupants() already holds), stamped
// specials2.act |= MOB_ISNPC (so char_to_room()'s race-power branch, which
// needs a live zone table this harness deliberately does not stand up, is
// skipped -- see clear_char()'s own `if (!IS_NPC(ch))` precedent and
// placement_tests.cpp's identical stack-fixture idiom) and abs_number =
// 0..count-1 (the payload arm's host-independent expected checksum).
//
// Interleaves a same-size throwaway allocation per character
// (census-d.md Sec5.2(a) step 1, retained from the draft design): the
// measured heap layout then reflects realistic fragmentation rather than a
// contiguous block that would flatter a vector-shaped control identically
// to the chain walk.
std::vector<std::unique_ptr<char_data>> make_npc_pool(int count) {
    std::vector<std::unique_ptr<char_data>> pool;
    pool.reserve(static_cast<std::size_t>(std::max(count, 0)));
    for (int i = 0; i < count; ++i) {
        std::unique_ptr<char_data> throwaway = std::make_unique<char_data>();
        std::unique_ptr<char_data> ch = std::make_unique<char_data>();
        ch->specials2.act |= MOB_ISNPC;
        ch->abs_number = i;
        pool.push_back(std::move(ch));
        // `throwaway` frees here, immediately after `ch`'s allocation --
        // the interleave the fragmentation comment above describes.
    }
    return pool;
}

// True when `room_id` (resolved via room_by_id_total(), the same resolver
// char_to_room()/detach_char_from_room() use) is empty -- the harness's
// own leak check, run after every arm.
bool room_is_empty(int room_id) {
    return rots::entity::first_occupant(room_by_id_total(room_id)) == nullptr;
}

// M0(a): tail-insert marginal cost. `resident_pool` (already sized
// occupancy - 1) is spliced into `room_id` once via char_to_room() calls
// that are NOT timed; each timed repetition inserts `extra` (the k-th
// occupant) via ONE char_to_room() call, then (untimed) detaches it again
// so every repetition measures the identical occupancy-1 starting shape.
bool measure_tail_insert(int room_id, int occupancy, int repetitions, TimingStat *out,
                         std::string *error) {
    std::vector<std::unique_ptr<char_data>> residents = make_npc_pool(occupancy - 1);
    for (std::unique_ptr<char_data> &resident : residents) {
        char_to_room(resident.get(), room_id);
    }

    std::unique_ptr<char_data> extra = std::make_unique<char_data>();
    extra->specials2.act |= MOB_ISNPC;

    bool detach_ok = true;
    *out = time_repetitions(
        repetitions, 1, [&]() { char_to_room(extra.get(), room_id); },
        [&]() { detach_ok = detach_ok && detach_char_from_room(extra.get()); });

    for (std::unique_ptr<char_data> &resident : residents) {
        detach_ok = detach_ok && detach_char_from_room(resident.get());
    }

    if (!detach_ok) {
        if (error) {
            *error = "measure_tail_insert: an expected detach_char_from_room() call returned false";
        }
        return false;
    }
    return true;
}

// M0(b): unlink cost at `position` (0-based index into the k-occupant
// chain, in publish order) within a k-occupant room. Each repetition
// (untimed) rebuilds the FULL k-chain in original order via char_to_room()
// calls, times ONLY ONE detach_char_from_room() call on the resident at
// `position`, then (untimed) detaches the remaining k-1 residents so the
// next repetition starts from an empty room again. The build/residual
// teardown are deliberately NOT run through time_repetitions()'s
// body/after_each split (which only supports untimed work AFTER the timed
// body): a manual loop is used here instead so the untimed rebuild runs
// BEFORE the one timed call, and only that one call's own span is
// recorded.
bool measure_unlink(int room_id, int occupancy, int position, int repetitions, TimingStat *out,
                    std::string *error) {
    std::vector<std::unique_ptr<char_data>> residents = make_npc_pool(occupancy);
    bool ok = true;

    std::vector<long> samples;
    samples.reserve(static_cast<std::size_t>(std::max(repetitions, 0)));
    for (int rep = 0; rep < repetitions; ++rep) {
        // Untimed: rebuild the full k-chain in original order.
        for (std::unique_ptr<char_data> &resident : residents) {
            char_to_room(resident.get(), room_id);
        }

        // Timed: the one detach under measurement.
        const Clock::time_point start = Clock::now();
        ok = ok && detach_char_from_room(residents[static_cast<std::size_t>(position)].get());
        const Clock::time_point end = Clock::now();
        samples.push_back(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());

        // Untimed: drain the remaining k-1 residents.
        for (std::size_t i = 0; i < residents.size(); ++i) {
            if (static_cast<int>(i) == position) {
                continue;
            }
            ok = ok && detach_char_from_room(residents[i].get());
        }
    }
    *out = summarize(std::move(samples), /*iterations=*/1, repetitions);

    if (!ok) {
        if (error) {
            *error = "measure_unlink: an expected detach_char_from_room() call returned false";
        }
        return false;
    }
    return true;
}

// M0(c): a full place-then-remove churn cycle over `occupancy` characters
// -- census C's K12 cost class (O(n) insert x O(n) unlink) realized as one
// measured unit rather than a single marginal op. Self-restoring: every
// timed span both builds and drains the room, so it leaves it empty.
bool measure_churn_cycle(int room_id, int occupancy, int repetitions, TimingStat *out,
                         std::string *error) {
    std::vector<std::unique_ptr<char_data>> pool = make_npc_pool(occupancy);
    bool ok = true;

    *out = time_repetitions(
        repetitions, occupancy * 2,
        [&]() {
            for (std::unique_ptr<char_data> &ch : pool) {
                char_to_room(ch.get(), room_id);
            }
            for (std::unique_ptr<char_data> &ch : pool) {
                ok = ok && detach_char_from_room(ch.get());
            }
        },
        [] {});

    if (!ok) {
        if (error) {
            *error = "measure_churn_cycle: an expected detach_char_from_room() call returned false";
        }
        return false;
    }
    return true;
}

// M1: occupant-chain iteration at one occupancy point. Builds the room
// ONCE (read-only walks below don't disturb it), self-verifies BOTH
// checksums against ground truth computed directly from the pool
// (independent of occupants() -- so a walk visiting the wrong set is
// caught here, not merely timed), then times `inner_repeat` full passes
// per repetition (inner_repeat scales up for small occupancy so the timed
// span stays measurable -- a 1-element walk alone is sub-tens-of-ns).
bool measure_iteration(int room_id, int occupancy, int repetitions, IterationRow *row,
                       std::string *error) {
    row->occupancy = occupancy;

    std::vector<std::unique_ptr<char_data>> pool = make_npc_pool(occupancy);
    for (std::unique_ptr<char_data> &ch : pool) {
        char_to_room(ch.get(), room_id);
    }

    uint64_t expected_pointer_checksum = 0;
    int64_t expected_payload_checksum = 0;
    for (std::unique_ptr<char_data> &ch : pool) {
        expected_pointer_checksum += reinterpret_cast<uintptr_t>(ch.get());
        expected_payload_checksum += ch->abs_number;
    }

    room_data *room = room_by_id_total(room_id);
    uint64_t observed_pointer_checksum = 0;
    int64_t observed_payload_checksum = 0;
    int observed_count = 0;
    for (char_data *c : rots::entity::occupants(room)) {
        observed_pointer_checksum += reinterpret_cast<uintptr_t>(c);
        observed_payload_checksum += c->abs_number;
        ++observed_count;
    }

    if (observed_count != occupancy || observed_pointer_checksum != expected_pointer_checksum ||
        observed_payload_checksum != expected_payload_checksum) {
        for (std::unique_ptr<char_data> &ch : pool) {
            detach_char_from_room(ch.get());
        }
        if (error) {
            *error = "measure_iteration: occupants() walk did not match the pool it was built from";
        }
        return false;
    }

    row->pointer_checksum = observed_pointer_checksum;
    row->payload_checksum = observed_payload_checksum;

    const int inner_repeat = std::max(1, 200 / std::max(occupancy, 1));

    // The two walks below store into a `volatile` sink so the optimizer cannot
    // elide them. The accumulation itself runs on a PLAIN local: C++20 deprecates
    // compound assignment whose left operand is volatile-qualified
    // ([depr.volatile.type]), and newer Apple clang promotes that deprecation to an
    // error under -Werror (CI macos-arm64 / macos-arm64-asan). A SIMPLE assignment
    // TO a volatile object is not deprecated, so one store per walk after the timed
    // region keeps the elision-proofing with none of the deprecation.
    uintptr_t pointer_accumulator = 0;
    row->pointer_walk = time_repetitions(repetitions, occupancy * inner_repeat, [&]() {
        for (int r = 0; r < inner_repeat; ++r) {
            for (char_data *c : rots::entity::occupants(room)) {
                pointer_accumulator += reinterpret_cast<uintptr_t>(c);
            }
        }
    });
    volatile uintptr_t pointer_sink = pointer_accumulator;

    int64_t payload_accumulator = 0;
    row->payload_walk = time_repetitions(repetitions, occupancy * inner_repeat, [&]() {
        for (int r = 0; r < inner_repeat; ++r) {
            for (char_data *c : rots::entity::occupants(room)) {
                payload_accumulator += c->abs_number;
            }
        }
    });
    volatile int64_t payload_sink = payload_accumulator;
    (void)pointer_sink;
    (void)payload_sink;

    bool ok = true;
    for (std::unique_ptr<char_data> &ch : pool) {
        ok = ok && detach_char_from_room(ch.get());
    }
    if (!ok) {
        if (error) {
            *error = "measure_iteration: an expected detach_char_from_room() call returned false";
        }
        return false;
    }
    return true;
}

// M2: location_of()/room_of() lookup cost over `character_count`
// characters spread round-robin across `room_ids`. Self-verifies both the
// deterministic location_checksum and the room_of()-resolves-to-the-right-
// room count before ever timing anything.
bool measure_lookup(const std::vector<int> &room_ids, int character_count, int repetitions,
                    LookupRow *row, std::string *error) {
    row->character_count = character_count;
    row->room_count = static_cast<int>(room_ids.size());

    std::vector<std::unique_ptr<char_data>> pool = make_npc_pool(character_count);
    for (int i = 0; i < character_count; ++i) {
        const int room_id = room_ids[static_cast<std::size_t>(i) % room_ids.size()];
        char_to_room(pool[static_cast<std::size_t>(i)].get(), room_id);
    }

    int64_t location_checksum = 0;
    int room_of_correct_count = 0;
    for (int i = 0; i < character_count; ++i) {
        const int expected_room_id = room_ids[static_cast<std::size_t>(i) % room_ids.size()];
        char_data *ch = pool[static_cast<std::size_t>(i)].get();
        location_checksum += location_of(ch);
        if (room_of(ch) == room_by_id_total(expected_room_id)) {
            ++room_of_correct_count;
        }
    }

    if (room_of_correct_count != character_count) {
        for (std::unique_ptr<char_data> &ch : pool) {
            detach_char_from_room(ch.get());
        }
        if (error) {
            *error =
                "measure_lookup: room_of() did not resolve every character to its assigned room";
        }
        return false;
    }

    row->location_checksum = location_checksum;
    row->room_of_correct_count = room_of_correct_count;

    // Plain accumulators + one simple volatile store per walk -- see the
    // [depr.volatile.type] note in measure_iteration() above.
    int64_t location_accumulator = 0;
    row->location_of_walk = time_repetitions(repetitions, character_count, [&]() {
        for (std::unique_ptr<char_data> &ch : pool) {
            location_accumulator += location_of(ch.get());
        }
    });
    volatile int64_t location_sink = location_accumulator;

    uintptr_t room_accumulator = 0;
    row->room_of_walk = time_repetitions(repetitions, character_count, [&]() {
        for (std::unique_ptr<char_data> &ch : pool) {
            room_accumulator += reinterpret_cast<uintptr_t>(room_of(ch.get()));
        }
    });
    volatile uintptr_t room_sink = room_accumulator;
    (void)location_sink;
    (void)room_sink;

    bool ok = true;
    for (std::unique_ptr<char_data> &ch : pool) {
        ok = ok && detach_char_from_room(ch.get());
    }
    if (!ok) {
        if (error) {
            *error = "measure_lookup: an expected detach_char_from_room() call returned false";
        }
        return false;
    }
    return true;
}

void append_timing(std::string *out, std::string_view prefix, const TimingStat &stat) {
    char line[256];
    std::snprintf(line, sizeof(line), "%.*s.iterations=%d\n", static_cast<int>(prefix.size()),
                  prefix.data(), stat.iterations);
    *out += line;
    std::snprintf(line, sizeof(line), "%.*s.repetitions=%d\n", static_cast<int>(prefix.size()),
                  prefix.data(), stat.repetitions);
    *out += line;
    std::snprintf(line, sizeof(line), "%.*s.ns_total_min=%ld\n", static_cast<int>(prefix.size()),
                  prefix.data(), stat.ns_total_min);
    *out += line;
    std::snprintf(line, sizeof(line), "%.*s.ns_total_median=%ld\n", static_cast<int>(prefix.size()),
                  prefix.data(), stat.ns_total_median);
    *out += line;
    std::snprintf(line, sizeof(line), "%.*s.ns_total_max=%ld\n", static_cast<int>(prefix.size()),
                  prefix.data(), stat.ns_total_max);
    *out += line;
    std::snprintf(line, sizeof(line), "%.*s.ns_per_op_median=%.3f\n",
                  static_cast<int>(prefix.size()), prefix.data(), stat.ns_per_op_median);
    *out += line;
}

} // namespace

bool run_benchmark(const std::vector<int> &occupancy_points, int room_id_base, int repetitions,
                   int lookup_population, BenchmarkReport *out, std::string *error) {
    if (!out) {
        if (error) {
            *error = "out must not be null";
        }
        return false;
    }
    if (occupancy_points.empty()) {
        if (error) {
            *error = "occupancy_points must not be empty";
        }
        return false;
    }
    for (int occupancy : occupancy_points) {
        if (occupancy < 1) {
            if (error) {
                *error = "occupancy_points must all be >= 1";
            }
            return false;
        }
    }
    if (repetitions < 5) {
        repetitions = 5; // process rule: >= 5 repetitions.
    }
    if (lookup_population < static_cast<int>(occupancy_points.size())) {
        if (error) {
            *error = "lookup_population must be >= occupancy_points.size()";
        }
        return false;
    }

    out->representation_tag = std::string(kRepresentationTag);
    out->mutation.clear();
    out->iteration.clear();
    out->lookup.clear();

    std::vector<int> room_ids;
    room_ids.reserve(occupancy_points.size());
    for (std::size_t i = 0; i < occupancy_points.size(); ++i) {
        room_ids.push_back(room_id_base + static_cast<int>(i));
    }

    for (std::size_t i = 0; i < occupancy_points.size(); ++i) {
        const int occupancy = occupancy_points[i];
        const int room_id = room_ids[i];
        MutationRow row;
        row.occupancy = occupancy;
        if (!measure_tail_insert(room_id, occupancy, repetitions, &row.tail_insert, error)) {
            return false;
        }
        if (!measure_unlink(room_id, occupancy, 0, repetitions, &row.unlink_head, error)) {
            return false;
        }
        if (!measure_unlink(room_id, occupancy, occupancy / 2, repetitions, &row.unlink_middle,
                            error)) {
            return false;
        }
        if (!measure_unlink(room_id, occupancy, occupancy - 1, repetitions, &row.unlink_tail,
                            error)) {
            return false;
        }
        if (!measure_churn_cycle(room_id, occupancy, repetitions, &row.place_then_remove_cycle,
                                 error)) {
            return false;
        }
        if (!room_is_empty(room_id)) {
            if (error) {
                *error = "room left non-empty after the mutation arm (fixture-hygiene self-check)";
            }
            return false;
        }
        out->mutation.push_back(row);
    }

    for (std::size_t i = 0; i < occupancy_points.size(); ++i) {
        const int occupancy = occupancy_points[i];
        const int room_id = room_ids[i];
        IterationRow row;
        if (!measure_iteration(room_id, occupancy, repetitions, &row, error)) {
            return false;
        }
        if (!room_is_empty(room_id)) {
            if (error) {
                *error = "room left non-empty after the iteration arm (fixture-hygiene self-check)";
            }
            return false;
        }
        out->iteration.push_back(row);
    }

    LookupRow lookup_row;
    if (!measure_lookup(room_ids, lookup_population, repetitions, &lookup_row, error)) {
        return false;
    }
    for (int room_id : room_ids) {
        if (!room_is_empty(room_id)) {
            if (error) {
                *error = "room left non-empty after the lookup arm (fixture-hygiene self-check)";
            }
            return false;
        }
    }
    out->lookup.push_back(lookup_row);

    return true;
}

std::string format_report(const BenchmarkReport &report) {
    std::string result = "# LS-3b T3 location benchmark report\n";
    result += "representation_tag=" + report.representation_tag + "\n";
    result += "mutation.count=" + std::to_string(report.mutation.size()) + "\n";
    for (std::size_t i = 0; i < report.mutation.size(); ++i) {
        const MutationRow &row = report.mutation[i];
        const std::string p = "mutation[" + std::to_string(i) + "]";
        result += p + ".occupancy=" + std::to_string(row.occupancy) + "\n";
        append_timing(&result, p + ".tail_insert", row.tail_insert);
        append_timing(&result, p + ".unlink_head", row.unlink_head);
        append_timing(&result, p + ".unlink_middle", row.unlink_middle);
        append_timing(&result, p + ".unlink_tail", row.unlink_tail);
        append_timing(&result, p + ".place_then_remove_cycle", row.place_then_remove_cycle);
    }

    result += "iteration.count=" + std::to_string(report.iteration.size()) + "\n";
    for (std::size_t i = 0; i < report.iteration.size(); ++i) {
        const IterationRow &row = report.iteration[i];
        const std::string p = "iteration[" + std::to_string(i) + "]";
        result += p + ".occupancy=" + std::to_string(row.occupancy) + "\n";
        append_timing(&result, p + ".pointer_walk", row.pointer_walk);
        append_timing(&result, p + ".payload_walk", row.payload_walk);
        result += p + ".pointer_checksum=" + std::to_string(row.pointer_checksum) + "\n";
        result += p + ".payload_checksum=" + std::to_string(row.payload_checksum) + "\n";
    }

    result += "lookup.count=" + std::to_string(report.lookup.size()) + "\n";
    for (std::size_t i = 0; i < report.lookup.size(); ++i) {
        const LookupRow &row = report.lookup[i];
        const std::string p = "lookup[" + std::to_string(i) + "]";
        result += p + ".character_count=" + std::to_string(row.character_count) + "\n";
        result += p + ".room_count=" + std::to_string(row.room_count) + "\n";
        append_timing(&result, p + ".location_of_walk", row.location_of_walk);
        append_timing(&result, p + ".room_of_walk", row.room_of_walk);
        result += p + ".location_checksum=" + std::to_string(row.location_checksum) + "\n";
        result += p + ".room_of_correct_count=" + std::to_string(row.room_of_correct_count) + "\n";
    }

    return result;
}

} // namespace locbench
