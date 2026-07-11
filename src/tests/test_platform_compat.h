#pragma once

// test_platform_compat.h: portable stand-ins for POSIX-only facilities used by
// test *fixtures* (TemporaryDirectory, ScopedStderrRedirect) across several
// tests/*.cpp files. Kept separate from ../platform_compat.h (Phase 3 Task 5)
// because these are test-scaffolding-only -- nothing in ROTS_SERVER_SOURCES /
// the ageland binary needs them, so they live here instead of growing the
// production compat surface, and are implemented inline (header-only) so no
// new source file needs adding to either build system's test source list.

#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <random>

#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

// MSVC's CRT has no STDERR_FILENO constant, but its low-level (_open/_dup/
// _dup2-family) file descriptor table numbers stdin/stdout/stderr 0/1/2 the
// same way POSIX does, so the historical POSIX value is exactly right here too.
#if defined(_WIN32) && !defined(STDERR_FILENO)
#define STDERR_FILENO 2
#endif

// rots_mkdtemp: portable replacement for POSIX mkdtemp(3), used by every
// TemporaryDirectory test fixture. `path_template` must end in six literal
// 'X' characters (identical contract to mkdtemp(3)): on success those six
// characters are overwritten in place with a name that didn't already exist
// (the directory itself is created before returning), and `path_template` is
// returned; returns nullptr on failure. POSIX: a direct passthrough to
// ::mkdtemp. Windows: the CRT has no mkdtemp/mkdtemp_s equivalent at all
// (only _mktemp_s, which substitutes the X's but never creates anything and
// never retries on a collision) -- reimplemented directly as substitute +
// create + retry-on-EEXIST, the same algorithm glibc's own mkdtemp() uses.
// Not a cryptographic RNG (this only needs to avoid same-process collisions
// among a handful of concurrently-running test fixtures, not resist an
// attacker) -- std::random_device rather than the game's rots_rng, since this
// is test scaffolding, not gameplay the characterization goldens pin.
inline char* rots_mkdtemp(char* path_template)
{
#if defined(_WIN32)
    const size_t length = std::strlen(path_template);
    if (length < 6 || std::strcmp(path_template + length - 6, "XXXXXX") != 0) {
        errno = EINVAL;
        return nullptr;
    }

    // Every fixture's template hardcodes a "/tmp/..." prefix; that parent
    // directory exists on any POSIX host but not on a Windows CI runner
    // ("/tmp" there resolves to <current drive>:\tmp, which nothing
    // pre-creates). Creating the missing parent chain here keeps the
    // fixtures' in-place template-buffer contract (the substituted name must
    // fit the caller's char array) instead of redirecting to %TEMP%, whose
    // longer path wouldn't.
    {
        const std::filesystem::path parent = std::filesystem::path(path_template).parent_path();
        std::error_code ec;
        if (!parent.empty())
            std::filesystem::create_directories(parent, ec);
        if (ec) {
            errno = ENOENT;
            return nullptr;
        }
    }

    static constexpr char kAlphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::random_device random_source;
    std::uniform_int_distribution<int> distribution(0, static_cast<int>(sizeof(kAlphabet) - 2));

    for (int attempt = 0; attempt < 100; ++attempt) {
        for (size_t index = length - 6; index < length; ++index)
            path_template[index] = kAlphabet[distribution(random_source)];

        if (_mkdir(path_template) == 0)
            return path_template;

        if (errno != EEXIST)
            return nullptr;
    }

    return nullptr;
#else
    return ::mkdtemp(path_template);
#endif
}

// rots_dup / rots_dup2 / rots_close_fd: portable replacements for the POSIX
// descriptor-duplication trio used only by ScopedStderrRedirect
// (db_loader_tests.cpp / interpre_account_menu_tests.cpp) to capture stderr
// output to a file for the duration of a test. POSIX: direct passthroughs to
// dup()/dup2()/close(). Windows: MSVC spells the identical CRT low-level I/O
// facility _dup()/_dup2()/_close() (declared in <io.h>); success/failure
// semantics match at every call site here (all of which only test `>= 0` on
// dup/dup2, a check both platforms' return-value conventions satisfy
// identically even though _dup2's success value, unlike dup2(2)'s, is always
// 0 rather than the new descriptor).
inline int rots_dup(int file_descriptor)
{
#if defined(_WIN32)
    return _dup(file_descriptor);
#else
    return dup(file_descriptor);
#endif
}

inline int rots_dup2(int old_descriptor, int new_descriptor)
{
#if defined(_WIN32)
    return _dup2(old_descriptor, new_descriptor);
#else
    return dup2(old_descriptor, new_descriptor);
#endif
}

inline int rots_close_fd(int file_descriptor)
{
#if defined(_WIN32)
    return _close(file_descriptor);
#else
    return close(file_descriptor);
#endif
}

// rots_setenv / rots_unsetenv: portable replacements for POSIX setenv(3)/
// unsetenv(3), used by the ScopedEnvironmentVariable test fixtures
// (account_management_tests.cpp / interpre_account_menu_tests.cpp) to
// override ROTS_SENDMAIL_COMMAND for a test's duration. MSVC's CRT spells
// both operations _putenv_s (an empty value string removes the variable).
// The POSIX branch keeps real setenv/unsetenv rather than putenv, matching
// the call sites' original overwrite semantics exactly.
inline int rots_setenv(const char* name, const char* value)
{
#if defined(_WIN32)
    return _putenv_s(name, value);
#else
    return setenv(name, value, 1);
#endif
}

inline int rots_unsetenv(const char* name)
{
#if defined(_WIN32)
    return _putenv_s(name, "");
#else
    return unsetenv(name);
#endif
}

// rots_mkdir: portable replacement for POSIX mkdir(2) in test fixtures that
// pre-create an "accounts"/"accounts/<bucket>" directory tree by hand
// (interpre_account_menu_tests.cpp). POSIX: direct passthrough, honoring the
// requested mode. Windows: the CRT's mkdir/_mkdir takes no mode argument at
// all (no owner/group/other bits -- the directory inherits its parent's ACL
// instead), the same asymmetry account_management.cpp's
// create_directory_if_missing already gates on; this is that same shape as a
// two-argument shim so call sites don't need their own #ifdef.
inline int rots_mkdir(const char* path, int mode)
{
#if defined(_WIN32)
    (void)mode;
    return _mkdir(path);
#else
    return mkdir(path, static_cast<mode_t>(mode));
#endif
}

// rots_chmod: compile-time stand-in for POSIX chmod(2) in test fixtures.
// On POSIX this is a direct passthrough. On Windows, _chmod only understands
// _S_IREAD/_S_IWRITE (there are no owner/group/other bits and no execute
// bit), so POSIX modes like 0500/0000 do NOT reproduce their POSIX meaning
// there -- every test that relies on chmod's *semantics* (permission-denial
// fault injection, executability) already GTEST_SKIPs on Windows before its
// first rots_chmod call; this shim exists so those skipped bodies still
// *compile* (GTEST_SKIP is a runtime early-return, not conditional
// compilation).
inline int rots_chmod(const char* path, int mode)
{
#if defined(_WIN32)
    return _chmod(path, mode);
#else
    return chmod(path, static_cast<mode_t>(mode));
#endif
}
