# RotS Modernization — Design Spec

**Date:** 2026-07-06
**Status:** Approved
**Scope:** Convert the RotS server to modern, idiomatic, 64-bit, cross-platform C++.

## 1. Goal and non-goals

**Goal:** Convert the RotS game server (~92k lines, 98 files in `src/`) into modern,
idiomatic C++20 that builds and runs natively 64-bit on macOS (arm64), Linux (x64),
and Windows (MSVC) — while the game stays bootable at the end of every phase and
gameplay behavior stays equivalent throughout.

**Non-goals:**

- No engine re-architecture. The game loop, module boundaries, and CircleMUD world
  format stay recognizable. No ECS, no coroutine rewrite, no event-bus redesign.
- No gameplay changes. Intentional oddities are preserved: mystic spirit has no
  regen (banked pool), confuse's end-of-duration negative penalty, non-`is_fast`
  affect durations ticking once per MUD-hour.
- The Rust proxy (`proxy/`) stays as-is; it is already modern and cross-platform.
- `levgen` and the Python analysis tools are out of scope.

## 2. Strategy

Incremental in-place modernization. The codebase remains the live codebase; there is
no parallel greenfield tree. Every phase ends with a bootable, test-passing server.

The work starts by **merging `upstream/account-management`** into this fork. That
branch supplies, already written and tested:

- JSON serialization for characters, objects, and exploits
  (`character_json.*`, `objects_json.*`, `exploits_json.*`, `json_utils.*` —
  a hand-rolled, dependency-free, unit-tested JSON reader/writer).
- The account system (`account_management_*.{h,cpp}`) with salted-hash password
  credentials (`generate_password_credentials` / `verify_password`).
- Legacy binary-player-file migration (`account_management_migration.*`).
- A large GoogleTest suite and CMake build updates.

This is exactly the machinery that removes the two things pinning the build at
32-bit: the binary `char_file_u` save format (`db.cpp:1742`–`store_to_char`) and its
struct-layout/ABI dependence (the documented reason for `-m32` in `src/Makefile`).

## 3. Scoping decisions (user-confirmed)

| Question | Decision |
|---|---|
| Conversion strategy | Incremental in-place (always bootable) |
| JSON/account prior art | Merge the whole `upstream/account-management` branch |
| Target platforms | macOS arm64, Linux x64, Windows (MSVC) — all native |
| Compatibility | Preserve world/player data **and** gameplay behavior |
| Modernization depth | Full idiomatic modernization; architecture unchanged |
| Dependency policy | Curated small set, pinned via CMake FetchContent |

## 4. Phases

Each phase has an explicit exit criterion; the server must boot and pass the full
test suite at every phase boundary.

### Phase 0 — Baseline + merge

- Merge `upstream/account-management` into the fork's `master`. Conflicts expected
  to be small (this fork is mostly docs-ahead of upstream).
- Verify: 32-bit Docker build, boot smoke test, full GoogleTest suite. Tag the
  result as the modernization baseline.
