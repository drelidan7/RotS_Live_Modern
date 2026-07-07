#include "../comm.h"
#include "../db.h"
#include "../structs.h"

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>
#include <vector>

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

    int create_listener_socket(in_port_t* port_out)
    {
        int listener = socket(AF_INET, SOCK_STREAM, 0);
        EXPECT_GE(listener, 0) << strerror(errno);
        if (listener < 0)
            return -1;

        sockaddr_in address {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;

        EXPECT_EQ(bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)), 0) << strerror(errno);
        if (getsockname(listener, reinterpret_cast<sockaddr*>(&address), reinterpret_cast<socklen_t*>(&socklen_)) == 0)
            *port_out = ntohs(address.sin_port);

        EXPECT_EQ(listen(listener, 1), 0) << strerror(errno);
        return listener;
    }

    int connect_client(in_port_t port)
    {
        int client = socket(AF_INET, SOCK_STREAM, 0);
        EXPECT_GE(client, 0) << strerror(errno);
        if (client < 0)
            return -1;

        timeval timeout {};
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        sockaddr_in address {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(port);

        EXPECT_EQ(connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)), 0) << strerror(errno);
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
    std::string read_client_data(int client)
    {
        std::string result;
        char buffer[2048];

        ssize_t bytes_read = recv(client, buffer, sizeof(buffer), 0);
        EXPECT_GT(bytes_read, 0) << strerror(errno);
        if (bytes_read <= 0)
            return result;
        result.append(buffer, static_cast<size_t>(bytes_read));

        // Shorten the timeout just for the drain loop: the first recv() above
        // already proved the server is done writing "for now", so any remaining
        // segments are already in flight over loopback and arrive within
        // milliseconds — no need to wait a full second to conclude there's
        // nothing more.
        timeval drain_timeout {};
        drain_timeout.tv_sec = 0;
        drain_timeout.tv_usec = 100000; // 100ms
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &drain_timeout, sizeof(drain_timeout));

        for (;;) {
            bytes_read = recv(client, buffer, sizeof(buffer), 0);
            if (bytes_read <= 0)
                break;
            result.append(buffer, static_cast<size_t>(bytes_read));
        }

        return result;
    }

    void expect_no_client_data_yet(int client)
    {
        char buffer[32];
        errno = 0;
        const ssize_t bytes_read = recv(client, buffer, sizeof(buffer), 0);
        EXPECT_EQ(bytes_read, -1);
        EXPECT_TRUE(errno == EAGAIN || errno == EWOULDBLOCK) << strerror(errno);
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
    void wait_until_readable(int fd, int timeout_ms)
    {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(fd, &read_fds);

        timeval timeout {};
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_usec = (timeout_ms % 1000) * 1000;

        select(fd + 1, &read_fds, nullptr, nullptr, &timeout);
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
    const int listener = create_listener_socket(&port);
    ASSERT_GE(listener, 0);
    const int client = connect_client(port);
    ASSERT_GE(client, 0);

    has_proxy = 0;
    ASSERT_EQ(pnew_descriptor(listener), 1);

    const std::string initial_output = read_client_data(client);
    EXPECT_NE(initial_output.find("RETURN OF THE SHADOW"), std::string::npos);
    EXPECT_NE(initial_output.find("Account email:"), std::string::npos);

    close(client);
    close(listener);
}

TEST_F(AcceptPathTest, ProxyConnectionsWaitForCompleteSplitHeaderBeforeSendingGreeting)
{
    in_port_t port = 0;
    const int listener = create_listener_socket(&port);
    ASSERT_GE(listener, 0);
    const int client = connect_client(port);
    ASSERT_GE(client, 0);

    has_proxy = 1;
    ASSERT_EQ(pnew_descriptor(listener), 1);

    const in_addr_t proxy_header = htonl(INADDR_LOOPBACK);
    const unsigned char* header_bytes = reinterpret_cast<const unsigned char*>(&proxy_header);
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

    close(client);
    close(listener);
}

TEST_F(AcceptPathTest, ProxyConnectionsWaitForHeaderBeforeSendingGreeting)
{
    in_port_t port = 0;
    const int listener = create_listener_socket(&port);
    ASSERT_GE(listener, 0);
    const int client = connect_client(port);
    ASSERT_GE(client, 0);

    has_proxy = 1;
    ASSERT_EQ(pnew_descriptor(listener), 1);

    expect_no_client_data_yet(client);

    const in_addr_t proxy_header = htonl(INADDR_LOOPBACK);
    ASSERT_EQ(send(client, &proxy_header, sizeof(proxy_header), 0), static_cast<ssize_t>(sizeof(proxy_header)));
    wait_until_readable(descriptor_list->descriptor, 500);
    ASSERT_EQ(process_input(descriptor_list), 0);

    const std::string initial_output = read_client_data(client);
    EXPECT_NE(initial_output.find("RETURN OF THE SHADOW"), std::string::npos);
    EXPECT_NE(initial_output.find("Account email:"), std::string::npos);

    close(client);
    close(listener);
}

TEST_F(AcceptPathTest, ProxyConnectionsRejectBannedHostsBeforeGreeting)
{
    ban_list_element banned {};
    strncpy(banned.site, "127.0.0.1", BANNED_SITE_LENGTH);
    banned.site[BANNED_SITE_LENGTH] = '\0';
    banned.type = BAN_ALL;
    ban_list = &banned;

    in_port_t port = 0;
    const int listener = create_listener_socket(&port);
    ASSERT_GE(listener, 0);
    const int client = connect_client(port);
    ASSERT_GE(client, 0);

    has_proxy = 1;
    ASSERT_EQ(pnew_descriptor(listener), 1);

    const in_addr_t proxy_header = htonl(INADDR_LOOPBACK);
    ASSERT_EQ(send(client, &proxy_header, sizeof(proxy_header), 0), static_cast<ssize_t>(sizeof(proxy_header)));
    wait_until_readable(descriptor_list->descriptor, 500);
    EXPECT_EQ(process_input(descriptor_list), -1);
    expect_no_client_data_yet(client);

    close(client);
    close(listener);
}

} // namespace
