#include "../char_utils.h"
#include "../limits.h"
#include "../protocol.h"
#include "../rots_net.h"
#include "rots/core/character.h"
#include "rots/core/room.h"
#include "rots/core/descriptor.h"
#include "rots/core/tables.h"
#include "rots/core/types.h"
#include "../utils.h"
#include "test_char_cleanup.h"

// IAC/DO/WILL/TELOPT_TTYPE: from <arpa/telnet.h> on POSIX; that header does
// not exist on Windows, where platdef.h hand-declares the same fixed RFC
// 854/1091 byte values instead (Phase 3 Task 5's stand-in, used here in
// Task 6). structs.h above already pulls in platdef.h, which on Windows
// brings in winsock2.h/ws2tcpip.h (socket()/bind()/listen()/connect()/
// accept()/getsockname()/htonl()) -- ProtocolDescriptor below needs those
// too, for its portable loopback pair.
#if !defined(_WIN32)
#include <arpa/inet.h>
#include <arpa/telnet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

extern descriptor_data* descriptor_list;
extern int top_of_world;
extern room_data world;
extern weather_data weather_info;
extern const std::string_view pc_races[];
extern const std::string_view pc_star_types[];

void clear_char(struct char_data* ch, int mode);
void msdp_update();
int get_percent_absorb(char_data* character);

namespace protocol_testing {
void write_packet(descriptor_t* descriptor, std::string_view packet);
}

namespace {

class ProtocolDescriptor {
public:
    ProtocolDescriptor()
    {
        descriptor.descriptor = rots_net::kInvalidSocket;
#if defined(_WIN32)
        // AF_UNIX socketpair() has no Windows equivalent at all, so the pair
        // is assembled from a portable AF_INET loopback bind+listen+connect+
        // accept sequence instead (mirrors startup_options_tests.cpp's
        // AcceptPathTest fixture). `descriptor.descriptor` (the accepted
        // server-side peer) is what Protocol* functions write to, simulating
        // the player's connection; `m_capture_socket` (the connecting client
        // end) is what read_output() drains to capture that output.
        SocketType listener = socket(AF_INET, SOCK_STREAM, 0);
        if (!rots_net::is_valid_socket(listener)) {
            ADD_FAILURE() << "Expected a loopback listener socket for protocol output capture.";
            return;
        }

        sockaddr_in address {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0
            || listen(listener, 1) != 0) {
            ADD_FAILURE() << "Expected the loopback listener to bind and listen.";
            rots_net::close_socket(listener);
            return;
        }

        socklen_t address_length = sizeof(address);
        if (getsockname(listener, reinterpret_cast<sockaddr*>(&address), &address_length) != 0) {
            ADD_FAILURE() << "Expected to read back the loopback listener's ephemeral port.";
            rots_net::close_socket(listener);
            return;
        }

        SocketType client = socket(AF_INET, SOCK_STREAM, 0);
        if (!rots_net::is_valid_socket(client)) {
            ADD_FAILURE() << "Expected a loopback client socket for protocol output capture.";
            rots_net::close_socket(listener);
            return;
        }

        if (connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
            ADD_FAILURE() << "Expected the loopback client to connect.";
            rots_net::close_socket(listener);
            rots_net::close_socket(client);
            return;
        }

        SocketType server_peer = accept(listener, nullptr, nullptr);
        rots_net::close_socket(listener);
        if (!rots_net::is_valid_socket(server_peer)) {
            ADD_FAILURE() << "Expected the loopback listener to accept the client's connection.";
            rots_net::close_socket(client);
            return;
        }

        // Disable Nagle on both ends: unlike POSIX's AF_UNIX socketpair (an
        // already-connected, directly-buffered in-kernel channel), a real
        // TCP/IP loopback pair goes through the full network stack, so a
        // single small write can sit uncombined/unflushed long enough that
        // read_output()'s drain loop below sees nothing yet without this.
        const int disable_nagle = 1;
        setsockopt(client, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&disable_nagle), sizeof(disable_nagle));
        setsockopt(server_peer, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&disable_nagle), sizeof(disable_nagle));

        rots_net::set_nonblocking(client);
        m_capture_socket = client;
        descriptor.descriptor = server_peer;
#else
        // POSIX: a direct AF_UNIX socketpair -- an already-connected pair of
        // endpoints sharing one in-kernel buffer (no network-stack framing
        // at all, unlike the Windows branch above), so a write is visible to
        // the peer's recv() effectively synchronously. Unchanged from before
        // this file gained Windows portability.
        SocketType raw_pair[2] = { rots_net::kInvalidSocket, rots_net::kInvalidSocket };
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, raw_pair) != 0) {
            ADD_FAILURE() << "Expected socketpair to create a local protocol output capture.";
            return;
        }
        rots_net::set_nonblocking(raw_pair[1]);
        m_capture_socket = raw_pair[1];
        descriptor.descriptor = raw_pair[0];
#endif
        descriptor.pProtocol = ProtocolCreate();
        descriptor.character = &character;
        SET_BIT(character.specials2.pref, PRF_MSDP);
    }

    ~ProtocolDescriptor()
    {
        if (descriptor.pProtocol != nullptr)
            ProtocolDestroy(descriptor.pProtocol);
        if (rots_net::is_valid_socket(descriptor.descriptor))
            rots_net::close_socket(descriptor.descriptor);
        if (rots_net::is_valid_socket(m_capture_socket))
            rots_net::close_socket(m_capture_socket);
        // initialize_msdp_player() (below) strdup()s a name straight into
        // character.player.name -- not one of the fields ScopedClearCharFields
        // owns (it only mirrors clear_char()'s own allocations), so it must be
        // released here explicitly (Phase 5 T6 leak sweep). RELEASE() is a
        // no-op for the many ProtocolDescriptor instances that never call
        // initialize_msdp_player() at all. character.player.short_descr is
        // strdup()'d directly by one test (MsdpUpdateSkipsInvalidDescriptors
        // WithoutStoppingList) and released here for the same reason.
        RELEASE(character.player.name);
        RELEASE(character.player.short_descr);
    }

    std::string read_output()
    {
        std::string output;
        if (!rots_net::is_valid_socket(m_capture_socket))
            return output;

        char buffer[4096];
        for (;;) {
            const rots_net::ssize_type bytes_read = rots_net::read_socket(m_capture_socket, buffer, sizeof(buffer));
            if (bytes_read > 0) {
                output.append(buffer, static_cast<size_t>(bytes_read));
                continue;
            }
#if defined(_WIN32)
            // The AF_INET loopback path above (Windows only) can still have a
            // just-written byte or two in flight through the network stack;
            // give it one short, bounded window to arrive before concluding
            // there is nothing left, rather than only ever trying once. POSIX
            // keeps the original immediate-return behavior (never needed
            // this with a direct socketpair buffer).
            if (wait_for_more_windows_loopback_data())
                continue;
#endif
            break;
        }
        return output;
    }

    descriptor_data descriptor {};
    char_data character {};
    // Releases character.profs/skills/knowledge (heap-allocated by
    // clear_char(), called either directly or via initialize_msdp_player()
    // below) at scope exit -- safe as a no-op (RELEASE() on a null pointer)
    // for the many ProtocolDescriptor instances that never call clear_char()
    // at all (Phase 5 T6 leak sweep).
    ScopedClearCharFields character_cleanup { character };

private:
#if defined(_WIN32)
    bool wait_for_more_windows_loopback_data()
    {
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(m_capture_socket, &read_set);
        timeval wait_time {};
        wait_time.tv_sec = 0;
        wait_time.tv_usec = 50000; // 50ms
        return select(0, &read_set, nullptr, nullptr, &wait_time) > 0;
    }
#endif

    SocketType m_capture_socket = rots_net::kInvalidSocket;
};

descriptor_data make_protocol_input_descriptor()
{
    descriptor_data descriptor {};
    descriptor.pProtocol = ProtocolCreate();
    return descriptor;
}

class ScopedDescriptorList {
public:
    ScopedDescriptorList()
        : m_previous_descriptor_list(descriptor_list)
    {
        descriptor_list = nullptr;
    }

    ~ScopedDescriptorList()
    {
        descriptor_list = m_previous_descriptor_list;
    }

private:
    descriptor_data* m_previous_descriptor_list;
};

class ScopedMSDPTestRoom {
public:
    ScopedMSDPTestRoom()
    {
        if (room_data::BASE_WORLD == nullptr)
            world.create_bulk(1);

        m_top_of_world = top_of_world;
        top_of_world = 0;
        room_data& room = world[0];
        m_number = room.number;
        m_name = room.name;
        m_description = room.description;
        m_room_flags = room.room_flags;
        m_sector_type = room.sector_type;
        m_light = room.light;

        room.number = 3001;
        room.name = const_cast<char*>("MSDP Test Room");
        room.description = const_cast<char*>("A room used for MSDP update tests.\n\r");
        room.room_flags = INDOORS;
        room.sector_type = SECT_INSIDE;
        room.light = 0;
    }

    ~ScopedMSDPTestRoom()
    {
        top_of_world = m_top_of_world;
        room_data& room = world[0];
        room.number = m_number;
        room.name = m_name;
        room.description = m_description;
        room.room_flags = m_room_flags;
        room.sector_type = m_sector_type;
        room.light = m_light;
    }

private:
    int m_top_of_world = 0;
    int m_number = 0;
    char* m_name = nullptr;
    char* m_description = nullptr;
    long m_room_flags = 0;
    int m_sector_type = 0;
    byte m_light = 0;
};

class ScopedSectorWeather {
public:
    ScopedSectorWeather(int sector, int sky)
        : m_sector(sector)
        , m_sky(weather_info.sky[sector])
    {
        weather_info.sky[sector] = sky;
    }

    ~ScopedSectorWeather()
    {
        weather_info.sky[m_sector] = m_sky;
    }

private:
    int m_sector = 0;
    int m_sky = 0;
};

void initialize_msdp_player(char_data* character, const char* name, int room = 0)
{
    clear_char(character, MOB_VOID);
    character->player.name = strdup(name);
    character->player.level = 10;
    character->player.race = RACE_HUMAN;
    character->in_room = room;
    character->abilities.hit = 150;
    character->tmpabilities.hit = 125;
    character->abilities.mana = 110;
    character->tmpabilities.mana = 90;
    character->abilities.move = 95;
    character->tmpabilities.move = 80;
    character->abilities.str = 16;
    character->tmpabilities.str = 14;
    character->abilities.intel = 15;
    character->tmpabilities.intel = 13;
    character->abilities.wil = 12;
    character->tmpabilities.wil = 11;
    character->abilities.dex = 17;
    character->tmpabilities.dex = 15;
    character->abilities.con = 18;
    character->tmpabilities.con = 16;
    character->abilities.lea = 9;
    character->tmpabilities.lea = 8;
    character->points.exp = xp_to_level(character->player.level) + 1234;
    character->points.gold = 543;
    character->points.spirit = 7;
    character->points.OB = 12;
    character->points.parry = 9;
    character->points.dodge = 6;
    character->points.ENE_regen = 45;
    character->points.willpower = 44;
    character->points.spell_pen = 5;
    character->points.spell_power = 6;
    character->specials2.alignment = 250;
    character->specials2.wimp_level = 33;
    character->specials2.saving_throw = -8;
    character->specials2.perception = 66;
    character->specials.position = POSITION_STANDING;
    character->specials.tactics = TACTICS_NORMAL;
    character->profs->prof_level[PROF_MAGE] = 4;
    character->profs->prof_level[PROF_CLERIC] = 3;
    character->profs->prof_level[PROF_RANGER] = 2;
    character->profs->prof_level[PROF_WARRIOR] = 5;
    SET_BIT(character->specials2.pref, PRF_MSDP);
}

void disable_all_msdp_reports(protocol_t* protocol)
{
    for (int i = eMSDP_NONE + 1; i < eMSDP_MAX; ++i) {
        protocol->pVariables[i]->bReport = false;
        protocol->pVariables[i]->bDirty = false;
    }
}

