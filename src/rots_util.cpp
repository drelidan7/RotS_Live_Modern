/* rots_util.cpp */
// Platform-pure helpers relocated out of utility.cpp (app tier) into
// rots_platform (L0), entity-seed Task 4: the CREATE/RELEASE allocator
// backers, the str_dup/str_cmp/str_cmp_nullable text helpers, the
// rots_remove/rots_rename_replace POSIX-semantics file ops, and the
// number()/number(int,int) RNG wrapper family (plus its TESTING hook).
// None of these bodies reference a game type or a comm/db/handler header --
// each was already platform-pure in utility.cpp, just physically homed in an
// app-layer TU. Bodies below are byte-verbatim copies of utility.cpp's
// current definitions (see git history pre-Task-4), except where a body used
// a trivial utils.h macro (LOWER in str_cmp/str_cmp_nullable, the CREATE
// expansion inside str_dup) -- those are inlined with a one-line comment at
// the call site, the plan's sanctioned deviation (precedent: rots_log.cpp
// inlining nz() for the same "L0 must not include utils.h" reason).
// Declarations for every symbol here stay in utils.h, unchanged (precedent:
// vmudlog() lives in rots_log.cpp while utils.h declares it too).

#include "platdef.h" /* PREDEF_PLATFORM_WINDOWS + (on Windows) the Win32 API declarations rots_remove()/rots_rename_replace() need */
#include "platform_compat.h" /* declares rots_remove()/rots_rename_replace() */
#include "rots_rng.h"
#include "text_view.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>

namespace {

// Inlined expansion of utils.h's LOWER(c) macro (ASCII-only case fold) --
// rots_util.cpp is an L0/rots_platform TU and must not include utils.h.
// Numerically identical to the macro: both promote to int for the
// subtraction str_cmp()/str_cmp_nullable() do with the result.
char lower_ascii(char c)
{
    return ((c) >= 'A' && (c) <= 'Z') ? ((c) + ('a' - 'A')) : (c);
}

} // namespace

/* This is to work together with CREATE macro, to try fighting
   memory fault crashes.
   */

void* create_pointer = 0;

void* create_function(int elem_size, int elem_num, int line, std::string_view file)
{

    //  printf("want to allocate size=%d, num=%d\n",elem_size,elem_num);

    if (elem_size * elem_num == 0)
        create_pointer = calloc(1, 1);
    else
        create_pointer = calloc(elem_size, elem_num);

    // if (elem_size * elem_num == 0)
    //   create_pointer = malloc(1);
    // else
    //   create_pointer = malloc(elem_size * elem_num);

    if (!create_pointer) {
        const std::string file_owner(rots::text::truncate_at_null(file));
        printf("CREATE: could not allocate memory %d size %d elements at line %d, file %s.\n",
            elem_size, elem_num, line, file_owner.c_str());
        exit(0);
    }
    //   for(i = 0; i<10; i++) j = random();
    //  printf("create_function, return%p\n",create_pointer);
    return create_pointer;
}

void free_function(void* pnt)
{
    create_pointer = pnt;
    //  printf("free_function, pnt=%p",create_pointer);
    if (create_pointer)
        free(create_pointer);
    //   for(i = 0; i<100; i++) j = random();
    //  printf("  free\n");
    create_pointer = 0;
}

/* Create a duplicate of a string */
char* str_dup(const char* source)
{
    if (!source)
        return NULL;

    char* new_string;
    int length = std::strlen(source);

    // Inlined expansion of utils.h's CREATE(result, type, number) macro
    // (rots_util.cpp must not include utils.h): CREATE(new_string, char,
    // ((int)(length / 0x100) + 1) * 0x100) is exactly this create_function()
    // call, since create_function is defined in this same TU.
    new_string = (char*)create_function(sizeof(char) + 1, ((int)(length / 0x100) + 1) * 0x100, __LINE__, __FILE__);

    for (int i = 0; i < length; i++) {
        new_string[i] = source[i];
    }
    new_string[length] = 0;

    return new_string;
}

/* returns: 0 if equal, 1 if arg1 > arg2, -1 if arg1 < arg2  */
/* scan 'till found different or end of both                 */
int str_cmp(std::string_view first, std::string_view second)
{
    // First-null semantics are folded into the loop: past-the-end or an
    // embedded null both read as '\0', exactly where truncate_at_null would
    // have cut -- without pre-scanning either argument.
    for (std::size_t index = 0;; ++index) {
        const char first_char = (index < first.size()) ? first[index] : '\0';
        const char second_char = (index < second.size()) ? second[index] : '\0';
        if (first_char == '\0' || second_char == '\0') {
            if (first_char == second_char) {
                return 0;
            }
            return (first_char == '\0') ? -1 : 1;
        }
        // LOWER(...) inlined as lower_ascii() -- see the anonymous namespace above.
        const int difference = lower_ascii(first_char) - lower_ascii(second_char);
        if (difference < 0) {
            return -1;
        }
        if (difference > 0) {
            return 1;
        }
    }
}

