# Repository Guidelines

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
- CMake alt build: `cmake -S src -B build && cmake --build build` (C++17).
- Rust proxy: `cargo build -p proxy` | `cargo test -p proxy` | `cargo run -p proxy -- --help`.
- Root Makefile wrappers (from the account-management merge; run inside the
  32-bit container): `make configure` (CMake tree), `make build`, `make test`
  (GoogleTest unit tests), `make smoke-account` (proxy-backed account smoke flow).
- Account/login/authentication changes REQUIRE `make smoke-account` (or
  `tools/account_smoke.py`) as a separate validation step — `make test` is
  intentionally unit-test-only.
- Per-platform CMake presets (host, CMake ≥3.23): from `src/`, `cmake --preset <linux-x64|macos-arm64|windows-msvc|linux-x86-legacy>` then `cmake --build --preset <name>`; as of Phase 2b, `linux-x64` and `macos-arm64` both build, boot, and pass tests (incl. characterization goldens) and are CI-required, while `windows-msvc` stays red until Phase 3 (see docs/BUILD.md "Build matrix").
- Native macOS arm64 (no Docker needed, Phase 2b primary Mac dev flow): `cd src && cmake --preset macos-arm64 && cmake --build --preset macos-arm64 -j4 && ctest --preset macos-arm64`; binary at `build/macos-arm64/ageland`; boot-check with `scripts/boot-golden.sh --native build/macos-arm64/ageland verify`.
- `rots64` container (64-bit Linux sibling of the i386 `rots` container, same bind-mounted `lib/`, host port 1064): `docker compose run --rm rots64 bash -lc 'cd /rots/src && cmake --preset linux-x64 && cmake --build --preset linux-x64 -j"$(nproc)" && ctest --preset linux-x64'`; boot-check with `scripts/boot-golden.sh --service rots64 verify`.

## Coding Style & Naming Conventions
- Formatter: run `cd src && make format` (WebKit style). Prefer this over local defaults; CI expects formatted diffs.
- .clang-format: present for IDEs; indentation 4 spaces; column limit ~100.
- Filenames: lower_snake_case for `.cpp`/`.h` (e.g., `act_comm.cpp`, `protocol.h`).
- C/C++: functions/variables lower_snake_case; constants UPPER_SNAKE_CASE; types TitleCase where applicable.
- Rust (proxy): follow `rustfmt` defaults; module/file lowercase with underscores.

## Testing Guidelines
- C/C++: a GoogleTest suite (`cd src/tests && make tests && ../../bin/tests`, or `ctest --test-dir build`) covers ~500 tests, including characterization goldens (`src/tests/goldens/`, `docs/superpowers/goldens/`) that pin existing behavior byte-for-byte. Smoke tests (build + boot, see `/build-and-smoke`) remain the final gate — verify server boots, accepts connections, and changed features behave as expected.
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

## Dead / Unused Code (read before touching combat)
Some files are compiled but never actually called — changing them has no effect on the running
game, and reading them to understand mechanics will mislead you. Known cases:
- **`src/combat_manager.cpp` (the whole `combat_manager` class) is unused.** It is in the
  Makefile and compiles, but is never instantiated or invoked. The **live** melee path is
  `src/fight.cpp::hit()` (driven by the round loop around `fight.cpp:2755-2761`).
- Consequently the OB/PB/DB functions in **`src/char_utils_combat.cpp`**
  (`get_real_ob`/`get_real_parry`/`get_real_dodge`, which take `weather`/`room` args) are also
  dead. The **live** OB/PB/DB are in **`src/utility.cpp`**: `get_real_OB`, `get_real_parry`,
  `get_real_dodge` (all take a single `char_data*`).
- The two implementations are close but differ materially (e.g., the live damage formula folds
  the strength term *inside* the random factor; there is no live "accurate hit" system — the
  guaranteed-hit mechanism is `frenzy`). When documenting or modifying combat, follow the
  `fight.cpp` / `utility.cpp` versions.
- Heuristic: before relying on a combat helper, grep for its callers
  (`grep -rn 'funcname(' src/`). If `combat_manager` is the only caller, it's dead.

