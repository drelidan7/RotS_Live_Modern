#include "rots_net.h"

#include <cstdio>
#include <cstdlib>

#if defined(PREDEF_PLATFORM_LINUX)
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace rots_net {

#if defined(PREDEF_PLATFORM_WINDOWS)

void startup()
{
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2, 2), &wsa_data);
}

void shutdown()
{
    WSACleanup();
}

int close_socket(SocketType socket_handle)
{
    return ::closesocket(socket_handle);
}

void set_nonblocking(SocketType socket_handle)
{
    u_long mode = 1;
    if (ioctlsocket(socket_handle, FIONBIO, &mode) != 0) {
        perror("Fatal error executing nonblock (rots_net)");
        exit(1);
    }
}

ssize_type read_socket(SocketType socket_handle, void* buffer, size_t length)
{
    return static_cast<ssize_type>(::recv(socket_handle, static_cast<char*>(buffer), static_cast<int>(length), 0));
}

ssize_type write_socket(SocketType socket_handle, const void* buffer, size_t length)
{
    return static_cast<ssize_type>(::send(socket_handle, static_cast<const char*>(buffer), static_cast<int>(length), 0));
}

int last_error()
{
    return WSAGetLastError();
}

bool error_is_would_block(int error_code)
{
    return error_code == WSAEWOULDBLOCK;
}

bool error_is_interrupted(int error_code)
{
    return error_code == WSAEINTR;
}

#else // PREDEF_PLATFORM_LINUX (POSIX)

void startup() { }

void shutdown() { }

int close_socket(SocketType socket_handle)
{
    return ::close(socket_handle);
}

// fcntl(F_GETFL)/fcntl(F_SETFL) pair, exactly as the historical comm.cpp
// nonblock() helper it replaces did (same flag read-modify-write, same
// fatal-on-failure exit).
void set_nonblocking(SocketType socket_handle)
{
    unsigned long flags = 0;
    flags = fcntl(socket_handle, F_GETFL, flags);
    flags |= O_NONBLOCK;
    if (fcntl(socket_handle, F_SETFL, flags) < 0) {
        perror("Fatal error executing nonblock (rots_net)");
        exit(1);
    }
}

ssize_type read_socket(SocketType socket_handle, void* buffer, size_t length)
{
    return static_cast<ssize_type>(::read(socket_handle, buffer, length));
}

ssize_type write_socket(SocketType socket_handle, const void* buffer, size_t length)
{
    return static_cast<ssize_type>(::write(socket_handle, buffer, length));
}

int last_error()
{
    return errno;
}

bool error_is_would_block(int error_code)
{
    return error_code == EWOULDBLOCK || error_code == EAGAIN;
}

bool error_is_interrupted(int error_code)
{
    return error_code == EINTR;
}

#endif

bool resolve_host_name(uint32_t ipv4_address_network_order, char* host_buffer, size_t host_buffer_length)
{
    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = ipv4_address_network_order;
    address.sin_port = 0;

    const int result = getnameinfo(reinterpret_cast<sockaddr*>(&address), sizeof(address), host_buffer,
        static_cast<socklen_t>(host_buffer_length), nullptr, 0, NI_NAMEREQD);
    return result == 0;
}

} // namespace rots_net