void enable_msdp_reports(protocol_t* protocol, const std::vector<variable_t>& variables)
{
    disable_all_msdp_reports(protocol);
    for (variable_t variable : variables)
        protocol->pVariables[variable]->bReport = true;
}

void seed_msdp_numbers(descriptor_data* descriptor, const std::vector<variable_t>& variables, int value)
{
    for (variable_t variable : variables)
        MSDPSetNumber(descriptor, variable, value);
}

std::string msdp_packet(const std::string& payload)
{
    std::string packet;
    packet.push_back(static_cast<char>(IAC));
    packet.push_back(static_cast<char>(SB));
    packet.push_back(static_cast<char>(TELOPT_MSDP));
    packet += payload;
    packet.push_back(static_cast<char>(IAC));
    packet.push_back(static_cast<char>(SE));
    return packet;
}

std::string msdp_pair_payload(std::string_view variable, std::string_view value)
{
    std::string payload;
    payload.push_back(static_cast<char>(MSDP_VAR));
    payload += variable;
    payload.push_back(static_cast<char>(MSDP_VAL));
    payload += value;
    return payload;
}

std::string telnet_sequence(unsigned char command, unsigned char option)
{
    std::string packet;
    packet.push_back(static_cast<char>(IAC));
    packet.push_back(static_cast<char>(command));
    packet.push_back(static_cast<char>(option));
    return packet;
}

std::string ttype_send_sequence()
{
    std::string packet;
    packet.push_back(static_cast<char>(IAC));
    packet.push_back(static_cast<char>(SB));
    packet.push_back(static_cast<char>(TELOPT_TTYPE));
    packet.push_back(static_cast<char>(SEND));
    packet.push_back(static_cast<char>(IAC));
    packet.push_back(static_cast<char>(SE));
    return packet;
}

std::string expected_msdp_pair(std::string_view variable, std::string_view value)
{
    return msdp_packet(msdp_pair_payload(variable, value));
}

std::string expected_msdp_array_pair(const std::string& variable, const std::vector<std::string>& values)
{
    std::string payload;
    payload.push_back(static_cast<char>(MSDP_VAR));
    payload += variable;
    payload.push_back(static_cast<char>(MSDP_VAL));
    payload.push_back(static_cast<char>(MSDP_ARRAY_OPEN));
    for (const std::string& value : values) {
        payload.push_back(static_cast<char>(MSDP_VAL));
        payload += value;
    }
    payload.push_back(static_cast<char>(MSDP_ARRAY_CLOSE));
    return msdp_packet(payload);
}

std::string expected_atcp_pair(const std::string& variable, const std::string& value)
{
    std::string packet;
    packet.push_back(static_cast<char>(IAC));
    packet.push_back(static_cast<char>(SB));
    packet.push_back(static_cast<char>(TELOPT_ATCP));
    packet += "MSDP.";
    packet += variable;
    packet.push_back(' ');
    packet += value;
    packet.push_back(static_cast<char>(IAC));
    packet.push_back(static_cast<char>(SE));
    return packet;
}

void feed_msdp_command(descriptor_data* descriptor, const std::string& variable, const std::string& value)
{
    std::string packet = msdp_packet(msdp_pair_payload(variable, value));
    char output[MAX_INPUT_LENGTH] = { '\0' };
    ProtocolInput(descriptor, packet.data(), static_cast<int>(packet.size()), output);
    EXPECT_STREQ(output, "");
}

void feed_protocol_input(descriptor_data* descriptor, const std::string& input, char* output)
{
    ProtocolInput(descriptor, const_cast<char*>(input.data()), static_cast<int>(input.size()), output);
}

bool output_contains_array_value(const std::string& output, const std::string& value)
{
    std::string marker;
    marker.push_back(static_cast<char>(MSDP_VAL));
    marker += value;
    const std::string terminal_marker = marker + static_cast<char>(MSDP_ARRAY_CLOSE);
    return output.find(marker + static_cast<char>(MSDP_VAL)) != std::string::npos
        || output.find(terminal_marker) != std::string::npos;
}

TEST(ProtocolInput, IgnoresLoneIacByteOnFreshDescriptor)
{
    descriptor_data descriptor = make_protocol_input_descriptor();
    char output[MAX_INPUT_LENGTH] = { '\0' };
    char input[] = { static_cast<char>(IAC) };

    ProtocolInput(&descriptor, input, 1, output);

    EXPECT_STREQ(output, "");

    ProtocolDestroy(descriptor.pProtocol);
}

TEST(ProtocolInput, IgnoresTruncatedHandshakeVerbWithoutReadingPastBuffer)
{
    descriptor_data descriptor = make_protocol_input_descriptor();
    char output[MAX_INPUT_LENGTH] = { '\0' };
    char input[] = { static_cast<char>(IAC), static_cast<char>(DO) };

    ProtocolInput(&descriptor, input, 2, output);

    EXPECT_STREQ(output, "");
    EXPECT_EQ(descriptor.pProtocol->PendingInputLength, 2);

    ProtocolDestroy(descriptor.pProtocol);
}

TEST(ProtocolInput, CompletesSplitHandshakeAcrossCallsWithoutLeakingTelnetBytes)
{
    descriptor_data descriptor = make_protocol_input_descriptor();
    char output[MAX_INPUT_LENGTH] = { '\0' };
    char partial[] = { static_cast<char>(IAC), static_cast<char>(WILL) };
    char option[] = { static_cast<char>(TELOPT_TTYPE) };

    ProtocolInput(&descriptor, partial, 2, output);

    EXPECT_STREQ(output, "");
    EXPECT_EQ(descriptor.pProtocol->PendingInputLength, 2);

    output[0] = '\0';
    ProtocolInput(&descriptor, option, 1, output);

    EXPECT_STREQ(output, "");
    EXPECT_EQ(descriptor.pProtocol->PendingInputLength, 0);
    EXPECT_TRUE(descriptor.pProtocol->bTTYPE);

    ProtocolDestroy(descriptor.pProtocol);
}

TEST(ProtocolInput, BuffersTruncatedMxpSequenceWithoutReadingPastBuffer)
{
    descriptor_data descriptor = make_protocol_input_descriptor();
    char output[MAX_INPUT_LENGTH] = { '\0' };
    std::string input = "\x1b[1";

    ProtocolInput(&descriptor, input.data(), static_cast<int>(input.size()), output);

    EXPECT_STREQ(output, "");
    EXPECT_EQ(descriptor.pProtocol->PendingInputLength, static_cast<int>(input.size()));

    ProtocolDestroy(descriptor.pProtocol);
}

TEST(ProtocolInput, BuffersLoneEscapeByteUntilMxpPrefixContinues)
{
    descriptor_data descriptor = make_protocol_input_descriptor();
    char output[MAX_INPUT_LENGTH] = { '\0' };
    std::string first = "\x1b";
    std::string second = "[1";

    ProtocolInput(&descriptor, first.data(), static_cast<int>(first.size()), output);

    EXPECT_STREQ(output, "");
    EXPECT_EQ(descriptor.pProtocol->PendingInputLength, static_cast<int>(first.size()));

    output[0] = '\0';
    ProtocolInput(&descriptor, second.data(), static_cast<int>(second.size()), output);

    EXPECT_STREQ(output, "");
    EXPECT_EQ(descriptor.pProtocol->PendingInputLength, 3);

    ProtocolDestroy(descriptor.pProtocol);
}

TEST(MSDPProtocol, SanitizesStringValuesForJsonLikeConsumers)
{
    EXPECT_EQ(MSDPSanitizeValue("plain"), "plain");
    EXPECT_EQ(MSDPSanitizeValue("quote\"slash\\\n\r\t\001"),
        "quote\\\"slash\\\\\\n\\r\\t\\u0001");
    EXPECT_EQ(MSDPSanitizeValue(std::string_view()), "");
}

TEST(MSDPProtocol, SanitizeValueAcceptsANonTerminatedBoundedSlice)
{
    const char storage[] = { 'x', 'A', '\n', 'B', 'y' };

    EXPECT_EQ(MSDPSanitizeValue(std::string_view(storage + 1, 3)), "A\\nB");
}

TEST(MSDPProtocol, SanitizeValueTruncatesSemanticTextAtEmbeddedNull)
{
    const char storage[] = { 'A', '\0', 'B' };

    EXPECT_EQ(MSDPSanitizeValue(std::string_view(storage, sizeof(storage))), "A");
}

TEST(MSDPProtocol, SanitizeValueAcceptsADirectUnterminatedRawArray)
{
    const char value[] = { 'A', '\n', 'B' };

    EXPECT_EQ(MSDPSanitizeValue(value), "A\\nB");
}

TEST(MSDPProtocol, ProtocolOutputUsesLengthAsProcessingLimitForTerminatedText)
{
    ProtocolDescriptor context;
    const char storage[] = { 'A', 'B', 'C', '\0' };
    int output_length = 2;

    const char* output = ProtocolOutput(&context.descriptor, storage, &output_length);

    ASSERT_NE(output, nullptr);
    EXPECT_EQ(std::string_view(output, static_cast<std::size_t>(output_length)), "AB");
    EXPECT_EQ(output_length, 2);
}

TEST(MSDPProtocol, ProtocolOutputStopsAtNullBeforeProcessingLimit)
{
    ProtocolDescriptor context;
    const char storage[] = { 'A', '\0', 'B', '\0' };
    int output_length = 3;

    const char* output = ProtocolOutput(&context.descriptor, storage, &output_length);

    ASSERT_NE(output, nullptr);
    EXPECT_EQ(std::string_view(output, static_cast<std::size_t>(output_length)), "A");
    EXPECT_EQ(output_length, 1);
}

TEST(MSDPProtocol, ProtocolCreateInitializesExpectedDefaults)
{
    ProtocolDescriptor context;
    protocol_t* protocol = context.descriptor.pProtocol;

    EXPECT_TRUE(protocol->bMSDP);
    EXPECT_FALSE(protocol->bNegotiated);
    EXPECT_FALSE(protocol->bRenegotiate);
    EXPECT_FALSE(protocol->bTTYPE);
    EXPECT_FALSE(protocol->bECHO);
    EXPECT_FALSE(protocol->bNAWS);
    EXPECT_FALSE(protocol->bCHARSET);
    EXPECT_FALSE(protocol->bMSSP);
    EXPECT_FALSE(protocol->bATCP);
    EXPECT_FALSE(protocol->bMSP);
    EXPECT_FALSE(protocol->bMXP);
    EXPECT_FALSE(protocol->bMCCP);
    EXPECT_EQ(protocol->b256Support, eUNKNOWN);
    EXPECT_EQ(protocol->ScreenWidth, 0);
    EXPECT_EQ(protocol->ScreenHeight, 0);
    EXPECT_STREQ(protocol->pMXPVersion, "Unknown");
    for (int i = eNEGOTIATED_TTYPE; i < eNEGOTIATED_MAX; ++i)
        EXPECT_FALSE(protocol->Negotiated[i]) << "negotiated enum " << i;
    EXPECT_EQ(protocol->PendingInputLength, 0);
    EXPECT_EQ(protocol->IacInputLength, 0);

    for (int i = eMSDP_NONE + 1; i < eMSDP_MAX; ++i) {
        EXPECT_TRUE(protocol->pVariables[i]->bReport) << "MSDP enum " << i;
        EXPECT_FALSE(protocol->pVariables[i]->bDirty) << "MSDP enum " << i;
    }

    EXPECT_EQ(protocol->pVariables[eMSDP_SNIPPET_VERSION]->ValueInt, 8);
    EXPECT_STREQ(protocol->pVariables[eMSDP_CLIENT_ID]->pValueString, "Unknown");
    EXPECT_STREQ(protocol->pVariables[eMSDP_CLIENT_VERSION]->pValueString, "Unknown");
    EXPECT_STREQ(protocol->pVariables[eMSDP_PLUGIN_ID]->pValueString, "Unknown");
    EXPECT_EQ(protocol->pVariables[eMSDP_ANSI_COLORS]->ValueInt, 1);
    EXPECT_EQ(protocol->pVariables[eMSDP_XTERM_256_COLORS]->ValueInt, 0);
    EXPECT_EQ(protocol->pVariables[eMSDP_UTF_8]->ValueInt, 0);
    EXPECT_EQ(protocol->pVariables[eMSDP_SOUND]->ValueInt, 0);
    EXPECT_EQ(protocol->pVariables[eMSDP_MXP]->ValueInt, 0);
    EXPECT_STREQ(protocol->pVariables[eMSDP_BUTTON_1]->pValueString, "\005\002Help\002help\006");
    EXPECT_STREQ(protocol->pVariables[eMSDP_GAUGE_1]->pValueString,
        "\005\002Health\002red\002HEALTH\002HEALTH_MAX\006");
}

