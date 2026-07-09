#include "../comm.h"
#include "../db.h"
#include "../rots_net.h"
#include "../structs.h"

#include <gtest/gtest.h>

// <arpa/inet.h>/<netinet/in.h>/<sys/socket.h>/<sys/time.h>/<unistd.h> are
// POSIX-only; platdef.h (pulled in transitively by comm.h above) already
// gives the Windows branch everything these provide instead (winsock2.h +
// ws2tcpip.h: socket()/bind()/connect()/listen()/getsockname()/send()/recv()/
// setsockopt()/htonl()/htons()/ntohs()/select()/fd_set/timeval, all with
// Winsock-compatible signatures).
#if !defined(_WIN32)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include <cerrno>
#include <cstring>
#include <string>
#include <vector>

// in_port_t/in_addr_t (POSIX <netinet/in.h> typedefs) have no Windows
// equivalent; Winsock's own headers use these underlying types for the same
// purpose (u_short for a network port, u_long for an IPv4 address), so alias
// them here rather than touching every call site below.
#if defined(_WIN32)
using in_port_t = unsigned short;
using in_addr_t = unsigned long;
#endif

SocketType pnew_descriptor(SocketType s);
int process_input(struct descriptor_data* t);

extern descriptor_data* descriptor_list;
extern SocketType maxdesc;
extern int avail_descs;
extern int has_proxy;
extern int nameserver_is_slow;
extern ban_list_element* ban_list;

namespace {

std::vector<char*> build_argv(std::vector<std::string>* storage)
{
    std::vector<char*> argv;
    argv.reserve(storage->size());
    for (std::string& item : *storage)
        argv.push_back(item.data());
    return argv;
}

class AcceptPathTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        saved_descriptor_list_ = descriptor_list;
        saved_maxdesc_ = maxdesc;
        saved_avail_descs_ = avail_descs;
        saved_has_proxy_ = has_proxy;
        saved_nameserver_is_slow_ = nameserver_is_slow;
        saved_ban_list_ = ban_list;

        descriptor_list = nullptr;
        maxdesc = 0;
        avail_descs = 64;
        has_proxy = 0;
        nameserver_is_slow = 1;
        ban_list = nullptr;
    }

    void TearDown() override
    {
        while (descriptor_list)
            close_socket(descriptor_list, FALSE);

        descriptor_list = saved_descriptor_list_;
        maxdesc = saved_maxdesc_;
        avail_descs = saved_avail_descs_;
        has_proxy = saved_has_proxy_;
        nameserver_is_slow = saved_nameserver_is_slow_;
        ban_list = saved_ban_list_;
    }

    SocketType create_listener_socket(in_port_t* port_out)
    {
        SocketType listener = socket(AF_INET, SOCK_STREAM, 0);
        EXPECT_TRUE(rots_net::is_valid_socket(listener)) << rots_net::last_error();
        if (!rots_net::is_valid_socket(listener))
            return rots_net::kInvalidSocket;

        sockaddr_in address {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;

        EXPECT_EQ(bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)), 0) << rots_net::last_error();
        if (getsockname(listener, reinterpret_cast<sockaddr*>(&address), reinterpret_cast<socklen_t*>(&socklen_)) == 0)
            *port_out = ntohs(address.sin_port);

        EXPECT_EQ(listen(listener, 1), 0) << rots_net::last_error();
        return listener;
    }

    SocketType connect_client(in_port_t port)
    {
        SocketType client = socket(AF_INET, SOCK_STREAM, 0);
        EXPECT_TRUE(rots_net::is_valid_socket(client)) << rots_net::last_error();
        if (!rots_net::is_valid_socket(client))
            return rots_net::kInvalidSocket;

        // SO_RCVTIMEO's option-value shape genuinely differs by platform: POSIX
        // takes a struct timeval (seconds/microseconds), Winsock takes a plain
        // DWORD count of milliseconds -- passing a timeval there on Windows
        // would silently misinterpret the byte layout rather than fail to
        // compile, so this needs a real branch, not just a type rename.
#if defined(_WIN32)
        DWORD timeout_ms = 1000;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
#else
        timeval timeout {};
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif

        sockaddr_in address {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(port);

        EXPECT_EQ(connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)), 0) << rots_net::last_error();
        return client;
    }

    // The server may hand the greeting to the kernel as more than one write()
    // (telnet negotiation, then the greeting text, then "Account email: "), and
    // those can arrive at the client as separate TCP segments instead of being
    // coalesced into one. A single recv() only has to return whatever is
    // available so far, so read once (same blocking wait as before), then drain
    // any further segments that show up within the client's existing
    // SO_RCVTIMEO window instead of returning a partial read. This is test-only
    // socket-plumbing robustness — it does not touch any production code path.
    std::string read_client_data(SocketType client)
    {
        std::string result;
        char buffer[2048];

        rots_net::ssize_type bytes_read = recv(client, buffer, sizeof(buffer), 0);
        EXPECT_GT(bytes_read, 0) << rots_net::last_error();
        if (bytes_read <= 0)
            return result;
        result.append(buffer, static_cast<size_t>(bytes_read));

        // Shorten the timeout just for the drain loop: the first recv() above
        // already proved the server is done writing "for now", so any remaining
        // segments are already in flight over loopback and arrive within
        // milliseconds — no need to wait a full second to conclude there's
        // nothing more.
#if defined(_WIN32)
        DWORD drain_timeout_ms = 100;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&drain_timeout_ms), sizeof(drain_timeout_ms));
