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
// As of this task the facility is built but unused: utility.cpp/utils.h are
// untouched and still own log()/mudlog()/vmudlog(). The Task 2 rewire turns
// log()/mudlog() into thin wrappers over write_stderr()/write(), moves
// vmudlog()'s definition here, and registers the broadcast sink from
// comm.cpp's run_the_game().

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
// call sites across the tree). Declared here so callers can reach it via this
// header, but NOT YET DEFINED in rots_log.cpp this task -- utility.cpp still
// owns the one definition of vmudlog(), and defining it here too would be a
// duplicate symbol. Task 2 deletes utility.cpp's copy and moves the
// definition into rots_log.cpp, where it will call
// rots::log::write(buf, type, rots::log::kVmudlogBroadcastLevel, true).
void vmudlog(char type, const char* format, ...);
