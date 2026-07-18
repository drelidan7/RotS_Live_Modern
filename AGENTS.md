# Repository Guidelines

## Instruction Precedence and Local Guidance

- This tracked file is the authoritative repository guide for all AI agent types.
- Also read `AGENTS.local.md` when it exists. That ignored file contains machine-specific guidance
  for the current checkout; it supplements this file and cannot override repository safety or
  data-handling rules.
- This depot is a child of `RotS_Live`. Some referenced workflows or agent actions may be supplied
  by that parent depot or by local tooling rather than by tracked files here. Do not delete a valid
  reference merely because its implementation is outside this checkout; use the concrete commands
  in this guide when a convenience action is unavailable.

## Project Structure & Module Organization
- src/: C/C++ game server sources, headers, and build scripts (`Makefile`, `CMakeLists.txt`).
- bin/: Built server binary (`ageland`) and backup (`ageland~`).
- lib/: Runtime data (players, world, text, etc.); many subpaths are git-ignored.
- build/: CMake build artifacts and test scaffolding; not checked in.
- proxy/: Rust workspace member (`cargo` crate) for proxy/CLI utilities.
- release-notes/, game design docs/, code documentation/: Docs and release history.

## Build, Test, and Development Commands
- Bootstrap data: `cd src && make setup` — creates required runtime directories/files under `lib/`, `log/`, and `bin/`.
- Build (Make): `cd src && make all` — compiles C/C++ sources to `bin/ageland`.
- Run: `cd src && make run` or `./bin/ageland -p &` — starts server in background.
- Clean: `cd src && make clean` — removes `*.o` objects.
- CMake alt build: `cmake -S src -B build && cmake --build build` (C++20).
- Rust proxy: `cargo build -p proxy` | `cargo test -p proxy` | `cargo run -p proxy -- --help`.
- Root Makefile wrappers (from the account-management merge; run inside the
  32-bit container): `make configure` (CMake tree), `make build`, `make test`
  (GoogleTest unit tests), `make smoke-account` (proxy-backed account smoke flow).
- Account/login/authentication changes REQUIRE `make smoke-account` (or
  `tools/account_smoke.py`) as a separate validation step — `make test` is
  intentionally unit-test-only.