- Build the **characterization harness** — the "before" snapshots every later phase
  is diffed against:
  - Seeded-RNG combat transcripts (fixed seed → identical hit/damage sequences).
    Requires making the RNG injectable and using an owned, platform-independent
    PRNG for characterization runs — libc `random()` sequences differ between
    glibc, macOS, and Windows, so goldens would otherwise only compare within
    one platform. (The account branch's `test_random_utils` is a starting point.)
  - Player save/load round-trip goldens (JSON in → JSON out, byte-stable).
  - Boot-log goldens (world load counts, zone resets).

**Exit:** merged tree builds in the 32-bit container, boots, passes all tests;
goldens captured and committed.

### Phase 1 — Build system + CI

- CMake becomes the single authoritative build (the branch already has CMake work);
  the Makefile survives only as long as the 32-bit legacy guard needs it.
- `CMakePresets.json` presets: `linux-x64`, `linux-x86-legacy`, `macos-arm64`,
  `windows-msvc`.
- GitHub Actions matrix: Linux 32-bit (legacy guard, Docker), Linux 64-bit, macOS
  arm64, Windows MSVC — build + unit tests on each. Platforms that cannot yet build
  are allowed-to-fail entries until their enabling phase lands, then become required.
- Warnings remain suppressed (`-w`) for now; they are surfaced per-module in Phase 5.

**Exit:** one `cmake --preset` command per platform; CI green on Linux 32-bit and
running (even if red) elsewhere.

### Phase 2 — 64-bit enablement

- Retire binary player I/O: the JSON path becomes the only save format; the
  migration tooling converts legacy binary files. `char_file_u` load/store code is
  kept only inside the migration module, then deleted when live data is confirmed
  migrated.
- 64-bit hazard audit and fix, in this order:
  1. Pointer↔integer casts (`int`/`long` holding pointers).
  2. `long`-size assumptions — fixed-width `int32_t`/`int64_t` etc. in anything
     persisted, protocol-facing, or overflow-sensitive. This doubles as Windows
     LLP64 preparation (`long` is 4 bytes on Windows).
  3. `time_t` and other platform-variant types in persisted structures.
  4. Struct packing/`sizeof` assumptions, unions, hand-rolled hashes over pointers.
- Drop `-m32` from the 64-bit presets (the legacy 32-bit preset keeps it until the
  Phase 5 retirement).

**Exit:** 64-bit Linux **and native macOS arm64** builds boot, pass tests, and match
the Phase 0 characterization goldens. Docker is no longer required for Mac dev.

### Phase 3 — Windows + platform layer

- Networking: replace the `select()` loop (`comm.cpp`, `interpre.cpp`) with
  standalone Asio (header-only). Same single-threaded, tick-driven loop semantics —
  Asio is a portability layer here, not a concurrency redesign.
- Paths and file I/O: `std::filesystem`; explicit binary/text modes; no assumptions
  about case-sensitive filesystems.
- Timing: `gettimeofday`/raw `time()` call sites behind `std::chrono`.
- Signals: small platform shim (POSIX signals vs. Windows console handlers) for
  shutdown/copyover behavior.
- Passwords: the account branch's salted-hash credentials become the only scheme;
  the remaining `crypt()` call (`act_wiz.cpp`) is removed. Legacy `crypt()` hashes
  migrate at first successful login (verify against old hash, re-store as new).

**Exit:** all three platforms build natively, boot, and pass the same suite; CI
matrix fully required.

### Phase 4 — Idiomatic modernization (module by module)

Order: leaf utilities first; `fight.cpp`, `db.cpp`, `comm.cpp` last. Per module:
characterization tests first → transform → build-and-smoke + golden diff.

Transform catalog:

- `sprintf`/`strcpy`/`strcat` family → `fmt` / bounded equivalents. Consult
  `upstream/sprintf-replacement` as prior art before starting.
- Manual `new`/`delete`/`malloc` → RAII; owner-explicit smart pointers at ownership
  boundaries (audit `char_data`/`obj_data` lifecycles first; raw non-owning pointers
  remain fine for the world graph).
- Hand-rolled intrusive linked lists (`character_list`, object contents chains,
  `affected_type` chains) → STL containers, preserving iteration-order-sensitive
  behavior where gameplay depends on it.
- `char[MAX_STRING_LENGTH]` buffers → `std::string`/`std::string_view`.
- Plain enums/`#define` constants → `enum class`/`constexpr` where mechanical.
- Namespaces introduced per subsystem; god-files split as modules are touched
  (never as a standalone big-bang).
- Dead code deleted, not modernized: `combat_manager.cpp`, the dead
  OB/PB/DB functions in `char_utils_combat.cpp` (live versions are in
  `utility.cpp`), and anything else whose only callers are dead.

**Exit:** transform catalog applied across `src/`; goldens still match; no
functional diffs observed in playtesting.

### Phase 5 — Hardening

- Remove `-w`; drive to `-Wall -Wextra` clean per module.
- ASan/UBSan jobs in CI (Linux + macOS).
- Resolve the **`-funsigned-char` dependency**: the code currently assumes `char`
  is unsigned. Either audit the assumption away (preferred; find signedness-
  sensitive sites) or pin the equivalent flag on all three compilers
  (`/J` on MSVC) as an interim. Treated as an audited work item, never a silent
  flag flip.
- clang-tidy configuration checked in; format enforced via existing `make format`
  (WebKit style) or its CMake equivalent.
- Retire the 32-bit legacy preset and Docker CI job once live player data is
  confirmed fully migrated to JSON.

**Exit:** warnings clean, sanitizers green, 32-bit toolchain deleted.

## 5. Key technical decisions

- **Language:** C++20 (comfortably supported by AppleClang, GCC, MSVC; C++23 adds
  little for this codebase). Confirmed as landed: Phase 4 Wave 1 raised every build
  path (`src/Makefile`, `src/tests/Makefile`, `src/CMakeLists.txt`, all four CMake
  presets) to C++20 on `debian:trixie`/g++ 14.2 containers, AppleClang 21, and MSVC
  `/std:c++20` — the "Language" line above is no longer aspirational.
- **Dependencies** (pinned via FetchContent; no system-package requirements):
  standalone **Asio** (networking), **fmt** (formatting), **GoogleTest** (already
  in use). **No JSON library** — the account branch's tested `json_utils` stays;
  replacing it with nlohmann/json would be churn with no behavioral payoff.

  > **2026-07-09 amendment (Phase 3 / Phase 4 Wave 1):** the no-third-party-libraries
  > constraint (user decision, 2026-07-09) supersedes both third-party rows above.
  > **Asio** was vendored in Phase 3 Task 1, then reverted the same day in favor of a
  > hand-rolled, platform-gated socket shim (`src/rots_net.h`/`.cpp`) — see
  > `docs/BUILD.md` "Windows: build+test green, boot verification deferred". **fmt**
  > is superseded by **`std::format`** (C++20 standard library, zero dependency) as
  > the sanctioned formatting/output-composition target, proven out on the leaf
  > modules (`color.cpp`, `char_utils.cpp`, `utility.cpp`) in Phase 4 Wave 1 — see
  > `docs/BUILD.md` "Formatting: `std::format` is the sanctioned target" for the
  > `char[N]`-decay lesson learned there. GoogleTest remains the sole dependency,
  > and remains test-only tooling never linked into the shipping binary.
- **World files:** unchanged (text CircleMUD format, separate repo:
  RotS-WorldFiles). Parsing must tolerate the documented quirks (`\n\r` endings,
  missing trailing newlines).
- **Player/account data:** JSON, per the account-management branch. Binary format
  exists only inside migration code, then dies.
- **Proxy contract:** the 4-byte client-IP header protocol (`-p` flag,
  `comm.cpp:1186`) is preserved verbatim through the Asio port.

## 6. Testing & verification

Four layers, all runnable locally and in CI:

1. **Unit tests** — the merged GoogleTest suite, extended as modules are touched.
2. **Characterization goldens** — seeded combat transcripts, save/load round-trips,
   boot logs; diffed at every phase boundary and after every Phase 4 module.
3. **Boot smoke test** — the existing `/build-and-smoke` flow (build, boot, accept
   a connection) gates every merge.
4. **Sanitizers** — ASan/UBSan from Phase 5 onward.

Rule for every transform: tests pin current behavior **before** the code changes.

## 7. Risks

| Risk | Mitigation |
|---|---|
| Phase 0 merge conflicts larger than expected (54k-insertion branch) | Fork is mostly docs-ahead; merge early, verify with full suite before anything else |
| `-funsigned-char` silently changes game logic if mishandled | Dedicated audited work item in Phase 5; flag pinned on all compilers until resolved |
| LLP64 `long` truncation on Windows | Fixed-width-type sweep happens in Phase 2, before Windows ever compiles |
| Characterization gaps (goldens can't cover everything) | Live-ish playtesting gates each phase; module order defers highest-risk files (fight.cpp, db.cpp, comm.cpp) to last, when the harness is most mature |
| Iteration-order dependence in list→STL conversions | Preserve insertion/iteration order explicitly; golden combat transcripts catch ordering drift |
| Accidentally "fixing" intentional mechanics | Non-goals list enumerates them; characterization tests encode them |
