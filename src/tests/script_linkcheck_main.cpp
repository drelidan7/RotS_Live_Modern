// Load-bearing script-layer acyclicity check. This TU is trivial; the test
// is whether rots_script_linkcheck LINKS: it force-loads the whole
// rots_script archive against RotS::pathfind + RotS::combat + RotS::world +
// RotS::persist + RotS::entity + RotS::core + RotS::platform (its seven
// legal downward/peer layers -- RotS::pathfind resolves the one sanctioned
// intra-band edge, find_first_step()) and NO app/game libraries, so any
// upward edge (e.g. a symbol defined in an app-layer TU like utility.cpp,
// db_boot.cpp, or comm.cpp) is an unresolved-symbol link error. See
// tests/pathfind_linkcheck_main.cpp for the L4-band analogue one tier down,
// and tests/world_linkcheck_main.cpp for the peer-edge-inclusion precedent
// (RotS::persist there, RotS::pathfind here).
int main() { return 0; }
