#include "../char_utils.h"
#include "../color.h"
#include "../comm.h"
#include "../db.h"
#include "../handler.h"
#include "../interpre.h"
#include "rots/core/character.h"
#include "rots/core/object.h"
#include "rots/core/room.h"
#include "rots/core/descriptor.h"
#include "rots/core/types.h"
#include "../utils.h"
#include "test_char_cleanup.h"
#include "test_placement.h"
#include "test_platform_compat.h"
#include "test_world.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

// Characterization tests for Phase 4 Wave 4 Task 5 (comm.cpp's convert_string,
// the hand-rolled $-token scanner every act()-routed message flows through).
// These pin the CURRENT byte-for-byte output of convert_string's three
// sprintf-family composition sites -- confirmed passing against the
// pre-conversion source before those sites were converted to
// std::format/std::string composition, and green again after. The scanner's
// architecture (the switch-driven $ token walk itself) is unchanged; only its
// sprintf-family calls convert. All tests go through act() (comm.h:53), the
// only public entry point to convert_string (comm.cpp has no other caller).
//
// Token inventory (comm.cpp:2172-2321, source-authoritative -- this
// supersedes the task brief's own placeholder list per Step 1's instruction:
// no $u/$U case exists in the source, and $K/$b/$B/$$ are real cases the
// brief's placeholder list omitted):
//   $C + one of N/C/Y/T/S/R/H/D/K/O/E/G -- CC_USE(to, COLOR_*) color escape
//   $K   -- PERS(vict_obj, to, capitalize=FALSE, force_visible=TRUE)
//   $n   -- PERS(ch, to, FALSE, FALSE), sets clobbered_color
//   $N   -- PERS(vict_obj, to, FALSE, FALSE), sets clobbered_color
//   $m/$M -- HMHR(ch) / HMHR(vict_obj)
//   $s/$S -- HSHR(ch) / HSHR(vict_obj)
//   $e/$E -- HSSH(ch) / HSSH(vict_obj)
//   $o/$O -- OBJN(obj, to) / OBJN(vict_obj, to)
//   $p/$P -- OBJS(obj, to) / OBJS(vict_obj, to)
//   $a/$A -- SANA(obj) / SANA(vict_obj)
//   $T   -- (char*)vict_obj, used verbatim as a raw string
//   $F   -- fname((char*)vict_obj)
//   $b/$B -- GET_CURRPART(ch) / GET_CURRPART(vict_obj)
//   $$   -- literal "$"
//   default (unrecognized $ code) -- SYSERR error log via strcpy(buf1,...)/
//     strcat(buf1,...)/log(buf1) [conversion sites 1 & 2], falls through
//     reusing the stale `i` pointer left by whatever $ token last set it
//   $C's own inner default (unrecognized color letter) -- vmudlog() only, no
//     strcpy/strcat/sprintf -- NOT one of the 3 conversion sites, noted here
//     for completeness but not separately pinned
//
// The 3rd sprintf-family site is the unconditional trailing
// `sprintf(point, "%s", CC_NORM(to))`, appended once `used_color` is
// non-null (i.e. any $C code was ever processed by the scanner), independent
// of whether a later $n/$N "clobbered" it along the way.

namespace {

// Mirrors act_format_tests.cpp:66-74 verbatim (Phase 4 Wave 2 Task 4's
// helper): points a descriptor's output at its OWN small_outbuf so
// act()/SEND_TO_Q output can be inspected directly instead of going to a
// real socket.
//
// CRITICAL: this mutates the caller's descriptor_data in place and must NEVER
// be replaced by a version that returns a descriptor_data by value.
// descriptor_data::output is a self-pointer into the same object's
// small_outbuf[]; copying/moving a descriptor_data (across a `return`, an
// `x = f()`, or a member-initializer) copies that pointer bytewise and leaves
// `output` aimed at the SOURCE object's buffer -- a dangling pointer once the
// source (a returned temporary) is destroyed. On MSVC's Debug config (NRVO
// disabled) that dangling write_to_output() target produced empty/garbage
// output, cross-descriptor bleed (a victim's message surfacing in the actor's
// buffer), and an eventual SEH 0xc0000005 access violation; Linux/macOS masked
// it via copy elision. Always declare the descriptor, then reset it in place.
void reset_capturing_descriptor(descriptor_data& descriptor, char_data* character)
{
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = 0; // CON_PLAYING
    descriptor.character = character;
}

// Two ordinary (non-NPC) PCs sharing room 0 of the process-wide test world.
// Mirrored locally from act_format_tests.cpp:101-140 per the task brief
// rather than shared, since that file's copy lives in an anonymous
// namespace and isn't reachable from other translation units. CAN_SEE()'s
// darkness gate is bypassed with PRF_HOLYLIGHT on the actor (real light
// bookkeeping is out of scope for a scanner characterization fixture).
struct RoomPairContext {
    ScopedTestWorld test_world;
    char_data actor { };
    char_data victim { };
    descriptor_data actor_descriptor { };
    descriptor_data victim_descriptor { };