#else
        timeval drain_timeout {};
        drain_timeout.tv_sec = 0;
        drain_timeout.tv_usec = 100000; // 100ms
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &drain_timeout, sizeof(drain_timeout));
#endif

        for (;;) {
            bytes_read = recv(client, buffer, sizeof(buffer), 0);
            if (bytes_read <= 0)
                break;
            result.append(buffer, static_cast<size_t>(bytes_read));
        }

        return result;
    }

    void expect_no_client_data_yet(SocketType client)
    {
        char buffer[32];
        // errno/EAGAIN/EWOULDBLOCK aren't how a failed Winsock call reports its
        // error (that's WSAGetLastError(), which rots_net::last_error() wraps) --
        // reuse the same last_error()/error_is_would_block() pair comm.cpp's
        // production code already uses after a fallible socket call.
#if defined(_WIN32)
        const rots_net::ssize_type bytes_read = recv(client, buffer, sizeof(buffer), 0);
        EXPECT_EQ(bytes_read, -1);
        const int last_error = rots_net::last_error();
        // The client socket carries an SO_RCVTIMEO, so on Windows a recv() that
        // finds nothing waiting returns WSAETIMEDOUT after the timeout rather
        // than WSAEWOULDBLOCK (which is what a *non-blocking* socket would
        // return immediately). Both mean "the server sent nothing", which is
        // exactly what this check asserts, so accept either (Phase 3 Task 6).
        EXPECT_TRUE(rots_net::error_is_would_block(last_error) || last_error == WSAETIMEDOUT) << last_error;
#else
        errno = 0;
        const ssize_t bytes_read = recv(client, buffer, sizeof(buffer), 0);
        EXPECT_EQ(bytes_read, -1);
        EXPECT_TRUE(errno == EAGAIN || errno == EWOULDBLOCK) << strerror(errno);
#endif
    }

    // Waits (bounded) for the server-side accepted socket to have data queued
    // before the test drives process_input(). On Linux, a client-side send()
    // that has already returned is reliably visible to an immediately-following
    // read() on the loopback peer within the same thread of execution; macOS's
    // network stack can dispatch loopback delivery asynchronously, so a
    // process_input() call issued right after send() can race ahead of the data
    // and see EWOULDBLOCK. select() blocks only until the data actually shows up
    // (or the bound elapses), so this doesn't slow down platforms that don't
    // need it and doesn't change process_input()'s own return-value contract.
    void wait_until_readable(SocketType fd, int timeout_ms)
    {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(fd, &read_fds);

        timeval timeout {};
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_usec = (timeout_ms % 1000) * 1000;

        // nfds (the first argument) is a POSIX-only convention -- Winsock's
        // select() ignores it entirely, since fd_set there is a small
        // explicit array of SOCKET handles rather than a bitmask indexed by
        // fd number -- so the static_cast is only for a clean signed/unsigned
        // conversion, not because Windows uses the value.
        select(static_cast<int>(fd) + 1, &read_fds, nullptr, nullptr, &timeout);
    }

private:
    descriptor_data* saved_descriptor_list_ = nullptr;
    SocketType saved_maxdesc_ = 0;
    int saved_avail_descs_ = 0;
    int saved_has_proxy_ = 0;
    int saved_nameserver_is_slow_ = 0;
    ban_list_element* saved_ban_list_ = nullptr;
    socklen_t socklen_ = sizeof(sockaddr_in);
};

