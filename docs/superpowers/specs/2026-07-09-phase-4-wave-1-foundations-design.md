# Phase 4 Wave 1 — Foundations (design)

**Date:** 2026-07-09 · **Parent spec:** `2026-07-06-cpp-modernization-design.md` (Phase 4 —
idiomatic modernization) · **Branch:** `modernization/phase-4-wave-1` off master (post-Phase-3,
tip 3536002 at design time).

## Why a wave, not all of Phase 4

The parent spec's Phase 4 exit ("transform catalog applied across `src/`") spans ~104k lines
with ~1,950 `sprintf`/`strcpy`/`strcat` calls alone, plus RAII/container/string/enum
migrations. Per the spec's own ordering rule (leaf utilities first; `fight.cpp`, `db.cpp`,
`comm.cpp` last), Phase 4 executes as a sequence of waves, each with its own plan and full
verification battery. **Wave 1 lays the foundations**: the language/toolchain base, dead-code
removal, test-infrastructure repair, accumulated backlog burn-down, and the `std::format`
conversion pattern proven on leaf modules.

## User decisions binding this wave (2026-07-09)

1. **C++20 now** (user sanctioned "C++20, or C++23 if widely supported", including updating
   the operating environment). C++23 is deferred: MSVC still gates it behind the moving-target
   `/std:c++latest`; revisit when `/std:c++23` stabilizes. C++20 is complete and stable on all
   four toolchains once the containers move to `debian:trixie` (g++ 14.2 — verified available
   for `linux/386`).
2. **`std::format` replaces the parent spec's `fmt` dependency** for the sprintf-family
   transform — the standing no-third-party-libraries constraint (2026-07-09 pivot) rules out
   `fmt`; C++20 makes the standard-library equivalent available. This supersedes the parent
   spec's "Dependencies" section for formatting, the same way `rots_net` superseded Asio.
3. Foundations-wave scope chosen over "safety-first repo-wide sprintf" and "full catalog on a
   few modules" alternatives.

## Tasks

### 1. C++20 + toolchain bump (the enabler — hardest gate)

- `Dockerfile` (i386): `i386/debian:bullseye` → `debian:trixie` with `--platform=linux/386`
  (g++ 14.2). `Dockerfile.x64`: `debian:bookworm` → `debian:trixie`. Keep everything else in
  the images equivalent (cmake, libgtest-dev, python3, telnet/procps; libcrypt-dev only if
  still referenced).
- All four build paths to C++20: `src/Makefile` (`-std=c++20`), `src/tests/Makefile`,
  `src/CMakeLists.txt` (`cxx_std_20` on both targets), CI job toolchains verified per
  platform (ubuntu runner g++ version, AppleClang 21, MSVC `/std:c++20`).
- Expect and fix a small wave of C++20-strictness fallout (e.g. deprecated `u8`/comma
  semantics, `char8_t`-adjacent issues) — mechanical, per-error.
- **Gate:** full battery ×4 (i386 runner + ctest + boot-golden; rots64; macOS native;
  windows CI) with **byte-identical characterization goldens** — a standard/toolchain bump
  must be behaviorally invisible. The frozen `legacy_*_fixture.bin` goldens and the legacy
  converters are untouched; the i386 container remains the legacy-format guard (newer
  compiler, same ABI: `-m32`, same struct layouts — the LLP64 probe test and fixture suites
  re-verify this on the new toolchain for free).
- Risk note: QEMU emulation of trixie/g++14 on Apple Silicon is slower at compile time;
  acceptable (CI containers run natively on amd64 runners).

### 2. Dead code deleted, not modernized (parent-spec catalog item)

Targets (each with caller-grep proof recorded in the task report):
- `src/combat_manager.{h,cpp}` (never instantiated; live melee is `fight.cpp::hit()`).
- The dead OB/PB/DB trio in `src/char_utils_combat.cpp` (`get_real_ob/parry/dodge` with
  weather/room args) — the rest of that file is LIVE and stays.
- `src/comm.cpp::write_to_descriptor_new` (zero callers, previously dispositioned).
- Dead `#ifdef SUNPROCESSING` blocks and any function whose only callers are dead.
- Remove deleted files from both Makefiles + CMakeLists; update the CLAUDE.md/AGENTS.md
  "Dead / Unused Code" sections (they currently *warn about* combat_manager — after deletion
  they should record that it was deleted in Phase 4 Wave 1 and that `utility.cpp` holds the
  live OB/PB/DB).
- **Gate:** battery ×4; goldens byte-identical (deleting dead code must change nothing).

### 3. Test-world RAII redesign (fixes the monolithic-runner segfaults)

