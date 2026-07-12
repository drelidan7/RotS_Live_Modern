#include "../color.h"
#include "../comm.h"
#include "../db.h"
#include "../handler.h"
#include "../interpre.h"
#include "../structs.h"
#include "../utils.h"
#include "test_platform_compat.h"
#include "test_world.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

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
    char_data* original_people = nullptr;

    RoomPairContext()
    {
        reset_capturing_descriptor(actor_descriptor, &actor);
        reset_capturing_descriptor(victim_descriptor, &victim);

        original_people = test_world.room().people;

        actor.in_room = 0;
        victim.in_room = 0;
        actor.next_in_room = &victim;
        victim.next_in_room = nullptr;
        test_world.room().people = &actor;

        actor.specials.position = POSITION_STANDING;
        victim.specials.position = POSITION_STANDING;
        actor.player.race = RACE_HUMAN;
        victim.player.race = RACE_HUMAN;
        SET_BIT(actor.specials2.pref, PRF_HOLYLIGHT);

        actor.desc = &actor_descriptor;
        victim.desc = &victim_descriptor;
    }

    ~RoomPairContext()
    {
        test_world.room().people = original_people;
        actor.next_in_room = nullptr;
        victim.next_in_room = nullptr;
        actor.in_room = NOWHERE;
        victim.in_room = NOWHERE;
    }
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
// two occupants, so a bystander is chained on locally; the context's own
// destructor already unconditionally resets both actor.next_in_room and
// victim.next_in_room, so no extra teardown is needed for this local link.
TEST(ActTokenExpansion, ActToNotVictExcludesBothActorAndVictim)
{
    RoomPairContext context;
    context.actor.player.name = const_cast<char*>("Actor");

    char_data bystander { };
    descriptor_data bystander_descriptor { };
    reset_capturing_descriptor(bystander_descriptor, &bystander);
    bystander.in_room = 0;
    bystander.specials.position = POSITION_STANDING;
    bystander.player.race = RACE_HUMAN;
    bystander.desc = &bystander_descriptor;

    context.victim.next_in_room = &bystander;
    bystander.next_in_room = nullptr;

    act("$n announces something.", FALSE, &context.actor, nullptr, &context.victim, TO_NOTVICT, 0);

    EXPECT_STREQ(bystander_descriptor.output, "Actor announces something.\n\r");
    EXPECT_STREQ(context.victim_descriptor.output, "");
    EXPECT_STREQ(context.actor_descriptor.output, "");

    bystander.in_room = NOWHERE;
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
