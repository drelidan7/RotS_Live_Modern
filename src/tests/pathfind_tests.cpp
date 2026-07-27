// Coverage rider for the LS-1 Wave (Tranche C) pathfind batch: graph.cpp's
// find_first_step()/BFS machinery is the sole rots_pathfind TU, and before
// this rider it had NO behavioral test anywhere in the tree (only
// pathfind_linkcheck_main.cpp, which exercises linkage, not behavior; see
// ls1-census.md Step 7's coverage-gap note for pathfind/graph.cpp). The
// batch converted find_first_step()'s internal &world[TOROOM(...)] reads
// (VALID_EDGE/MARK/UNMARK macro bodies, the src_room/target_room inits, the
// BFS-queue MARK/bfs_enqueue calls) to room_by_id_total(...), and its two
// ch->in_room/vict->in_room argument reads (do_wiztrack/hunt_victim) to
// location_of(...). This suite exercises find_first_step() directly across
// a small multi-room graph so every converted macro expansion actually
// runs, closing the gap the census flagged.

#include "../handler.h"
#include "rots/core/character.h"
#include "rots/core/room.h"
#include "test_world.h"
#include <gtest/gtest.h>

// Declared the same way every existing consumer (mobact.cpp/mudlle.cpp/
// spec_pro.cpp) forward-declares it -- graph.cpp has no header of its own.
int find_first_step(int src, int target);

extern room_data world;
extern int top_of_world;

namespace {

// Three-room world: 0 -(NORTH)-> 1 -(NORTH)-> 2. Room 0 also gets a spare
// exit slot the "no path" test repurposes to isolate room 2.
constexpr int kPathfindTestWorldRoomCount = 3;

// find_first_step()'s BFS walks every room in [0, top_of_world] via
// world[curr_room] each call (the UNMARK(room_by_id_total(curr_room))
// clear-marks loop), so every room in range must be a real, initialized
// room -- not just the three this suite wires exits between.
// ScopedTestWorld's reset_all_rooms() guarantees exactly that for the whole
// allocation, in either constructor branch: dummy_room_data() zeroes
// dir_option[]/room_flags, and LS-3a T1 Stage B added the bfs_dir/bfs_next/
// funct zeroing that neither dummy_room_data() nor room_data's constructor
// does. A hand-rolled loop re-zeroing all five per room used to stand here;
// it is now redundant with the fixture member below and has been retired.
// Only the two non-default exits this suite actually needs are installed.
struct PathfindTestWorld {
    ScopedTestWorld scoped_world{kPathfindTestWorldRoomCount};
    room_direction_data room0_north{};
    room_direction_data room1_north{};

    PathfindTestWorld() {
        top_of_world = kPathfindTestWorldRoomCount - 1;

        room0_north.exit_info = 0;
        room0_north.to_room = 1;
        room_by_id_total(0)->dir_option[NORTH] = &room0_north;

        room1_north.exit_info = 0;
        room1_north.to_room = 2;
        room_by_id_total(1)->dir_option[NORTH] = &room1_north;
    }
};

} // namespace

TEST(FindFirstStep, ReturnsAlreadyThereWhenSourceEqualsTarget) {
    PathfindTestWorld guard;

    EXPECT_EQ(find_first_step(0, 0), BFS_ALREADY_THERE);
}

TEST(FindFirstStep, ReturnsErrorForOutOfRangeRoomIds) {
    PathfindTestWorld guard;

    EXPECT_EQ(find_first_step(-1, 0), BFS_ERROR);
    EXPECT_EQ(find_first_step(0, top_of_world + 1), BFS_ERROR);
}

TEST(FindFirstStep, FindsTheFirstStepAcrossAMultiHopPath) {
    PathfindTestWorld guard;

    // Exercises every converted macro expansion in find_first_step(): the
    // clear-marks UNMARK(room_by_id_total(curr_room)) loop over all three
    // rooms, src_room/target_room = room_by_id_total(...), the first-hop
    // VALID_EDGE/MARK/bfs_enqueue(room_by_id_total(...)) at room 0, and the
    // BFS-queue MARK/bfs_enqueue(room_by_id_total(...)) reached from room 1
    // while walking toward room 2.
    EXPECT_EQ(find_first_step(0, 2), NORTH);
    EXPECT_EQ(find_first_step(1, 2), NORTH);
}

TEST(FindFirstStep, ReturnsNoPathWhenTargetIsUnreachable) {
    PathfindTestWorld guard;

    // Room 2 has no outbound exit back, and nothing else in the graph
    // reaches it except via room 1 -- sever that one edge so room 2 is
    // isolated, then ask for a path FROM the isolated room.
    room_by_id_total(1)->dir_option[NORTH] = nullptr;

    EXPECT_EQ(find_first_step(0, 2), BFS_NO_PATH);
}