- Root cause (documented in `docs/superpowers/phase4-seed-64bit-cosmetic.md`): three-plus
  divergent `ensure_test_world_room()` clones (`interpre_account_menu_tests.cpp:372`,
  `db_loader_tests.cpp:413`, `spell_pa_tests.cpp:29`, plus `damage_test_context.h`'s
  `ensure_test_world()`) share the process-wide `room_data::BASE_WORLD` with no scoped
  reset — cross-suite state pollution segfaults the monolithic `./ageland_tests` binary
  under `--gtest_repeat`/`--gtest_shuffle`.
- Design: one shared test helper (e.g. `src/tests/test_world.h`) providing a scoped
  RAII world fixture in the style of the existing `ScopedDescriptorListReset` /
  `ScopedPlayerTableReset`; the clones are replaced, not multiplied. Leak-free re-entry
  (no re-`strdup` over live pointers), explicit reset of `world[0].people` and friends.
- **Gate:** monolithic `./ageland_tests --gtest_repeat=2` clean AND a 10-seed
  `--gtest_shuffle` sample clean (macOS + rots64); ctest counts unchanged; battery ×4.

### 4. Backlog burn-down (small correctness fixes, each individually testable)

- `fight.cpp::perform_violence` first-ever-call garbage time-delta: initialize the reference
  point on first call; pin with a test. (Behavior-neutral in practice; removes the documented
  garbage value.)
- `shapemdl.cpp` / `obj2html.cpp` `fprintf(stderr, str)` / `printf(str)` format-string
  vulnerabilities → `fputs`-style or `"%s"` (mechanical, admin-facing paths).
- MSSP uptime logic bug (`protocol.cpp::GetMSSP_Uptime` reports absolute boot epoch, not
  elapsed): **deliberate behavior fix** — report per MSSP convention (spec says UPTIME is the
  boot epoch — verify against the MSSP spec first; if epoch is correct, fix the misleading
  variable/comment instead). Whichever way it lands, record the decision.
- `zone.cpp` `fscanf`-family `%hd`-vs-`int*`/`short*` mismatches (varargs UB class) — align
  types with format specs.
- `comm.cpp` `sprintf("Socket %d", SocketType)` sites → correct spelling for both platforms.
- **Gate:** targeted tests per fix + battery ×4; the MSSP change is the only intended
  behavior delta and gets called out in its commit message.

### 5. `std::format` conversion pattern on leaf modules

- Introduce the conversion pattern the later waves will follow, on low-fan-in leaf modules
  only: `color.cpp`, `object_utils.cpp`, `environment_utils.cpp`, `wait_functions.cpp`,
  `clock.cpp`, `char_utils.cpp` (final list confirmed by fan-in check at planning time).
- Per module: convert `sprintf`/`strcpy`/`strcat`/fixed `char[]` composition to
  `std::format`/`std::string` equivalents. printf→format-spec translation is per-site work
  (`%-20s` → `{:<20}` etc.) — **output must stay byte-identical**; goldens plus targeted
  output-pinning tests (added BEFORE converting, per the parent spec's characterization rule)
  gate each module.
- Deliberately NOT in wave 1: `act_*`, `fight.cpp`, `db.cpp`, `comm.cpp`, shape* tools, and
  any RAII/container/enum-class/namespace work (later waves).
- **Gate:** per-module characterization-first evidence + battery ×4.

### 6. Exit: docs, tracking, ledger

- `docs/BUILD.md`: toolchain matrix rows (trixie/g++14 containers, C++20 everywhere).
- Parent-spec amendment note (in the spec file, dated): formatting = `std::format` (user
  C++20 decision + no-third-party constraint); Asio row already superseded by `rots_net`.
- Create the **Windows operational-gaps tracking issue** (Phase 3 final-review
  recommendation): sendmail no-op, umask/ACLs, backtrace, SIGVTALRM watchdog, WSA
  error-reporting polish, Windows-boot prerequisites (Windows host + world data).
- Exit battery ×4 + CI run URL recorded; ledger updated.

## Explicitly deferred from wave 1 (recorded, not dropped)

- Y2038-class `int`-timestamp narrowings (need a deliberate on-disk format version bump —
  see the seed doc).
- RAII / owner-explicit smart pointers (needs the `char_data`/`obj_data` lifecycle audit
  first — its own wave).
- Intrusive-list → STL container migrations; `char[MAX_STRING_LENGTH]` → `std::string` in
  the big modules; enum class / namespaces; god-file splits.
- C++23 bump (MSVC stabilization gate).
- `upstream/sprintf-replacement` branch: consult as prior art during Task 5 planning; not
  merged wholesale.

## Verification model (unchanged from Phases 2-3)

Full battery ×4 gates every task: i386 container (Makefile runner + ctest + boot-golden),
rots64 (ctest + boot-golden --service), macOS native (ctest + boot-golden --native),
windows-msvc CI (required job, full ctest incl. goldens). Goldens byte-identical always —
except where a task explicitly declares a behavior fix (Task 4 MSSP), which must not be
golden-covered anyway. Frozen legacy fixtures never regenerated. SDD per-task review gates;
final whole-branch review before merge.
