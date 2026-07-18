// Load-bearing persist-layer acyclicity check. This TU is trivial; the test
// is whether rots_persist_linkcheck LINKS: it force-loads the whole
// rots_persist archive against RotS::entity + RotS::core + RotS::platform
// (its three legal downward layers) and NO app/game libraries, so any
// upward edge (e.g. a symbol defined in an app-layer TU like spec_pro.cpp,
// db_boot.cpp, or db_world.cpp) is an unresolved-symbol link error. See
// tests/entity_linkcheck_main.cpp for the L2 analogue this mirrors.
int main() { return 0; }
