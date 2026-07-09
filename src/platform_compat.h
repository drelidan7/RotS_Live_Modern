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
