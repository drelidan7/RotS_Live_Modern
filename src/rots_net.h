#pragma once

// rots_net: thin platform shim over the small set of networking primitives that
// diverge between POSIX BSD sockets and Winsock. comm.cpp's single-threaded
// select() game loop is unchanged on every platform; only the OS-divergent calls
// below (close, nonblocking mode, byte I/O, error-code mapping, process lifetime,
// and reverse-DNS) are routed through here. On POSIX every function in this file
// compiles down to exactly the libc call it replaces -- no behavior change versus
// the code it replaces (see .superpowers/sdd/task-4-brief.md, Phase 3 Task 4).
//
// No third-party libraries: this is a hand-rolled replacement for the cancelled
// vendored-Asio design (see docs amending the Phase 3 plan on 2026-07-09).

#include "platdef.h"

#include <cstddef>
#include <cstdint>

namespace rots_net {

#if defined(PREDEF_PLATFORM_WINDOWS)
// SSIZE_T comes from <basetsd.h>, pulled in transitively by <windows.h>, which
// <winsock2.h> (included by platdef.h) pulls in turn.
using ssize_type = SSIZE_T;
#else
// ssize_t comes from <sys/types.h>, already included by platdef.h on POSIX.
using ssize_type = ssize_t;
#endif

// Sentinel for "no socket" / an OS-level invalid handle: -1 on POSIX (matching
// every historical `< 0` check that used to follow socket()/accept());
// INVALID_SOCKET (an unsigned all-ones value) on Windows, where SocketType is
// unsigned and can never be negative.
#if defined(PREDEF_PLATFORM_WINDOWS)
constexpr SocketType kInvalidSocket = INVALID_SOCKET;
#else
constexpr SocketType kInvalidSocket = -1;
#endif

// True when `socket_handle` is a real, non-sentinel handle. Replaces the
// historical `< 0` checks that followed socket()/accept() -- SocketType is
// unsigned on Windows, so a bare `< 0` comparison there would never fire.
constexpr bool is_valid_socket(SocketType socket_handle)
{
    return socket_handle != kInvalidSocket;
}

// One-time process lifetime hooks. WSAStartup(2,2)/WSACleanup on Windows
// (Winsock requires this pairing before/after any socket call); no-ops on
// POSIX, where BSD sockets need no equivalent setup. Call startup() once early
// in main() and shutdown() once at the matching exit path (run_the_game(),
// after the mother/listener socket and all descriptors are closed).
void startup();
void shutdown();

// ::close vs ::closesocket. Route every close of a game socket handle through
// here (not the bare OS close()/closesocket()) so future platforms only need
// this one choke point.
int close_socket(SocketType socket_handle);

// fcntl(..., O_NONBLOCK) vs ioctlsocket(..., FIONBIO, ...). Replaces the
// historical nonblock() helper in comm.cpp. Exits the process on failure,
// exactly as the helper it replaces did -- a listener/descriptor that can't be
// made nonblocking is not a state the single-threaded select() loop can run in.
void set_nonblocking(SocketType socket_handle);

// ::read/::write vs Winsock ::recv/::send (Windows cannot read()/write() a
// SOCKET handle). Signatures mirror POSIX read/write; return value mirrors
// ssize_t (bytes transferred, 0 on orderly EOF, -1 on error with last_error()
// set to the platform's error code).
ssize_type read_socket(SocketType socket_handle, void* buffer, size_t length);
ssize_type write_socket(SocketType socket_handle, const void* buffer, size_t length);

// Last-error retrieval, plus the two predicates comm.cpp tests after every
// fallible socket call: errno/EAGAIN/EWOULDBLOCK/EINTR on POSIX,
// WSAGetLastError()/WSAEWOULDBLOCK/WSAEINTR on Windows.
int last_error();
bool error_is_would_block(int error_code);
bool error_is_interrupted(int error_code);

// gethostbyaddr() replacement: reverse-resolves an IPv4 address (network byte
// order, as stored in sockaddr_in::sin_addr.s_addr) to a hostname via the
// portable getnameinfo() (POSIX + Winsock; no new dependency). Returns false
// (leaving host_buffer untouched) on any resolution failure -- callers fall
// back to numeric IP formatting exactly as they did around the old
// gethostbyaddr() failure path.
bool resolve_host_name(uint32_t ipv4_address_network_order, char* host_buffer, size_t host_buffer_length);

} // namespace rots_net
