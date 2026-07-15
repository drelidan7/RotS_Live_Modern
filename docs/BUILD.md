# Building & running RotS locally

RotS's **shipping** binary compiles **32-bit** (`-m32` in `src/Makefile`). That cannot
build or run natively on Apple Silicon / modern macOS (Apple removed 32-bit support in
2019), so the 32-bit binary is built and run **unchanged** inside a 32-bit Linux (i386)
container. On arm64 Macs, Docker runs the i386 image via QEMU emulation — slower than
native, but fine for development.

As of Phase 2b, **Docker is no longer required for Mac dev.** The `macos-arm64` CMake
preset now builds, boots, and passes tests (including the characterization goldens)
natively on Apple Silicon — see "Native macOS arm64 build" below, which is the primary
day-to-day flow. The i386 container remains the **32-bit legacy-format guard**: it's
still how the *shipping* 32-bit binary is built, and it stays load-bearing until the
live server's player/world data is confirmed fully migrated off the legacy binary
formats — a trigger for its own future phase, not yet scheduled — at which point the
32-bit preset itself gets retired. Until then, keep using it whenever you need to
reproduce the exact shipping ABI.

This keeps the original binary save-file format intact (no code changes), so a Docker
build matches how the live game behaves.

## Prerequisites

1. **Docker Desktop for Mac** (Apple Silicon build) — https://www.docker.com/products/docker-desktop/
   Verify with `docker --version`. Make sure Docker Desktop is running before the commands below.
   Required only for the container flows below (the 32-bit legacy-format guard and the
   `rots64` container); the native macOS flow ("Native macOS arm64 build" below) needs none.
2. **World files** at `lib/world/` — required to **boot** (not to compile). These are *not*
   in this repo; `lib/world/` is git-ignored and the upstream `Noobinabox/RotS-WorldFiles`
   repo is no longer publicly available. You need to obtain the world data and place it so
   that files exist at `lib/world/wld*`, `lib/world/mob*`, `lib/world/obj*`,
   `lib/world/zon*`, `lib/world/shp*`, etc. (The game `chdir`s into `lib/` at startup —
   `src/comm.cpp` / `config.cpp:58 DFLT_DIR="lib"` — and reads the `world/...` prefixes from
   `src/db.h`.)

   **If you have an old server backup**, use the importer to populate `lib/` from it:

   ```bash
   scripts/import-world-data.sh --backup /path/to/old/rots   # world + active players
   scripts/import-world-data.sh --backup /path/to/old/rots --world-only
   ```

   It extracts the world from the backup's clean `lib/world-for-6001.tbz` snapshot
   (whose `world/scr/*.scr` filenames match `world/scr/index` — the loose `lib/world`
   dir in a backup may contain macOS-munged `*.scr.txt` files that would make
   `index_boot()` fail), and copies the `A-E … U-Z` player buckets (the only ones the
   game indexes) into `lib/players`, `lib/plrobjs`, and `lib/exploits`. CVS metadata
   and `.DS_Store` are stripped; the `players/ZZZ` graveyard is skipped unless you pass
   `--with-graveyard`. Run `scripts/import-world-data.sh --help` for all options. All of
   this data stays git-ignored.

## Commands

A helper script wraps the common flows (`scripts/rots-docker.sh`):

```bash
scripts/rots-docker.sh build      # build the i386 toolchain image (first time / Dockerfile changes)
scripts/rots-docker.sh compile    # make setup + make all  -> bin/ageland  (no world files needed)
scripts/rots-docker.sh boot       # compile then start the server on port 1024 (needs world files)
scripts/rots-docker.sh shell      # interactive shell inside the container
```

### Read-only text API census

Production C++ read-only text inputs and literal lookup storage use `std::string_view` unless a
contract requires owning, binary, nullable, C/ABI, printf-varargs, or sentinel-table behavior.
Every text-taking public or helper boundary truncates its view with
`rots::text::truncate_at_null`; callers must not assume that constructing a bounded view alone
removes bytes after an embedded null. A view borrows storage and must never be retained beyond the
owner's lifetime. When a downstream C API needs a null-terminated pointer, first truncate the view
and create an owning `std::string`; explicit-length and binary inputs instead preserve their full
byte range.

Run the authoritative repository census from the root:

```bash
python3 -m unittest tools/string_view_census_tests.py
python3 tools/string_view_census.py --check
```

