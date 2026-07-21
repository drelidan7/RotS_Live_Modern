// editor_hooks.cpp -- backing storage + dispatch for editor_hooks.h's one
// hook (Cluster B wave Task 1; cb-task-1-brief.md Step 3). modify.cpp
// registers the real string_add_init() body (session-coupled, stays
// app-compiled permanently); the six shape*.cpp editors are this hook's
// future consumers, bound for the rots_olc library a later Cluster B task
// creates -- neither side is this TU's single natural owner (the same
// "seam header, no single owning caller" shape as combat_hooks.cpp/
// script_hooks.cpp), so this file starts in ROTS_SERVER_SOURCES (app
// layer) and is a plain candidate to join rots_olc once that library
// exists and needs it -- see this file's own CMakeLists.txt placement.

#include "editor_hooks.h"

#include "rots/platform/log.h"

#include <cstdlib>

namespace {

// Backing storage for the registered string-editor-init hook
// (register_string_editor_init_hook(), modify.cpp). Null until that
// registration runs; the null default is an abort tripwire (see
// dispatch_string_editor_init() below) -- see editor_hooks.h's
// string_editor_init_fn comment for why this class, not a logged no-op.
rots::editor::string_editor_init_fn g_string_editor_init_hook = nullptr;

} // namespace

namespace rots::editor {

void set_string_editor_init_hook(string_editor_init_fn hook)
{
    g_string_editor_init_hook = hook;
}

void dispatch_string_editor_init(descriptor_data* d, char** str)
{
    if (g_string_editor_init_hook) {
        g_string_editor_init_hook(d, str);
        return;
    }
    rots::log::write_stderr(
        "rots::editor: FATAL dispatch_string_editor_init() called with no handler registered -- "
        "this should be unreachable once register_string_editor_init_hook() has run.");
    std::abort();
}

} // namespace rots::editor