- Per-platform CMake presets (host, CMake ≥3.23): from `src/`, `cmake --preset <linux-x64|macos-arm64|windows-msvc|linux-x86-legacy>` then `cmake --build --preset <name>`; as of Phase 3, `linux-x64`, `macos-arm64`, and `windows-msvc` all build and pass tests (incl. characterization goldens) and are all CI-required (see docs/BUILD.md "Build matrix"). `linux-x64`/`macos-arm64` also boot-check locally via `scripts/boot-golden.sh`; Windows verification on CI is configure+build+ctest only — no Windows host with world data exists yet for a boot check (deferred, see docs/BUILD.md and the Phase 3 plan's exit note).
- Native macOS arm64 (no Docker needed, Phase 2b primary Mac dev flow): `cd src && cmake --preset macos-arm64 && cmake --build --preset macos-arm64 -j4 && ctest --preset macos-arm64`; binary at `build/macos-arm64/ageland`; boot-check with `scripts/boot-golden.sh --native build/macos-arm64/ageland verify`.
- `rots64` container (64-bit Linux sibling of the i386 `rots` container, same bind-mounted `lib/`, host port 1064): `docker compose run --rm rots64 bash -lc 'cd /rots/src && cmake --preset linux-x64 && cmake --build --preset linux-x64 -j"$(nproc)" && ctest --preset linux-x64'`; boot-check with `scripts/boot-golden.sh --service rots64 verify`.
- `rots_convert` (standalone character-conversion executable, CMake-only — not in the flat Makefiles) builds as part of `all`/every CMake preset; it is the CI-linked check that persistence stays de-welded from combat/world/commands — see docs/BUILD.md.
- `rots_entity` (L2 static library seeded by the entity-seed wave: `entity_lifecycle.cpp`/`object_utils.cpp`/`environment_utils.cpp`, linked as `RotS::entity`) and its `EntityLayerAcyclicity` linkcheck are CI-linked the same way as `rots_platform`/`rots_core` — see docs/BUILD.md "Library layering".

## Verification Cadence

- Use the host-appropriate build, CTest, sanitizer, and boot-golden gates documented here and in
  `docs/BUILD.md`. Machine-specific command sequences and performance constraints belong in
  `AGENTS.local.md`.
- Do not push merely to trigger the full remote CI matrix after every small change. At branch or
  wave finalization and before merge, run the canonical i386 battery and require all six blocking
  CI jobs to pass: `legacy-32bit`, `linux-x64`, `sanitize-linux`, `macos-arm64`,
  `sanitize-macos`, and `windows-msvc`. `clang-tidy-advisory` is non-blocking.
- Any i386-only or MSVC-only regression found at finalization must be fixed before merge. Never
  tolerate a monolithic-runner SIGSEGV; clean stale objects and investigate it as a real failure.
- A new or substantially rewritten test file requires a sanitizer run in addition to its normal
  test run. Use an available sanitizer preset; machine-specific invocation belongs in
  `AGENTS.local.md`.

## Toolchain and Warning Policy

- All supported game and test builds use C++20. `std::format` is the sanctioned
  formatting/output-composition target; do not add a production `{fmt}` dependency.
- New read-only textual parameters, scalar constants, and lookup-table entries use
  `std::string_view` by default. Normalize every externally callable or helper text input with
  `rots::text::truncate_at_null` at its boundary; a bounded view does not by itself preserve the
  repository's historical first-null text semantics.
- A view never owns or extends storage. Keep an owning `std::string` when data is retained, and
  stage a first-null-normalized owner before calling an API that requires a null-terminated
  pointer. Preserve full bytes for binary or explicit-length contracts, nullable pointers when
  null is distinct from empty, C/ABI callbacks and printf-varargs formats, and legacy sentinel
  tables only as documented exceptions.
- After changing textual interfaces, run `python3 tools/string_view_census.py --check`. Every
  remaining candidate must have a file-specific, normalized declaration and precise contract in
  `docs/superpowers/string-view-exceptions.md`; do not add generic convenience exceptions.
- Both Linux container images use `debian:trixie` with g++ 14.2. The i386 image still compiles with
  `-m32`; the newer compiler does not change its ABI.
- GNU-family targets compile with `-Wall -Wextra -Werror`; MSVC compiles with `/W4 /WX`. A warning
  is a build failure, not an accepted baseline. `ROTS_SUPPRESS_TEST_WARNINGS` is a local-debugging
  escape hatch only and must remain off in CI and merge verification.
- Signedness is pinned with `-funsigned-char` on GNU-family compilers and `/J` on MSVC; do not
  remove one side of that cross-platform behavior contract.
- Fixed-size `char[N]` struct members must be explicitly decayed with
  `static_cast<const char*>` before passing them to `std::format`; libc++ and libstdc++ otherwise
  differ in their formatting behavior.
- Deterministic FP is enforced (SSE, no x87/fast-math/`long double`/transcendentals in the
  combat path); see `docs/BUILD.md` "FP determinism".
- GoogleTest is test-only tooling and is never linked into the game binary. The Windows MSVC and
  macOS sanitizer presets provision it with CMake `FetchContent`; other presets use installed
  packages.
- Production networking uses the repository's platform-gated `rots_net` socket shim. Do not add a
  third-party networking dependency merely to replace that compatibility layer.
- Windows CI verifies configure, build, full CTest, and characterization goldens. It does not boot
  against world data because no Windows world-data host is available.
- The i386 container remains the canonical shipping ABI and legacy-format guard until production
  migration away from the retained binary formats is explicitly confirmed.

## Server Startup and Proxy Behavior

- `-p <port>` or `-p<port>` sets the listen port; the default is 1024. It no longer means
  “expect a proxy.” Root `make run` starts `./bin/ageland -p 3791` for direct connections.
- `-x` means the connection comes through a proxy that prepends a four-byte client-IP header. A
  direct client connecting to an `-x` server desynchronizes the first read, so use `-x` only when
  the Rust proxy or `tools/account_smoke.py` is in front of the game.
- `proxy/` prepends the four-byte client IP before forwarding. Its `--cloudflare` mode reads the
  address from the `CF-Connecting-IP` header.
- `scripts/rots-docker.sh boot` starts the server without `-x`, so plain telnet connects directly
  on the default port.

## Coding Style & Naming Conventions
- Formatter: run `cd src && make format` (WebKit style). Prefer this over local defaults; CI expects formatted diffs.
- .clang-format: present for IDEs; indentation 4 spaces; column limit ~100.
- Filenames: lower_snake_case for `.cpp`/`.h` (e.g., `act_comm.cpp`, `protocol.h`).
- C/C++: functions/variables lower_snake_case; constants UPPER_SNAKE_CASE; types TitleCase where applicable.
- Rust (proxy): follow `rustfmt` defaults; module/file lowercase with underscores.

## Testing Guidelines
- C/C++: a GoogleTest suite (`cd src/tests && make tests && ../../bin/tests`, or `ctest --test-dir build`) covers ~1071 tests (1071 total on the i386 container, `rots64`, and macOS native, where skip counts differ by platform — 7 on i386, 71 on macOS / 73 on rots64 for POSIX/32-bit-fixture-gated cases; Windows count tracked separately in CI, where a handful of POSIX-only cases don't build/run), including characterization goldens (`src/tests/goldens/`, `docs/superpowers/goldens/`) that pin existing behavior byte-for-byte. Smoke tests (build + boot, see `/build-and-smoke`) remain the final gate — verify server boots, accepts connections, and changed features behave as expected. (Backlog Cleanup wave: 1015 → 1057, T1 +8, T3 +34. RAII Lifecycle-Audit wave: 1057 → 1071, T4 +12 `ActCommAlias.*`, T6 +2 `DbLoaderFactory.*`; T3/T5 added no new tests.)
- Rust: write unit/integration tests in `proxy/`; run with `cargo test -p proxy` and keep coverage reasonable.
- Characterization goldens pin current behavior: gtest suites `CharacterizationCombatTest.*`
  / `CharacterizationJson.*` (goldens in `src/tests/goldens/`) and
  `scripts/boot-golden.sh verify`. If a change intentionally alters behavior,
  regenerate with `UPDATE_GOLDENS=1` (or `boot-golden.sh capture`) and say so
  in the commit message. Unintentional drift = a bug in your change.
- All game randomness flows through `rots_rng` (mt19937, platform-independent).
  Never call `rand()`/`random()` directly.
- **`legacy_*_fixture.bin` goldens are 32-bit-only; never regenerate them on a 64-bit build.**
  `src/tests/goldens/legacy_{rent,board,mail,pkill,crime,exploits}_fixture.bin` encode the
  historical 32-bit compiler struct layout (padding, `long`/`int` sizes) that the legacy
  binary decoders read. `UPDATE_GOLDENS=1` against these is only valid from inside the
  32-bit i386 container (`scripts/rots-docker.sh` / `docker compose run rots`); running it
  on a 64-bit host or the `linux-x64`/`macos-arm64` CMake presets would silently bake in the
  wrong (64-bit) layout and defeat the fixture's entire purpose.

## Commit & Pull Request Guidelines
- Commits: concise, imperative subject (<=72 chars). Reference issues/PRs, e.g., "ranger: fix stun timing (#255)".
- Scope small, logically grouped changes; include short body for context when needed.
- PRs: describe changes, link issues, list validation steps (build/run commands), and note data/world impacts. Include logs/screens where useful.
- Do not commit generated binaries or runtime data (`bin/`, `build/`, many `lib/` paths are git-ignored).

## Security & Configuration
- World files live in a separate repo; keep `lib/world/` and player data out of commits.
- Never check in PII or live server logs (`log/`). Use local testing accounts and sanitized samples.

## Runtime Data and Persistence

- World files are not stored here; `lib/world/` comes from the separate RotS world-data depot.
  The historical source URL is `https://github.com/Noobinabox/RotS-WorldFiles`. Player data, object
  saves, exploit history, and logs are also ignored. Run `cd src && make setup` to create the local
  runtime layout, and never commit files from those paths.
- On a fresh setup, the first character created is promoted to a level-100 Implementor. This is
  expected local-development behavior.
- Object/rent, board, mail, pkill, crime, and exploit live saves and loads use JSON. The retained
  binary decoders are one-time migration converters: they decode old data, write JSON, verify it,
  and rename the original to `.migrated`. Do not replace their explicit-offset handling with
  whole-struct `memcpy`/`fwrite`; the old layout is ABI-dependent.
- Exploit conversion is lazy per character, so unmigrated `.exploits` files can legitimately remain
  until those characters log in.
- Player persistence has two live paths: account-native characters use JSON through
  `account::write_account_character_file`, while characters not linked to accounts still use
  `save_player`'s line-oriented text format. That text path is current behavior, not a legacy
  migration decoder.

## Dead / Unused Code (read before touching combat)
Some files are compiled but never actually called — changing them has no effect on the running
game, and reading them to understand mechanics will mislead you. Known cases:
- **`src/combat_manager.{h,cpp}` (the whole `combat_manager` class) was deleted in Phase 4 Wave 1**
  (it was never instantiated or invoked — dead since at least Phase 2). The **live** melee path is
  `src/fight.cpp::hit()` (driven by the round loop around `fight.cpp:2755-2761`). If you find a
  reference to `combat_manager` in an older doc or comment, it's describing pre-deletion history,
  not current code.
- The weather/room-arg OB/PB/DB trio that only `combat_manager` called
  (`utils::get_real_ob`/`get_real_parry`/`get_real_dodge` in `src/char_utils_combat.cpp`) was
  deleted alongside it. The **live** OB/PB/DB are in **`src/utility.cpp`**: `get_real_OB`,
  `get_real_parry`, `get_real_dodge` (all take a single `char_data*`, global namespace, declared
  in `src/utils.h`) — do not confuse these with the deleted `utils::`-namespaced trio that took
  `weather_data`/`room_data` arguments. `char_utils_combat.cpp` itself is still live: it also
  defines `get_engaged_characters`/`is_victim_player`/`get_controlling_player`/
  `on_attacked_character`, used by `fight.cpp`/`clerics.cpp`/`ranger.cpp`.
- The deleted implementation differed materially from the live one (e.g., its damage formula
  added the strength term *outside* the random factor, where the live formula folds it *inside*;
  there is no live "accurate hit" system — the guaranteed-hit mechanism is `frenzy`). When
  documenting or modifying combat, follow the `fight.cpp` / `utility.cpp` versions.
- Heuristic: before relying on a combat helper, grep for its callers (`grep -rn 'funcname(' src/`).
  A helper with no caller outside its own file (or only called by other dead code) is dead —
  that's how `combat_manager` and the OB/PB/DB trio above were identified before deletion.
