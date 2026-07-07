#pragma once

// Portable, self-contained SHA-512-crypt ($6$), vendored so account
// credentials hash and verify byte-identically on every platform (glibc,
// macOS libc, and eventually Windows) rather than depending on glibc's
// crypt(3), which is the only libc that implements the $6$ scheme natively.
//
// Implements Ulrich Drepper's public-domain reference algorithm
// (https://www.akkadia.org/drepper/SHA-crypt.txt): a SHA-512 core (FIPS
// 180-4) plus the crypt_r salt/rounds schedule and custom base64 alphabet.

// Hashes `key` against the crypt(3)-style `setting` string and returns the
// full encoded hash (same format libc's crypt() returns: "$6$salt$hash" or
// "$6$rounds=N$salt$hash").
//
// `setting` must begin with "$6$" (optionally "$6$rounds=N$" with N in
// [1000, 999999999]) followed by up to 16 salt characters and a terminating
// '$'. Anything else — a non-$6$ scheme, or a malformed/out-of-range
// "rounds=" spec — returns nullptr, mirroring the reference libcrypt's
// failure behavior (see the rounds-parsing comment in rots_crypt.cpp).
//
// Role: single-threaded server (see AGENTS.md/CLAUDE.md) — like libc's
// crypt(), the result points at a static buffer that the next call
// overwrites. Copy it out (e.g. into a std::string) before calling again.
const char* rots_crypt(const char* key, const char* setting);
