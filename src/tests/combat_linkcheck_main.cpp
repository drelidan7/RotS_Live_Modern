// Load-bearing combat-layer acyclicity check. This TU is trivial; the test is
// whether rots_combat_linkcheck LINKS: it force-loads the whole rots_combat
// archive against RotS::entity + RotS::core + RotS::platform (its three legal
// downward layers) and NO app/game libraries, so any upward edge (e.g. a
// symbol defined in an app-layer TU like utility.cpp, db_boot.cpp, or
// comm.cpp) is an unresolved-symbol link error. See
// tests/world_linkcheck_main.cpp for the L3-peer analogue this mirrors.
int main() { return 0; }
