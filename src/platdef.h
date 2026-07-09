
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