TEST(MSDPProtocol, TTypeNegotiationRequestsFullProtocolSet)
{
    ProtocolDescriptor context;
    char output[MAX_INPUT_LENGTH] = { '\0' };

    feed_protocol_input(&context.descriptor, telnet_sequence(WILL, TELOPT_TTYPE), output);

    std::string negotiation = context.read_output();
    EXPECT_STREQ(output, "");
    EXPECT_TRUE(context.descriptor.pProtocol->bNegotiated);
    EXPECT_TRUE(context.descriptor.pProtocol->bRenegotiate);
    EXPECT_TRUE(context.descriptor.pProtocol->bTTYPE);
    EXPECT_TRUE(context.descriptor.pProtocol->Negotiated[eNEGOTIATED_TTYPE]);
    EXPECT_EQ(negotiation,
        telnet_sequence(DO, TELOPT_TTYPE)
            + ttype_send_sequence()
            + telnet_sequence(DO, TELOPT_NAWS)
            + telnet_sequence(DO, TELOPT_CHARSET)
            + telnet_sequence(WILL, TELOPT_MSDP)
            + telnet_sequence(WILL, TELOPT_MSSP)
            + telnet_sequence(DO, TELOPT_ATCP)
            + telnet_sequence(WILL, TELOPT_MSP)
            + telnet_sequence(DO, TELOPT_MXP));
}

TEST(MSDPProtocol, NegotiatesAndRejectsMSDPSupport)
{
    ProtocolDescriptor context;
    char output[MAX_INPUT_LENGTH] = { '\0' };

    feed_protocol_input(&context.descriptor, telnet_sequence(DO, TELOPT_MSDP), output);

    EXPECT_STREQ(output, "");
    EXPECT_TRUE(context.descriptor.pProtocol->Negotiated[eNEGOTIATED_MSDP]);
    EXPECT_TRUE(context.descriptor.pProtocol->bMSDP);
    EXPECT_EQ(context.read_output(), telnet_sequence(WILL, TELOPT_MSDP));

    feed_protocol_input(&context.descriptor, telnet_sequence(DONT, TELOPT_MSDP), output);

    EXPECT_STREQ(output, "");
    EXPECT_FALSE(context.descriptor.pProtocol->Negotiated[eNEGOTIATED_MSDP]);
    EXPECT_FALSE(context.descriptor.pProtocol->bMSDP);
    EXPECT_EQ(context.read_output(), telnet_sequence(WONT, TELOPT_MSDP));

    feed_protocol_input(&context.descriptor, telnet_sequence(DO, TELOPT_MSDP), output);

    EXPECT_STREQ(output, "");
    EXPECT_TRUE(context.descriptor.pProtocol->Negotiated[eNEGOTIATED_MSDP]);
    EXPECT_TRUE(context.descriptor.pProtocol->bMSDP);
    EXPECT_EQ(context.read_output(),
        telnet_sequence(WILL, TELOPT_MSDP) + expected_msdp_pair("SERVER_ID", MUD_NAME));
}

TEST(MSDPProtocol, SetNumberMarksDirtyOnlyWhenValueChanges)
{
    ProtocolDescriptor context;

    EXPECT_FALSE(context.descriptor.pProtocol->pVariables[eMSDP_HEALTH]->bDirty);

    MSDPSetNumber(&context.descriptor, eMSDP_HEALTH, 42);
    EXPECT_TRUE(context.descriptor.pProtocol->pVariables[eMSDP_HEALTH]->bDirty);
    EXPECT_EQ(context.descriptor.pProtocol->pVariables[eMSDP_HEALTH]->ValueInt, 42);

    context.descriptor.pProtocol->pVariables[eMSDP_HEALTH]->bDirty = false;
    MSDPSetNumber(&context.descriptor, eMSDP_HEALTH, 42);
    EXPECT_FALSE(context.descriptor.pProtocol->pVariables[eMSDP_HEALTH]->bDirty);
}

TEST(MSDPProtocol, SetStringSanitizesAndMarksDirtyOnlyWhenValueChanges)
{
    ProtocolDescriptor context;

    MSDPSetString(&context.descriptor, eMSDP_CHARACTER_NAME, "A \"B\"\n");

    EXPECT_TRUE(context.descriptor.pProtocol->pVariables[eMSDP_CHARACTER_NAME]->bDirty);
    EXPECT_STREQ(context.descriptor.pProtocol->pVariables[eMSDP_CHARACTER_NAME]->pValueString,
        "A \\\"B\\\"\\n");

    context.descriptor.pProtocol->pVariables[eMSDP_CHARACTER_NAME]->bDirty = false;
    MSDPSetString(&context.descriptor, eMSDP_CHARACTER_NAME, "A \"B\"\n");
    EXPECT_FALSE(context.descriptor.pProtocol->pVariables[eMSDP_CHARACTER_NAME]->bDirty);
}

TEST(MSDPProtocol, TextHelpersExposeBoundedSignatures)
{
    static_assert(std::is_same_v<decltype(static_cast<std::string (*)(std::string_view)>(
                                     &MSDPSanitizeValue)),
        std::string (*)(std::string_view)>);
    static_assert(std::is_same_v<decltype(static_cast<void (*)(descriptor_t*, variable_t,
                                     std::string_view)>(&MSDPSetString)),
        void (*)(descriptor_t*, variable_t, std::string_view)>);
    static_assert(std::is_same_v<decltype(static_cast<void (*)(descriptor_t*, std::string_view,
                                     std::string_view)>(&MSDPSendPair)),
        void (*)(descriptor_t*, std::string_view, std::string_view)>);
    static_assert(std::is_same_v<decltype(static_cast<void (*)(descriptor_t*, std::string_view,
                                     std::string_view)>(&MSDPSendList)),
        void (*)(descriptor_t*, std::string_view, std::string_view)>);
    static_assert(std::is_same_v<decltype(static_cast<void (*)(descriptor_t*, std::string_view)>(
                                     &MXPSendTag)),
        void (*)(descriptor_t*, std::string_view)>);
}

TEST(MSDPProtocol, NullableCompatibilityOverloadsAcceptPointerArguments)
{
    static_assert(requires(descriptor_t* descriptor, variable_t variable, const char* text) {
        CopyoverSet(descriptor, text);
        MSDPSanitizeValue(text);
        MSDPSetString(descriptor, variable, text);
        MSDPSendPair(descriptor, text, text);
        MSDPSendList(descriptor, text, text);
        MSDPSetTable(descriptor, variable, text);
        MSDPSendTable(descriptor, variable, text);
        MSDPSetArray(descriptor, variable, text);
        MXPSendTag(descriptor, text);
        SoundSend(descriptor, text);
        ColourRGB(descriptor, text);
    });
}

TEST(MSDPProtocol, NullSetInputsLeaveProtocolStateUnchanged)
{
    ProtocolDescriptor context;
    protocol_t* protocol = context.descriptor.pProtocol;
    const char* null_text = nullptr;
    std::string expected_table(1, static_cast<char>(MSDP_TABLE_OPEN));
    expected_table += "existing table";
    expected_table += static_cast<char>(MSDP_TABLE_CLOSE);
    std::string expected_array(1, static_cast<char>(MSDP_ARRAY_OPEN));
    expected_array += "existing array";
    expected_array += static_cast<char>(MSDP_ARRAY_CLOSE);

    MSDPSetString(&context.descriptor, eMSDP_CHARACTER_NAME, "existing string");
    MSDPSetTable(&context.descriptor, eMSDP_ROOM, "existing table");
    MSDPSetArray(&context.descriptor, eMSDP_ROOM_EXITS, "existing array");
    protocol->pVariables[eMSDP_CHARACTER_NAME]->bDirty = false;
    protocol->pVariables[eMSDP_ROOM]->bDirty = false;
    protocol->pVariables[eMSDP_ROOM_EXITS]->bDirty = false;

    MSDPSetString(&context.descriptor, eMSDP_CHARACTER_NAME, nullptr);
    MSDPSetTable(&context.descriptor, eMSDP_ROOM, nullptr);
    MSDPSendTable(&context.descriptor, eMSDP_ROOM, nullptr);
    MSDPSetArray(&context.descriptor, eMSDP_ROOM_EXITS, nullptr);
    MSDPSetString(&context.descriptor, eMSDP_CHARACTER_NAME, null_text);
    MSDPSetTable(&context.descriptor, eMSDP_ROOM, null_text);
    MSDPSendTable(&context.descriptor, eMSDP_ROOM, null_text);
    MSDPSetArray(&context.descriptor, eMSDP_ROOM_EXITS, null_text);

    EXPECT_STREQ(protocol->pVariables[eMSDP_CHARACTER_NAME]->pValueString, "existing string");
    EXPECT_EQ(protocol->pVariables[eMSDP_ROOM]->pValueString, expected_table);
    EXPECT_EQ(protocol->pVariables[eMSDP_ROOM_EXITS]->pValueString, expected_array);
    EXPECT_FALSE(protocol->pVariables[eMSDP_CHARACTER_NAME]->bDirty);
    EXPECT_FALSE(protocol->pVariables[eMSDP_ROOM]->bDirty);
    EXPECT_FALSE(protocol->pVariables[eMSDP_ROOM_EXITS]->bDirty);
    EXPECT_EQ(context.read_output(), "");
}

TEST(MSDPProtocol, NullOutputInputsRemainNoOps)
{
    ProtocolDescriptor context;
    protocol_t* protocol = context.descriptor.pProtocol;
    const char* null_text = nullptr;
    protocol->pVariables[eMSDP_MXP]->ValueInt = 1;
    protocol->pVariables[eMSDP_SOUND]->ValueInt = 1;

    MSDPSendPair(&context.descriptor, nullptr, "value");
    MSDPSendPair(&context.descriptor, "VARIABLE", nullptr);
    MSDPSendList(&context.descriptor, nullptr, "value");
    MSDPSendList(&context.descriptor, "VARIABLE", nullptr);
    MXPSendTag(&context.descriptor, nullptr);
    SoundSend(&context.descriptor, nullptr);
    MSDPSendPair(&context.descriptor, null_text, "value");
    MSDPSendPair(&context.descriptor, "VARIABLE", null_text);
    MSDPSendList(&context.descriptor, null_text, "value");
    MSDPSendList(&context.descriptor, "VARIABLE", null_text);
    MXPSendTag(&context.descriptor, null_text);
    SoundSend(&context.descriptor, null_text);

    EXPECT_EQ(context.read_output(), "");
}

TEST(MSDPProtocol, NullCopyoverInputLeavesSettingsUnchanged)
{
    ProtocolDescriptor context;
    protocol_t* protocol = context.descriptor.pProtocol;
    const char* null_text = nullptr;
    protocol->ScreenWidth = 80;
    protocol->ScreenHeight = 24;

    CopyoverSet(&context.descriptor, nullptr);
    CopyoverSet(&context.descriptor, null_text);

    EXPECT_EQ(protocol->ScreenWidth, 80);
    EXPECT_EQ(protocol->ScreenHeight, 24);
}

