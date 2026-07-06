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

## Coding Style & Naming Conventions
- Formatter: run `cd src && make format` (WebKit style). Prefer this over local defaults; CI expects formatted diffs.
- .clang-format: present for IDEs; indentation 4 spaces; column limit ~100.
- Filenames: lower_snake_case for `.cpp`/`.h` (e.g., `act_comm.cpp`, `protocol.h`).
- C/C++: functions/variables lower_snake_case; constants UPPER_SNAKE_CASE; types TitleCase where applicable.
- Rust (proxy): follow `rustfmt` defaults; module/file lowercase with underscores.

## Testing Guidelines
- C/C++: no formal unit tests; perform smoke tests by building and running locally. Verify server boots, accepts connections, and changed features behave as expected.
- Rust: write unit/integration tests in `proxy/`; run with `cargo test -p proxy` and keep coverage reasonable.

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

