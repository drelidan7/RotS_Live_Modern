#pragma once

#include "../db.h"
#include "../structs.h"
#include "../utils.h"

// clear_char() (db.cpp) heap-allocates ch->profs (always, via CREATE1) and,
// when mode != MOB_ISNPC, sizes ch->skills/ch->knowledge to MAX_SKILLS.
// ch->skills/ch->knowledge are owning std::vector<byte> members (RAII T3;
// were CREATE()/RELEASE()'d byte* before) -- their own destructors release
// their heap buffers automatically whenever the enclosing char_data goes out
// of scope, so a stack-allocated test fixture that never calls free_char()
// no longer leaks them (nothing left for this guard to do for those two
// fields). ch->profs is still a raw pointer (CONDITIONAL per the RAII audit's
// T2 ruling -- prototype-shared for NPCs, out of scope for conversion), so
// production code still frees it explicitly via free_char()'s RELEASE()
// macro (utils.h), and this guard still exists to do the same at scope exit
// for fixtures that can't call free_char() on a stack object (its RELEASE(ch)
// would free() a stack address).
//
// Usage: construct immediately after clear_char(&ch, mode).
struct ScopedClearCharFields {
    // The char_data whose clear_char()-allocated ch->profs this guard owns
    // the release of; never the object itself (that stays stack-owned by the
    // caller).
    char_data& ch;

    explicit ScopedClearCharFields(char_data& character)
        : ch(character)
    {
    }

    ScopedClearCharFields(const ScopedClearCharFields&) = delete;
    ScopedClearCharFields& operator=(const ScopedClearCharFields&) = delete;

    // Releases ch.profs right away -- some fixtures (e.g.
    // act_wiz_format_tests.cpp's PcTargetContext) overwrite character.profs
    // with the address of a stack char_prof_data member right after
    // clear_char(), which would otherwise both leak the heap allocation AND,
    // without this guard, have the destructor free() the stack member's
    // address. Sets m_profs_released so the destructor skips its own
    // RELEASE(ch.profs) afterward.
    void release_profs_now()
    {
        RELEASE(ch.profs);
        m_profs_released = true;
    }

    ~ScopedClearCharFields()
    {
        if (!m_profs_released)
            RELEASE(ch.profs);
    }

private:
    // Set by release_profs_now() once ch.profs has already been released
    // (and possibly overwritten with a non-owned pointer by the caller) --
    // guards the destructor against releasing that pointer too.
    bool m_profs_released = false;
};

// store_to_char() (db.cpp) heap-allocates ch->player.title (if the source
// char_file_u has one), ch->player.description (always), and
// ch->player.name (always) via CREATE()+strcpy(). Production code reaches
// store_to_char() only on a char_data whose eventual free_char() call
// releases those same three fields; this test suite's "production
// clear_char()+store_to_char() pair" idiom (per db_save_roundtrip_tests.cpp's
// build_round_trip_character()) instead runs store_to_char() directly on an
// already-stack-allocated char_data, which -- like ScopedClearCharFields
// above -- can't call free_char() on itself. Deliberately a SEPARATE guard
// from ScopedClearCharFields (not folded into it): many fixtures set
// ch.player.name/title to a STRING LITERAL via const_cast *without* ever
// calling store_to_char(), and RELEASE()-ing (free()-ing) a string literal's
// address is a crash, not a leak fix -- so this type is strictly opt-in,
// constructed only at call sites that actually invoke store_to_char() on a
// stack char_data (Phase 5 T6 leak sweep).
struct ScopedStoreToCharFields {
    // The char_data whose store_to_char()-allocated title/description/name
    // this guard releases at scope exit.
    char_data& ch;

    explicit ScopedStoreToCharFields(char_data& character)
        : ch(character)
    {
    }

    ScopedStoreToCharFields(const ScopedStoreToCharFields&) = delete;
    ScopedStoreToCharFields& operator=(const ScopedStoreToCharFields&) = delete;

