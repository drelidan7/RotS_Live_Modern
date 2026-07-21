#pragma once
// editor_hooks.h -- dependency-inversion seam for the shared descriptor
// string-editor init call (Cluster B wave Task 1; cb-task-1-brief.md
// Step 3; cb-census.md section 5.1). string_add_init(descriptor_data*,
// char**) (modify.cpp:175) is the interactive multi-line editor state-
// machine init every shape*.cpp OLC editor's macro-driven "start editing
// this text field" branch calls (shapemdl.cpp:264, shapeobj.cpp:277,
// shapemob.cpp:396, shaperom.cpp:338, shapescript.cpp:1197,
// shapezon.cpp:546) -- a genuinely session-coupled body (reads/writes
// d->str/d->max_str/d->len_str/d->character, calls send_to_char(),
// CREATE/CREATE1) that cannot itself relocate.
//
// HEADER-HOME RULING (cb-census.md section 5.1): NOT output_seam.h -- that
// header's own file comment scopes it to comm.cpp's send_to_char/act
// output sinks; string_add_init is (a) owned by modify.cpp, not comm.cpp,
// and (b) a descriptor editor state-machine init, not an output sink --
// outside that header's stated single responsibility. This standalone
// header follows the world_hooks.h/persist_hooks.h shape instead: a single
// hook, real body registered by its one owning TU (modify.cpp).
//
// Backing storage + dispatch live in editor_hooks.cpp (not folded into
// modify.cpp): modify.cpp stays app-compiled permanently (session-coupled),
// while every consumer (the six shape*.cpp editors) is bound for the
// rots_olc library (a later Cluster B task) -- mirroring script_hooks.cpp's
// own PERS precedent (real body/registrar in one app-tier owner, backing
// storage in a TU that can join the consumer's library once it promotes).
// CONSUMER-FREE at this task's landing time: no shape*.cpp call site
// converts yet.

struct descriptor_data;

namespace rots::editor {

// string_add_init(descriptor_data*, char**) (modify.cpp:175). Abort-tripwire
// default: there is no safe placeholder editor-init behavior to fall back
// to (an unregistered hit would leave the descriptor's PLR_WRITING/str
// state exactly as session-coupled and unrecoverable as a null PERS()
// pointer would be) -- mirrors script_hooks.h's pers_fn/virt_program_fn/
// virt_assignmob_fn taxonomy, not command_interpreter_fn's loud-logged-
// no-op class. Untested-by-design for its unregistered path (no death
// test, standing rule); the registered path is this hook's discriminator
// coverage, same posture as every other abort tripwire in the tree.
using string_editor_init_fn = void (*)(descriptor_data* d, char** str);
void set_string_editor_init_hook(string_editor_init_fn hook);
void dispatch_string_editor_init(descriptor_data* d, char** str);

} // namespace rots::editor