TEST(MSDPProtocol, NullSanitizeAndColourInputsPreserveLegacyResults)
{
    ProtocolDescriptor context;
    const char* null_text = nullptr;

    EXPECT_EQ(MSDPSanitizeValue(nullptr), "");
    EXPECT_STREQ(ColourRGB(&context.descriptor, nullptr), "\033[0;00m");
    EXPECT_EQ(MSDPSanitizeValue(null_text), "");
    EXPECT_STREQ(ColourRGB(&context.descriptor, null_text), "\033[0;00m");
}

TEST(MSDPProtocol, SetStringCopiesAndSanitizesABoundedValue)
{
    ProtocolDescriptor context;
    std::string caller_storage { 'A', '\n', 'B', 'x' };

    MSDPSetString(&context.descriptor, eMSDP_CHARACTER_NAME,
        std::string_view(caller_storage.data(), 3));
    caller_storage.assign(caller_storage.size(), 'X');

    EXPECT_STREQ(context.descriptor.pProtocol->pVariables[eMSDP_CHARACTER_NAME]->pValueString,
        "A\\nB");
}

TEST(MSDPProtocol, SetStringTruncatesSemanticTextAtEmbeddedNull)
{
    ProtocolDescriptor context;
    const char value_storage[] = { 'A', '\0', 'B', 'x' };

    MSDPSetString(&context.descriptor, eMSDP_CHARACTER_NAME,
        std::string_view(value_storage, 3));

    EXPECT_STREQ(context.descriptor.pProtocol->pVariables[eMSDP_CHARACTER_NAME]->pValueString,
        "A");
}

TEST(MSDPProtocol, SendPairTruncatesSemanticTextAtEmbeddedNull)
{
    ProtocolDescriptor context;
    const char variable_storage[] = { 'x', 'N', 'A', 'M', 'E', 'y' };
    const char value_storage[] = { 'A', '\0', 'B', 'x' };
    const std::string_view variable(variable_storage + 1, 4);
    const std::string_view value(value_storage, 3);

    MSDPSendPair(&context.descriptor, variable, value);

    EXPECT_EQ(context.read_output(), expected_msdp_pair("NAME", "A"));
}

TEST(MSDPProtocol, SendPairAcceptsNonTerminatedBoundedText)
{
    ProtocolDescriptor context;
    const char variable_storage[] = { 'x', 'N', 'A', 'M', 'E', 'y' };
    const char value_storage[] = { 'x', 'A', 'B', 'y' };

    MSDPSendPair(&context.descriptor, std::string_view(variable_storage + 1, 4),
        std::string_view(value_storage + 1, 2));

    EXPECT_EQ(context.read_output(), expected_msdp_pair("NAME", "AB"));
}

TEST(MSDPProtocol, SendPairAcceptsDirectUnterminatedRawArrayCombinations)
{
    ProtocolDescriptor context;
    const char variable[] = { 'N', 'A', 'M', 'E' };
    const char value[] = { 'A', 'B' };
    const std::string_view variable_view(variable, sizeof(variable));
    const std::string_view value_view(value, sizeof(value));
    const char* terminated_variable = "NAME";
    const char* terminated_value = "AB";

    MSDPSendPair(&context.descriptor, variable, value);
    MSDPSendPair(&context.descriptor, variable, value_view);
    MSDPSendPair(&context.descriptor, variable_view, value);
    MSDPSendPair(&context.descriptor, variable, terminated_value);
    MSDPSendPair(&context.descriptor, terminated_variable, value);

    const std::string expected_output = expected_msdp_pair("NAME", "AB");
    EXPECT_EQ(context.read_output(), expected_output + expected_output + expected_output
            + expected_output + expected_output);
}

TEST(MSDPProtocol, SendListTruncatesSemanticTextAtEmbeddedNull)
{
    ProtocolDescriptor context;
    const char variable_storage[] = { 'x', 'L', 'I', 'S', 'T', 'y' };
    const char value_storage[] = { 'A', ' ', 'B', '\0', 'C', 'x' };
    const std::string_view variable(variable_storage + 1, 4);
    const std::string_view value(value_storage, 5);

    MSDPSendList(&context.descriptor, variable, value);

    EXPECT_EQ(context.read_output(), expected_msdp_array_pair("LIST", { "A", "B" }));
}

TEST(MSDPProtocol, SendListAcceptsNonTerminatedBoundedText)
{
    ProtocolDescriptor context;
    const char variable_storage[] = { 'x', 'L', 'I', 'S', 'T', 'y' };
    const char value_storage[] = { 'x', 'A', ' ', 'B', 'y' };

    MSDPSendList(&context.descriptor, std::string_view(variable_storage + 1, 4),
        std::string_view(value_storage + 1, 3));

    EXPECT_EQ(context.read_output(), expected_msdp_array_pair("LIST", { "A", "B" }));
}

TEST(MSDPProtocol, SendListAcceptsDirectUnterminatedRawArrayCombinations)
{
    ProtocolDescriptor context;
    const char variable[] = { 'L', 'I', 'S', 'T' };
    const char value[] = { 'A', ' ', 'B' };
    const std::string_view variable_view(variable, sizeof(variable));
    const std::string_view value_view(value, sizeof(value));
    const char* terminated_variable = "LIST";
    const char* terminated_value = "A B";

    MSDPSendList(&context.descriptor, variable, value);
    MSDPSendList(&context.descriptor, variable, value_view);
    MSDPSendList(&context.descriptor, variable_view, value);
    MSDPSendList(&context.descriptor, variable, terminated_value);
    MSDPSendList(&context.descriptor, terminated_variable, value);

    const std::string expected_output = expected_msdp_array_pair("LIST", { "A", "B" });
    EXPECT_EQ(context.read_output(), expected_output + expected_output + expected_output
            + expected_output + expected_output);
}

TEST(MSDPProtocol, MxpSendTagTruncatesSemanticTextAtEmbeddedNull)
{
    ProtocolDescriptor context;
    context.descriptor.pProtocol->pVariables[eMSDP_MXP]->ValueInt = 1;
    const char tag_storage[] = { 'x', '<', 'A', '\0', 'B', '>', 'y' };

    MXPSendTag(&context.descriptor, std::string_view(tag_storage + 1, 5));

    EXPECT_EQ(context.read_output(), "\033[1z<A\033[7z\r\n");
}

TEST(MSDPProtocol, MxpSendTagAcceptsNonTerminatedBoundedText)
{
    ProtocolDescriptor context;
    context.descriptor.pProtocol->pVariables[eMSDP_MXP]->ValueInt = 1;
    const char tag_storage[] = { 'x', '<', 'A', '>', 'y' };

    MXPSendTag(&context.descriptor, std::string_view(tag_storage + 1, 3));

    EXPECT_EQ(context.read_output(), "\033[1z<A>\033[7z\r\n");
}

TEST(MSDPProtocol, InternalPacketWriterPreservesEmbeddedNullBytes)
{
    ProtocolDescriptor context;
    const std::string packet("A\0B", 3);

    protocol_testing::write_packet(&context.descriptor, packet);

    EXPECT_EQ(context.read_output(), packet);
}

TEST(MSDPProtocol, SendsStringAndNumberVariablesAsMSDPPackets)
{
    ProtocolDescriptor context;

    MSDPSetString(&context.descriptor, eMSDP_CHARACTER_NAME, "Aragorn");
    MSDPSend(&context.descriptor, eMSDP_CHARACTER_NAME);
    EXPECT_EQ(context.read_output(), expected_msdp_pair("CHARACTER_NAME", "Aragorn"));

    MSDPSetNumber(&context.descriptor, eMSDP_HEALTH, 87);
    MSDPSend(&context.descriptor, eMSDP_HEALTH);
    EXPECT_EQ(context.read_output(), expected_msdp_pair("HEALTH", "87"));
}

TEST(MSDPProtocol, DoesNotSendMSDPWhenPlayerPreferenceIsDisabled)
{
    ProtocolDescriptor context;
    REMOVE_BIT(context.character.specials2.pref, PRF_MSDP);

    MSDPSetNumber(&context.descriptor, eMSDP_HEALTH, 87);
    MSDPSend(&context.descriptor, eMSDP_HEALTH);

    EXPECT_EQ(context.read_output(), "");
}

TEST(MSDPProtocol, SendHelpersIgnoreMissingDescriptorProtocolOrCharacter)
{
    MSDPSend(nullptr, eMSDP_HEALTH);
    MSDPFlush(nullptr, eMSDP_HEALTH);
    MSDPSendPair(nullptr, "HEALTH", "1");
    MSDPSendList(nullptr, "COMMANDS", "LIST REPORT");

    descriptor_data descriptor_without_protocol {};
    MSDPSend(&descriptor_without_protocol, eMSDP_HEALTH);
    MSDPFlush(&descriptor_without_protocol, eMSDP_HEALTH);
    MSDPSendPair(&descriptor_without_protocol, "HEALTH", "1");
    MSDPSendList(&descriptor_without_protocol, "COMMANDS", "LIST REPORT");

    ProtocolDescriptor context;
    context.descriptor.character = nullptr;
    MSDPSetNumber(&context.descriptor, eMSDP_HEALTH, 87);
    MSDPSend(&context.descriptor, eMSDP_HEALTH);
    feed_msdp_command(&context.descriptor, "SEND", "HEALTH");
    EXPECT_EQ(context.read_output(), "");
}

TEST(MSDPProtocol, SendsATCPFallbackWhenMSDPIsUnavailable)
{
    ProtocolDescriptor context;
    context.descriptor.pProtocol->bMSDP = false;
    context.descriptor.pProtocol->bATCP = true;

    MSDPSetNumber(&context.descriptor, eMSDP_HEALTH, 87);
    MSDPSend(&context.descriptor, eMSDP_HEALTH);

    EXPECT_EQ(context.read_output(), expected_atcp_pair("HEALTH", "87"));
}

TEST(MSDPProtocol, SendsListsTablesAndArraysWithMSDPMarkers)
{
    ProtocolDescriptor context;

    MSDPSendList(&context.descriptor, "COMMANDS", "LIST REPORT RESET");
    EXPECT_EQ(context.read_output(),
        expected_msdp_array_pair("COMMANDS", { "LIST", "REPORT", "RESET" }));

    std::string table_payload;
    table_payload.push_back(static_cast<char>(MSDP_VAR));
    table_payload += "VNUM";
    table_payload.push_back(static_cast<char>(MSDP_VAL));
    table_payload += "3001";
    MSDPSetTable(&context.descriptor, eMSDP_ROOM, table_payload);
    MSDPSend(&context.descriptor, eMSDP_ROOM);

    std::string expected_table;
    expected_table.push_back(static_cast<char>(MSDP_TABLE_OPEN));
    expected_table += table_payload;
    expected_table.push_back(static_cast<char>(MSDP_TABLE_CLOSE));
    EXPECT_EQ(context.read_output(), expected_msdp_pair("ROOM", expected_table));

    std::string array_payload;
    array_payload.push_back(static_cast<char>(MSDP_VAL));
    array_payload += "n";
    array_payload.push_back(static_cast<char>(MSDP_VAL));
    array_payload += "e";
    MSDPSetArray(&context.descriptor, eMSDP_ROOM_EXITS, array_payload);
    MSDPSend(&context.descriptor, eMSDP_ROOM_EXITS);

    std::string expected_array;
    expected_array.push_back(static_cast<char>(MSDP_ARRAY_OPEN));
    expected_array += array_payload;
    expected_array.push_back(static_cast<char>(MSDP_ARRAY_CLOSE));
    EXPECT_EQ(context.read_output(), expected_msdp_pair("ROOM_EXITS", expected_array));
}

