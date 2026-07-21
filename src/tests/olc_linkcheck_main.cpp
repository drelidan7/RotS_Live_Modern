// Load-bearing OLC-layer acyclicity check. This TU is trivial; the test
// is whether rots_olc_linkcheck LINKS: it force-loads the whole rots_olc
// archive against RotS::script (+ transitively RotS::pathfind +
// RotS::combat + RotS::world + RotS::persist + RotS::entity + RotS::core +
// RotS::platform, its eight legal downward layers) and NO app/game
// libraries, so any upward edge (e.g. a symbol defined in an app-layer TU
// like utility.cpp, db_boot.cpp, modify.cpp, or comm.cpp) is an
// unresolved-symbol link error. See tests/script_linkcheck_main.cpp for the
// L4-band analogue one tier down, and tests/pathfind_linkcheck_main.cpp for
// the single-nearest-layer link-list precedent this checker also follows.
int main() { return 0; }
