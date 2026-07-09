// Verifies the header is self-contained (compiles on its own). The "../" quote
// path (not a bare "account_management_migration.h") keeps this portable: GCC/Clang
// builds add -iquote search paths for src/, but MSVC has no quote-only include
// option (/I would also expose src/ to angle-bracket lookups, letting the
// project's limits.h shadow the CRT <limits.h>), so the relative path is the
// only spelling that works on every toolchain (Phase 3 Task 6).
#include "../account_management_migration.h"

int account_management_migration_header_compiles = 0;