    // The occupant chain: actor at the head, victim behind it -- exactly the
    // head-first order this fixture used to publish by hand (LS-3a T3,
    // test_placement.h). Declared LAST so it unwinds before the characters it
    // manages and before the ScopedTestWorld whose room it points into. Its
    // constructor stamps both locations through set_location(), so this
    // fixture no longer writes in_room or next_in_room anywhere.
    ScopedRoomOccupants occupants { &test_world.room(), 0, { &actor, &victim } };

    RoomPairContext()
    {
        reset_capturing_descriptor(actor_descriptor, &actor);
        reset_capturing_descriptor(victim_descriptor, &victim);

        actor.specials.position = POSITION_STANDING;
        victim.specials.position = POSITION_STANDING;
        actor.player.race = RACE_HUMAN;
        victim.player.race = RACE_HUMAN;
        SET_BIT(actor.specials2.pref, PRF_HOLYLIGHT);

        actor.desc = &actor_descriptor;
        victim.desc = &victim_descriptor;
    }

    // The chain-head restore, the two unlinks and the two NOWHERE
    // de-locations this destructor used to perform are now `occupants`, which
    // unwinds immediately after it. ScopedTestWorld leaves room 0's head null
    // at construction, so restoring the SAVED head is byte-identical to the
    // `people = original_people` this used to write.
    ~RoomPairContext() = default;
};

// A single PC with a fully-allocated profs block (via clear_char(), which
// CREATE1()s ch->profs and memsets its colors[] to CNRM) -- needed only by
// the $C color-code tests, which read PRF_COLOR + ch->profs->colors[col] via
// CC_USE/CC_NORM. RoomPairContext's actor/victim are raw value-initialized
// char_data (profs == nullptr), which is fine for every other token family
// (CC_USE degrades to "" on a null profs -- see color.cpp's
// get_color_sequence -- so it never crashes) but can't produce a non-empty
// escape sequence, which the site-3 sprintf(point, "%s", CC_NORM(to)) pin
// specifically needs to exercise.
struct SelfColorContext {
    char_data character { };
    descriptor_data descriptor { };
    // Releases character.profs/skills/knowledge (clear_char() heap
    // allocations) at scope exit (Phase 5 T6 leak sweep).
    ScopedClearCharFields character_cleanup { character };