Check mode fails if a candidate is missing a file-specific declaration, permitted reason, or
nonempty contract in `docs/superpowers/string-view-exceptions.md`.

Or directly with compose:

```bash
docker compose build
docker compose run --rm --service-ports rots bash
#   inside the container:
cd /rots/src && make setup && make all      # build
cd /rots && ./bin/ageland                   # run on port 1024 (see -p note below)
```

## Connecting

The server listens on **port 1024** (default `DFLT_PORT`). Published to the host by
compose, so from a second macOS terminal:

```bash
telnet localhost 1024
```

The first character you create is auto-promoted to a level-100 Implementor.

### The `-p` flag (important)

`make run` launches `./bin/ageland -p`. The `-p` flag means **"expect a proxy"** — the
game then reads a 4-byte client-IP header from each new connection before anything else
(`src/comm.cpp:1186`). That header is supplied by the Rust `proxy/` (WebSocket bridge).

For plain `telnet` development, run **without** `-p` (as the helper's `boot` does), so the
game falls back to `getpeername` for the client address. Only use `-p` if you also run the
proxy in front of the game.

## Notes

- Build artifacts (`bin/ageland`, `*.o`) land on the host via the bind mount, but the
  binary is a Linux i386 ELF — it only runs inside the container.
- `make all` builds warning-clean under `-Wall -Wextra -Werror` (Phase 5) — the old
  "deprecated function warnings are expected" caveat no longer applies; any warning
  is now a build failure (see "Warnings policy" below).
- **`docker compose run` can hang at "Image ... Pulling" even when the image already
  exists locally** (`docker images` shows it) — Compose still probes the registry for a
  newer version before falling back to the local image, and that probe can hang
  indefinitely on this host. This is a distinct hang from the documented qemu-under-load
  hang below; the tell is the log stopping right after the `Pulling` line, before any
  build/qemu output. Fix: kill the stuck process/container and rerun with
  `docker compose run --rm --pull never <service> ...` to force the local image and skip
  the registry check entirely.
- The Rust proxy (`proxy/`) is not needed for telnet play; it builds natively on macOS
  with `cargo build -p proxy` if you later want browser-client access.
- **The canonical i386 test gate is `make test` (ctest), not a bare `./bin/tests`
  invocation.** `make test` runs the suite via ctest, which honors each test's
  `WORKING_DIRECTORY` (`gtest_discover_tests(... WORKING_DIRECTORY ...)` in
  `src/CMakeLists.txt`); the characterization goldens (`src/tests/goldens/`) are read
  with paths relative to that directory. Running `bin/tests` directly from the wrong
  cwd bypasses that `WORKING_DIRECTORY` and produces **false golden-read failures**
  that look like a real regression but are purely an invocation artifact — the RAII
  Lifecycle-Audit wave (T6) hit exactly this. Same reasoning is why the monolithic
  runner must be invoked `cd src/tests && make tests && ../../bin/tests` (run from
  `src/tests`, not the repo root) — golden paths resolve relative to that cwd too.

## FP determinism

RotS ships across a build matrix (i386/x64/arm64 GNU-family plus MSVC), and the
characterization goldens (`src/tests/goldens/combat_transcript_seed42.txt` and friends) are only
meaningful if every platform in that matrix runs the *same* floating-point arithmetic and
produces the *same* rounding — otherwise a golden pins one platform's quirks rather than the
actual shipping binary's behavior. Phase 1 (FP-unification) made that true by policy, not by
accident:

- **The deterministic FP subset is `+ − × ÷` and `sqrt`, `double`-only.** Combat, stat, and HP
  math must stay inside this subset.
- **Banned in that deterministic path:** `long double`, `-ffast-math`/`-Ofast`/`/fp:fast`, and
  libm transcendentals (`pow`/`exp`/`log`/trig). If a future formula genuinely needs a
  transcendental, vendor a fixed portable implementation so every platform runs the same code,
  rather than calling platform libm — glibc, Apple's libm, and MSVC's ucrt aren't required to
  agree in the last bit.
- **Shipping and test builds share one FP flag set:** `-msse2 -mfpmath=sse` (GNU-family x86,
  including i386 — eliminates x87 80-bit extended-precision evaluation), `-ffp-contract=off`
  (all GNU-family targets, including arm64 — prevents the compiler from silently fusing
  `a*b+c` into a single FMA that rounds once instead of twice), and `/fp:precise` (MSVC — the
  MSVC analogue of both). These are defined once as `ROTS_FP_OPTIONS` in `src/CMakeLists.txt`
  and mirrored in `src/Makefile`/`src/tests/Makefile`; the three must stay in sync — a shipping
  build and a test build computing combat math under different FP semantics defeats the whole
  point of the goldens.
- **`src/fp_policy.h`** fails the build (via `static_assert`/`#error`) if x87 evaluation or
  fast-math regress. It's included by the combat translation units, so it guards the shipping
  binary itself, not just the test tree. **`src/tests/fp_determinism_smoke_tests.cpp`**
  (`FpDeterminismSmoke.*`) is the runtime tripwire for cases the compile-time guard can't catch
  (e.g. indeterminate `FLT_EVAL_METHOD`).
- **Regenerating the combat golden is a deliberate, reviewed act.** If a change intentionally
  alters combat math, regenerate `combat_transcript_seed42.txt` with `UPDATE_GOLDENS=1` and say
  so in the commit message; unintentional drift is a bug in the change, not a golden to update.

## Native macOS arm64 build (Phase 2b, primary Mac dev flow)

No Docker needed. Requires CMake ≥ 3.23 and GoogleTest (`brew install googletest`).

```bash
cd src
cmake --preset macos-arm64
cmake --build --preset macos-arm64 -j4
ctest --preset macos-arm64                                  # unit tests + goldens
scripts/boot-golden.sh --native build/macos-arm64/ageland verify   # boot vs. Phase 0 golden
```

The built binary lands at `build/macos-arm64/ageland`. `boot-golden.sh --native` runs it
directly on the host (no container) against the repo's real `lib/` data — same
poll/normalize/compare as the container flow, so a diff here is a real native-vs-container
behavior difference, not noise. This preset is 64-bit (native ABI, JSON-only persistence
from Phase 2a) — it is not the shipping 32-bit binary; use the i386 container (above) when
you need that exact ABI.

## The `rots64` container (64-bit Linux sibling)

`docker-compose.yml` also defines `rots64` — a 64-bit (Trixie x64, `Dockerfile.x64`)
sibling of the `rots` i386 container, running the `linux-x64` CMake preset against the
SAME bind-mounted `lib/` data. It exists to verify cross-ABI compatibility of the JSON
persistence layer (Phase 2a) independent of the macOS toolchain, and maps host port
**1064** (not 1024) so it can run alongside `rots` without a port collision — in practice
only one is booted against live `lib/` data at a time.

```bash
docker compose build rots64
docker compose run --rm rots64 bash -lc 'cd /rots/src && cmake --preset linux-x64 && cmake --build --preset linux-x64 -j"$(nproc)"'
docker compose run --rm rots64 bash -lc 'cd /rots/src && ctest --preset linux-x64'
scripts/boot-golden.sh --service rots64 verify
```

## Build matrix (Phase 3, toolchain updated Phase 4 Wave 1)

The authoritative build is CMake. Presets live in `src/CMakePresets.json`
(CMake ≥ 3.23 to use presets; the i386 container's CMake 3.18 uses the root
`Makefile` flow instead).

As of Phase 4 Wave 1, both Linux containers (`Dockerfile`, `Dockerfile.x64`) build
from `debian:trixie` (g++ 14.2 on both the `linux/386` and `linux/amd64` platforms),
and **every** build path — containers, native macOS, MSVC — compiles as **C++20**
(`src/Makefile`, `src/tests/Makefile`, `src/CMakeLists.txt` all specify `c++20`/
`cxx_std_20`). The i386 image is still `-m32` on top of the newer compiler — only
the toolchain version changed, the ABI did not.

| Preset / path | Platform | Toolchain | Status |
|---|---|---|---|
| container + root `Makefile` (`make configure/build/test`) | Linux i386 (`-m32`) | `debian:trixie`, g++ 14.2, C++20 | **green — the shipping ABI; CI-required** |
| `linux-x86-legacy` | Linux i386 via multilib | host g++, C++20 | builds the game; tests stay in the container path |
| `linux-x64` (native or `rots64` container) | Linux x86-64 | `debian:trixie`, g++ 14.2, C++20 | **green — boots, passes tests, matches the Phase 0 goldens; CI-required** |
| `macos-arm64` (native) | macOS arm64 | AppleClang 21, C++20 | **green — boots, passes tests, matches the Phase 0 goldens; CI-required** |
| `windows-msvc` | Windows x64 MSVC | MSVC (windows-2022 runner), `/std:c++20` | **green — configure/build/full ctest (goldens included); CI-required as of Phase 3 Task 7** |
| `linux-x64-sanitize` | Linux x86-64 | `debian:trixie`/ubuntu-24.04, g++ 14, C++20, ASan+UBSan, `_GLIBCXX_ASSERTIONS` | **green — full ctest under `-fsanitize=address,undefined`, leak detection on; CI-required as of Phase 5 Task 6** |
| `macos-arm64-asan` | macOS arm64 | AppleClang 21, C++20, ASan+UBSan, libc++ extensive hardening | **green — full ctest under `-fsanitize=address,undefined`, leak detection off (AppleClang LeakSanitizer is unreliable on macOS); CI-required as of Phase 5 Task 6** |

Per-platform (from `src/`): `cmake --preset <name>`, `cmake --build --preset <name>`,
`ctest --preset <name>`. `cmake --list-presets` shows what runs on this host.

CI (`.github/workflows/ci.yml`) runs six required jobs — `legacy-32bit`, `linux-x64`,
`sanitize-linux`, `macos-arm64`, `sanitize-macos`, and `windows-msvc` — none of which
is allowed to fail. A seventh job, `clang-tidy-advisory` (Phase 5 Task 7), runs the
checked-in root `.clang-tidy` config over just the `.cpp` files changed vs.
`origin/master` and is deliberately advisory (`continue-on-error: true`) — a finding
there never blocks a merge.

Both sanitizer presets additionally arm standard-library precondition checks:
`linux-x64-sanitize` defines `_GLIBCXX_ASSERTIONS` (libstdc++) and `macos-arm64-asan`
defines `_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE` (libc++). These catch
library-contract violations ASan/UBSan cannot see — e.g. `string_view::operator[]` at
`size()` on a literal-backed view reads inside the literal's allocation, so ASan stays
silent, but the assertion aborts with an exact location. Both macros are ABI-compatible
(no container-layout change, unlike `_GLIBCXX_DEBUG`, which must not be used here), so
mixing with the system-package GoogleTest on Linux is safe. They stay off in shipping
and non-sanitizer presets: the checks cost cycles on hot paths, and their abort-on-
violation failure mode would turn a latent out-of-contract read on a live server into a
player-triggerable crash.

## Warnings policy (Phase 5 Task 8)

**GNU-family builds (GCC, Clang, AppleClang) compile with `-Wall -Wextra -Werror`
everywhere.** The historical blanket `-w` suppression is gone from every build path:
`src/CMakeLists.txt`'s `ageland` target (both `ROTS_GNULIKE` compile-options blocks),
its `ageland_tests` target (`ROTS_SUPPRESS_TEST_WARNINGS` now defaults **OFF**, so the
`-Wall -Wextra -Werror` branch is what actually runs; flipping the option back to `ON`
is an explicit, documented opt-out for local debugging only — never for CI or a merge
gate), `src/Makefile`'s `REQ_CXXFLAGS`, and `src/tests/Makefile`'s `CXXFLAGS`. The
sanitizer presets (`linux-x64-sanitize`, `macos-arm64-asan`) inherit from their
non-sanitizing base presets, so they pick up `-Werror` too — by design, not an
oversight.

This is the payoff of Phase 5 Tasks 1-7: a mechanical, category-by-category sweep
(`-Wchar-subscripts`, `-Wwritable-strings`/const-correctness, sprintf-family
conversions to `std::format`, `-Wunused`, sanitizer-visible UB, tail-sweep stragglers)
that drove the GNU-family warning census to zero on every locally-built configuration
(macOS AppleClang, `rots64` g++14) before this flip. `-Werror` only earns its keep once
the tree is already clean — flipping it first would just be a wall of red with no
signal.

**Task 8's own clean-build census still found twelve stragglers** that the
per-category probe builds in Tasks 1-7 missed, because a full
`ageland`/`ageland_tests` build compiles a different (larger) translation-unit set
than a targeted probe. Ten `-Wimplicit-fallthrough` sites across six files —
`fight.cpp`'s `weapon_hit_type`, `limits.cpp`'s `affect_update_person`,
`modify.cpp`'s `string_add`, `ranger.cpp`'s `do_tame`, `shapemob.cpp`'s
`recalculate_mob`, and five in `shapescript.cpp`'s `get_parameter` — all annotated
with the standard `[[fallthrough]];` attribute, which changes neither the generated
code nor observable behavior (a fall-through switch case is still a fall-through
switch case; the attribute only tells the compiler the fall-through is
acknowledged). Eight of the ten are genuinely intentional fall-throughs; the two
dead-assignment sites (`fight.cpp` case 13→14, `shapemob.cpp`
`RACE_EASTERLING`→`default`) are marked `FIXME` in the source rather than claimed
intentional — independent evidence (`spells.h`'s `weapon_skill_num` treating cases
13/14 as distinct skills; `spec_pro.cpp`'s race→language switch giving
`RACE_EASTERLING` its own `break`) suggests each is a historical missing-break bug;
they are preserved byte-for-byte per the Phase 5 byte-identical constraint and
recorded as behavior-fix candidates for a future disclosed-delta effort. Two
`-Wformat-truncation` sites: `objsave.cpp`'s `Crash_get_file_by_name` was converted
from `snprintf` into a fixed buffer to `std::format` — the same idiom already used
two lines above it in the same function — which is both the project's sanctioned
modernization target for sprintf-family call sites (see "Formatting" below) and
immune to this class of diagnostic (no fixed-size destination buffer for the
compiler to reason about); and a test fixture literal in
`account_management_tests.cpp` that overflowed a `MAX_PWD_LENGTH+1` field was
shortened (no assertion ever inspected it). None of these were suppressions: every
one is a real, behavior-preserving fix.

**Suppression discipline still applies when a real fix isn't available:** a warning
may be silenced with a pragma/attribute only alongside a comment stating why the code
is intentional. No blanket `-Wno-*` flag exists anywhere in this policy; none is
expected to be needed for the GNU-family census going forward.

**Pinned-toolchain rationale for new-warning arrivals:** the container images
(`Dockerfile`, `Dockerfile.x64`) pin an exact `debian:trixie` base, and macOS/Windows
CI runners pin exact OS images — so a warning that's clean today does not silently
reappear from a toolchain upgrade nobody asked for. When a toolchain bump is
deliberately taken in a future phase, treat any warnings it introduces the same way
Tasks 1-7 did: real fix first, `[[fallthrough]]`/cast/restructure over suppression,
pragma-plus-comment-plus-ledger-entry only as a last resort.

**The `-funsigned-char` (GNU) / `/J` (MSVC) pin is the accepted resolution, not a
placeholder.** Phase 5 design decision 2 (`docs/superpowers/specs/
2026-07-12-phase-5-hardening-design.md`) confirmed the existing 4-compiler pin —
`-funsigned-char` on GCC/Clang/AppleClang, `/J` on MSVC, present in every build path
before this task (`src/CMakeLists.txt:80/113/324`, `src/Makefile`'s `REQ_CXXFLAGS`,
`src/tests/Makefile`'s `CXXFLAGS`) — **is** the resolution: an earlier spec draft
floated auditing char-signedness assumptions away entirely, but that audit is not
being pursued. `char` stays pinned unsigned on every platform this project builds
for; do not remove or gate the pin.

**MSVC got its own campaign, not this flip.** `/W4 /WX` for the `windows-msvc`
preset was Phase 5 Task 9's job — a staged effort (CI-driven census across 3 cycles,
triage, ~30 real fixes including UB-path bugs, documented `/wd` suppressions for
genuinely MSVC-only noise classes, then the flip), run separately because MSVC's
warning set and diagnostic text don't line up 1:1 with GCC/Clang's. As of Task 9,
`windows-msvc` builds with `/W4 /WX` and is required-green in CI alongside the
GNU-family `-Werror` jobs — both warnings-as-errors policies are now live
simultaneously.

**The `/wd4244 /wd4267` narrowing suppression was revisited rigorously and
re-affirmed (Backlog Cleanup Task 5, 2026-07-13).** Phase 5 Task 9 had suppressed
the two int-narrowing classes on a 20-site sample; this revisit ran a dedicated
census CI cycle (run 29222339494 — `/WX` and both `/wd` flags lifted by one
clearly-labeled commit, reverted immediately after harvest) and captured the
complete site list. The raw harvest was **1,167 deduplicated warning lines,
exactly matching the Task 9 census count (993 C4244 + 174 C4267 — zero drift
since)**; 100 of those lines are MSVC multi-line template-note continuations of
other sites on the list (the `with [_Ty=int]` elaborations MSVC prints for
template-argument conversions), so the census resolves to **1,067 unique
file:line sites (893 C4244 + 174 C4267) across 59 files**. Stratification of
the 1,067: world-file loaders + OLC shape editors 436; combat/spell math 215;
sizes/lengths 171 (this stratum is 100% C4267 — `strlen()`/`size_t` narrowing;
the remaining 3 of the 174 C4267 sites sit in the test-fixture stratum);
`time_t` narrowing 37; misc (indices, flags, encumb, tolower) 190; test
fixtures 18. An 84-site stratified sample was read in source,
plus a full (not sampled) read of every suspected-real site. The framing fact:
`sh_int`/`byte` are fixed-width on **all four platforms**, so C4244 truncation
behavior is identical everywhere — MSVC is merely the only compiler that diagnoses
it at `/W4` (the GNU-family `-Wall -Wextra` bar deliberately excludes
`-Wconversion`).

Real-truncation findings, three classes:

1. **Fixed — `target_data::ch_num` widened `sh_int`→`int`** (`structs.h`; 46
   warning sites). It stores `abs_number`, an `int` slot index permitted up to
   `MAX_CHARACTERS` (64000); a value above 32767 would truncate negative and
   index `char_control_array` out of bounds through `char_exists()`. Unreachable
   on current world data (concurrent character counts sit far below 32k), so the
   widening is value-preserving for every reachable input — an in-memory struct
   only, never persisted, no goldens affected.
2. **Fixed — `random_exit`'s `romfl` local widened `sh_int`→`long`** (`mage.cpp`).
   `room_flags` is a `long` bitvector; the truncation was benign-as-used (only
   bits 1/7/14 are tested) but the local now matches the field it snapshots.
3. **Not fixed, escalated as format-frozen:** player idnums stored into `sh_int`
   fields of two *persisted* record layouts — `exploit_record.shintVictimID`
   (db.cpp, 2 sites) and `crime_record_type.criminal/victim/witness` (db.cpp,
   9 sites). Real truncation once a server's idnum counter passes 32767 — but
   both layouts are frozen on-disk legacy formats (`static_assert
   sizeof(exploit_record) == 80`; `legacy_crime_file_from_binary`), and the JSON
   codecs intentionally preserve the `sh_int` width for migration compatibility.
   Widening them is a versioned format migration — a disclosed behavior/format
   change for a future owner-approved effort, recorded here rather than slipped
   through. The 37 `time_t`→`int`/`long` sites (rent/board/pkill timestamps,
   account-API `long` timestamps) are the same shape: Y2038-class narrowing
   pinned by persisted formats and identical to the shipping 32-bit binary's
   behavior; deferred to a coordinated Y2038/format effort, not fixable
   piecemeal. (The interpreter's TARGET_GOLD amount-into-`ch_num` sites are
   dead code — no command mask ever sets `TAR_GOLD`.)

**Disposition: the global `/wd4244 /wd4267` stays.** The 972 remaining sites
(1,067 − 46 `ch_num` fixed − 1 `romfl` fixed − 11 idnum-record escalated − 37
`time_t` escalated) are diffuse (spanning most of the 59 census files) and
domain-provably benign — stats bounded ≤ ~100 into
`signed char` (±127), hp/mana/move bounded ≤ ~32k into `sh_int`, `strlen()` of
`MAX_STRING_LENGTH`-bounded strings into `int`, world-file stats parsed via
`fscanf("%d")` into their canonical field widths. Narrowing the suppression to
per-file pragmas would mean annotating essentially every legacy file to silence
the same benign pattern — strictly worse than one ledgered global flag. The real
fix remains a future typed-fields effort, as the Task 9 ledger already noted.

The census is reproducible: repeat the labeled census-commit procedure (see
commits `3953857` — lift `/WX /wd4244 /wd4267` on the MSVC blocks of both
targets — and its revert `23c09d3`), push, and harvest the C4244/C4267 lines
from the `windows-msvc` job log of the resulting run (reference run:
29222339494; strip CRLF and the two-target vcxproj duplication, then drop the
template-note continuation lines to get unique sites).

**The i386 container leg (`-m32`, g++14 `debian:trixie`) is `-Werror`-clean too —
verified at Phase 5 Task 10 (finalization), as planned, not silently skipped.** The
per-task verification cadence for this phase ran macOS native + `rots64` only (see
CLAUDE.md's verification-cadence gotcha), deferring the i386 container battery to the
finalization pass; that pass has now run. Despite the `-m32` translation-unit set's
slightly different warning surface (32-bit pointer/size_t widths change what
`-Wformat`, `-Wsign-compare`, etc. can flag), the first full i386 build under
`-Wall -Wextra -Werror` — both the CMake `ageland`/`ageland_tests` targets and the
`src/tests/Makefile` path — compiled with **zero** target-specific stragglers: the
Tasks 1-8 sweeps (which included g++14 on `rots64`) had already covered everything
the 32-bit ABI could flag. Should a future change introduce an i386-only warning,
the same disposition rigor applies (real fix or restructure; pragma+comment+ledger
only as last resort).

**32-bit retirement is a future-phase trigger, not part of this warnings policy.**
The i386 preset/container/CI leg stays required — including once it starts
building under `-Werror` at Task 10 — until the depot owner confirms live player
data is fully migrated off the binary/text-legacy save formats (see AGENTS.md's
JSON-persistence-migration notes and CLAUDE.md's account-native-vs-`save_player`
split). That confirmation, not this task, is what starts the 32-bit-retirement
phase; until then `legacy_*_fixture.bin` goldens and the i386 build stay canonical
for the shipping ABI exactly as they are today.

### Formatting: `std::format` is the sanctioned target

Output composition (`sprintf`/`strcpy`/`strcat`-family call sites being modernized in
Phase 4) converts to **`std::format`**, not a third-party formatting library — the
project's standing no-third-party-libraries constraint rules out `{fmt}` even though
an earlier spec draft listed it (see the Dependencies section of
`docs/superpowers/specs/2026-07-06-cpp-modernization-design.md` for the dated
amendment). C++20's `<format>` is available on all four toolchains above, so this is
a zero-dependency, zero-portability-risk choice.

**Lesson learned in Wave 1 (reaffirmed in Wave 2, still applies to every future
`std::format` conversion):** a fixed-size `char[N]` class/struct member (e.g.
`skill_data::name`) does **not** format the same across standard libraries —
libc++ (AppleClang) formats a `char` array as a *range* (prints each
element/looks nothing like a C string), while libstdc++ decays it and prints
the string. This only surfaces as a divergence between local macOS runs and
Linux CI, not as a local build failure, so it's easy to miss. Always
`static_cast<const char*>(the_array)` (or otherwise take an explicit pointer)
before handing a `char[N]` member to `std::format` — never pass the array
itself. Wave 2's `act_othe.cpp`/`act_offe.cpp` conversions hit this again
(a `char[256]` local and a `skill_data::name` `char[50]`) and applied the same
cast; a `char*` (already a pointer, e.g. `GET_NAME`/`GET_TITLE`) needs no cast.

**World-data format-template hardening (`safe_template`, Wave 2):** some
`sprintf`-family call sites don't just format a value — they use a
**builder/world-data-supplied string as the format template itself**
(`spec_pro.cpp`'s Herald `death_cry2` mob field, `shop.cpp`'s ~10
`shop_index[].message_*`/`no_such_item*` fields). A malformed template there
(wrong conversion count/type, a stray `%n`) is undefined behavior at the
`sprintf` call, not just a wrong-looking message. `src/safe_template.h`/`.cpp`
adds `safe_template::expand_checked()`: it scans the template's conversions,
compares them against the site's expected signature (currently `%s`-only,
prefix-tolerant — a template with *fewer* conversions than the declared
signature is accepted and expands sprintf-style, matching real world-data
templates that leave trailing args unused), and only expands when the shape
matches; on mismatch it logs one `mudlog` line and returns a neutral fallback
string instead of touching the mismatched varargs. Well-formed templates —
the entire live `lib/world/` data set — expand byte-identical to the old
`sprintf`/`snprintf` call. Convert a *new* world-data-as-template site through
`safe_template`, not a raw `std::format`/`sprintf` call, since `std::format`
has no runtime concept of "validate the template against caller-supplied
data" and would reintroduce the same class of bug in a different form. As of
Wave 2 this covers `death_cry2` and the 10 enumerated `shop.cpp` fields only —
see the Wave 2 exit section of
`docs/superpowers/plans/2026-07-10-phase-4-wave-2.md` for what's still
outstanding in this template-hardening class.

### Windows: build+test green, boot verification deferred

`windows-msvc` networking runs through a hand-rolled, platform-gated socket shim
(`src/rots_net.h`/`.cpp`) — no third-party networking library. An earlier vendoring of
Asio (Task 1) was implemented, reviewed, and then **reverted** per a 2026-07-09
no-third-party-libraries decision; `platdef.h`'s Windows scaffold (winsock2 includes,
`SocketType`) survived the revert and is what the shim builds on. GoogleTest is the one
exception — it is test-only tooling, never linked into the shipping game binary, and on
Windows CI it is provisioned via CMake `FetchContent` (source-built against the exact
MSVC/CRT, cached in `actions/cache` keyed on `src/CMakeLists.txt`) since the windows-2022
runner carries no system GTest package. Every other preset — including `linux-x64`,
`linux-x64-sanitize`, and `macos-arm64` — keeps `find_package(GTest REQUIRED)` against
apt/brew packages; `linux-x64-sanitize` is not an exception just because it's a
sanitizing preset. The one other exception is `macos-arm64-asan` (Backlog T4), which
opts into `FetchContent` via the `ROTS_FETCH_GTEST` cache option (set `ON` only by that
preset) so gtest itself compiles under `-fsanitize=address,undefined` instead of linking
brew's uninstrumented prebuilt static lib — see `src/CMakeLists.txt`'s
GoogleTest-provisioning comment and the `sanitize-macos` job in
`.github/workflows/ci.yml` for the mixed-instrumentation false positive this fixes. In
short: `FetchContent` is used only by `windows-msvc` and `macos-arm64-asan`; every other
preset uses `find_package`.

