// Load-bearing foundation-acyclicity check. This TU is trivial; the test is
// whether rots_platform_linkcheck LINKS: it force-loads the whole rots_platform
// archive against only libc/libstdc++ (no game libraries), so any upward edge
// (an undefined game symbol like vmudlog) is an unresolved-symbol link error.
int main() { return 0; }