TEST(StartupOptions, UsesDefaultPortAndNoProxyWhenNoArgumentsAreProvided)
{
    StartupOptions options {};
    std::string error_message;
    std::vector<std::string> args = { "ageland" };
    std::vector<char*> argv = build_argv(&args);

    ASSERT_TRUE(parse_startup_options(static_cast<int>(argv.size()), argv.data(), &options, &error_message))
        << error_message;

    EXPECT_EQ(options.port, 1024);
    EXPECT_FALSE(options.has_proxy);
}

TEST(StartupOptions, TreatsDashPArgumentAsPortInsteadOfProxyMode)
{
    StartupOptions options {};
    std::string error_message;
    std::vector<std::string> args = { "ageland", "-p", "3791" };
    std::vector<char*> argv = build_argv(&args);

    ASSERT_TRUE(parse_startup_options(static_cast<int>(argv.size()), argv.data(), &options, &error_message))
        << error_message;

    EXPECT_EQ(options.port, 3791);
    EXPECT_FALSE(options.has_proxy);
}

TEST(StartupOptions, AcceptsExplicitProxyFlagWithPositionalPort)
{
    StartupOptions options {};
    std::string error_message;
    std::vector<std::string> args = { "ageland", "-x", "4001" };
    std::vector<char*> argv = build_argv(&args);

    ASSERT_TRUE(parse_startup_options(static_cast<int>(argv.size()), argv.data(), &options, &error_message))
        << error_message;

    EXPECT_EQ(options.port, 4001);
    EXPECT_TRUE(options.has_proxy);
}

TEST(StartupOptions, AcceptsExplicitProxyFlagWithDashPPort)
{
    StartupOptions options {};
    std::string error_message;
    std::vector<std::string> args = { "ageland", "-x", "-p", "4001" };
    std::vector<char*> argv = build_argv(&args);

    ASSERT_TRUE(parse_startup_options(static_cast<int>(argv.size()), argv.data(), &options, &error_message))
        << error_message;

    EXPECT_EQ(options.port, 4001);
    EXPECT_TRUE(options.has_proxy);
}

TEST(StartupOptions, RejectsUnexpectedExtraArgumentAfterDashPPort)
{
    StartupOptions options {};
    std::string error_message;
    std::vector<std::string> args = { "ageland", "-p", "3791", "4001" };
    std::vector<char*> argv = build_argv(&args);

    EXPECT_FALSE(parse_startup_options(static_cast<int>(argv.size()), argv.data(), &options, &error_message));
    EXPECT_FALSE(error_message.empty());
}

TEST(StartupOptions, RejectsUnexpectedExtraArgumentAfterPositionalPort)
{
    StartupOptions options {};
    std::string error_message;
    std::vector<std::string> args = { "ageland", "3791", "-x" };
    std::vector<char*> argv = build_argv(&args);

    EXPECT_FALSE(parse_startup_options(static_cast<int>(argv.size()), argv.data(), &options, &error_message));
    EXPECT_FALSE(error_message.empty());
}

TEST(StartupOptions, AllowsExplicitProxyFlagAfterDashPPort)
{
    StartupOptions options {};
    std::string error_message;
    std::vector<std::string> args = { "ageland", "-p", "3791", "-x" };
    std::vector<char*> argv = build_argv(&args);

    ASSERT_TRUE(parse_startup_options(static_cast<int>(argv.size()), argv.data(), &options, &error_message))
        << error_message;

    EXPECT_EQ(options.port, 3791);
    EXPECT_TRUE(options.has_proxy);
}

TEST(StartupOptions, AcceptsCompactDashPPortForm)
{
    StartupOptions options {};
    std::string error_message;
    std::vector<std::string> args = { "ageland", "-p3791" };
    std::vector<char*> argv = build_argv(&args);

    ASSERT_TRUE(parse_startup_options(static_cast<int>(argv.size()), argv.data(), &options, &error_message))
        << error_message;

    EXPECT_EQ(options.port, 3791);
    EXPECT_FALSE(options.has_proxy);
}

TEST_F(AcceptPathTest, DirectConnectionsReceiveGreetingWithoutWaitingForInput)
{
    in_port_t port = 0;
    const SocketType listener = create_listener_socket(&port);
    ASSERT_TRUE(rots_net::is_valid_socket(listener));
    const SocketType client = connect_client(port);
    ASSERT_TRUE(rots_net::is_valid_socket(client));

    has_proxy = 0;
    ASSERT_EQ(pnew_descriptor(listener), 1);

    const std::string initial_output = read_client_data(client);
    EXPECT_NE(initial_output.find("RETURN OF THE SHADOW"), std::string::npos);
    EXPECT_NE(initial_output.find("Account email:"), std::string::npos);

    rots_net::close_socket(client);
    rots_net::close_socket(listener);
}

