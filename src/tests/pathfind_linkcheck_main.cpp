// Load-bearing pathfind-layer acyclicity check. This TU is trivial; the test
// is whether rots_pathfind_linkcheck LINKS: it force-loads the whole
// rots_pathfind archive against RotS::combat + RotS::world + RotS::persist +
// RotS::entity + RotS::core + RotS::platform (its six legal downward
// layers) and NO app/game libraries, so any upward edge (e.g. a symbol
// defined in an app-layer TU like utility.cpp, db_boot.cpp, or comm.cpp, or
// in rots_script above it) is an unresolved-symbol link error. See
// tests/combat_linkcheck_main.cpp for the L3-peer analogue this mirrors one
// tier down.
int main() { return 0; }
