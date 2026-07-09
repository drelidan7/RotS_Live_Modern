
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

// Windows branch skeleton (Phase 3 Task 1). This is a scaffold only: nothing
// in comm.cpp is wired to it yet — the raw BSD-socket calls (init_socket,
// pnew_connection, nonblock, close_socket, process_input/process_output,
// write_to_descriptor, the fd_set selects, gethostbyaddr) still assume POSIX
// and do not compile here. Phase 3 Task 4 (hand-rolled, platform-gated
// networking layer — no third-party libraries, per the 2026-07-09 plan
// amendment) gives comm.cpp a thin shim over the BSD-sockets/Winsock split
// (WSAStartup/WSACleanup lifetime, closesocket vs close, ioctlsocket vs
// fcntl O_NONBLOCK, send/recv vs read/write on sockets, WSA* error-code
// mapping); the select() pulse loop itself is portable across both. This
// header supplies the platform includes and the SocketType handle below.
//
// WIN32_LEAN_AND_MEAN keeps <windows.h> (pulled in transitively by
// <winsock2.h>) from dragging in most of the Win32 API surface; winsock2.h
// must be included before any accidental <windows.h> include elsewhere in a
// translation unit, or the legacy winsock.h it declares conflicts with
// winsock2.h's own definitions.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

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