TEST_F(AcceptPathTest, ProxyConnectionsWaitForCompleteSplitHeaderBeforeSendingGreeting)
{
    in_port_t port = 0;
    const SocketType listener = create_listener_socket(&port);
    ASSERT_TRUE(rots_net::is_valid_socket(listener));
    const SocketType client = connect_client(port);
    ASSERT_TRUE(rots_net::is_valid_socket(client));

    has_proxy = 1;
    ASSERT_EQ(pnew_descriptor(listener), 1);

    const in_addr_t proxy_header = htonl(INADDR_LOOPBACK);
    // char* (not unsigned char*): POSIX send()'s buffer parameter is a
    // permissive `const void*`, but Winsock's is `const char*` specifically --
    // an implicit unsigned-char*-to-char* conversion isn't allowed in C++ on
    // either platform, so this needs to already be the right pointer type
    // rather than relying on POSIX's laxer signature.
    const char* header_bytes = reinterpret_cast<const char*>(&proxy_header);
    ASSERT_EQ(send(client, header_bytes, 2, 0), 2);
    wait_until_readable(descriptor_list->descriptor, 500);
    ASSERT_EQ(process_input(descriptor_list), 0);
    expect_no_client_data_yet(client);
    ASSERT_EQ(send(client, header_bytes + 2, 2, 0), 2);
    wait_until_readable(descriptor_list->descriptor, 500);
    ASSERT_EQ(process_input(descriptor_list), 0);

    const std::string initial_output = read_client_data(client);
    EXPECT_NE(initial_output.find("RETURN OF THE SHADOW"), std::string::npos);
    EXPECT_NE(initial_output.find("Account email:"), std::string::npos);

    rots_net::close_socket(client);
    rots_net::close_socket(listener);
}

TEST_F(AcceptPathTest, ProxyConnectionsWaitForHeaderBeforeSendingGreeting)
{
    in_port_t port = 0;
    const SocketType listener = create_listener_socket(&port);
    ASSERT_TRUE(rots_net::is_valid_socket(listener));
    const SocketType client = connect_client(port);
    ASSERT_TRUE(rots_net::is_valid_socket(client));

    has_proxy = 1;
    ASSERT_EQ(pnew_descriptor(listener), 1);

    expect_no_client_data_yet(client);

    const in_addr_t proxy_header = htonl(INADDR_LOOPBACK);
    ASSERT_EQ(send(client, reinterpret_cast<const char*>(&proxy_header), sizeof(proxy_header), 0), static_cast<rots_net::ssize_type>(sizeof(proxy_header)));
    wait_until_readable(descriptor_list->descriptor, 500);
    ASSERT_EQ(process_input(descriptor_list), 0);

    const std::string initial_output = read_client_data(client);
    EXPECT_NE(initial_output.find("RETURN OF THE SHADOW"), std::string::npos);
    EXPECT_NE(initial_output.find("Account email:"), std::string::npos);

    rots_net::close_socket(client);
    rots_net::close_socket(listener);
}

TEST_F(AcceptPathTest, ProxyConnectionsRejectBannedHostsBeforeGreeting)
{
    ban_list_element banned {};
    strncpy(banned.site, "127.0.0.1", BANNED_SITE_LENGTH);
    banned.site[BANNED_SITE_LENGTH] = '\0';
    banned.type = BAN_ALL;
    ban_list = &banned;

    in_port_t port = 0;
    const SocketType listener = create_listener_socket(&port);
    ASSERT_TRUE(rots_net::is_valid_socket(listener));
    const SocketType client = connect_client(port);
    ASSERT_TRUE(rots_net::is_valid_socket(client));

    has_proxy = 1;
    ASSERT_EQ(pnew_descriptor(listener), 1);

    const in_addr_t proxy_header = htonl(INADDR_LOOPBACK);
    ASSERT_EQ(send(client, reinterpret_cast<const char*>(&proxy_header), sizeof(proxy_header), 0), static_cast<rots_net::ssize_type>(sizeof(proxy_header)));
    wait_until_readable(descriptor_list->descriptor, 500);
    EXPECT_EQ(process_input(descriptor_list), -1);
    expect_no_client_data_yet(client);

    rots_net::close_socket(client);
    rots_net::close_socket(listener);
}

} // namespace
