# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Team conventions (build/test/style/commit/security) live in AGENTS.md. Read it:

@AGENTS.md

## Non-obvious gotchas

- **Docker is no longer mandatory for building/running on this Mac (Phase 2b).** The native `macos-arm64` CMake preset (`src/CMakePresets.json`) now builds, boots, and passes tests — including the characterization goldens — directly on Apple Silicon: `cd src && cmake --preset macos-arm64 && cmake --build --preset macos-arm64 && ctest --preset macos-arm64`; binary lands at `build/macos-arm64/ageland`, boot-checked with `scripts/boot-golden.sh --native build/macos-arm64/ageland verify`. `windows-msvc` still stays red until Phase 3 — don't "fix" its build errors ad hoc, it's tracked porting work. The i386 Docker container remains **canonical for the shipping 32-bit binary** and is the legacy-format guard until live server data is confirmed fully migrated off the binary save formats (planned Phase 5) — use it (and the `rots64` sibling service for the 64-bit Linux runtime) whenever you need that exact ABI. See `docs/BUILD.md` and `scripts/rots-docker.sh`. Warnings (`-w` suppresses most) and "deprecated function" notices on build are expected — the game still compiles.
- **STALE since the account-management merge: `-p` is now a port number, not "expect a proxy".** `-p <port>`/`-p<port>` sets the listen port (`comm.cpp:274-290`, default 1024 via `DFLT_PORT`); the flag that means "expect the proxy's 4-byte client-IP header" is now `-x` (`comm.cpp:291-293`, usage line `comm.cpp:413`). Root `make run` runs `./bin/ageland -p 3791` — no `-x` — so it does **not** expect the proxy and plain telnet works directly against port 3791. `scripts/rots-docker.sh boot` boots with neither flag (plain telnet on the default port 1024). Only pass `-x` when a proxy (or `tools/account_smoke.py`, which launches the game with `-x`) is actually sitting in front prepending the IP header — connecting to an `-x` server directly desyncs the very first read.
- **World files and player data are not in this repo.** `lib/world/` comes from a separate repo (https://github.com/Noobinabox/RotS-WorldFiles); `lib/players/`, `lib/plrobjs/`, `lib/exploits/`, and `log/` are git-ignored. Run `cd src && make setup` once to create the runtime directory structure. Never commit anything under these paths.
- **First character created becomes a level-100 Implementor** on a fresh `make setup` install — expected for local dev.
- **Running C/C++ tests** (Google Test): `cd src/tests && make tests`, then run the produced binary at `bin/tests`. The suite has grown to ~630 tests, including characterization goldens (`src/tests/goldens/`, `docs/superpowers/goldens/`) that pin existing behavior byte-for-byte — treat a golden diff as a signal to investigate, not to blindly update. Primary verification is still a build + boot smoke test (see `/build-and-smoke`).
- **Proxy IP header:** `proxy/` (Rust) prepends a 4-byte client IP to the game connection before forwarding. Relevant when touching connection/auth/logging code. `--cloudflare` reads the IP from the `CF-Connecting-IP` header instead.
- **Characterization goldens gate refactors.** `src/tests/goldens/` +
  `scripts/boot-golden.sh` pin behavior under seed 42; run them after any
  combat/persistence/boot change. RNG is owned (`src/rots_rng.*`) — libc
  `rand()`/`random()` are banned.
- **Object/board/mail/pkill/crime/exploit persistence is JSON end-to-end now (Phase 2a).**
  Live saves and loads for these all go through JSON (`objects_json.cpp`, `boards.cpp`,
  `mail.cpp`, `pkill.cpp`, `db.cpp`'s `crime_json`, `exploits_json.cpp`) — nothing on the
  live path writes the old binary struct dumps anymore. The binary decoders documented in
  `docs/data-formats/object-rent-files.md` (now marked LEGACY) still exist, but only as
  one-time migration converters: on first load they detect a pre-migration legacy file,
  decode it, write the JSON equivalent, and rename the legacy file to `.migrated` (never
  deleted outright). Exploit-history conversion is per-character and lazy (triggered by
  that character's next login), so `lib/exploits/` can legitimately still contain many
  un-migrated `.exploits` files at any given time — that is by design, not drift. Don't
  "simplify" a binary decoder by making it whole-struct `memcpy`/`fwrite` again — the
  explicit-offset encoding (where already converted; see `crime_json`) exists specifically
  to survive a future 64-bit rebuild's different `long`/padding sizes. **Player saves are
  different**: only account-native characters save as JSON
  (`account::write_account_character_file`); a character not linked to an account still
  saves through `save_player`'s live line-oriented **text** writer (`db.cpp`, see
  `player-save.md` — not LEGACY, not a migration converter, still written on every save
  for that population). Explicit-offset framing applies to the binary decoders above, not
  to this text format.