    ~ScopedStoreToCharFields()
    {
        RELEASE(ch.player.title);
        RELEASE(ch.player.description);
        RELEASE(ch.player.name);
    }
};

// Crash_alias_load() (objsave.cpp) builds ch->specials.alias as a linked
// list (each node CREATE1()'d, each with a CREATE()'d .command string).
// Production code only ever releases that list via free_char()'s
// free_alias_list() call (db.cpp, backlog T2's ownership fix) -- fixtures
// that reach Crash_alias_load() (directly, or via Crash_load()) on an
// already-stack-allocated char_data without ever calling free_char() on it
// must release the list themselves at scope exit, same rationale as
// ScopedClearCharFields/ScopedStoreToCharFields above.
struct ScopedAliasListRelease {
    // The char_data whose specials.alias chain this guard releases (via
    // free_alias_list(), db.cpp) at scope exit.
    char_data& ch;

    explicit ScopedAliasListRelease(char_data& character)
        : ch(character)
    {
    }

    ScopedAliasListRelease(const ScopedAliasListRelease&) = delete;
    ScopedAliasListRelease& operator=(const ScopedAliasListRelease&) = delete;

    ~ScopedAliasListRelease()
    {
        free_alias_list(ch.specials.alias);
        ch.specials.alias = nullptr;
    }
};

// Process-global free list of large ("32KB") output buffers (comm.cpp);
// declared there without an existing header declaration, so test files that
// need it (via ScopedDescriptorLargeOutbufReturn below) get it from here.
extern struct txt_block* bufpool;

// Returns descriptor.large_outbuf to the process-global `bufpool` free list
// at scope exit -- mirrors production's own descriptor-teardown/flush path
// (comm.cpp's process_output()/close_socket(), e.g. "d->large_outbuf->next =
// bufpool; bufpool = d->large_outbuf;"), which returns (never frees) a large
// output buffer once a connection is done with it, for reuse by the next
// connection that overflows its small buffer.
//
// write_to_output() (comm.cpp) transparently promotes a descriptor from its
// small_outbuf to a heap-allocated large_outbuf block whenever captured
// output exceeds SMALL_BUFSIZE -- common in this test suite's longer
// stat/score/info renders. Test descriptor_data fixtures are stack-local and
// never run through production's close path, so without this guard any test
// whose captured output is long enough orphans that block (LeakSanitizer,
// Phase 5 T6) -- in production it would just be sitting in bufpool, reachable
// and reused; here it's simply dropped when the stack descriptor goes out of
// scope.
struct ScopedDescriptorLargeOutbufReturn {
    // The descriptor whose large_outbuf (if any) this guard returns to
    // bufpool at scope exit; never touches the descriptor otherwise.
    descriptor_data& d;

    explicit ScopedDescriptorLargeOutbufReturn(descriptor_data& descriptor)
        : d(descriptor)
    {
    }

    ScopedDescriptorLargeOutbufReturn(const ScopedDescriptorLargeOutbufReturn&) = delete;
    ScopedDescriptorLargeOutbufReturn& operator=(const ScopedDescriptorLargeOutbufReturn&) = delete;

    ~ScopedDescriptorLargeOutbufReturn()
    {
        if (d.large_outbuf) {
            d.large_outbuf->next = bufpool;
            bufpool = d.large_outbuf;
            d.large_outbuf = nullptr;
        }
        // page_string() (modify.cpp) CREATE()s d->showstr_head for any output
        // routed through the pager (do_levels/do_who/etc. for long listings)
        // that doesn't fit in one page; production frees it via
        // close_socket()'s RELEASE(conn_descriptor->showstr_head) once the
        // connection closes (or earlier, once the player finishes paging
        // through it) -- same "test descriptor never runs through
        // production's real teardown" gap as large_outbuf above (Phase 5 T6
        // leak sweep).
        RELEASE(d.showstr_head);
    }
};