    SelfColorContext()
    {
        clear_char(&character, MOB_VOID);
        reset_capturing_descriptor(descriptor, &character);
        character.desc = &descriptor;
        character.specials.position = POSITION_STANDING;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// PERS-family tokens: $n / $N / $K
// ---------------------------------------------------------------------------

// $n expands to PERS(ch, to) -- the actor's own name, as seen by the
// observer -- and is delivered TO_ROOM (every other occupant, i.e. victim).
TEST(ActTokenExpansion, DollarNExpandsToActorNameForSeeingObserver)
{
    RoomPairContext context;
    context.actor.player.name = const_cast<char*>("Actor");

    act("$n waves.", FALSE, &context.actor, nullptr, nullptr, TO_ROOM, 0);

    EXPECT_STREQ(context.victim_descriptor.output, "Actor waves.\n\r");
}

// $N expands to PERS(vict_obj, to) when the observer CAN see the victim --
// delivered TO_CHAR (to == ch == actor), with the leading char capitalized
// by convert_string's trailing CAP(strp) regardless of PERS's own
// capitalize=FALSE argument.
TEST(ActTokenExpansion, DollarCapitalNExpandsToVictimNameForSeeingObserver)
{
    RoomPairContext context;
    context.victim.player.name = const_cast<char*>("victim");

    act("$N nods.", FALSE, &context.actor, nullptr, &context.victim, TO_CHAR, 0);

    EXPECT_STREQ(context.actor_descriptor.output, "Victim nods.\n\r");
}

// $N falls back to PERS's "someone" branch when the observer cannot see the
// victim -- pinned via GET_INVIS_LEV (an immortal-invisibility level check
// that PRF_HOLYLIGHT does NOT bypass, unlike the light/AFF_INVISIBLE checks),
// contrasted with $K below which forces past this via PERS's force_visible.
TEST(ActTokenExpansion, DollarCapitalNRendersSomeoneWhenObserverCannotSeeVictim)
{
    RoomPairContext context;
    context.victim.player.name = const_cast<char*>("Victim");
    context.victim.specials.invis_level = 30;
    context.actor.player.level = 0;

    act("$N nods.", FALSE, &context.actor, nullptr, &context.victim, TO_CHAR, 0);

    EXPECT_STREQ(context.actor_descriptor.output, "Someone nods.\n\r");
}

// $K is PERS(..., force_visible=TRUE) -- resolves the victim's real name
// even though the same GET_INVIS_LEV gate that made $N render "someone"
// above still applies; force_visible bypasses the CAN_SEE call entirely.
TEST(ActTokenExpansion, DollarKExpandsToForceVisibleVictimNameDespiteInvisLevel)
{
    RoomPairContext context;
    context.victim.player.name = const_cast<char*>("Victim");
    context.victim.specials.invis_level = 30;
    context.actor.player.level = 0;

    act("$K nods.", FALSE, &context.actor, nullptr, &context.victim, TO_CHAR, 0);

    EXPECT_STREQ(context.actor_descriptor.output, "Victim nods.\n\r");
}

// ---------------------------------------------------------------------------
// Pronoun tokens: $m/$M, $s/$S, $e/$E
// ---------------------------------------------------------------------------

TEST(ActTokenExpansion, DollarMExpandsToActorObjectPronoun)
{
    RoomPairContext context;
    context.actor.player.sex = SEX_MALE;

    act("Someone hits $m.", FALSE, &context.actor, nullptr, nullptr, TO_CHAR, 0);

    EXPECT_STREQ(context.actor_descriptor.output, "Someone hits him.\n\r");
}

TEST(ActTokenExpansion, DollarCapitalMExpandsToVictimObjectPronoun)
{
    RoomPairContext context;
    context.victim.player.sex = SEX_FEMALE;

    act("Someone hits $M.", FALSE, &context.actor, nullptr, &context.victim, TO_CHAR, 0);

    EXPECT_STREQ(context.actor_descriptor.output, "Someone hits her.\n\r");
}

TEST(ActTokenExpansion, DollarSExpandsToActorPossessivePronoun)
{
    RoomPairContext context;
    context.actor.player.sex = SEX_MALE;

    act("$s sword gleams.", FALSE, &context.actor, nullptr, nullptr, TO_CHAR, 0);

    EXPECT_STREQ(context.actor_descriptor.output, "His sword gleams.\n\r");
}

TEST(ActTokenExpansion, DollarCapitalSExpandsToVictimPossessivePronoun)
{
    RoomPairContext context;
    context.victim.player.sex = SEX_FEMALE;

    act("$S sword gleams.", FALSE, &context.actor, nullptr, &context.victim, TO_CHAR, 0);

    EXPECT_STREQ(context.actor_descriptor.output, "Her sword gleams.\n\r");
}

TEST(ActTokenExpansion, DollarEExpandsToActorSubjectPronoun)
{
    RoomPairContext context;
    context.actor.player.sex = SEX_MALE;

    act("$e grins.", FALSE, &context.actor, nullptr, nullptr, TO_CHAR, 0);

    EXPECT_STREQ(context.actor_descriptor.output, "He grins.\n\r");
}

TEST(ActTokenExpansion, DollarCapitalEExpandsToVictimSubjectPronoun)
{
    RoomPairContext context;
    context.victim.player.sex = SEX_FEMALE;

    act("$E grins.", FALSE, &context.actor, nullptr, &context.victim, TO_CHAR, 0);

    EXPECT_STREQ(context.actor_descriptor.output, "She grins.\n\r");
}

// ---------------------------------------------------------------------------
// Object tokens: $o/$O, $p/$P, $a/$A
// ---------------------------------------------------------------------------

TEST(ActTokenExpansion, DollarOExpandsToActorObjectFirstWord)
{
    RoomPairContext context;
    obj_data sword { };
    sword.name = const_cast<char*>("sword sharp steel");
    sword.short_description = const_cast<char*>("a sharp steel sword");

    act("You wield the $o.", FALSE, &context.actor, &sword, nullptr, TO_CHAR, 0);

    EXPECT_STREQ(context.actor_descriptor.output, "You wield the sword.\n\r");
}

TEST(ActTokenExpansion, DollarCapitalOExpandsToVictimObjectFirstWord)
{
    RoomPairContext context;
    obj_data dagger { };
    dagger.name = const_cast<char*>("dagger swift silver");
    dagger.short_description = const_cast<char*>("a swift silver dagger");

    act("You see the $O.", FALSE, &context.actor, nullptr, &dagger, TO_CHAR, 0);

    EXPECT_STREQ(context.actor_descriptor.output, "You see the dagger.\n\r");
}

TEST(ActTokenExpansion, DollarPExpandsToActorObjectShortDescription)
{
    RoomPairContext context;
    obj_data sword { };
    sword.name = const_cast<char*>("sword");
    sword.short_description = const_cast<char*>("a sharp steel sword");

    act("$p falls to the ground.", FALSE, &context.actor, &sword, nullptr, TO_CHAR, 0);

    EXPECT_STREQ(context.actor_descriptor.output, "A sharp steel sword falls to the ground.\n\r");
}

TEST(ActTokenExpansion, DollarCapitalPExpandsToVictimObjectShortDescription)
{
    RoomPairContext context;
    obj_data dagger { };
    dagger.name = const_cast<char*>("dagger");
    dagger.short_description = const_cast<char*>("a swift silver dagger");

    act("$P falls to the ground.", FALSE, &context.actor, nullptr, &dagger, TO_CHAR, 0);

    EXPECT_STREQ(context.actor_descriptor.output, "A swift silver dagger falls to the ground.\n\r");
}

TEST(ActTokenExpansion, DollarAExpandsToActorObjectArticle)
{
    RoomPairContext context;
    obj_data sword { };
    sword.name = const_cast<char*>("sword sharp");

    act("It is $a sword.", FALSE, &context.actor, &sword, nullptr, TO_CHAR, 0);

    EXPECT_STREQ(context.actor_descriptor.output, "It is a sword.\n\r");
}

TEST(ActTokenExpansion, DollarCapitalAExpandsToVictimObjectVowelArticle)
{
    RoomPairContext context;
    obj_data apple { };
    apple.name = const_cast<char*>("apple shiny");

    act("It is $A apple.", FALSE, &context.actor, nullptr, &apple, TO_CHAR, 0);

    EXPECT_STREQ(context.actor_descriptor.output, "It is an apple.\n\r");
}

// ---------------------------------------------------------------------------
// Raw-string tokens: $T, $F
// ---------------------------------------------------------------------------

// $T copies vict_obj verbatim as a `const char*` -- callers that use it pass
// a raw C string through the void* slot, not a char_data*/obj_data*.
TEST(ActTokenExpansion, DollarTExpandsToRawVictObjString)
{
    RoomPairContext context;

    act("$T is here.", FALSE, &context.actor, nullptr, const_cast<char*>("tunnel"), TO_CHAR, 0);

    EXPECT_STREQ(context.actor_descriptor.output, "Tunnel is here.\n\r");
}

// $F is fname((char*)vict_obj) -- takes the first alpha run of the raw
// string vict_obj points at (fname's contract, handler.cpp:103).
TEST(ActTokenExpansion, DollarFExpandsToFnameOfVictObjString)
{
    RoomPairContext context;

    act("The $F glows.", FALSE, &context.actor, nullptr, const_cast<char*>("golden gate"), TO_CHAR,
        0);

    EXPECT_STREQ(context.actor_descriptor.output, "The golden glows.\n\r");
}

// ---------------------------------------------------------------------------
// Body-part tokens: $b/$B
// ---------------------------------------------------------------------------

TEST(ActTokenExpansion, DollarBExpandsToActorCurrentBodyPart)
{
    RoomPairContext context;
    context.actor.specials.current_bodypart = 1; // bodyparts[RACE_HUMAN].parts[1] == "head"

    act("You grab your $b.", FALSE, &context.actor, nullptr, nullptr, TO_CHAR, 0);

    EXPECT_STREQ(context.actor_descriptor.output, "You grab your head.\n\r");
}

TEST(ActTokenExpansion, DollarCapitalBExpandsToVictimCurrentBodyPart)
{
    RoomPairContext context;
    context.victim.specials.current_bodypart = 2; // bodyparts[RACE_HUMAN].parts[2] == "body"

    act("You aim for the $B.", FALSE, &context.actor, nullptr, &context.victim, TO_CHAR, 0);

    EXPECT_STREQ(context.actor_descriptor.output, "You aim for the body.\n\r");
}

// ---------------------------------------------------------------------------
// Literal-dollar and unrecognized-token (SYSERR default branch) cases
// ---------------------------------------------------------------------------

TEST(ActTokenExpansion, DollarDollarExpandsToLiteralDollarSign)
{
    RoomPairContext context;

    act("That costs $$5.", FALSE, &context.actor, nullptr, nullptr, TO_CHAR, 0);

    EXPECT_STREQ(context.actor_descriptor.output, "That costs $5.\n\r");
}

// Pins conversion sites 1 & 2: an unrecognized $-code hits the default
// branch, which builds and logs "SYSERR: <str>" via strcpy(buf1,...)+
// strcat(buf1,...)+log(buf1), then falls through WITHOUT reassigning `i` --
// so the token-append loop right after the switch re-reads whatever `i` was
// left pointing at by the PRECEDING token. That prior loop
// (`while ((*point = *(i++))) ++point;`) itself walks `i` one PAST its own
// string's terminator, so the stale re-read lands just past PERS()'s static
// "Actor\0" buffer -- zero-initialized static storage there yields an
// immediate '\0', so nothing extra is actually appended; the default branch
// only contributes its (unasserted) stderr SYSERR log line, not visible
// bytes. Real, existing (if easy to mis-predict) scanner behavior: pinned
// as observed, not as reasoned-from-the-source, per the task's
// characterization-not-TDD instruction ("run to discover actual bytes").
TEST(ActTokenExpansion, UnrecognizedDollarCodeLogsSyserrAndLeavesOutputUnaffected)
{
    RoomPairContext context;
    context.actor.player.name = const_cast<char*>("Actor");

    act("$n$Z", FALSE, &context.actor, nullptr, nullptr, TO_CHAR, 0);

    EXPECT_STREQ(context.actor_descriptor.output, "Actor\n\r");
}

// ---------------------------------------------------------------------------
// Color-code tokens ($C + letter) and the trailing CC_NORM sprintf (site 3)
// ---------------------------------------------------------------------------

// With PRF_COLOR off (the RoomPairContext/SelfColorContext default), CC_USE
// returns "" -- but `used_color` is still set to that (non-null) empty
// literal, so the trailing `sprintf(point, "%s", CC_NORM(to))` (site 3)
// still executes; CC_NORM also returns "" while color is off, so the net
// visible effect is a no-op, but the code path is exercised.
TEST(ActTokenExpansion, ColorCodeWithColorOffExpandsToEmptyAndStillHitsNormSite)
{
    SelfColorContext context;
    ASSERT_FALSE(PRF_FLAGGED(&context.character, PRF_COLOR));

    act("$CNHello.", FALSE, &context.character, nullptr, nullptr, TO_CHAR, 0);

    EXPECT_STREQ(context.descriptor.output, "Hello.\n\r");
}

// With PRF_COLOR on and a non-default color assigned, $CN expands to the
// real CC_USE(to, COLOR_NARR) escape sequence, and the trailing
// sprintf(point, "%s", CC_NORM(to)) (site 3) appends the real CC_NORM(to)
// reset sequence after the message. Expected bytes are built from the same
// production helpers (get_color_sequence/color_sequence[0]) rather than
// hardcoded ANSI literals, per the "pin real bytes, don't assume" rule.
TEST(ActTokenExpansion, ColorCodeWithColorOnAppendsUseSequenceThenNormAtEnd)
{
    SelfColorContext context;
    SET_BIT(context.character.specials2.pref, PRF_COLOR);
    context.character.profs->colors[COLOR_NARR] = CRED;

    std::string narr_sequence = get_color_sequence(&context.character, COLOR_NARR);
    ASSERT_FALSE(narr_sequence.empty());

    act("$CNHello.", FALSE, &context.character, nullptr, nullptr, TO_CHAR, 0);

    std::string expected = narr_sequence + "Hello.\n\r" + std::string(color_sequence[0]);
    EXPECT_EQ(std::string(context.descriptor.output), expected);
}

// $n after $C "clobbers" the color mid-message: convert_string re-appends
// `used_color` (the same $C escape) immediately after $n's expansion, in
// addition to (not instead of) the trailing CC_NORM(to) site-3 append at the
// very end. This is the scanner's own internal clobbered_color/used_color
// bookkeeping (unchanged, out of scope) interacting with site 3's output.
TEST(ActTokenExpansion, ColorClobberedByDollarNReappendsColorThenNormAtEnd)
{
    SelfColorContext context;
    context.character.player.name = const_cast<char*>("Actor");
    SET_BIT(context.character.specials2.pref, PRF_COLOR);
    context.character.profs->colors[COLOR_NARR] = CRED;

    std::string narr_sequence = get_color_sequence(&context.character, COLOR_NARR);
    ASSERT_FALSE(narr_sequence.empty());

    act("$CN$n waves.", FALSE, &context.character, nullptr, nullptr, TO_CHAR, 0);

    std::string expected = narr_sequence + "Actor" + narr_sequence + " waves.\n\r" + std::string(color_sequence[0]);
    EXPECT_EQ(std::string(context.descriptor.output), expected);
}

// ---------------------------------------------------------------------------
// act() routing: TO_CHAR / TO_VICT / TO_ROOM / TO_NOTVICT
// ---------------------------------------------------------------------------

TEST(ActTokenExpansion, ActToCharDeliversOnlyToActor)
{
    RoomPairContext context;

    act("You feel great.", FALSE, &context.actor, nullptr, nullptr, TO_CHAR, 0);

    EXPECT_STREQ(context.actor_descriptor.output, "You feel great.\n\r");
    EXPECT_STREQ(context.victim_descriptor.output, "");
}

TEST(ActTokenExpansion, ActToVictDeliversOnlyToVictObj)
{
    RoomPairContext context;
    context.actor.player.name = const_cast<char*>("Actor");

    act("$n hands you a gift.", FALSE, &context.actor, nullptr, &context.victim, TO_VICT, 0);

    EXPECT_STREQ(context.victim_descriptor.output, "Actor hands you a gift.\n\r");
    EXPECT_STREQ(context.actor_descriptor.output, "");
}

TEST(ActTokenExpansion, ActToRoomDeliversToOthersButNotActor)
{
    RoomPairContext context;
    context.actor.player.name = const_cast<char*>("Actor");

    act("$n waves.", FALSE, &context.actor, nullptr, nullptr, TO_ROOM, 0);

    EXPECT_STREQ(context.victim_descriptor.output, "Actor waves.\n\r");
    EXPECT_STREQ(context.actor_descriptor.output, "");
}

// TO_NOTVICT excludes BOTH ch (actor) and vict_obj (victim); only a third
// bystander in the room receives the message. RoomPairContext only wires up
// two occupants, so a bystander joins the chain locally -- as a NESTED
// ScopedRoomOccupants restating the WHOLE chain (LS-3a T3 idiom rule 4),
// which unwinds before the context's own helper and hands room 0 back exactly
// as RoomPairContext published it.
TEST(ActTokenExpansion, ActToNotVictExcludesBothActorAndVictim)
{
    RoomPairContext context;
    context.actor.player.name = const_cast<char*>("Actor");

    char_data bystander { };
    descriptor_data bystander_descriptor { };
    reset_capturing_descriptor(bystander_descriptor, &bystander);
    bystander.specials.position = POSITION_STANDING;
    bystander.player.race = RACE_HUMAN;
    bystander.desc = &bystander_descriptor;

    ScopedRoomOccupants occupants {
        &context.test_world.room(), 0, { &context.actor, &context.victim, &bystander }
    };

    act("$n announces something.", FALSE, &context.actor, nullptr, &context.victim, TO_NOTVICT, 0);

    EXPECT_STREQ(bystander_descriptor.output, "Actor announces something.\n\r");
    EXPECT_STREQ(context.victim_descriptor.output, "");
    EXPECT_STREQ(context.actor_descriptor.output, "");
}

// ---------------------------------------------------------------------------
// hide_invisible gate: `CAN_SEE(to, ch) || !hide_invisible`
// ---------------------------------------------------------------------------

TEST(ActTokenExpansion, HideInvisibleGateSkipsObserverWhoCannotSeeActor)
{
    RoomPairContext context;
    context.actor.specials.invis_level = 30;
    context.victim.player.level = 0;

    act("$n waves.", TRUE, &context.actor, nullptr, nullptr, TO_ROOM, 0);

    EXPECT_STREQ(context.victim_descriptor.output, "");
}

// Deliberately token-free ("Something happens." has no $ codes): $n/$N would
// independently re-run PERS's OWN CAN_SEE(observer, target) check inside
// convert_string, a SEPARATE gate from act()'s own per-receiver delivery
// gate this test targets -- mixing the two would conflate "was it
// delivered" with "did the name resolve", which is a distinct pin (see the
// $N/$K tests above).
TEST(ActTokenExpansion, HideInvisibleGateClearedDeliversRegardlessOfVisibility)
{
    RoomPairContext context;
    context.actor.specials.invis_level = 30;
    context.victim.player.level = 0;

    act("Something happens.", FALSE, &context.actor, nullptr, nullptr, TO_ROOM, 0);

    EXPECT_STREQ(context.victim_descriptor.output, "Something happens.\n\r");
}

// ---------------------------------------------------------------------------
// PRF_SPAM gate: `!spam_only || PRF_FLAGGED(to, PRF_SPAM)`
// ---------------------------------------------------------------------------

TEST(ActTokenExpansion, PrfSpamGateSkipsObserverWithoutSpamPreference)
{
    RoomPairContext context;

    act("Spam message.", FALSE, &context.actor, nullptr, nullptr, TO_ROOM, 1);

    EXPECT_STREQ(context.victim_descriptor.output, "");
}

TEST(ActTokenExpansion, PrfSpamGateDeliversToObserverWithSpamPreference)
{
    RoomPairContext context;
    SET_BIT(context.victim.specials2.pref, PRF_SPAM);

    act("Spam message.", FALSE, &context.actor, nullptr, nullptr, TO_ROOM, 1);

    EXPECT_STREQ(context.victim_descriptor.output, "Spam message.\n\r");
}

// ---------------------------------------------------------------------------
// act()'s per-receiver empty-expansion guard: `if (*buf != '\0')`
// ---------------------------------------------------------------------------

// convert_string unconditionally overwrites its own trailing null terminator
// with "\n\r\0" (comm.cpp:2303-2305) before returning, for EVERY input --
// including a minimal one-token string -- so `buf[0]` is always '\n' (never
// '\0') by the time act() checks it. This pins that the guard's false branch
// is unreached given convert_string's current unconditional trailing-append;
// it is not dead by omission in this test, it is dead in the source itself.
TEST(ActTokenExpansion, EmptyExpansionGuardNeverSkipsDeliveryGivenGuaranteedTrailingCrlf)
{
    RoomPairContext context;

    act("$n", FALSE, &context.actor, nullptr, nullptr, TO_CHAR, 0);

    const std::string output(context.actor_descriptor.output);
    EXPECT_FALSE(output.empty());
    EXPECT_EQ(output.substr(output.size() - 2), "\n\r");
}

TEST(ActTokenExpansion, ExposesABoundedFormatSignature)
{
    static_assert(std::is_same_v<decltype(&act),
        void (*)(std::string_view, int, char_data*, obj_data*, void*, int, char)>);
}

TEST(ActTokenExpansion, AcceptsANonTerminatedFormatSlice)
{
    RoomPairContext context;
    const char storage[] = { 'x', 'H', 'e', 'l', 'l', 'o', 'y' };

    act(std::string_view(storage + 1, 5), FALSE, &context.actor, nullptr, nullptr, TO_CHAR, 0);

    EXPECT_STREQ(context.actor_descriptor.output, "Hello\n\r");
}

TEST(ActTokenExpansion, TruncatesAFormatAtItsFirstEmbeddedNull)
{
    RoomPairContext context;
    const char storage[] = { 'H', 'i', '\0', '$', 'n' };

    act(std::string_view(storage, sizeof(storage)), FALSE, &context.actor, nullptr, nullptr,
        TO_CHAR, 0);

    EXPECT_STREQ(context.actor_descriptor.output, "Hi\n\r");
}

TEST(ActTokenExpansion, ExpandsATokenWhoseCodeIsTheFinalBoundedByte)
{
    RoomPairContext context;
    context.actor.player.name = const_cast<char*>("Actor");
    const char storage[] = { 'x', '$', 'n', 'y' };

    act(std::string_view(storage + 1, 2), FALSE, &context.actor, nullptr, nullptr, TO_CHAR, 0);

    EXPECT_STREQ(context.actor_descriptor.output, "Actor\n\r");
}

TEST(ActTokenExpansion, IgnoresAnEmptyBoundedFormat)
{
    RoomPairContext context;

    act(std::string_view(), FALSE, &context.actor, nullptr, nullptr, TO_CHAR, 0);

    EXPECT_STREQ(context.actor_descriptor.output, "");
}

TEST(ActTokenExpansion, AcceptsATemporaryFormattedMessage)
{
    RoomPairContext context;

    act(std::format("{} {}", "Temporary", 42), FALSE, &context.actor, nullptr, nullptr,
        TO_CHAR, 0);

    EXPECT_STREQ(context.actor_descriptor.output, "Temporary 42\n\r");
}

// ---------------------------------------------------------------------------
// clean_expose_elements() (LS-2 Wave Task T3d rider: ls2-task-3d-report.md --
// zero prior coverage anywhere in src/tests/ before this). Not declared in
// any header (comm.cpp-internal maintenance sweep, called only from
// game_loop's pulse loop) -- forward-declared here the same way this file's
// convert_string/act_impl-adjacent helpers are, per the header-less-product-
// helper convention spec_pro_tests.cpp/mage_tests.cpp already use.
void clean_expose_elements();

// comm.cpp:578's process-wide mage roster itself -- the storage
// clean_expose_elements() sweeps every PULSE_FAST_UPDATE and that
// track_specialized_mage()/untrack_specialized_mage() maintain. Like
// clean_expose_elements() above it has no header home, so it is
// forward-declared here under the same header-less-product-helper convention;
// the teardown tests at the end of this file must observe roster MEMBERSHIP
// directly, and no seam exposes that.
extern std::vector<char_data*> specialized_mages;

namespace {

// RAII wrapper around track_specialized_mage()/untrack_specialized_mage()
// (utils.h, forwarded through output_seam.h's registered comm.cpp bodies --
// register_game_output_sinks() wires the real bodies in gtest_main.cpp's
// global setup). Mandatory: specialized_mages is a comm.cpp-owned
// process-wide std::vector<char_data*>, so a test that tracks a
// stack-local char_data MUST untrack it before the stack unwinds, or a
// later suite's own clean_expose_elements()/track call walks a dangling
// pointer -- the exact class of cross-suite state pollution test_world.h's
// own top-of-file comment documents for the monolithic runner.
class ScopedSpecializedMage {
public:
    explicit ScopedSpecializedMage(char_data* mage)
        : mage_(mage)
    {
        track_specialized_mage(mage_);
    }

    ~ScopedSpecializedMage() { untrack_specialized_mage(mage_); }

    ScopedSpecializedMage(const ScopedSpecializedMage&) = delete;
    ScopedSpecializedMage& operator=(const ScopedSpecializedMage&) = delete;

private:
    char_data* mage_;
};

// Whole-roster save/restore, per the fixture-hygiene rule. ScopedSpecializedMage
// above balances ONE track with ONE untrack, which is enough for a test that
// only ever tracks; the teardown tests below deliberately drive a path that (in
// the RED phase, before the free_char() fix) LEAKS an entry the test itself
// cannot name a matching untrack for. Restoring the whole vector is what keeps
// such a failure from handing a dangling char_data* to the next suite's
// clean_expose_elements() in the single-process monolithic runner -- exactly the
// cross-suite pollution class test_world.h's top-of-file comment documents.
class ScopedMageRosterState {
public:
    ScopedMageRosterState()
        : saved_(specialized_mages)
    {
    }

    ~ScopedMageRosterState() { specialized_mages = saved_; }

    ScopedMageRosterState(const ScopedMageRosterState&) = delete;
    ScopedMageRosterState& operator=(const ScopedMageRosterState&) = delete;

private:
    // Roster contents captured at construction and written back at
    // destruction, so a test that leaks (or over-erases) an entry cannot
    // outlive its own scope.
    std::vector<char_data*> saved_;
};

// Pointer-identity membership test against the roster. Compares POINTER VALUES
// only -- never dereferences -- so it stays valid (and ASan-quiet) when asked
// about a character that has already been torn down, which is the whole point
// of the two tests below.
bool roster_contains(const char_data* character)
{
    return std::find(specialized_mages.begin(), specialized_mages.end(), character)
        != specialized_mages.end();
}

} // namespace

// Exercises the converted self-room read (location_of(mage)/room_of-style
// room_by_id_total(room_number)) and the converted const_occupant_range walk
// (act_offe.cpp:858) together: the exposed target is still present in the
// mage's room, so clean_expose_elements() must leave the specialization
// bookkeeping untouched and send no message.
TEST(CleanExposeElements, LeavesExposedTargetIntactWhenStillPresentInRoom)
{
    RoomPairContext context;
    char_prof_data actor_profs {};
    context.actor.profs = &actor_profs;
    actor_profs.specialization = static_cast<int>(game_types::PS_Cold);
    context.actor.extra_specialization_data.set(context.actor);
    elemental_spec_data* spec_data = context.actor.extra_specialization_data.get_mage_spec();
    ASSERT_NE(spec_data, nullptr) << "PS_Cold must construct an elemental_spec_data-derived spec.";
    spec_data->exposed_target = &context.victim;

    ScopedSpecializedMage tracked(&context.actor);

    clean_expose_elements();

    EXPECT_EQ(spec_data->exposed_target, &context.victim)
        << "The converted occupants() walk must still find the target in the mage's own room.";
    EXPECT_STREQ(context.actor_descriptor.output, "")
        << "No 'no longer vulnerable' message when the target is still present.";
}

// The exposed target has left the mage's room (the room now holds only the
// mage): the converted walk must find nothing, triggering the reset() +
// notification path.
TEST(CleanExposeElements, ResetsAndNotifiesWhenExposedTargetHasLeftTheRoom)
{
    RoomPairContext context;
    char_prof_data actor_profs {};
    context.actor.profs = &actor_profs;
    actor_profs.specialization = static_cast<int>(game_types::PS_Cold);
    context.actor.extra_specialization_data.set(context.actor);
    elemental_spec_data* spec_data = context.actor.extra_specialization_data.get_mage_spec();
    ASSERT_NE(spec_data, nullptr) << "PS_Cold must construct an elemental_spec_data-derived spec.";

    // A target that is NOT linked into room 0's occupant chain (RoomPairContext
    // only chains actor -> victim); using a wholly separate char_data proves
    // the walk is a real search, not a tautology against context.victim.
    char_data departed_target {};
    spec_data->exposed_target = &departed_target;

    ScopedSpecializedMage tracked(&context.actor);

    clean_expose_elements();

    EXPECT_EQ(spec_data->exposed_target, nullptr)
        << "spec_data->reset() must clear exposed_target once the walk finds no match.";
    EXPECT_STREQ(context.actor_descriptor.output,
        "Your target is no longer vulnerable to your spells.\r\n");
}

// ---------------------------------------------------------------------------
// specialized_mages roster teardown (fix/specialized-mages-roster).
//
// store_to_char() (db_players.cpp:1356-1358) -> utils::set_specialization()
// (entity_lifecycle.cpp:1603) -> track_specialized_mage() pushes a raw
// char_data* onto comm.cpp's process-global roster. Before this branch NOTHING
// removed it on teardown: free_char() (entity_lifecycle.cpp:623) ran
// dispatch_char_teardown() (staged object bytes only) and ~char_data() ->
// ~specialization_data() (character.h:660, frees current_spec_info only),
// neither of which touches the roster. Every teardown of a mage-spec character
// therefore left a DANGLING pointer in a live global that
// clean_expose_elements() (comm.cpp:845) dereferences every PULSE_FAST_UPDATE
// -- reachable from `stat file <mage>` (act_wiz.cpp:1198's char_data_ptr) and
// `wizset file` (act_wiz.cpp:2869/3300's free_char(cbuf)) among others.
// ---------------------------------------------------------------------------

// The leak itself, driven through the production allocation/teardown pair:
// make_char_data(MOB_VOID) is exactly what `stat file` uses, and letting the
// char_data_ptr go releases through free_char_deleter -> free_char() (db.h:139).
// utils::set_specialization() is the real registration path store_to_char()
// takes, not a hand-rolled track_specialized_mage() call.
TEST(SpecializedMageRoster, FreeCharRemovesTheCharacterFromTheRoster)
{
    ScopedMageRosterState roster_guard;

    char_data_ptr mage = make_char_data(MOB_VOID);
    ASSERT_NE(mage->profs, nullptr)
        << "clear_char(MOB_VOID) must allocate profs -- set_specialization() no-ops without it.";
    ASSERT_FALSE(IS_NPC(mage.get()))
        << "MOB_VOID must leave MOB_ISNPC clear -- set_specialization() no-ops on an NPC.";

    utils::set_specialization(*mage, game_types::PS_Cold);

    char_data* const released = mage.get();
    ASSERT_TRUE(roster_contains(released))
        << "A mage specialization must register the character on the roster.";

    mage.reset();

    EXPECT_FALSE(roster_contains(released))
        << "free_char() must untrack the character; otherwise the roster holds a dangling "
           "char_data* that clean_expose_elements() dereferences every fast-update pulse.";
}

// The consequence: the real clean_expose_elements() sweep running AFTER a
// mage-spec character has been torn down. Pre-fix this dereferences the freed
// character (`mage->extra_specialization_data.is_mage_spec()`, comm.cpp:848) --
// a use-after-free under ASan. A SURVIVING mage is kept on the roster
// throughout, so the test cannot pass merely by the roster being empty: the
// sweep must still do its real work on the survivor.
TEST(SpecializedMageRoster, CleanExposeElementsSweepsCleanlyAfterAMageIsTornDown)
{
    ScopedMageRosterState roster_guard;

    RoomPairContext context;
    char_prof_data survivor_profs {};
    context.actor.profs = &survivor_profs;
    survivor_profs.specialization = static_cast<int>(game_types::PS_Cold);
    context.actor.extra_specialization_data.set(context.actor);
    elemental_spec_data* survivor_spec = context.actor.extra_specialization_data.get_mage_spec();
    ASSERT_NE(survivor_spec, nullptr) << "PS_Cold must construct an elemental_spec_data.";
    survivor_spec->exposed_target = &context.victim;
    ScopedSpecializedMage survivor(&context.actor);

    char_data_ptr departing = make_char_data(MOB_VOID);
    ASSERT_NE(departing->profs, nullptr);
    utils::set_specialization(*departing, game_types::PS_Fire);
    char_data* const departed = departing.get();
    ASSERT_TRUE(roster_contains(departed));

    departing.reset();

    clean_expose_elements();

    EXPECT_FALSE(roster_contains(departed))
        << "The torn-down character must not survive on the roster the sweep walks.";
    EXPECT_EQ(survivor_spec->exposed_target, &context.victim)
        << "The sweep must still do its real work: the survivor's present target stays exposed.";
    EXPECT_STREQ(context.actor_descriptor.output, "")
        << "No 'no longer vulnerable' message -- the survivor's target never left the room.";
}
