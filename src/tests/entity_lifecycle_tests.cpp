#include "../comm.h"
#include "../entity_hooks.h"
#include "../handler.h"
#include "rots/core/character.h"
#include "rots/core/types.h"

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

// ---------------------------------------------------------------------------
// target_data::operator=()/cleanup()'s TARGET_TEXT txt-block-pool traffic
// (entity_lifecycle.cpp, relocated verbatim from interpre.cpp -- world-seed
// Task 2). Zero prior coverage: olog_hai_tests.cpp builds a target_data with
// a TARGET_TEXT ptr.text directly (bypassing operator=()/cleanup() entirely)
// to test is_target_valid(), but nothing exercises the pool-hook dispatch
// itself -- world-seed Task 5b, Candidate 2.
//
// CRITICAL HYGIENE: gtest_main.cpp registers comm.cpp's REAL
// get_from_txt_block_pool()/put_to_txt_block_pool() as entity_hooks.h's
// txt-block-pool hook pair for the whole test binary (register_txt_block_pool_hooks(),
// called once in main()). Every test below swaps in a test-owned pool via
// ScopedTxtBlockPoolHooks and that fixture's destructor unconditionally
// re-registers the real pair before returning control to the rest of the
// suite -- see ScopedTxtBlockPoolHooks below.
// ---------------------------------------------------------------------------

namespace {

// Test double for comm.cpp's txt_block_pool -- a private LIFO free list plus
// call counters, so a test can assert exactly which pool operation
// target_data::operator=()/cleanup() triggered without pulling in comm.cpp's
// descriptor/output-buffer machinery (bufpool/large_outbuf) the real pool is
// entangled with.
class TestTxtBlockPool {
public:
    // Number of times the get-hook dispatched to this pool; asserted to
    // confirm operator=() acquires exactly one block per TARGET_TEXT copy
    // (and zero for a non-TARGET_TEXT copy).
    int get_calls = 0;
    // Number of times the put-hook dispatched to this pool; asserted the
    // same way for cleanup()'s release path.
    int put_calls = 0;
    // The most recent block handed back via the put-hook, so a test can
    // confirm cleanup() released the specific block operator=() had
    // acquired -- not some other one.
    txt_block* last_put = nullptr;

    txt_block* Acquire()
    {
        ++get_calls;
        if (m_free_list != nullptr) {
            txt_block* block = m_free_list;
            m_free_list = block->next;
            block->next = nullptr;
            return block;
        }
        // Sized the same as comm.cpp's real get_from_txt_block_pool()
        // (CREATE(pnew->text, char, MAX_INPUT_LENGTH)), so operator=()'s
        // strcpy() into this block can never overrun it.
        txt_block* block = new txt_block;
        block->text = new char[MAX_INPUT_LENGTH];
        block->text[0] = '\0';
        block->next = nullptr;
        m_allocated.push_back(block);
        return block;
    }

    void Release(txt_block* block)
    {
        ++put_calls;
        last_put = block;
        block->next = m_free_list;
        m_free_list = block;
    }