TEST(MSDPProtocol, SendPairAndListRejectOversizedPayloads)
{
    ProtocolDescriptor context;
    std::string huge_value(MAX_VARIABLE_LENGTH, 'X');

    MSDPSendPair(&context.descriptor, "HEALTH", huge_value);
    MSDPSendList(&context.descriptor, "COMMANDS", huge_value);

    EXPECT_EQ(context.read_output(), "");
}

TEST(MSDPProtocol, PublicSetHelpersIgnoreInvalidVariables)
{
    ProtocolDescriptor context;

    EXPECT_FALSE(MSDPIsValidVariable(eMSDP_NONE));
    EXPECT_FALSE(MSDPIsValidVariable(eMSDP_MAX));

    MSDPSetNumber(&context.descriptor, eMSDP_MAX, 99);
    MSDPSetNumber(&context.descriptor, eMSDP_NONE, 99);
    MSDPSetString(&context.descriptor, eMSDP_MAX, "ignored");
    MSDPSetString(&context.descriptor, eMSDP_NONE, "ignored");
    MSDPSetTable(&context.descriptor, eMSDP_MAX, "ignored");
    MSDPSetTable(&context.descriptor, eMSDP_NONE, "ignored");
    MSDPSendTable(&context.descriptor, eMSDP_MAX, "ignored");
    MSDPSendTable(&context.descriptor, eMSDP_NONE, "ignored");
    MSDPSetArray(&context.descriptor, eMSDP_MAX, "ignored");
    MSDPSetArray(&context.descriptor, eMSDP_NONE, "ignored");

    EXPECT_EQ(context.read_output(), "");
    EXPECT_STREQ(context.descriptor.pProtocol->pVariables[eMSDP_CHARACTER_NAME]->pValueString, "");
    EXPECT_FALSE(context.descriptor.pProtocol->pVariables[eMSDP_CHARACTER_NAME]->bDirty);
}

TEST(MSDPProtocol, UpdateFlushesOnlyDirtyReportedVariables)
{
    ProtocolDescriptor context;

    for (int i = eMSDP_NONE + 1; i < eMSDP_MAX; ++i)
        context.descriptor.pProtocol->pVariables[i]->bReport = false;

    context.descriptor.pProtocol->pVariables[eMSDP_HEALTH]->bReport = true;
    context.descriptor.pProtocol->pVariables[eMSDP_MANA]->bReport = true;
    MSDPSetNumber(&context.descriptor, eMSDP_HEALTH, 45);
    MSDPSetNumber(&context.descriptor, eMSDP_MANA, 12);
    MSDPSetNumber(&context.descriptor, eMSDP_MOVEMENT, 77);

    MSDPUpdate(&context.descriptor);

    EXPECT_EQ(context.read_output(),
        expected_msdp_pair("HEALTH", "45") + expected_msdp_pair("MANA", "12"));
    EXPECT_FALSE(context.descriptor.pProtocol->pVariables[eMSDP_HEALTH]->bDirty);
    EXPECT_FALSE(context.descriptor.pProtocol->pVariables[eMSDP_MANA]->bDirty);
    EXPECT_TRUE(context.descriptor.pProtocol->pVariables[eMSDP_MOVEMENT]->bDirty);
}

TEST(MSDPProtocol, FlushSendsOnlyTheRequestedDirtyReportedVariable)
{
    ProtocolDescriptor context;

    MSDPSetNumber(&context.descriptor, eMSDP_HEALTH, 45);
    MSDPSetNumber(&context.descriptor, eMSDP_MANA, 12);

    MSDPFlush(&context.descriptor, eMSDP_HEALTH);

    EXPECT_EQ(context.read_output(), expected_msdp_pair("HEALTH", "45"));
    EXPECT_FALSE(context.descriptor.pProtocol->pVariables[eMSDP_HEALTH]->bDirty);
    EXPECT_TRUE(context.descriptor.pProtocol->pVariables[eMSDP_MANA]->bDirty);
}

TEST(MSDPProtocol, ParsesReportUnreportResetAndSendCommands)
{
    ProtocolDescriptor context;

    context.descriptor.pProtocol->pVariables[eMSDP_HEALTH]->bReport = false;
    feed_msdp_command(&context.descriptor, "REPORT", "HEALTH");
    EXPECT_TRUE(context.descriptor.pProtocol->pVariables[eMSDP_HEALTH]->bReport);
    EXPECT_TRUE(context.descriptor.pProtocol->pVariables[eMSDP_HEALTH]->bDirty);

    context.read_output();
    MSDPSetNumber(&context.descriptor, eMSDP_HEALTH, 75);
    feed_msdp_command(&context.descriptor, "SEND", "HEALTH");
    EXPECT_EQ(context.read_output(), expected_msdp_pair("HEALTH", "75"));

    feed_msdp_command(&context.descriptor, "UNREPORT", "HEALTH");
    EXPECT_FALSE(context.descriptor.pProtocol->pVariables[eMSDP_HEALTH]->bReport);
    EXPECT_FALSE(context.descriptor.pProtocol->pVariables[eMSDP_HEALTH]->bDirty);

    const char* reset_targets[] = { "REPORTED_VARIABLES", "REPORTABLE_VARIABLES", "VARIABLES" };
    for (const char* reset_target : reset_targets) {
        context.descriptor.pProtocol->pVariables[eMSDP_HEALTH]->bReport = true;
        context.descriptor.pProtocol->pVariables[eMSDP_MANA]->bReport = true;
        context.descriptor.pProtocol->pVariables[eMSDP_HEALTH]->bDirty = true;
        context.descriptor.pProtocol->pVariables[eMSDP_MANA]->bDirty = true;
        feed_msdp_command(&context.descriptor, "RESET", reset_target);
        EXPECT_FALSE(context.descriptor.pProtocol->pVariables[eMSDP_HEALTH]->bReport) << reset_target;
        EXPECT_FALSE(context.descriptor.pProtocol->pVariables[eMSDP_MANA]->bReport) << reset_target;
        EXPECT_FALSE(context.descriptor.pProtocol->pVariables[eMSDP_HEALTH]->bDirty) << reset_target;
        EXPECT_FALSE(context.descriptor.pProtocol->pVariables[eMSDP_MANA]->bDirty) << reset_target;
    }
}

TEST(MSDPProtocol, ListsCommandsAndReportedVariables)
{
    ProtocolDescriptor context;

    feed_msdp_command(&context.descriptor, "LIST", "COMMANDS");
    EXPECT_EQ(context.read_output(),
        expected_msdp_array_pair("COMMANDS", { "LIST", "REPORT", "RESET", "SEND", "UNREPORT" }));

    for (int i = eMSDP_NONE + 1; i < eMSDP_MAX; ++i)
        context.descriptor.pProtocol->pVariables[i]->bReport = false;
    context.descriptor.pProtocol->pVariables[eMSDP_HEALTH]->bReport = true;
    context.descriptor.pProtocol->pVariables[eMSDP_MANA]->bReport = true;

    feed_msdp_command(&context.descriptor, "LIST", "REPORTED_VARIABLES");
    EXPECT_EQ(context.read_output(),
        expected_msdp_array_pair("REPORTED_VARIABLES", { "HEALTH", "MANA" }));
}

TEST(MSDPProtocol, ListsConfigurableAndGuiVariables)
{
    ProtocolDescriptor context;

    feed_msdp_command(&context.descriptor, "LIST", "LISTS");
    EXPECT_EQ(context.read_output(),
        expected_msdp_array_pair("LISTS",
            { "COMMANDS", "LISTS", "CONFIGURABLE_VARIABLES", "REPORTABLE_VARIABLES",
                "REPORTED_VARIABLES", "SENDABLE_VARIABLES", "GUI_VARIABLES" }));

    feed_msdp_command(&context.descriptor, "LIST", "SENDABLE_VARIABLES");
    std::string sendable_output = context.read_output();
    EXPECT_TRUE(output_contains_array_value(sendable_output, "CHARACTER_NAME")) << sendable_output;
    EXPECT_TRUE(output_contains_array_value(sendable_output, "HEALTH")) << sendable_output;
    EXPECT_FALSE(output_contains_array_value(sendable_output, "BUTTON_1")) << sendable_output;

    feed_msdp_command(&context.descriptor, "LIST", "REPORTABLE_VARIABLES");
    std::string reportable_output = context.read_output();
    EXPECT_TRUE(output_contains_array_value(reportable_output, "CHARACTER_NAME")) << reportable_output;
    EXPECT_TRUE(output_contains_array_value(reportable_output, "HEALTH")) << reportable_output;
    EXPECT_FALSE(output_contains_array_value(reportable_output, "BUTTON_1")) << reportable_output;

    feed_msdp_command(&context.descriptor, "LIST", "CONFIGURABLE_VARIABLES");
    EXPECT_EQ(context.read_output(),
        expected_msdp_array_pair("CONFIGURABLE_VARIABLES",
            { "CLIENT_ID", "CLIENT_VERSION", "PLUGIN_ID", "ANSI_COLORS",
                "XTERM_256_COLORS", "UTF_8", "SOUND", "MXP" }));

    feed_msdp_command(&context.descriptor, "LIST", "GUI_VARIABLES");
    EXPECT_EQ(context.read_output(),
        expected_msdp_array_pair("GUI_VARIABLES",
            { "BUTTON_1", "BUTTON_2", "BUTTON_3", "BUTTON_4", "BUTTON_5",
                "GAUGE_1", "GAUGE_2", "GAUGE_3", "GAUGE_4", "GAUGE_5" }));
}

