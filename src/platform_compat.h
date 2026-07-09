/* ************************************************************************
 *   File: platform_compat.h                                               *
 *  Usage: small portable stand-ins for POSIX/BSD/glibc extensions that    *
 *         MSVC's CRT does not provide (Phase 3 Task 5: MSVC bring-up)     *
 ************************************************************************ */

#pragma once

// rots_asprintf: portable replacement for the asprintf() extension (POSIX/BSD/glibc,
// available for free on Linux and macOS but absent from MSVC's CRT). Implemented via
// vsnprintf-size-then-malloc in utility.cpp. Ownership contract matches asprintf(3):
// on success, *out receives a malloc()'d NUL-terminated buffer that the caller owns
// and must free(); on failure (encoding error or allocation failure), *out is set to
// nullptr and -1 is returned. The return value on success is the number of characters
// written, excluding the terminating NUL (same as asprintf(3) / vsnprintf(3)).
int rots_asprintf(char** out, const char* fmt, ...);

// rots_rename_replace: rename() with POSIX semantics on every platform --
// atomically replaces an existing destination. std::rename() does this on
// POSIX, but on Windows it FAILS (EEXIST) when the destination exists, which
// breaks every temp-file+rename atomic-write path in the persistence layer
// (account files, boards/mail/pkill/crime/exploits JSON stores) the moment a
// file is saved for the second time. The Windows branch (utility.cpp) uses
// MoveFileExA(MOVEFILE_REPLACE_EXISTING), the OS primitive with exactly the
// POSIX replace semantics, and maps the common failure causes onto errno so
// existing strerror(errno)-based error messages stay meaningful. Returns 0 on
// success, nonzero with errno set on failure -- same contract as
// std::rename(), so call sites are a drop-in rename.
int rots_rename_replace(const char* from, const char* to);

// rots_remove: remove() with POSIX semantics on every platform. POSIX
// remove(3) deletes a file OR an empty directory (it is unlink-then-rmdir);
// MSVC's CRT remove() refuses directories with EACCES, so cleanup/rollback
// paths that must guarantee "nothing left at this path" (e.g. interpre.cpp's
// new-character rollback via the account_management_assets.cpp removers)
// silently leave directories behind on Windows. The Windows branch
// (utility.cpp) retries a directory-shaped EACCES failure with _rmdir().
// Returns 0 on success, nonzero with errno set on failure -- same contract as
// std::remove(), so call sites are a drop-in rename.
int rots_remove(const char* path);
