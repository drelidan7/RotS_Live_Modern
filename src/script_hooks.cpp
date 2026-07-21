// script_hooks.cpp -- backing storage + dispatch for script_hooks.h's PERS
// forwarder (l4-seed wave, Task 1; l4-task-1-brief.md Step 2c; l4-census.md
// section 3.2). Unlike script_hooks.h's command_interpreter hook (backing
// storage in interpre.cpp, the TU that already defines the real body), PERS's
// real body and registrar are utility.cpp -- a different owner -- and
// neither mudlle.cpp nor mudlle2.cpp (PERS's eventual caller) converts this
// task, so there is no single natural owning TU yet. Mirrors combat_hooks.cpp
// (a seam header with several FUTURE callers rather than one owning TU,
// hence its own dedicated .cpp instead of folding storage into a caller
// file): it only reaches down into rots::log (RotS::platform) for its
// tripwire message and passes char_data* straight through opaquely -- never
// dereferenced, never allocated -- so no game-type header is needed here.

#include "script_hooks.h"

#include "rots/platform/log.h"

#include <cstdlib>

namespace {

// Backing storage for the registered PERS hook (register_pers_hook(),
// utility.cpp). Null until that registration runs; the null default is an
// abort tripwire (see dispatch_pers() below) -- see script_hooks.h's pers_fn
// comment for why this class, not a logged no-op.
rots::script::pers_fn g_pers_hook = nullptr;

} // namespace

namespace rots::script {

void set_pers_hook(pers_fn hook) { g_pers_hook = hook; }

char* dispatch_pers(char_data* target, char_data* observer, int capitalize, int force_visible)
{
    if (g_pers_hook) {
        return g_pers_hook(target, observer, capitalize, force_visible);
    }
    rots::log::write_stderr(
        "rots::script: FATAL dispatch_pers() called with no handler registered -- this should "
        "be unreachable once register_pers_hook() has run.");
    std::abort();
}

} // namespace rots::script