    ~TestTxtBlockPool()
    {
        // Every block this pool ever handed out is either back on
        // m_free_list (properly released) or still held by a test_data
        // instance the test forgot to cleanup() (a bug in the test, not this
        // fixture) -- free everything reachable from m_allocated either way,
        // so a leaked-by-the-test block does not also fail the ASan gate.
        for (txt_block* block : m_allocated) {
            delete[] block->text;
            delete block;
        }
    }

private:
    // Released blocks awaiting reuse, most-recently-released first -- same
    // LIFO discipline as comm.cpp's real txt_block_pool.
    txt_block* m_free_list = nullptr;
    // Every block this pool has ever allocated, regardless of its current
    // free/in-use state; walked by the destructor to release the backing
    // storage exactly once per block.
    std::vector<txt_block*> m_allocated;
};

// The pool the file-scope trampoline functions below dispatch to; set by
// whichever ScopedTxtBlockPoolHooks is currently alive. entity_hooks.h's hook
// typedefs are bare function pointers (get_txt_block_fn = txt_block* (*)()),
// not std::function, so the trampolines cannot capture a pool reference
// directly -- this indirection stands in for that capture.
TestTxtBlockPool* g_active_pool = nullptr;

txt_block* test_get_txt_block_from_pool()
{
    return g_active_pool->Acquire();
}

void test_put_txt_block_to_pool(txt_block* block)
{
    g_active_pool->Release(block);
}

// RAII hook-restore hygiene (Task 5b brief, Candidate 2 CRITICAL HYGIENE
// requirement). Swaps entity_hooks.h's txt-block-pool hook pair to point at
// a test-owned TestTxtBlockPool for this fixture's scope, and unconditionally
// restores comm.cpp's real hooks (register_txt_block_pool_hooks(), comm.h) on
// destruction -- including when the test body fails an assertion and unwinds
// early -- so no later test in the binary ever observes a stub still
// installed.
class ScopedTxtBlockPoolHooks {
public:
    explicit ScopedTxtBlockPoolHooks(TestTxtBlockPool& pool)
        // Whatever g_active_pool held before this fixture ran -- always
        // nullptr in practice, since these tests never nest -- restored so a
        // dispatch that somehow raced past teardown cannot dereference a
        // dangling pointer instead of crashing loudly.
        : m_previous_active_pool(g_active_pool)
    {
        g_active_pool = &pool;
        rots::entity::set_get_txt_block_pool_hook(&test_get_txt_block_from_pool);
        rots::entity::set_put_txt_block_pool_hook(&test_put_txt_block_to_pool);
    }

    ~ScopedTxtBlockPoolHooks()
    {
        register_txt_block_pool_hooks();
        g_active_pool = m_previous_active_pool;
    }

private:
    TestTxtBlockPool* m_previous_active_pool;
};

// Owns a plain (non-pooled) txt_block used only as an operator=() SOURCE.
// operator=() (entity_lifecycle.cpp) only reads t2.ptr.text->text for a
// TARGET_TEXT source -- it never pool-manages the source side -- so this
// does not need to come from TestTxtBlockPool at all (mirrors
// olog_hai_tests.cpp's existing ad hoc stack txt_block for the same reason).
class SourceTextBlock {
public:
    explicit SourceTextBlock(std::string_view text)
    {
        std::memset(m_storage, 0, sizeof(m_storage));
        const std::size_t copy_size = std::min(text.size(), sizeof(m_storage) - 1);
        std::memcpy(m_storage, text.data(), copy_size);
        block.text = m_storage;
        block.next = nullptr;
    }

    // The txt_block a test points a source target_data's ptr.text at;
    // std::memcpy above keeps its text[] buffer null-terminated regardless
    // of the requested text's length.
    txt_block block {};

private:
    // Owned text storage for `block` above -- sized the same as the pool's
    // real blocks (MAX_INPUT_LENGTH) even though this block never goes
    // through the pool, so a test can freely pass pool-sized strings.
    char m_storage[MAX_INPUT_LENGTH];
};

} // namespace

TEST(TargetDataPoolHooks, OperatorAssignDeepCopiesTargetTextThroughAcquiredBlock)
{
    TestTxtBlockPool pool;
    ScopedTxtBlockPoolHooks hooks(pool);
    SourceTextBlock source_text("look at the shiny sword");

    target_data source;
    source.type = TARGET_TEXT;
    source.ptr.text = &source_text.block;
    source.ch_num = 42;
    source.choice = 7;

    target_data destination;
    destination = source;

    EXPECT_EQ(pool.get_calls, 1);
    EXPECT_EQ(pool.put_calls, 0);
    EXPECT_EQ(destination.type, TARGET_TEXT);
    EXPECT_EQ(destination.ch_num, 42);
    EXPECT_EQ(destination.choice, 7);
    ASSERT_NE(destination.ptr.text, nullptr);
    EXPECT_NE(destination.ptr.text, &source_text.block);
    EXPECT_STREQ(destination.ptr.text->text, "look at the shiny sword");

    // Mutating the source afterward must not affect the destination's copy --
    // operator=() strcpy()s into a distinct, pool-acquired block rather than
    // aliasing the source's txt_block.
    std::strcpy(source_text.block.text, "mutated");
    EXPECT_STREQ(destination.ptr.text->text, "look at the shiny sword");

    txt_block* acquired = destination.ptr.text;
    destination.cleanup();
    EXPECT_EQ(pool.put_calls, 1);
    EXPECT_EQ(pool.last_put, acquired);
}

