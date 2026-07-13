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
formats (planned Phase 5) — at that point the 32-bit preset itself gets retired. Until
then, keep using it whenever you need to reproduce the exact shipping ABI.

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

Per-platform (from `src/`): `cmake --preset <name>`, `cmake --build --preset <name>`,
`ctest --preset <name>`. `cmake --list-presets` shows what runs on this host.

CI (`.github/workflows/ci.yml`): `legacy-32bit`, `linux-x64`, `macos-arm64`, and
`windows-msvc` are all required jobs — no job in the matrix is allowed to fail anymore.

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
runner carries no system GTest package; Linux/macOS keep `find_package(GTest REQUIRED)`
against apt/brew packages.

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