TEST(MSDPProtocol, ConfigurableVariablesValidateRangeLengthAndWriteOnceRules)
{
    ProtocolDescriptor context;

    feed_msdp_command(&context.descriptor, "XTERM_256_COLORS", "1");
    EXPECT_EQ(context.descriptor.pProtocol->pVariables[eMSDP_XTERM_256_COLORS]->ValueInt, 1);

    feed_msdp_command(&context.descriptor, "XTERM_256_COLORS", "abc");
    EXPECT_EQ(context.descriptor.pProtocol->pVariables[eMSDP_XTERM_256_COLORS]->ValueInt, 1);

    feed_msdp_command(&context.descriptor, "XTERM_256_COLORS", "-1");
    EXPECT_EQ(context.descriptor.pProtocol->pVariables[eMSDP_XTERM_256_COLORS]->ValueInt, 1);

    feed_msdp_command(&context.descriptor, "XTERM_256_COLORS", "");
    EXPECT_EQ(context.descriptor.pProtocol->pVariables[eMSDP_XTERM_256_COLORS]->ValueInt, 1);

    feed_msdp_command(&context.descriptor, "XTERM_256_COLORS", "2");
    EXPECT_EQ(context.descriptor.pProtocol->pVariables[eMSDP_XTERM_256_COLORS]->ValueInt, 1);

    std::string hostile_plugin_id;
    hostile_plugin_id.push_back(static_cast<char>(0xff));
    hostile_plugin_id += "\007RotS";
    feed_msdp_command(&context.descriptor, "PLUGIN_ID", hostile_plugin_id);
    EXPECT_STREQ(context.descriptor.pProtocol->pVariables[eMSDP_PLUGIN_ID]->pValueString, "RotS");
    feed_msdp_command(&context.descriptor, "PLUGIN_ID", "\007");
    EXPECT_STREQ(context.descriptor.pProtocol->pVariables[eMSDP_PLUGIN_ID]->pValueString, "RotS");

    feed_msdp_command(&context.descriptor, "PLUGIN_ID", std::string(48, 'A'));
    EXPECT_STREQ(context.descriptor.pProtocol->pVariables[eMSDP_PLUGIN_ID]->pValueString,
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");

    feed_msdp_command(&context.descriptor, "CLIENT_ID", "Mudlet");
    EXPECT_STREQ(context.descriptor.pProtocol->pVariables[eMSDP_CLIENT_ID]->pValueString, "Mudlet");
    feed_msdp_command(&context.descriptor, "CLIENT_ID", "TinTin");
    EXPECT_STREQ(context.descriptor.pProtocol->pVariables[eMSDP_CLIENT_ID]->pValueString, "Mudlet");
}

TEST(MSDPProtocol, UnknownCommandsAndVariablesDoNotChangeKnownState)
{
    ProtocolDescriptor context;

    context.descriptor.pProtocol->pVariables[eMSDP_HEALTH]->bReport = false;
    context.descriptor.pProtocol->pVariables[eMSDP_HEALTH]->bDirty = false;
    feed_msdp_command(&context.descriptor, "REPORT", "NOT_A_VARIABLE");
    feed_msdp_command(&context.descriptor, "NOT_A_COMMAND", "HEALTH");

    EXPECT_FALSE(context.descriptor.pProtocol->pVariables[eMSDP_HEALTH]->bReport);
    EXPECT_FALSE(context.descriptor.pProtocol->pVariables[eMSDP_HEALTH]->bDirty);
    EXPECT_EQ(context.read_output(), "");
}

TEST(MSDPProtocol, IgnoresMalformedAndOversizedSubnegotiationInput)
{
    ProtocolDescriptor context;
    char output[MAX_INPUT_LENGTH] = { '\0' };

    context.descriptor.pProtocol->pVariables[eMSDP_HEALTH]->bReport = false;
    feed_msdp_command(&context.descriptor, "REPORT", "");
    EXPECT_FALSE(context.descriptor.pProtocol->pVariables[eMSDP_HEALTH]->bReport);

    std::string oversized_value = "HEALTH";
    oversized_value.append(120, 'X');
    feed_msdp_command(&context.descriptor, "REPORT", oversized_value);
    EXPECT_FALSE(context.descriptor.pProtocol->pVariables[eMSDP_HEALTH]->bReport);

    std::string truncated = msdp_packet(msdp_pair_payload("REPORT", "HEALTH"));
    truncated.resize(truncated.size() - 2);
    feed_protocol_input(&context.descriptor, truncated, output);

    EXPECT_STREQ(output, "");
    EXPECT_TRUE(context.descriptor.pProtocol->bIACMode);
    EXPECT_FALSE(context.descriptor.pProtocol->pVariables[eMSDP_HEALTH]->bReport);
    EXPECT_EQ(context.read_output(), "");

    std::string terminator;
    terminator.push_back(static_cast<char>(IAC));
    terminator.push_back(static_cast<char>(SE));
    feed_protocol_input(&context.descriptor, terminator, output);

    EXPECT_STREQ(output, "");
    EXPECT_FALSE(context.descriptor.pProtocol->bIACMode);
    EXPECT_TRUE(context.descriptor.pProtocol->pVariables[eMSDP_HEALTH]->bReport);
}

TEST(MSDPProtocol, IgnoresMalformedMarkerOrderWithoutUsingStalePairs)
{
    ProtocolDescriptor context;

    context.descriptor.pProtocol->pVariables[eMSDP_HEALTH]->bReport = false;

    std::string value_without_variable;
    value_without_variable.push_back(static_cast<char>(MSDP_VAL));
    value_without_variable += "HEALTH";
    char output[MAX_INPUT_LENGTH] = { '\0' };
    feed_protocol_input(&context.descriptor, msdp_packet(value_without_variable), output);
    EXPECT_FALSE(context.descriptor.pProtocol->pVariables[eMSDP_HEALTH]->bReport);

    output[0] = '\0';
    std::string repeated_variable;
    repeated_variable.push_back(static_cast<char>(MSDP_VAR));
    repeated_variable += "REPORT";
    repeated_variable.push_back(static_cast<char>(MSDP_VAR));
    repeated_variable += "UNREPORT";
    repeated_variable.push_back(static_cast<char>(MSDP_VAL));
    repeated_variable += "HEALTH";
    feed_protocol_input(&context.descriptor, msdp_packet(repeated_variable), output);

    EXPECT_STREQ(output, "");
    EXPECT_FALSE(context.descriptor.pProtocol->pVariables[eMSDP_HEALTH]->bReport);
    EXPECT_FALSE(context.descriptor.pProtocol->pVariables[eMSDP_HEALTH]->bDirty);
    EXPECT_EQ(context.read_output(), "");
}

TEST(MSDPProtocol, CompletesSplitMSDPSubnegotiationAcrossProtocolInputCalls)
{
    ProtocolDescriptor context;
    char output[MAX_INPUT_LENGTH] = { '\0' };
    std::string packet = msdp_packet(msdp_pair_payload("REPORT", "HEALTH"));

    context.descriptor.pProtocol->pVariables[eMSDP_HEALTH]->bReport = false;
    feed_protocol_input(&context.descriptor, packet.substr(0, 5), output);

    EXPECT_STREQ(output, "");
    EXPECT_TRUE(context.descriptor.pProtocol->bIACMode);
    EXPECT_FALSE(context.descriptor.pProtocol->pVariables[eMSDP_HEALTH]->bReport);

    feed_protocol_input(&context.descriptor, packet.substr(5), output);

    EXPECT_STREQ(output, "");
    EXPECT_FALSE(context.descriptor.pProtocol->bIACMode);
    EXPECT_TRUE(context.descriptor.pProtocol->pVariables[eMSDP_HEALTH]->bReport);
    EXPECT_TRUE(context.descriptor.pProtocol->pVariables[eMSDP_HEALTH]->bDirty);
}

TEST(MSDPProtocol, CompletesMSDPSubnegotiationSplitBetweenIacAndSe)
{
    ProtocolDescriptor context;
    char output[MAX_INPUT_LENGTH] = { '\0' };
    std::string packet = msdp_packet(msdp_pair_payload("REPORT", "HEALTH"));

    context.descriptor.pProtocol->pVariables[eMSDP_HEALTH]->bReport = false;
    feed_protocol_input(&context.descriptor, packet.substr(0, packet.size() - 1), output);

    EXPECT_STREQ(output, "");
    EXPECT_TRUE(context.descriptor.pProtocol->bIACMode);
    EXPECT_EQ(context.descriptor.pProtocol->PendingInputLength, 1);
    EXPECT_FALSE(context.descriptor.pProtocol->pVariables[eMSDP_HEALTH]->bReport);

    output[0] = '\0';
    feed_protocol_input(&context.descriptor, packet.substr(packet.size() - 1), output);

    EXPECT_STREQ(output, "");
    EXPECT_FALSE(context.descriptor.pProtocol->bIACMode);
    EXPECT_EQ(context.descriptor.pProtocol->PendingInputLength, 0);
    EXPECT_TRUE(context.descriptor.pProtocol->pVariables[eMSDP_HEALTH]->bReport);
    EXPECT_TRUE(context.descriptor.pProtocol->pVariables[eMSDP_HEALTH]->bDirty);
}

TEST(MSDPProtocol, RecoversAfterOversizedUnterminatedSubnegotiation)
{
    ProtocolDescriptor context;
    char output[MAX_INPUT_LENGTH] = { '\0' };
    std::string oversized;
    oversized.push_back(static_cast<char>(IAC));
    oversized.push_back(static_cast<char>(SB));
    oversized.push_back(static_cast<char>(TELOPT_MSDP));
    oversized.append(MAX_PROTOCOL_BUFFER, 'X');

    feed_protocol_input(&context.descriptor, oversized, output);

    EXPECT_STREQ(output, "");
    EXPECT_FALSE(context.descriptor.pProtocol->bIACMode);
    EXPECT_EQ(context.descriptor.pProtocol->IacInputLength, 0);

    std::string terminator;
    terminator.push_back(static_cast<char>(IAC));
    terminator.push_back(static_cast<char>(SE));
    output[0] = '\0';
    feed_protocol_input(&context.descriptor, terminator, output);

    EXPECT_STREQ(output, "");
    EXPECT_FALSE(context.descriptor.pProtocol->bIACMode);
    EXPECT_EQ(context.descriptor.pProtocol->IacInputLength, 0);

    output[0] = '\0';
    feed_protocol_input(&context.descriptor, "look", output);

    EXPECT_STREQ(output, "look");
}

TEST(MSDPProtocol, MsdpUpdateSkipsInvalidDescriptorsWithoutStoppingList)
{
    ScopedDescriptorList descriptor_list_scope;
    ScopedMSDPTestRoom room_scope;

    descriptor_data no_character {};
    ProtocolDescriptor npc_context;
    ProtocolDescriptor nowhere_context;
    ProtocolDescriptor valid_context;
    descriptor_data missing_protocol {};
    char_data missing_protocol_character {};

    clear_char(&missing_protocol_character, MOB_VOID);
    // Releases missing_protocol_character.profs/skills/knowledge (clear_char() heap
    // allocations) at scope exit (Phase 5 T6 leak sweep).
    ScopedClearCharFields missing_protocol_character_cleanup { missing_protocol_character };
    missing_protocol_character.player.name = strdup("NoProtocol");
    missing_protocol_character.in_room = 0;
    missing_protocol.character = &missing_protocol_character;

    initialize_msdp_player(&npc_context.character, "IgnoredNpc");
    SET_BIT(npc_context.character.specials2.act, MOB_ISNPC);
    npc_context.character.player.short_descr = strdup("ignored npc");

    initialize_msdp_player(&nowhere_context.character, "Nowhere");
    nowhere_context.character.in_room = NOWHERE;

    initialize_msdp_player(&valid_context.character, "Updated");
    enable_msdp_reports(valid_context.descriptor.pProtocol, { eMSDP_CHARACTER_NAME, eMSDP_HEALTH });

    no_character.next = &npc_context.descriptor;
    npc_context.descriptor.next = &missing_protocol;
    missing_protocol.next = &nowhere_context.descriptor;
    nowhere_context.descriptor.next = &valid_context.descriptor;
    descriptor_list = &no_character;

    msdp_update();

    EXPECT_STREQ(valid_context.descriptor.pProtocol->pVariables[eMSDP_CHARACTER_NAME]->pValueString,
        "Updated");
    EXPECT_EQ(valid_context.descriptor.pProtocol->pVariables[eMSDP_HEALTH]->ValueInt, 125);
    EXPECT_EQ(valid_context.read_output(),
        expected_msdp_pair("CHARACTER_NAME", "Updated") + expected_msdp_pair("HEALTH", "125"));
    EXPECT_EQ(npc_context.read_output(), "");
    EXPECT_EQ(nowhere_context.read_output(), "");
    EXPECT_FALSE(valid_context.descriptor.pProtocol->pVariables[eMSDP_CHARACTER_NAME]->bDirty);
    EXPECT_FALSE(valid_context.descriptor.pProtocol->pVariables[eMSDP_HEALTH]->bDirty);

    // Releases missing_protocol_character.player.name (strdup() above) at
    // scope exit (Phase 5 T6 leak sweep) -- not one of ScopedClearCharFields's
    // fields.
    RELEASE(missing_protocol_character.player.name);
}

TEST(MSDPProtocol, MsdpUpdateSkipsOutOfRangeRoomsWithoutStoppingList)
{
    ScopedDescriptorList descriptor_list_scope;
    ScopedMSDPTestRoom room_scope;
    ProtocolDescriptor negative_room_context;
    ProtocolDescriptor high_room_context;
    ProtocolDescriptor valid_context;

    initialize_msdp_player(&negative_room_context.character, "NegativeRoom", -2);
    initialize_msdp_player(&high_room_context.character, "HighRoom", top_of_world + 1);
    initialize_msdp_player(&valid_context.character, "StillUpdated");

    enable_msdp_reports(negative_room_context.descriptor.pProtocol,
        { eMSDP_CHARACTER_NAME, eMSDP_ROOM_NAME, eMDSP_WEATHER });
    enable_msdp_reports(high_room_context.descriptor.pProtocol,
        { eMSDP_CHARACTER_NAME, eMSDP_ROOM_NAME, eMDSP_WEATHER });
    enable_msdp_reports(valid_context.descriptor.pProtocol,
        { eMSDP_CHARACTER_NAME, eMSDP_ROOM_NAME, eMSDP_ROOM_VNUM });

    MSDPSetString(&negative_room_context.descriptor, eMSDP_ROOM_NAME, "stale negative room");
    MSDPSetString(&negative_room_context.descriptor, eMDSP_WEATHER, "stale negative weather");
    MSDPSetString(&high_room_context.descriptor, eMSDP_ROOM_NAME, "stale high room");
    MSDPSetString(&high_room_context.descriptor, eMDSP_WEATHER, "stale high weather");

    negative_room_context.descriptor.next = &high_room_context.descriptor;
    high_room_context.descriptor.next = &valid_context.descriptor;
    descriptor_list = &negative_room_context.descriptor;

    msdp_update();

    EXPECT_EQ(negative_room_context.read_output(), "");
    EXPECT_EQ(high_room_context.read_output(), "");
    EXPECT_STREQ(negative_room_context.descriptor.pProtocol->pVariables[eMSDP_ROOM_NAME]->pValueString,
        "stale negative room");
    EXPECT_STREQ(negative_room_context.descriptor.pProtocol->pVariables[eMDSP_WEATHER]->pValueString,
        "stale negative weather");
    EXPECT_STREQ(negative_room_context.descriptor.pProtocol->pVariables[eMSDP_CHARACTER_NAME]->pValueString,
        "");
    EXPECT_STREQ(high_room_context.descriptor.pProtocol->pVariables[eMSDP_ROOM_NAME]->pValueString,
        "stale high room");
    EXPECT_STREQ(high_room_context.descriptor.pProtocol->pVariables[eMDSP_WEATHER]->pValueString,
        "stale high weather");
    EXPECT_STREQ(high_room_context.descriptor.pProtocol->pVariables[eMSDP_CHARACTER_NAME]->pValueString,
        "");
    EXPECT_EQ(valid_context.read_output(),
        expected_msdp_pair("CHARACTER_NAME", "StillUpdated")
            + expected_msdp_pair("ROOM_NAME", "MSDP Test Room")
            + expected_msdp_pair("ROOM_VNUM", "3001"));
}

TEST(MSDPProtocol, MsdpUpdateEmitsMinimalPlayerState)
{
    ScopedDescriptorList descriptor_list_scope;
    ScopedMSDPTestRoom room_scope;
    ProtocolDescriptor context;

    initialize_msdp_player(&context.character, "Aragorn");
    enable_msdp_reports(context.descriptor.pProtocol,
        { eMSDP_CHARACTER_NAME, eMSDP_LEVEL, eMSDP_HEALTH, eMSDP_HEALTH_MAX,
            eMSDP_ROOM_NAME, eMSDP_ROOM_VNUM, eMSDP_RACE, eMDSP_TACTIC, eMSDP_SPIRIT });
    descriptor_list = &context.descriptor;

    msdp_update();

    protocol_t* protocol = context.descriptor.pProtocol;
    EXPECT_STREQ(protocol->pVariables[eMSDP_CHARACTER_NAME]->pValueString, "Aragorn");
    EXPECT_EQ(protocol->pVariables[eMSDP_LEVEL]->ValueInt, 10);
    EXPECT_EQ(protocol->pVariables[eMSDP_HEALTH]->ValueInt, 125);
    EXPECT_EQ(protocol->pVariables[eMSDP_HEALTH_MAX]->ValueInt, 150);
    EXPECT_STREQ(protocol->pVariables[eMSDP_ROOM_NAME]->pValueString, "MSDP Test Room");
    EXPECT_EQ(protocol->pVariables[eMSDP_ROOM_VNUM]->ValueInt, 3001);
    EXPECT_EQ(protocol->pVariables[eMSDP_RACE]->pValueString, pc_races[RACE_HUMAN]);
    EXPECT_STREQ(protocol->pVariables[eMDSP_TACTIC]->pValueString, "normal");
    EXPECT_EQ(protocol->pVariables[eMSDP_SPIRIT]->ValueInt, 7);
    EXPECT_EQ(context.read_output(),
        expected_msdp_pair("CHARACTER_NAME", "Aragorn") + expected_msdp_pair("HEALTH", "125")
            + expected_msdp_pair("HEALTH_MAX", "150") + expected_msdp_pair("LEVEL", "10")
            + expected_msdp_pair("RACE", pc_races[RACE_HUMAN])
            + expected_msdp_pair("SPIRIT", "7") + expected_msdp_pair("TACTIC", "normal")
            + expected_msdp_pair("ROOM_NAME", "MSDP Test Room")
            + expected_msdp_pair("ROOM_VNUM", "3001"));
    EXPECT_FALSE(protocol->pVariables[eMSDP_CHARACTER_NAME]->bDirty);
    EXPECT_FALSE(protocol->pVariables[eMSDP_HEALTH]->bDirty);
    EXPECT_FALSE(protocol->pVariables[eMSDP_ROOM_NAME]->bDirty);
}

TEST(MSDPProtocol, MsdpUpdateEmitsBroadCharacterStats)
{
    ScopedDescriptorList descriptor_list_scope;
    ScopedMSDPTestRoom room_scope;
    ProtocolDescriptor context;
    const std::vector<variable_t> variables = {
        eMSDP_ALIGNMENT, eMSDP_EXPERIENCE, eMSDP_EXPERIENCE_MAX, eMSDP_MANA,
        eMSDP_MANA_MAX, eMSDP_WIMPY, eMSDP_MONEY, eMSDP_MOVEMENT, eMSDP_MOVEMENT_MAX,
        eMSDP_STR, eMSDP_INT, eMSDP_WILL, eMSDP_DEX, eMSDP_CON, eMSDP_LEA,
        eMSDP_STR_PERM, eMSDP_INT_PERM, eMSDP_WIL_PERM, eMSDP_DEX_PERM,
        eMSDP_CON_PERM, eMSDP_LEA_PERM, eMDSP_SPELL_SAVE, eMDSP_SPELL_PEN,
        eMDSP_SPELL_POWER, eMDSP_ARMOUR_ABS, eMDSP_OFFENSIVE_BONUS, eMDSP_PARRY,
        eMDSP_DODGE, eMDSP_ATTACK_SPEED, eMDSP_PERCEPTION, eMDSP_WILLPOWER,
        eMDSP_SKILL_ENCUMBRANCE, eMDSP_MOVEMENT_ENCUMBRANCE, eMDSP_HEALTH_REGENERATION,
        eMDSP_STAMINA_REGENERATION, eMDSP_MOVEMENT_REGENERATION
    };

    initialize_msdp_player(&context.character, "Aragorn");
    enable_msdp_reports(context.descriptor.pProtocol, variables);
    seed_msdp_numbers(&context.descriptor, variables, -999);
    descriptor_list = &context.descriptor;

    msdp_update();

    protocol_t* protocol = context.descriptor.pProtocol;
    const int next_level_xp = xp_to_level(GET_LEVEL(&context.character) + 1);
    const int current_level_xp = xp_to_level(GET_LEVEL(&context.character));
    EXPECT_EQ(protocol->pVariables[eMSDP_ALIGNMENT]->ValueInt, 250);
    EXPECT_EQ(protocol->pVariables[eMSDP_EXPERIENCE_MAX]->ValueInt,
        next_level_xp - current_level_xp);
    EXPECT_EQ(protocol->pVariables[eMSDP_EXPERIENCE]->ValueInt,
        next_level_xp - GET_EXP(&context.character));
    EXPECT_EQ(protocol->pVariables[eMSDP_MANA]->ValueInt, 90);
    EXPECT_EQ(protocol->pVariables[eMSDP_MANA_MAX]->ValueInt, 110);
    EXPECT_EQ(protocol->pVariables[eMSDP_MOVEMENT]->ValueInt, 80);
    EXPECT_EQ(protocol->pVariables[eMSDP_MOVEMENT_MAX]->ValueInt, 95);
    EXPECT_EQ(protocol->pVariables[eMSDP_MONEY]->ValueInt, 543);
    EXPECT_EQ(protocol->pVariables[eMSDP_STR]->ValueInt, 14);
    EXPECT_EQ(protocol->pVariables[eMSDP_INT]->ValueInt, 13);
    EXPECT_EQ(protocol->pVariables[eMSDP_WILL]->ValueInt, 11);
    EXPECT_EQ(protocol->pVariables[eMSDP_DEX]->ValueInt, 15);
    EXPECT_EQ(protocol->pVariables[eMSDP_CON]->ValueInt, 16);
    EXPECT_EQ(protocol->pVariables[eMSDP_LEA]->ValueInt, 8);
    EXPECT_EQ(protocol->pVariables[eMSDP_STR_PERM]->ValueInt, 16);
    EXPECT_EQ(protocol->pVariables[eMSDP_INT_PERM]->ValueInt, 15);
    EXPECT_EQ(protocol->pVariables[eMSDP_WIL_PERM]->ValueInt, 12);
    EXPECT_EQ(protocol->pVariables[eMSDP_DEX_PERM]->ValueInt, 17);
    EXPECT_EQ(protocol->pVariables[eMSDP_CON_PERM]->ValueInt, 18);
    EXPECT_EQ(protocol->pVariables[eMSDP_LEA_PERM]->ValueInt, 9);
    EXPECT_EQ(protocol->pVariables[eMSDP_WIMPY]->ValueInt, 33);
    EXPECT_EQ(protocol->pVariables[eMDSP_SPELL_SAVE]->ValueInt, -8);
    EXPECT_EQ(protocol->pVariables[eMDSP_SPELL_PEN]->ValueInt, 5);
    EXPECT_EQ(protocol->pVariables[eMDSP_SPELL_POWER]->ValueInt, 6);
    EXPECT_EQ(protocol->pVariables[eMDSP_ARMOUR_ABS]->ValueInt,
        get_percent_absorb(&context.character));
    EXPECT_EQ(protocol->pVariables[eMDSP_OFFENSIVE_BONUS]->ValueInt,
        get_real_OB(&context.character));
    EXPECT_EQ(protocol->pVariables[eMDSP_PARRY]->ValueInt, get_real_parry(&context.character));
    EXPECT_EQ(protocol->pVariables[eMDSP_DODGE]->ValueInt, get_real_dodge(&context.character));
    EXPECT_EQ(protocol->pVariables[eMDSP_ATTACK_SPEED]->ValueInt,
        utils::get_energy_regen(context.character) / 5);
    EXPECT_EQ(protocol->pVariables[eMDSP_PERCEPTION]->ValueInt, 66);
    EXPECT_EQ(protocol->pVariables[eMDSP_WILLPOWER]->ValueInt, 44);
    EXPECT_EQ(protocol->pVariables[eMDSP_SKILL_ENCUMBRANCE]->ValueInt,
        utils::get_encumbrance(context.character));
    EXPECT_EQ(protocol->pVariables[eMDSP_MOVEMENT_ENCUMBRANCE]->ValueInt,
        utils::get_leg_encumbrance(context.character));
    EXPECT_EQ(protocol->pVariables[eMDSP_HEALTH_REGENERATION]->ValueInt,
        static_cast<int>(hit_gain(&context.character)));
    EXPECT_EQ(protocol->pVariables[eMDSP_STAMINA_REGENERATION]->ValueInt,
        static_cast<int>(mana_gain(&context.character)));
    EXPECT_EQ(protocol->pVariables[eMDSP_MOVEMENT_REGENERATION]->ValueInt,
        static_cast<int>(move_gain(&context.character)));

    EXPECT_EQ(context.read_output(),
        expected_msdp_pair("ALIGNMENT", "250")
            + expected_msdp_pair("EXPERIENCE",
                std::to_string(next_level_xp - GET_EXP(&context.character)))
            + expected_msdp_pair("EXPERIENCE_MAX",
                std::to_string(next_level_xp - current_level_xp))
            + expected_msdp_pair("MANA", "90") + expected_msdp_pair("MANA_MAX", "110")
            + expected_msdp_pair("WIMPY", "33") + expected_msdp_pair("MONEY", "543")
            + expected_msdp_pair("MOVEMENT", "80")
            + expected_msdp_pair("MOVEMENT_MAX", "95") + expected_msdp_pair("STR", "14")
            + expected_msdp_pair("INT", "13") + expected_msdp_pair("WILL", "11")
            + expected_msdp_pair("DEX", "15") + expected_msdp_pair("CON", "16")
            + expected_msdp_pair("LEA", "8") + expected_msdp_pair("STR_PERM", "16")
            + expected_msdp_pair("INT_PERM", "15") + expected_msdp_pair("WIL_PERM", "12")
            + expected_msdp_pair("DEX_PERM", "17") + expected_msdp_pair("CON_PERM", "18")
            + expected_msdp_pair("LEA_PERM", "9") + expected_msdp_pair("SPELL_SAVE", "-8")
            + expected_msdp_pair("SPELL_PEN", "5") + expected_msdp_pair("SPELL_POWER", "6")
            + expected_msdp_pair("ARMOUR_ABS",
                std::to_string(get_percent_absorb(&context.character)))
            + expected_msdp_pair("OFFENSIVE_BONUS", std::to_string(get_real_OB(&context.character)))
            + expected_msdp_pair("PARRY", std::to_string(get_real_parry(&context.character)))
            + expected_msdp_pair("DODGE", std::to_string(get_real_dodge(&context.character)))
            + expected_msdp_pair("ATTACK_SPEED",
                std::to_string(utils::get_energy_regen(context.character) / 5))
            + expected_msdp_pair("PERCEPTION", "66") + expected_msdp_pair("WILLPOWER", "44")
            + expected_msdp_pair("SKILL_ENCUMBRANCE",
                std::to_string(utils::get_encumbrance(context.character)))
            + expected_msdp_pair("MOVEMENT_ENCUMBRANCE",
                std::to_string(utils::get_leg_encumbrance(context.character)))
            + expected_msdp_pair("HEALTH_REGENERATION",
                std::to_string(static_cast<int>(hit_gain(&context.character))))
            + expected_msdp_pair("STAMINA_REGENERATION",
                std::to_string(static_cast<int>(mana_gain(&context.character))))
            + expected_msdp_pair("MOVEMENT_REGENERATION",
                std::to_string(static_cast<int>(move_gain(&context.character)))));
    for (variable_t variable : variables)
        EXPECT_FALSE(protocol->pVariables[variable]->bDirty);
}

TEST(MSDPProtocol, MsdpUpdateEmitsIndoorAndOutdoorWeather)
{
    ScopedDescriptorList descriptor_list_scope;
    ScopedMSDPTestRoom room_scope;
    ProtocolDescriptor indoor_context;
    ProtocolDescriptor outdoor_context;

    initialize_msdp_player(&indoor_context.character, "Indoor");
    enable_msdp_reports(indoor_context.descriptor.pProtocol, { eMDSP_WEATHER });
    descriptor_list = &indoor_context.descriptor;

    msdp_update();

    EXPECT_STREQ(indoor_context.descriptor.pProtocol->pVariables[eMDSP_WEATHER]->pValueString,
        "You can have no feeling about the weather here.");
    EXPECT_EQ(indoor_context.read_output(),
        expected_msdp_pair("WEATHER", "You can have no feeling about the weather here."));
    EXPECT_FALSE(indoor_context.descriptor.pProtocol->pVariables[eMDSP_WEATHER]->bDirty);

    world[0].room_flags = 0;
    world[0].sector_type = SECT_FIELD;
    ScopedSectorWeather field_weather(SECT_FIELD, SKY_CLOUDLESS);
    initialize_msdp_player(&outdoor_context.character, "Outdoor");
    enable_msdp_reports(outdoor_context.descriptor.pProtocol, { eMDSP_WEATHER });
    descriptor_list = &outdoor_context.descriptor;

    msdp_update();

    EXPECT_STREQ(outdoor_context.descriptor.pProtocol->pVariables[eMDSP_WEATHER]->pValueString,
        "Above the fields, not a cloud can be seen in the sky.");
    EXPECT_EQ(outdoor_context.read_output(),
        expected_msdp_pair("WEATHER", "Above the fields, not a cloud can be seen in the sky."));
    EXPECT_FALSE(outdoor_context.descriptor.pProtocol->pVariables[eMDSP_WEATHER]->bDirty);
}

TEST(MSDPProtocol, MsdpUpdateClearsOpponentFieldsWhenNotFighting)
{
    ScopedDescriptorList descriptor_list_scope;
    ScopedMSDPTestRoom room_scope;
    ProtocolDescriptor context;

    initialize_msdp_player(&context.character, "Aragorn");
    enable_msdp_reports(context.descriptor.pProtocol,
        { eMSDP_OPPONENT_HEALTH, eMSDP_OPPONENT_NAME, eMSDP_OPPONENT_LEVEL });
    MSDPSetNumber(&context.descriptor, eMSDP_OPPONENT_HEALTH, 88);
    MSDPSetString(&context.descriptor, eMSDP_OPPONENT_NAME, "stale opponent");
    MSDPSetString(&context.descriptor, eMSDP_OPPONENT_LEVEL, "44");
    descriptor_list = &context.descriptor;

    msdp_update();

    protocol_t* protocol = context.descriptor.pProtocol;
    EXPECT_EQ(protocol->pVariables[eMSDP_OPPONENT_HEALTH]->ValueInt, 0);
    EXPECT_STREQ(protocol->pVariables[eMSDP_OPPONENT_NAME]->pValueString, "");
    EXPECT_STREQ(protocol->pVariables[eMSDP_OPPONENT_LEVEL]->pValueString, "");
    EXPECT_EQ(context.read_output(),
        expected_msdp_pair("OPPONENT_HEALTH", "0") + expected_msdp_pair("OPPONENT_LEVEL", "")
            + expected_msdp_pair("OPPONENT_NAME", ""));
}

TEST(MSDPProtocol, MsdpUpdateEmitsNpcOpponentDetails)
{
    ScopedDescriptorList descriptor_list_scope;
    ScopedMSDPTestRoom room_scope;
    ProtocolDescriptor context;
    char_data opponent {};

    initialize_msdp_player(&context.character, "Aragorn");
    clear_char(&opponent, MOB_ISNPC);
    // Releases opponent.profs (clear_char()'s only heap allocation for
    // MOB_ISNPC) at scope exit (Phase 5 T6 leak sweep).
    ScopedClearCharFields opponent_cleanup { opponent };
    SET_BIT(opponent.specials2.act, MOB_ISNPC);
    opponent.player.short_descr = strdup("a snarling orc");
    opponent.player.level = 12;
    opponent.abilities.hit = 200;
    opponent.tmpabilities.hit = 50;
    context.character.specials.fighting = &opponent;
    enable_msdp_reports(context.descriptor.pProtocol,
        { eMSDP_OPPONENT_HEALTH, eMSDP_OPPONENT_NAME, eMSDP_OPPONENT_LEVEL });
    descriptor_list = &context.descriptor;

    msdp_update();

    protocol_t* protocol = context.descriptor.pProtocol;
    EXPECT_EQ(protocol->pVariables[eMSDP_OPPONENT_HEALTH]->ValueInt, 25);
    EXPECT_STREQ(protocol->pVariables[eMSDP_OPPONENT_NAME]->pValueString, "a snarling orc");
    EXPECT_STREQ(protocol->pVariables[eMSDP_OPPONENT_LEVEL]->pValueString, "12");
    EXPECT_EQ(context.read_output(),
        expected_msdp_pair("OPPONENT_HEALTH", "25") + expected_msdp_pair("OPPONENT_LEVEL", "12")
            + expected_msdp_pair("OPPONENT_NAME", "a snarling orc"));

    // Releases opponent.player.short_descr (strdup() above) at scope exit
    // (Phase 5 T6 leak sweep) -- not one of ScopedClearCharFields's fields.
    RELEASE(opponent.player.short_descr);
}

TEST(MSDPProtocol, MsdpUpdateHandlesOpponentWithInvalidMaxHealth)
{
    ScopedDescriptorList descriptor_list_scope;
    ScopedMSDPTestRoom room_scope;
    ProtocolDescriptor context;
    char_data opponent {};

    initialize_msdp_player(&context.character, "Aragorn");
    clear_char(&opponent, MOB_ISNPC);
    // Releases opponent.profs (clear_char()'s only heap allocation for
    // MOB_ISNPC) at scope exit (Phase 5 T6 leak sweep).
    ScopedClearCharFields opponent_cleanup { opponent };
    SET_BIT(opponent.specials2.act, MOB_ISNPC);
    opponent.player.short_descr = strdup("a wounded orc");
    opponent.player.level = 12;
    opponent.abilities.hit = 0;
    opponent.tmpabilities.hit = 50;
    context.character.specials.fighting = &opponent;
    enable_msdp_reports(context.descriptor.pProtocol, { eMSDP_OPPONENT_HEALTH });
    MSDPSetNumber(&context.descriptor, eMSDP_OPPONENT_HEALTH, 88);
    descriptor_list = &context.descriptor;

    msdp_update();

    EXPECT_EQ(context.descriptor.pProtocol->pVariables[eMSDP_OPPONENT_HEALTH]->ValueInt, 0);
    EXPECT_EQ(context.read_output(), expected_msdp_pair("OPPONENT_HEALTH", "0"));

    // Releases opponent.player.short_descr (strdup() above) at scope exit
    // (Phase 5 T6 leak sweep) -- not one of ScopedClearCharFields's fields.
    RELEASE(opponent.player.short_descr);
}

TEST(MSDPProtocol, MsdpUpdateMasksPlayerOpponentDetails)
{
    ScopedDescriptorList descriptor_list_scope;
    ScopedMSDPTestRoom room_scope;
    ProtocolDescriptor context;
    char_data opponent {};

    initialize_msdp_player(&context.character, "Aragorn");
    initialize_msdp_player(&opponent, "Boromir");
    // Releases opponent.profs/skills/knowledge (clear_char() heap
    // allocations, via initialize_msdp_player()) at scope exit (Phase 5 T6
    // leak sweep). opponent.player.name (also strdup()'d by
    // initialize_msdp_player()) is released explicitly below -- not one of
    // ScopedClearCharFields's fields, and `opponent` (unlike context.character)
    // has no ProtocolDescriptor destructor to release it for it.
    ScopedClearCharFields opponent_cleanup { opponent };
    opponent.player.level = 18;
    opponent.player.race = RACE_HUMAN;
    opponent.abilities.hit = 120;
    opponent.tmpabilities.hit = 30;
    context.character.specials.fighting = &opponent;
    enable_msdp_reports(context.descriptor.pProtocol,
        { eMSDP_OPPONENT_HEALTH, eMSDP_OPPONENT_NAME, eMSDP_OPPONENT_LEVEL });
    descriptor_list = &context.descriptor;

    msdp_update();

    protocol_t* protocol = context.descriptor.pProtocol;
    EXPECT_EQ(protocol->pVariables[eMSDP_OPPONENT_HEALTH]->ValueInt, 25);
    EXPECT_EQ(protocol->pVariables[eMSDP_OPPONENT_NAME]->pValueString,
        pc_star_types[RACE_HUMAN]);
    EXPECT_STREQ(protocol->pVariables[eMSDP_OPPONENT_LEVEL]->pValueString, "???");
    EXPECT_EQ(context.read_output(),
        expected_msdp_pair("OPPONENT_HEALTH", "25") + expected_msdp_pair("OPPONENT_LEVEL", "???")
            + expected_msdp_pair("OPPONENT_NAME", pc_star_types[RACE_HUMAN]));

    RELEASE(opponent.player.name);
}

} // namespace
