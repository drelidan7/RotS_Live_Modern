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
#include "rots/platform/log.h" /* declares mudlog()/BRF + kDiceUnderflowLogLevel -- dice() below */
#include "rots_rng.h"
#include "text_view.h"

#include <cctype>
#include <cerrno>
#include <cstdarg> // va_list/va_start/va_copy/va_end -- rots_asprintf() (placement-seam Task 5)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format> // money_message() below (placement-seam Task 4)
#include <iterator> // std::back_inserter -- money_message() below
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

// log() (global-namespace text logger) is declared in utils.h, an L2 header
// rots_util.cpp must not include (same L0 constraint as this file's other
// relocations). Forward-declared locally instead of pulling that header --
// reshuffle() (placement-seam Task 5) calls it. Defined in rots_log.cpp
// (rots_platform, this same library): a same-tier L0->L0 call, unlike the
// mudlog()/BRF declaration rots/platform/log.h already provides above.
void log(std::string_view message);

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

// Parses (and destructively strips) a legacy "N.keyword" match-ordinal
// prefix from *name in place: on success returns N (>=1) and advances
// *name past the removed prefix (via an overlap-safe memmove -- see the
// inline comment below); returns 0 for a malformed numeric prefix;
// returns 1 (no mutation) when no "." prefix is present at all. Relocated
// verbatim from handler.cpp (placement-seam Task 1's sequencing fix --
// get_char_room, moving to placement.cpp/rots_entity the same task, is
// get_number's only caller inside rots_entity; ranger.cpp's/spec_pro.cpp's/
// act_offe.cpp's pre-existing local `extern` forward declarations are
// untouched and still resolve to this same definition). Declaration:
// utils.h.
int get_number(char** name)
{
    // MAX_INPUT_LENGTH's value (rots/core/types.h, an L1 constant) inlined
    // as a local literal -- rots_util.cpp is an L0/rots_platform TU and
    // must not include rots/core/types.h just for one constant (mirrors
    // this file's lower_ascii()/dice() precedent for the same L0
    // constraint, applied there to a utils.h macro / LEVEL_IMMORT
    // respectively). Unlike dice()'s kDiceUnderflowLogLevel, `number`
    // below is pure internal scratch space (never returned or compared
    // against another layer's copy of the constant), so no static_assert
    // pinning is needed.
    constexpr int kMaxInputLength = 255;

    int i;
    char* ppos;
    char number[kMaxInputLength] = "";

    if ((ppos = strchr(*name, '.'))) {
        *ppos++ = '\0';
        strcpy(number, *name);
        // *name and ppos alias the same caller buffer (ppos = *name + prefix length),
        // so this in-place left-shift needs an overlap-safe copy; strcpy's parameters
        // may not overlap per the C standard, and ASan's strcpy-param-overlap check
        // catches it even though a naive forward byte copy happens to be correct here.
        memmove(*name, ppos, strlen(ppos) + 1);

        for (i = 0; *(number + i); i++)
            if (!isdigit(*(number + i)))
                return (0);

        return (atoi(number));
    }

    return (1);
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

/* simulates dice roll */
// Relocated verbatim from utility.cpp (world-seed Task 1), except two
// deviations forced by rots_util.cpp being an L0/rots_platform TU: TRUE
// (utils.h macro) inlined as 1, and LEVEL_IMMORT (rots/core/types.h, an L1
// constant) replaced with rots::log::kDiceUnderflowLogLevel -- the same
// hard-coded-plus-static_assert technique rots::log::kVmudlogBroadcastLevel
// already uses (see rots/platform/log.h; utility.cpp carries the
// static_assert pinning both constants to their real values).
int dice(int number, int size)
{
    int r;
    int sum = 0;

    //   assert(size >= 1);
    if (size < 1) {
        mudlog("Dice rolled with size < 1!", BRF, rots::log::kDiceUnderflowLogLevel, 1);
        return 0;
    }

    for (r = 1; r <= number; r++) {
        sum += (rots_rng::next() % size) + 1;
    }

    return (sum);
}

/* a function to scan for "all" or "all.x" */
// Relocated verbatim from handler.cpp (placement-seam Task 4; census
// verdict MOVE-OTHER(platform)). FIND_ALL/FIND_ALLDOT/FIND_INDIV
// (handler.h -- an L2 header rots_util.cpp must not include, same
// constraint as get_number()'s MAX_INPUT_LENGTH above) inlined as local
// constexpr ints -- their values (1/2/0) are long-stable CircleMUD
// find-mode constants, not expected to change.
int find_all_dots(char* arg)
{
    constexpr int kFindIndiv = 0;
    constexpr int kFindAll = 1;
    constexpr int kFindAllDot = 2;

    if (!strcmp(arg, "all"))
        return kFindAll;
    else if (!strncmp(arg, "all.", 4)) {
        strcpy(arg, arg + 4);
        return kFindAllDot;
    } else
        return kFindIndiv;
}

// Relocated verbatim from handler.cpp (placement-seam Task 4; census
// verdict MOVE-OTHER(platform)). COPP_IN_GOLD/COPP_IN_SILV
// (rots/core/types.h -- an L1 header rots_util.cpp must not include, same
// constraint as get_number()'s MAX_INPUT_LENGTH above) inlined as local
// constexpr ints, matching their #define values (1000/100).
char* money_message(int sum, int mode)
{
    constexpr int kCoppInGold = 1000;
    constexpr int kCoppInSilv = 100;

    static char moneystr[100];
    int g, s, c;

    *moneystr = 0;

    if (sum < 0) {
        strcpy(moneystr, std::format("{} copper coins", sum).c_str());
        return moneystr;
    }

    g = sum / kCoppInGold;
    c = sum % kCoppInGold;
    s = c / kCoppInSilv;
    c = c % kCoppInSilv;

    std::string out;
    if (g)
        std::format_to(std::back_inserter(out), "{} gold", g);
    if (g && c && s)
        out += ", ";
    if (!c && s && g)
        out += " and ";
    if (s)
        std::format_to(std::back_inserter(out), "{} silver", s);
    if ((g || s) && c)
        out += " and ";
    if (c || (!sum))
        std::format_to(std::back_inserter(out), "{} copper", c);

    if (mode)
        std::format_to(std::back_inserter(out), " coin{}",
            ((g == 1) && (s == 1)) || c == 1 ? "" : "s");

    strcpy(moneystr, out.c_str());
    return moneystr;
}

//============================================================================
// string_to_new_value()/number(double)/number_d()/rots_asprintf()/
// strn_cmp()/strn_cmp_nullable()/sprintbit()/sprinttype()/nth()/
// strcpy_lang()/reshuffle() relocated verbatim from utility.cpp
// (placement-seam Task 5; census verdict MOVE-OTHER(platform) for all --
// see placement-census.md's utility.cpp table). All are pure text/RNG/
// formatting helpers with no game-type dependency. rots_asprintf() is
// declared in platform_compat.h and now defined here, matching that header's
// existing rots_remove()/rots_rename_replace() precedent (declared in
// platform_compat.h, defined in this same TU) -- the census's tentative
// "platform_compat" target-TU note is resolved in rots_util.cpp's favor by
// that precedent (see task-5-report.md). strn_cmp()/strn_cmp_nullable()
// reuse this file's existing lower_ascii() helper for utils.h's LOWER macro
// (same precedent str_cmp()/str_cmp_nullable() above already established).
// sprintbit() inlines utils.h's IS_SET macro and rots/core/room.h's
// BFS_MARK constant, the same "L0 must not include L1/L2 headers" precedent
// as get_number()'s MAX_INPUT_LENGTH/find_all_dots()'s FIND_ALL family
// above. real_time_passed()/mud_time_passed()/day_to_str() are NOT in this
// batch -- see utility.cpp's own comments at their original locations for
// the blocking finding (time_info_data is an L1 rots/core/types.h type;
// day_to_str() additionally reads month_name[], an external rots_core
// symbol -- a genuine PlatformLayerAcyclicity-breaking upward edge the
// census's "upward refs: none" missed). Declarations unchanged (utils.h /
// platform_compat.h, except do_squareroot()-adjacent file-local functions
// n/a here).
//============================================================================

int string_to_new_value(char* arg, int* value)
{
    while (*arg && (*arg <= ' '))
        arg++;

    if (!*arg)
        return *value;

    if (isdigit(*arg))
        *value = atoi(arg);
    if (*arg == '+')
        *value += atoi(arg + 1);
    if (*arg == '-')
        *value -= atoi(arg + 1);
    if ((*arg == 'p') || (*arg == 'P'))
        *value |= 1 << atoi(arg + 1);
    if ((*arg == 'm') || (*arg == 'M'))
        *value &= ~(1 << atoi(arg + 1));

    return *value;
}

// returns a random number from 0.0 to max
double number(double max)
{
    return number() * max;
}

// returns a random number in interval [from;to] */
double number_d(double from, double to)
{
    if (from > to) {
        std::swap(from, to);
    }

    return number(to) + from;
}

// rots_asprintf: portable stand-in for the asprintf() extension (see platform_compat.h
// for the full ownership-contract writeup). Sizes the formatted output with a
// zero-length vsnprintf pass (which returns the would-be length, excluding the NUL,
// per the C99/POSIX contract), allocates exactly that many bytes plus one for the NUL,
// then formats into it for real. A va_list is invalidated after any va_arg use
// including the "measure" pass, so it must be va_copy'd before that pass and the
// original consumed only once, by the final vsnprintf.
int rots_asprintf(char** out, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    va_list size_args;
    va_copy(size_args, args);
    const int needed = vsnprintf(nullptr, 0, fmt, size_args);
    va_end(size_args);

    if (needed < 0) {
        va_end(args);
        *out = nullptr;
        return -1;
    }

    char* buffer = (char*)malloc((size_t)needed + 1);
    if (buffer == nullptr) {
        va_end(args);
        *out = nullptr;
        return -1;
    }

    const int written = vsnprintf(buffer, (size_t)needed + 1, fmt, args);
    va_end(args);

    if (written < 0) {
        free(buffer);
        *out = nullptr;
        return -1;
    }

    *out = buffer;
    return written;
}

/* returns: 0 if equal, 1 if arg1 > arg2, -1 if arg1 < arg2  */
/* scan 'till found different, end of both, or n reached     */
int strn_cmp(std::string_view first, std::string_view second, int count)
{
    if (count <= 0) {
        return 0;
    }
    const std::size_t comparison_limit = static_cast<std::size_t>(count);
    for (std::size_t index = 0; index < comparison_limit; ++index) {
        const char first_char = (index < first.size()) ? first[index] : '\0';
        const char second_char = (index < second.size()) ? second[index] : '\0';
        if (first_char == '\0' || second_char == '\0') {
            if (first_char == second_char) {
                return 0;
            }
            return (first_char == '\0') ? -1 : 1;
        }
        // LOWER macro inlined as lower_ascii() -- see the anonymous namespace above.
        const int difference = lower_ascii(first_char) - lower_ascii(second_char);
        if (difference < 0) {
            return -1;
        }
        if (difference > 0) {
            return 1;
        }
    }
    return 0;
}

int strn_cmp_nullable(const char* first, const char* second, int count)
{
    if (first == nullptr || second == nullptr) {
        if (first == second) {
            return 0;
        }
        return first == nullptr ? -1 : 1;
    }
    if (count <= 0) {
        return 0;
    }
    for (int index = 0; index < count; ++first, ++second, ++index) {
        // LOWER macro inlined as lower_ascii() -- see the anonymous namespace above.
        const int difference = lower_ascii(*first) - lower_ascii(*second);
        if (difference != 0) {
            return (difference < 0) ? -1 : 1;
        }
        if (*first == '\0') {
            return 0;
        }
    }
    return 0;
}

/*
 * Sprintbit now contains an extra variable (int var) so it can
 * discern when identify is using it.
 */
void sprintbit(long vektor, const std::string_view names[], char* result, int var)
{
    // BFS_MARK (rots/core/room.h -- an L1 header rots_util.cpp must not
    // include, same constraint as get_number()'s MAX_INPUT_LENGTH above)
    // inlined as a local constexpr, matching its #define value (1 << 11).
    constexpr long kBfsMark = 1 << 11;

    long nr;
    int count;

    count = 0;

    if (vektor < 0) {
        strcpy(result, "SPRINTBIT ERROR!");
        return;
    }

    if (vektor == 0) {
        strcpy(result, var != 0 ? "has no additional attributes. " : "<NONE>");
        return;
    }

    // Composed here (rather than strcat-chained straight into `result`) so
    // the "did the loop append anything" check below (the NOFLAGS fallback)
    // is a plain std::string::empty() instead of testing *result -- result
    // itself is only written once, at the very end, via the same unbounded
    // strcpy the original code used (callers still supply their own
    // sufficiently-sized buffer; no size is available here to bound against).
    std::string composed;
    for (nr = 0; vektor; vektor >>= 1) {
        // IS_SET macro (utils.h) inlined as its own (flag)&(bit) definition --
        // rots_util.cpp must not include utils.h (this file's lower_ascii()/
        // str_cmp() precedent, same L0 constraint).
        if ((1 & vektor) && (vektor != kBfsMark)) {
            if (names[nr] != "\n") {
                /*
                 * Where the variable passed in is not 0
                 * then identify is using sprintbit
                 * The block of code contained here is used only
                 * for identify.
                 */
                if (var != 0) {
                    if (var == 2) {
                        composed += (count == 0) ? " " : " and ";
                    } else {
                        composed += (count == 0) ? "has the following attributes.\r\n" : ".\r\n";
                    }
                } else /* normal sprintbit resumes here */
                    composed += " ";
                composed += names[nr];
                count++;
            } else {
                composed += "UNDEFINE ";
            }
        }
        if (names[nr] != "\n")
            nr++;
    }

    if (composed.empty())
        composed += "NOFLAGS";

    composed += ".";
    strcpy(result, composed.c_str());
}

void sprinttype(int type, const std::string_view names[], char* result)
{
    int nr;

    for (nr = 0; names[nr] != "\n"; nr++)
        ;

    const std::string_view value = (type < nr) ? names[type] : "UNDEFINED";
    strcpy(result, value.data());
}

/*
 * Return the string corresponding to the "nth" number.
 * I.e., if n is 1, then the string is "1st", if n is 2
 * the string is "2nd", and so on.
 *
 * This string is dynamically allocated and must be freed
 * by the caller.
 */
char* nth(int n)
{
    const char *s;
    char* r;
    const char* first = "st";
    const char* second = "nd";
    const char* third = "rd";
    const char* other = "th";

    /* 11, 12 and 13 don't follow the general rule */
    if (n == 11 || n == 12 || n == 13)
        s = other;
    else {
        switch (n % 10) {
        case 1:
            s = first;
            break;
        case 2:
            s = second;
            break;
        case 3:
            s = third;
            break;
        default:
            s = other;
        }
    }

    rots_asprintf(&r, "%d%s", n, s);

    return r;
}

char* strcpy_lang(char* str1, char* str2, byte freq, int maxlen)
{
    int i, len;

    len = strlen(str2);
    if (len > maxlen)
        len = maxlen;

    for (i = 0; i < len; i++) {
        if ((number(1, 100) > freq) && isalpha(str2[i])) {
            if (isupper(str2[i]))
                str1[i] = number(65, 90);
            else
                str1[i] = number(97, 122);
        } else
            str1[i] = str2[i];
    }
    str1[i] = 0;

    return str1;
}

void reshuffle(int* arr, int len)
{
    int tmp, tmp2, num;
    int newarr[255];
    char flags[255];

    if (len >= 255) {
        len = 254;
        log("Reshuffle called for more than 255 elements.");
    }

    for (tmp = 0; tmp < len; tmp++)
        flags[tmp] = 1;

    for (tmp = 0; tmp < len; tmp++) {
        num = 1 + number(0, len - tmp - 1);
        for (tmp2 = 0; (tmp2 < len) && num; tmp2++)
            num -= flags[tmp2];
        if (tmp2 >= len + 1) {
            tmp2 = len - 1;
            log("trouble in reshuffle.");
        }
        flags[tmp2 - 1] = 0;
        newarr[tmp] = arr[tmp2 - 1];
    }
    for (tmp = 0; tmp < len; tmp++)
        arr[tmp] = newarr[tmp];
    return;
}
