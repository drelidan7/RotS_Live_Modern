#pragma once
// rots/platform/log.h — dependency-inverted logging sink (spec §13). First
// file of the platform include tree (rots_platform links only libc/libstdc++
// -- see the whole-archive PlatformLayerAcyclicity check in CMakeLists.txt --
// so it cannot call up into the game to broadcast a log line to connected
// players). write_stderr()/write() are the platform half of the historic
// log()/mudlog() bodies (utility.cpp); the game-coupled broadcast half (the
// LEVEL_AREAGOD clamp, PRF_LOG* preference gating, descriptor_list walk,
// color framing) registers itself here as a Sink at boot instead, so control
// flows back up through a callback at runtime while static dependencies stay
// pointing down.
//
// As of the Task 2 rewire, log()/mudlog() (utility.cpp) are thin wrappers
// over write_stderr()/write() below, vmudlog()'s one definition lives in
// rots_log.cpp, and comm.cpp's run_the_game() registers the broadcast sink
// (register_mudlog_broadcast_sink()) immediately after resetting
// descriptor_list, before the first log() call.

#include <functional>
#include <string_view>

namespace rots::log {

// Callback a higher layer (comm.cpp) registers to receive mudlog broadcasts.
// msg is the raw (unformatted, unclamped) message body write() was given,
// type is the mudlog channel (OFF/BRF/NRM/SPL/CMP below), level is the
// caller-supplied mudlog level (pre-LEVEL_AREAGOD-clamp -- the sink owns that
// clamp, since LEVEL_AREAGOD is a game constant this header must not depend
// on). No sink registered => notify is a no-op: byte-identical to today's
// behavior, where a pre-boot broadcast over the empty descriptor_list also
// emits nothing.
using Sink = std::function<void(std::string_view msg, char type, int level)>;

// Installs a new sink and returns the previous one (possibly empty), so a
// caller can chain-restore it -- e.g. a test that saves the old sink, installs
// a capturing one, and restores the original in an RAII guard's destructor.
Sink set_sink(Sink sink);

// Writes a timestamped message line to stderr. This is the platform half of
// the historic log(): byte-identical to that function's body (including its
// rots::text::truncate_at_null boundary call and the LLP64 time_t note).
void write_stderr(std::string_view message);

// The platform half of the historic mudlog(): writes a typed timestamped
// line to stderr when to_file is set (mudlog's `file` flag), returns without
// notifying the sink when level < 0 (mudlog's file-only path), and otherwise
// hands the message to the registered sink, if any.
void write(std::string_view message, char type, int level, bool to_file);

// Mudlog's own broadcast level (LEVEL_GOD's current value, rots/core/types.h)
// vmudlog() always logs at. Hard-coded here rather than including
// rots/core/types.h, so the platform layer's include tree does not pull in
// the core data-model headers just for one constant. utility.cpp
// static_asserts this literal against LEVEL_GOD directly (Task 2), so the
// two values can never silently diverge.
inline constexpr int kVmudlogBroadcastLevel = 93;

} // namespace rots::log

/* defines for mudlog() */

#define OFF 0
#define BRF 1
#define NRM 2
#define SPL 3
#define CMP 4

// Printf-style mudlog entry point (global namespace, matching its existing
// call sites across the tree). Defined in rots_log.cpp (Task 2 rewire moved
// the one definition there from utility.cpp), formatting into a fixed
// BUFSIZE-2048 buffer and calling
// rots::log::write(buf, type, rots::log::kVmudlogBroadcastLevel, true).
void vmudlog(char type, const char* format, ...);
