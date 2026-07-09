
#pragma once

#if defined(__linux__) || defined(unix) || defined(__unix) || defined(__unix__) || defined(__FreeBSD__) \
    || defined(__APPLE__)
#define PREDEF_PLATFORM_LINUX
#endif

#if defined(_WIN32)
#define PREDEF_PLATFORM_WINDOWS
#endif

#if defined PREDEF_PLATFORM_LINUX

#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/param.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#endif

#if defined PREDEF_PLATFORM_WINDOWS

// Windows branch scaffold, originally added in Phase 3 Task 1 and wired up by
// Task 4's hand-rolled, platform-gated networking layer (no third-party
// libraries, per the 2026-07-09 plan amendment): comm.cpp's OS-divergent
// socket calls (init_socket, pnew_connection, close_socket,
// process_input/process_output, write_to_descriptor, populate_descriptor_host)
// now route through rots_net (rots_net.h/.cpp) instead of raw BSD-socket calls
// -- WSAStartup/WSACleanup lifetime, closesocket vs close, ioctlsocket vs
// fcntl O_NONBLOCK, send/recv vs read/write, WSA* error-code mapping, and
// getnameinfo() for reverse DNS. The select() pulse loop itself is unchanged
// and portable across both. The POSIX branch is built, tested, and
// network-smoke-verified (see .superpowers/sdd/p3-task-4-report.md); the
// Windows branch below is written but NOT locally compiled or verified (no
// Windows/MSVC environment here) -- windows-msvc stays an allowed-to-fail CI
// preset until its own bring-up task. This header supplies the platform
// includes and the SocketType handle below.
//
// WIN32_LEAN_AND_MEAN keeps <windows.h> (pulled in transitively by
// <winsock2.h>) from dragging in most of the Win32 API surface; winsock2.h
// must be included before any accidental <windows.h> include elsewhere in a
// translation unit, or the legacy winsock.h it declares conflicts with
// winsock2.h's own definitions.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// NOMINMAX (found via the windows-msvc CI bring-up loop, Phase 3 Task 5): without
// this, <windows.h> (pulled in transitively by <winsock2.h> below) defines `max`/
// `min` as function-like macros. Every `std::max(...)`/`std::min(...)` call in the
// codebase (structs.h's get_weight()/get_capped_level()/etc., and many .cpp call
// sites) then has its `max`/`min` token macro-substituted mid-expression, producing
// nonsense like `std::(2147483647)()` -- this was the single root cause behind the
// large majority of the "missing function header" / C2589 "illegal token on right
// side of '::'" / C2059 syntax-error cascade in the first windows-msvc CI log: one
// corrupted parse inside structs.h (included by nearly every TU) derails the parser
// for the rest of that translation unit, manifesting as seemingly unrelated syntax
// errors dozens of lines later in whatever .cpp file happens to include it.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

// MSVC header hygiene (Phase 3 Task 5): the Linux branch above pulls in <time.h>
// (needed everywhere time_t/time()/localtime()/asctime() are used, e.g. db.cpp's
// boot/save-file timestamps and mudlog's log line headers) as part of the platform
// header set; the Windows branch had no equivalent, so every TU that relied on
// platdef.h alone to make those visible got "identifier not found" errors under
// MSVC. <time.h> is the same standard C header on Windows (declared by the
// Universal CRT) as on POSIX -- no translation needed, just the missing include.
#include <time.h>
// signal.h is used directly by product code (signals.cpp's SIGINT/SIGTERM/
// SIGILL/SIGFPE handling, all ISO C signals MSVC's CRT defines) and, like
// <time.h>, is part of the Linux branch's platform include set above but was
// missing here.
#include <signal.h>

// <arpa/telnet.h> stand-in (Phase 3 Task 5, found via the windows-msvc CI bring-up
// loop): protocol.cpp and utility.cpp's telnet negotiation code use the IAC/WILL/
// WONT/DO/DONT/SB/SE command bytes and a handful of TELOPT_* option numbers.
// These come from the POSIX/glibc/BSD header <arpa/telnet.h> (guarded to the Linux
// branch already in both of those .cpp files), which does not exist on Windows --
// MSVC's CRT has no telnet header at all, since Winsock is a raw byte-stream
// sockets API with no protocol-specific knowledge. Every one of these is a fixed,
// RFC 854/855-standardized wire-protocol byte value (not a platform-specific OS
// concept the way sockets/signals are), so hand-declaring them here is exactly as
// portable as the system header would be -- just without relying on Windows to
// ship one. (protocol.h already separately hand-defines the MUD-specific extension
// options -- TELOPT_MSDP/MSSP/MCCP/MSP/MXP/ATCP/CHARSET -- unconditionally on every
// platform; only the base RFC854/855/1091/1073 set below was still missing.)
#if !defined(IAC)
#define IAC 255
#define DONT 254
#define DO 253
#define WONT 252
#define WILL 251
#define SB 250
#define GA 249
#define SE 240
#define TELOPT_ECHO 1
#define TELOPT_NAOCRD 10
#define TELOPT_NAOFFD 13
#define TELOPT_TTYPE 24
#define TELOPT_NAWS 31
#endif

#endif

#if defined PREDEF_PLATFORM_WINDOWS
using SocketType = SOCKET;
#else
using SocketType = int;
#endif

using sh_int = signed short int;
using ush_int = unsigned short int;

using byte = unsigned char;
using sbyte = signed char;
using ubyte = unsigned char;

constexpr auto COPY_COMMAND = "cp";

// Shim replacing the BSD-only sigmask()/sigsetmask() pair that comm.cpp's game_loop
// used to shield its per-pulse timing bookkeeping with: sigsetmask(mask) *set* (not
// additively blocked, unlike sigprocmask(SIG_BLOCK, ...)) the process signal mask to
// a fixed set of signals (SIGUSR1/SIGUSR2/SIGALRM/SIGTERM/SIGURG/SIGXCPU/SIGHUP/
// SIGSEGV/SIGBUS), then sigsetmask(0) reset it back to empty afterward. sigsetmask()
// is absent from strict POSIX and from Windows entirely; sigprocmask(SIG_SETMASK, ...)
// is what Linux/glibc and macOS/Darwin actually implement the historical BSD call in
// terms of, so it reproduces the identical "set, don't just block" semantics. This is
// now belt-and-braces only: the select()/read() call sites it used to guard are
// themselves EINTR-tolerant retry loops, so a missed or racy mask no longer corrupts
// game-loop state the way it would have under the original single-shot error handling.
#if defined PREDEF_PLATFORM_LINUX
inline void platform_block_game_loop_signals()
{
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    sigaddset(&mask, SIGUSR2);
    sigaddset(&mask, SIGALRM);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGURG);
    sigaddset(&mask, SIGXCPU);
    sigaddset(&mask, SIGHUP);
    sigaddset(&mask, SIGSEGV);
    sigaddset(&mask, SIGBUS);
    sigprocmask(SIG_SETMASK, &mask, nullptr);
}

inline void platform_restore_game_loop_signals()
{
    sigset_t empty;
    sigemptyset(&empty);
    sigprocmask(SIG_SETMASK, &empty, nullptr);
}
#elif defined PREDEF_PLATFORM_WINDOWS
// Windows has no process-wide POSIX signal mask, and comm.cpp's raw BSD-socket game
// loop isn't wired up on this platform yet (Phase 3 Task 4). No-op placeholders so the
// call sites need no #ifdef once that task lands.
inline void platform_block_game_loop_signals() { }
inline void platform_restore_game_loop_signals() { }
#endif