TEST(TargetDataPoolHooks, CleanupReturnsBlockAndResetsTypeAndChNumButNotChoice)
{
    TestTxtBlockPool pool;
    ScopedTxtBlockPoolHooks hooks(pool);
    SourceTextBlock source_text("shout for help");

    target_data source;
    source.type = TARGET_TEXT;
    source.ptr.text = &source_text.block;
    source.ch_num = 5;
    source.choice = 9;

    target_data destination;
    destination = source;
    txt_block* acquired = destination.ptr.text;
    ASSERT_EQ(pool.get_calls, 1);

    destination.cleanup();

    EXPECT_EQ(pool.put_calls, 1);
    EXPECT_EQ(pool.last_put, acquired);
    EXPECT_EQ(destination.type, TARGET_NONE);
    EXPECT_EQ(destination.ptr.other, nullptr);
    EXPECT_EQ(destination.ch_num, 0);
    // cleanup() (entity_lifecycle.cpp) resets type/ptr/ch_num but --
    // unlike a "clears everything" reading of its doc comment -- deliberately
    // does not touch choice. Pinning that as observed, current behavior.
    EXPECT_EQ(destination.choice, 9);
}

TEST(TargetDataPoolHooks, OperatorAssignNonTargetTextCopiesPointerWithoutPoolTraffic)
{
    TestTxtBlockPool pool;
    ScopedTxtBlockPoolHooks hooks(pool);
    char_data fake_char_slot {};

    target_data source;
    source.type = TARGET_CHAR;
    source.ptr.ch = &fake_char_slot;
    source.ch_num = 11;
    source.choice = 3;

    target_data destination;
    destination = source;

    EXPECT_EQ(pool.get_calls, 0);
    EXPECT_EQ(pool.put_calls, 0);
    EXPECT_EQ(destination.type, TARGET_CHAR);
    EXPECT_EQ(destination.ptr.ch, &fake_char_slot);
    EXPECT_EQ(destination.ch_num, 11);
    EXPECT_EQ(destination.choice, 3);

    // A non-TARGET_TEXT cleanup() also performs no pool traffic.
    destination.cleanup();
    EXPECT_EQ(pool.get_calls, 0);
    EXPECT_EQ(pool.put_calls, 0);
    EXPECT_EQ(destination.type, TARGET_NONE);
}

TEST(TargetDataPoolHooks, OperatorAssignReleasesThePreviousBlockBeforeAcquiringTheNextOne)
{
    TestTxtBlockPool pool;
    ScopedTxtBlockPoolHooks hooks(pool);
    SourceTextBlock first_text("first");
    SourceTextBlock second_text("second");

    target_data first_source;
    first_source.type = TARGET_TEXT;
    first_source.ptr.text = &first_text.block;

    target_data second_source;
    second_source.type = TARGET_TEXT;
    second_source.ptr.text = &second_text.block;

    target_data destination;
    destination = first_source;
    txt_block* first_acquired = destination.ptr.text;
    ASSERT_EQ(pool.get_calls, 1);

    // operator=()'s leading cleanup() call must release the block already
    // held by `destination` BEFORE acquiring a new one for the incoming
    // TARGET_TEXT value -- otherwise the first block would leak (get_calls
    // permanently outrunning put_calls) instead of round-tripping through
    // the pool.
    destination = second_source;

    EXPECT_EQ(pool.get_calls, 2);
    EXPECT_EQ(pool.put_calls, 1);
    EXPECT_EQ(pool.last_put, first_acquired);
    EXPECT_STREQ(destination.ptr.text->text, "second");
    // TestTxtBlockPool's free list is LIFO like comm.cpp's real pool, so the
    // just-released first block is exactly what the second acquire hands
    // back.
    EXPECT_EQ(destination.ptr.text, first_acquired);

    destination.cleanup();
    EXPECT_EQ(pool.put_calls, 2);
}

