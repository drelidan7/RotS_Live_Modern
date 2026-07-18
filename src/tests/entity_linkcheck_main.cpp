// Load-bearing entity-layer acyclicity check. This TU is trivial; the test is
// whether rots_entity_linkcheck LINKS: it force-loads the whole rots_entity
// archive against RotS::core + RotS::platform (its two legal downward
// layers) and NO app/game libraries, so any upward edge (e.g. a symbol
// defined in an app-layer TU like utility.cpp or comm.cpp) is an
// unresolved-symbol link error. See tests/core_linkcheck_main.cpp for the
// L1 analogue this mirrors.
int main() { return 0; }
