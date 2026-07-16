#include "../color.h"
#include "../spells.h"
#include "rots/core/character.h"
#include "rots/core/room.h"
#include "rots/core/descriptor.h"
#include "rots/core/types.h"
#include "../utils.h"
#include "test_char_cleanup.h"
#include "test_world.h"

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <string>
#include <string_view>

extern struct room_data world;
extern int top_of_world;
void clear_char(struct char_data* ch, int mode);
void say_spell(struct char_data* caster, int spell_index);
void send_magic_room_message(struct char_data* caster, std::string_view message);

namespace {

descriptor_data make_descriptor()
{
    descriptor_data descriptor {};
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    return descriptor;
}

void attach_character_to_room(char_data* character, int room_rnum, char_data* next_in_room)
{
    character->in_room = room_rnum;
    character->next_in_room = next_in_room;
}

void initialize_player_character(char_data* character, const char* name)
{
    clear_char(character, MOB_VOID);
    character->player.name = const_cast<char*>(name);
    character->specials.position = POSITION_STANDING;
}

} // namespace

TEST(SpellParser, SaySpellUsesMagicColorForColorEnabledObservers)
{
    ScopedTestWorld test_world;
    test_world.room().number = 3001;

    char_data caster {};
    char_data observer {};
    descriptor_data observer_descriptor = make_descriptor();
    // Re-point output to THIS object's own small_outbuf: make_descriptor()
    // returns by value and its internal `output = &small_outbuf` self-pointer
    // dangles into the returned-from frame when NRVO isn't applied (MSVC
    // Debug) -- writes would otherwise corrupt freed stack. Phase 3 Task 6.
    observer_descriptor.output = observer_descriptor.small_outbuf;

    initialize_player_character(&caster, "caster");
    // Releases caster.profs/skills/knowledge (clear_char() heap
    // allocations, via initialize_player_character()) at scope exit
    // (Phase 5 T6 leak sweep).
    ScopedClearCharFields caster_cleanup { caster };
    initialize_player_character(&observer, "observer");
    // Releases observer.profs/skills/knowledge (clear_char() heap
    // allocations, via initialize_player_character()) at scope exit
    // (Phase 5 T6 leak sweep).
    ScopedClearCharFields observer_cleanup { observer };
    observer.desc = &observer_descriptor;
    SET_BIT(PRF_FLAGS(&observer), PRF_COLOR);
    set_colornum(&observer, COLOR_MAGIC, CBBLU);

    attach_character_to_room(&observer, 0, nullptr);
    attach_character_to_room(&caster, 0, &observer);
    world[0].people = &caster;

    say_spell(&caster, SPELL_MAGIC_MISSILE);

    const std::string output = observer_descriptor.output;
    EXPECT_NE(output.find(color_sequence[CBBLU]), std::string::npos) << output;
    EXPECT_NE(output.find("Caster utters a strange command, 'magic missile'"), std::string::npos)
        << output;
    EXPECT_NE(output.find(color_sequence[CNRM]), std::string::npos) << output;
}

TEST(SpellParser, MagicRoomMessageOmitsColorCodesForObserversWithoutColorEnabled)
{
    ScopedTestWorld test_world;
    test_world.room().number = 3002;

    char_data caster {};
    char_data observer {};
    descriptor_data observer_descriptor = make_descriptor();
    // Re-point output to THIS object's own small_outbuf: make_descriptor()
    // returns by value and its internal `output = &small_outbuf` self-pointer
    // dangles into the returned-from frame when NRVO isn't applied (MSVC
    // Debug) -- writes would otherwise corrupt freed stack. Phase 3 Task 6.
    observer_descriptor.output = observer_descriptor.small_outbuf;

    initialize_player_character(&caster, "caster");
    // Releases caster.profs/skills/knowledge (clear_char() heap
    // allocations, via initialize_player_character()) at scope exit
    // (Phase 5 T6 leak sweep).
    ScopedClearCharFields caster_cleanup { caster };
    initialize_player_character(&observer, "observer");
    // Releases observer.profs/skills/knowledge (clear_char() heap
    // allocations, via initialize_player_character()) at scope exit
    // (Phase 5 T6 leak sweep).
    ScopedClearCharFields observer_cleanup { observer };
    observer.desc = &observer_descriptor;
    REMOVE_BIT(PRF_FLAGS(&observer), PRF_COLOR);
    set_colornum(&observer, COLOR_MAGIC, CBBLU);

    attach_character_to_room(&observer, 0, nullptr);
    attach_character_to_room(&caster, 0, &observer);
    world[0].people = &caster;

    send_magic_room_message(&caster, "$n begins quietly muttering some strange, powerful words.\n\r");

    const std::string output = observer_descriptor.output;
    EXPECT_EQ(output.find(color_sequence[CBBLU]), std::string::npos) << output;
    EXPECT_EQ(output.find(color_sequence[CNRM]), std::string::npos) << output;
    EXPECT_NE(output.find("Caster begins quietly muttering some strange, powerful words."), std::string::npos) << output;
}

TEST(SpellParser, MagicRoomMessageAcceptsBoundedTextAndStopsAtEmbeddedNull)
{
    ScopedTestWorld test_world;

    char_data caster {};
    char_data observer {};
    descriptor_data observer_descriptor = make_descriptor();
    observer_descriptor.output = observer_descriptor.small_outbuf;

    initialize_player_character(&caster, "caster");
    ScopedClearCharFields caster_cleanup { caster };
    initialize_player_character(&observer, "observer");
    ScopedClearCharFields observer_cleanup { observer };
    observer.desc = &observer_descriptor;

    attach_character_to_room(&observer, 0, nullptr);
    attach_character_to_room(&caster, 0, &observer);
    world[0].people = &caster;

    const std::array<char, 20> message {
        '$', 'n', ' ', 'c', 'a', 's', 't', 's', '.', '\n', '\r', '\0',
        'i', 'g', 'n', 'o', 'r', 'e', 'd', '!'
    };
    send_magic_room_message(&caster, std::string_view(message.data(), message.size()));

    EXPECT_EQ(std::string(observer_descriptor.output), "Caster casts.\n\r\n\r");
}
