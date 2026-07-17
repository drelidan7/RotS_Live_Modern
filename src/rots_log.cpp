/* rots_log.cpp */
// Implements the dependency-inverted logging sink declared in
// rots/platform/log.h (spec §13). write_stderr()/write() are copied
// verbatim from the current log()/mudlog() bodies (utility.cpp); see that
// header's file comment for why the sink exists and what moves where in
// Task 2. vmudlog() is NOT defined here yet -- utility.cpp still owns its
// one definition this task (see log.h's comment on the declaration).

#include "rots/platform/log.h"

#include "text_view.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <utility>

namespace {

// Backing storage for the currently registered sink. A function-local static
// (rather than a namespace-scope global) constructs on first use -- guaranteed
// before that first use by the language -- so it can never race a global/
// static initializer in another translation unit that registers a sink
// during its own startup; ordinary namespace-scope statics across TUs have no
// such guarantee (the classic static-initialization-order fiasco).
rots::log::Sink& current_sink()
{
    static rots::log::Sink sink;
    return sink;
}

} // namespace

namespace rots::log {

Sink set_sink(Sink sink)
{
    Sink& slot = current_sink();
    Sink previous = std::move(slot);
    slot = std::move(sink);
    return previous;
}

/* writes a string to the log -- verbatim copy of utility.cpp's log() body */
void write_stderr(std::string_view message)
{
    // time_t ct(0);
    // char* time_string = asctime(localtime(&ct));

    //*(time_string + std::strlen(time_string) - 1) = '\0';
    // fprintf(stderr, "%-19.19s :: %s\n", time_string, str);

    // time_t (not long): localtime()/time() take a time_t*, which is 8 bytes on
    // Windows LLP64 while `long` is only 4 -- passing a `long*` made localtime()
    // read 4 bytes of adjacent stack as the high word, yielding an out-of-range
    // time, a null return, and an asctime(nullptr) abort (Phase 3 Task 6 crash,
    // STATUS_STACK_BUFFER_OVERRUN). Identical width to the old `long` on both the
    // 32-bit legacy build and 64-bit POSIX, so this only changes Windows behavior.
    message = rots::text::truncate_at_null(message);
    time_t current_time;
    current_time = time(0);
    char* timestamp = asctime(localtime(&current_time));
    *(timestamp + strlen(timestamp) - 1) = '\0';
    std::fprintf(stderr, "%-19.19s :: ", timestamp);
    if (!message.empty()) {
        // The view may not have a terminator, so write its known length instead
        // of rescanning it or allocating a temporary C string.
        std::fwrite(message.data(), sizeof(char), message.size(), stderr);
    }
    std::fputc('\n', stderr);
}

// The platform half of mudlog(): the file-write branch (verbatim) and the
// level < 0 early return (verbatim), then a sink notify in place of the
// game-coupled broadcast loop that used to follow directly (that loop -- the
// LEVEL_AREAGOD clamp, descriptor_list walk, PRF_LOG* preference gating,
// color framing -- becomes the app-layer sink registered via set_sink(),
// Task 2).
void write(std::string_view message, char type, int level, bool to_file)
{
    char* timestamp;
    // time_t (not long): see write_stderr() above -- a `long*` passed to
    // localtime() is only 4 of the 8 bytes it reads on Windows LLP64,
    // aborting in asctime(). This is on nearly every logging path, so the bug
    // crashed ~60 tests (Phase 3 Task 6).
    time_t current_time;

    message = rots::text::truncate_at_null(message);
    current_time = time(0);
    timestamp = asctime(localtime(&current_time));

    if (to_file) {
        std::fprintf(stderr, "%d, %-19.19s :: ", type, timestamp);
        if (!message.empty()) {
            // Keep stderr output bounded by the view rather than assuming its
            // data pointer reaches a null terminator.
            std::fwrite(message.data(), sizeof(char), message.size(), stderr);
        }
        std::fputc('\n', stderr);
    }
    if (level < 0) {
        return;
    }

    const Sink& sink = current_sink();
    if (sink) {
        sink(message, type, level);
    }
}

} // namespace rots::log