// ---------------------------------------------------------------------------
// extract_char() hook (RE-HOMED from combat_hooks.{h,cpp}/
// combat_hooks_tests.cpp, l4-seed wave Task 1; l4-task-1-brief.md Step 2a;
// l4-census.md section 3.4). Originally landed as CombatHooksExtractChar in
// combat_hooks_tests.cpp (combat-pilot wave Task 4b); moved here verbatim
// with rots::combat:: updated to rots::entity:: throughout, since
// extract_char() itself moved from combat_hooks.h/.cpp to
// entity_hooks.h/entity_lifecycle.cpp. CONSUMER-FREE at original landing --
// fight.cpp's three call sites now dispatch through this hook for real
// (combat-pilot wave Task 5); this re-home does not change that. Same
// "registered stub receives args intact; unregistered default semantics
// asserted" discriminator shape as this file's txt-block-pool suite above.
// ---------------------------------------------------------------------------

namespace {

struct RecordedExtractCharCall {
    char_data* ch = nullptr;
    int new_room = 0;
    bool called = false;
};

RecordedExtractCharCall g_recorded_extract_char_call;

void recording_extract_char_stub(char_data* ch, int new_room)
{
    g_recorded_extract_char_call = RecordedExtractCharCall { ch, new_room, true };
}

// Swaps entity_hooks.h's extract_char hook, then restores the REAL
// handler.cpp forwarder via register_extract_char_hook() on destruction --
// same restore-via-real-registrar shape as this tree's other Scoped*
// fixtures.
class ScopedExtractCharHook {
public:
    explicit ScopedExtractCharHook(rots::entity::extract_char_fn hook)
    {
        rots::entity::set_extract_char_hook(hook);
    }

    ~ScopedExtractCharHook() { register_extract_char_hook(); }

    ScopedExtractCharHook(const ScopedExtractCharHook&) = delete;
    ScopedExtractCharHook& operator=(const ScopedExtractCharHook&) = delete;
};

} // namespace

// rots::entity::extract_char(ch, new_room) -- DISCRIMINATOR: a recording
// stub proves the 2-arg dispatch overload forwards both arguments intact.

TEST(ExtractCharHook, TwoArgDispatchReachesARegisteredStubWithArgsIntact)
{
    g_recorded_extract_char_call = RecordedExtractCharCall {};
    ScopedExtractCharHook scoped(recording_extract_char_stub);
    char_data character {};

    rots::entity::extract_char(&character, 7);

    EXPECT_TRUE(g_recorded_extract_char_call.called)
        << "Expected the registered stub to have been reached.";
    EXPECT_EQ(g_recorded_extract_char_call.ch, &character);
    EXPECT_EQ(g_recorded_extract_char_call.new_room, 7);
}

// DISCRIMINATOR: the 1-arg overload forwards to the 2-arg overload with the
// -1 sentinel, mirroring handler.cpp's own extract_char(ch) ->
// extract_char(ch, -1) forward exactly (pilot-census.md section 3.6).

TEST(ExtractCharHook, OneArgDispatchForwardsWithNegativeOneSentinel)
{
    g_recorded_extract_char_call = RecordedExtractCharCall {};
    ScopedExtractCharHook scoped(recording_extract_char_stub);
    char_data character {};

    rots::entity::extract_char(&character);

    EXPECT_TRUE(g_recorded_extract_char_call.called);
    EXPECT_EQ(g_recorded_extract_char_call.ch, &character);
    EXPECT_EQ(g_recorded_extract_char_call.new_room, -1)
        << "Expected the 1-arg overload to reach the stub with handler.h:197's own sentinel "
           "default (-1), matching the real extract_char(ch) -> extract_char(ch, -1) forward.";
}

TEST(ExtractCharHook, DispatchDefaultsToANoOpWhenUnregistered)
{
    g_recorded_extract_char_call = RecordedExtractCharCall {};
    ScopedExtractCharHook unregistered(nullptr);
    char_data character {};

    rots::entity::extract_char(&character, 7);

    EXPECT_FALSE(g_recorded_extract_char_call.called)
        << "Expected an unregistered extract_char hook to leave the (unrelated) stub's own "
           "recording flag untouched -- the real forwarder never ran, and the tripwire default "
           "is a logged no-op, not a call to any stub.";
}