int str_cmp_nullable(const char* first, const char* second)
{
    if (first == nullptr || second == nullptr) {
        if (first == second) {
            return 0;
        }
        return first == nullptr ? -1 : 1;
    }
    // Nullable legacy callers provide null-terminated strings (see the
    // isname_c_string precedent): a raw sentinel walk avoids constructing and
    // scanning two views on the player-table lookup paths.
    for (;; ++first, ++second) {
        // LOWER(...) inlined as lower_ascii() -- see the anonymous namespace above.
        const int difference = lower_ascii(*first) - lower_ascii(*second);
        if (difference != 0) {
            return (difference < 0) ? -1 : 1;
        }
        if (*first == '\0') {
            return 0;
        }
    }
}

// rots_remove: POSIX-remove-semantics deletion on every platform (see
// platform_compat.h -- POSIX remove(3) also deletes empty directories, MSVC's
// CRT remove() refuses them, leaking directories from rollback paths).
int rots_remove(std::string_view path)
{
    const std::string path_owner(rots::text::truncate_at_null(path));
#if defined PREDEF_PLATFORM_WINDOWS
    if (std::remove(path_owner.c_str()) == 0) {
        return 0;
    }

    // CRT remove() rejects a directory with EACCES; retry as a directory the
    // way POSIX remove() falls back to rmdir(). RemoveDirectoryA is the Win32
    // primitive (same header set MoveFileExA above comes from); map its common
    // failures onto errno so callers' strerror(errno) messages stay meaningful.
    const DWORD attributes = GetFileAttributesA(path_owner.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        if (RemoveDirectoryA(path_owner.c_str())) {
            return 0;
        }
        switch (GetLastError()) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
            errno = ENOENT;
            break;
        case ERROR_DIR_NOT_EMPTY:
            errno = ENOTEMPTY;
            break;
        case ERROR_ACCESS_DENIED:
        case ERROR_SHARING_VIOLATION:
            errno = EACCES;
            break;
        default:
            errno = EIO;
            break;
        }
    }
    return -1;
#else
    return std::remove(path_owner.c_str());
#endif
}

// rots_rename_replace: POSIX-replace-semantics rename on every platform (see
// platform_compat.h for the full rationale -- std::rename() refuses to
// overwrite an existing destination on Windows, breaking every temp+rename
// atomic write on the second save of any file).
int rots_rename_replace(std::string_view source_path, std::string_view destination_path)
{
    const std::string source_path_owner(rots::text::truncate_at_null(source_path));
    const std::string destination_path_owner(rots::text::truncate_at_null(destination_path));
#if defined PREDEF_PLATFORM_WINDOWS
    // MoveFileExA + MOVEFILE_REPLACE_EXISTING is the Win32 primitive with
    // exactly POSIX rename()'s replace behavior (atomic on NTFS same-volume
    // moves, which every persistence-layer temp file is -- the temp lives
    // next to its final path by construction).
    if (MoveFileExA(source_path_owner.c_str(), destination_path_owner.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        return 0;
    }

    // Map the common failure causes onto errno so the call sites' existing
    // strerror(errno)-based error messages describe the real problem instead
    // of whatever stale errno was lying around.
    switch (GetLastError()) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
        errno = ENOENT;
        break;
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
        errno = EACCES;
        break;
    default:
        errno = EIO;
        break;
    }
    return -1;
#else
    return std::rename(source_path_owner.c_str(), destination_path_owner.c_str());
#endif
}

#ifdef TESTING
// Test seam: when non-null, number()/number(int,int) consult this hook
// before drawing from the PRNG. Installed only by test_random_utils.cpp,
// which pops a queued value (clamped to [0.0, 1.0)) per call and returns a
// negative sentinel when the queue is empty, telling the caller to fall
// through to the real PRNG below — the same fallthrough the old
// __wrap__Z6numberv/__wrap__Z6numberii linker-wrap seam provided. The symbol
// does not exist in production builds (no -DTESTING there).
double (*rots_test_random_hook)() = nullptr;
#endif

// returns a random number from 0.0 to 1.0
double number()
{
#ifdef TESTING
    if (rots_test_random_hook) {
        double hooked_value = rots_test_random_hook();
        if (hooked_value >= 0.0) {
            return hooked_value;
        }
    }
#endif
    // 2^32 as double; next() is a full 32-bit draw, so this is uniform [0,1).
    return rots_rng::next() / 4294967296.0;
}

/* creates a random number in interval [from;to] */
int number(int from, int to)
{
    if (from == to) {
        return from;
    }
    // Ensure that our to/from is in the proper order.
    if (from > to) {
        std::swap(to, from);
    }

    int upper_end = to - from + 1;
    if (upper_end == 0) {
        //       fprintf(stderr, "SYSERR: number(%d, %d)\n",from,to);
        to = from;
    }

#ifdef TESTING
    if (rots_test_random_hook) {
        double hooked_value = rots_test_random_hook();
        if (hooked_value >= 0.0) {
            return from + static_cast<int>(hooked_value * upper_end);
        }
    }
#endif

    return (rots_rng::next() % upper_end) + from;
}