What Windows CI verifies today: `cmake --preset windows-msvc`, build, and
`ctest --preset windows-msvc` — the full unit suite including the characterization
goldens (JSON goldens are platform-neutral; the combat golden's RNG is `mt19937`,
platform-independent by construction). What it does **not** verify: a live server boot
against real world data — the windows-2022 runner has no `lib/world/` and there is no
Windows workstation in this project to run one locally, so `scripts/boot-golden.sh` has
no Windows path. This is a deliberate, documented deviation from the general "boots and
passes tests" bar the other three platforms clear, not an oversight.

Enabling a Windows boot check in a future phase would need: (1) a Windows host (or a
Windows CI runner willing to accept a long-running background process) with `lib/world/`
staged onto it, and (2) either a `boot-golden.sh` Windows/PowerShell port or a
cross-platform rewrite of that script, since it currently shells out to POSIX tools
(`nc`/process-signaling) that don't exist on Windows as-is.

## Lifecycle lesson: placement-new construction demands a matching explicit destructor call (RAII Lifecycle-Audit)

`char_data`/`obj_data` are `calloc`'d raw storage (`CREATE`/`CREATE1`, `utils.h`) that
`clear_char`/`read_mobile` construct in place with `new (ch) char_data();`
(`db.cpp`) — this is required because those types carry non-trivial members
(`specialization_data`, `player_damage_details::damage_map`, and, after the RAII
Lifecycle-Audit wave, `owned_alias_list`/`std::string`/`std::vector<byte>` fields
too). Before that wave, teardown (`free_char`) never called `~char_data()` — it
hand-freed a fixed set of members and then `free()`'d the raw storage, working only
because every durable member was a POD pointer with nothing for a destructor to do.
**The construct/teardown asymmetry becomes a live bug the moment any member gains a
non-trivial destructor**: skip the explicit dtor call and a `std::string`/
`std::vector`/RAII wrapper member leaks (or, worse, its destructor never runs but the
storage is freed out from under it). RAII T6 fixed this for good: `free_char` now
calls `ch->~char_data();` immediately before `RELEASE(ch)` (`free_function`/`free`),
restoring symmetry — calloc + placement-new on construction, explicit destructor call
+ `free` on teardown. The general lesson for any type built with placement-new over
non-`new`-allocated storage: the destructor call is not optional busywork once a
non-trivial member exists, and adding such a member without auditing the paired
teardown path is the exact bug class this wave's ASan/LeakSanitizer gate (`macOS ASan`
+ `linux-x64-sanitize`) exists to catch. See `docs/superpowers/ownership-map.md` §0/§6
and `.superpowers/sdd/task-6-report.md` for the full before/after.
