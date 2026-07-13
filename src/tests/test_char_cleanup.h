#pragma once

#include "../db.h"
#include "../structs.h"
#include "../utils.h"

// clear_char() (db.cpp) heap-allocates ch->profs (always, via CREATE1) and,
// when mode != MOB_ISNPC, also ch->skills and ch->knowledge (via CREATE(),
// which wraps create_function()/calloc). Production code always eventually
// calls free_char() (db.cpp), which releases those same three fields via the
// RELEASE() macro (utils.h) before freeing the char_data itself.
//
// Test fixtures instead call clear_char() directly on an already
// stack-allocated char_data (documented as safe by clear_char()'s own
// comment: its placement-new has nothing live to leak/double-free on a fresh
// stack object). But they can't call free_char() on that object -- its
// RELEASE(ch) would free() a stack address -- so without this guard,
// ch->profs/skills/knowledge leak on every such test (LeakSanitizer, Phase 5
// T6 sweep).
//
// Usage: construct immediately after clear_char(&ch, mode). If the fixture
// is about to overwrite ch.knowledge with a non-owned pointer (e.g. a local
// byte[MAX_SKILLS] array, a common pattern in this test suite), call
// release_knowledge_now() first so the heap allocation clear_char() gave it
// isn't orphaned by the overwrite instead of released.
struct ScopedClearCharFields {
    // The char_data whose clear_char()-allocated fields this guard owns the
    // release of; never the object itself (that stays stack-owned by the
    // caller).
    char_data& ch;

    explicit ScopedClearCharFields(char_data& character)
        : ch(character)
    {
    }

    ScopedClearCharFields(const ScopedClearCharFields&) = delete;
    ScopedClearCharFields& operator=(const ScopedClearCharFields&) = delete;

    // Releases ch.knowledge right away, before a later assignment replaces
    // the pointer with one this guard doesn't own (else clear_char()'s
    // allocation is orphaned at that assignment -- leaked either way, but
    // NOT calling this first would otherwise leave the destructor below
    // RELEASE()-ing, i.e. free()-ing, whatever non-owned pointer (e.g. a
    // stack array) the fixture later assigned into ch.knowledge -- a
    // heap-corruption bug, not just a leak). Sets m_knowledge_released so
    // the destructor skips its own RELEASE(ch.knowledge) afterward.
    void release_knowledge_now()
    {
        RELEASE(ch.knowledge);
        m_knowledge_released = true;
    }

    // Same idea as release_knowledge_now(), for ch.profs -- some fixtures
    // (e.g. act_wiz_format_tests.cpp's PcTargetContext) overwrite
    // character.profs with the address of a stack char_prof_data member
    // right after clear_char(), which would otherwise both leak the heap
    // allocation AND, without this guard, have the destructor free() the
    // stack member's address.
    void release_profs_now()
    {
        RELEASE(ch.profs);
        m_profs_released = true;
    }

    ~ScopedClearCharFields()
    {
        if (!m_profs_released)
            RELEASE(ch.profs);
        RELEASE(ch.skills);
        if (!m_knowledge_released)
            RELEASE(ch.knowledge);
    }

private:
    // Set by release_knowledge_now()/release_profs_now() once the
    // corresponding field has already been released (and possibly
    // overwritten with a non-owned pointer by the caller) -- guards the
    // destructor against releasing that pointer too.
    bool m_knowledge_released = false;
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
