// Load-bearing core-layer acyclicity check. This TU is trivial; the test is
// whether rots_core_linkcheck LINKS: it force-loads the whole rots_core
// archive against only libc/libstdc++ (no game libraries), so any upward edge
// (e.g. a symbol defined in an app-layer TU like utility.cpp) is an
// unresolved-symbol link error. See tests/platform_linkcheck_main.cpp for the
// L0 analogue this mirrors.
int main() { return 0; }
