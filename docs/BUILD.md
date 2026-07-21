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

## Container build isolation

The `rots` and `rots64` services mount `/rots/build` from a **container-private named
Docker volume**, not from the repo bind mount. This closes a class of failure that hit
finalization batteries three times on record (see `scripts/i386-battery.sh` step 0 for
the full citations): stale or cross-contaminated CMake state in the shared, non-preset
top-level `build/` tree. In order:

1. The header-split wave's finalization battery hit a `CoreLayerAcyclicity` target
   missing from a stale tree (fixed by a root-Makefile reconfigure, commit `3280c73`).
2. The entity-seed wave's i386 battery round 2 found the container's `build/` CMake
   cache poisoned with **host** paths: a root-Makefile fixer's `make -n test`
   verification still executed the `+`-prefixed `cmake --build` recipe line on the
   host (GNU make runs `+`-prefixed lines even under `-n`), regenerating `build/`'s
   cache host-side.
3. The entity-complete wave's i386 battery round 2 hit `ageland_tests` failing to link
   interpreter-tier symbols **in the container only**, with both native hosts green
   off the identical `CMakeLists.txt` — logged as the third occurrence of the same
   class.

All three trace back to host↔container bind-mount mtime skew and cache poisoning on a
`build/` tree the host and both containers used to share. Named volumes make the
cross-environment half of that (incident 2) **structurally impossible**: the
container's `build/` and the host's `build/` are no longer the same filesystem
location, so a poisoned or stale host cache can never reach a container configure, and
vice versa. This was verified by test, not just by design — a poison run planted a
bogus `CMAKE_HOME_DIRECTORY` in the host's `build/CMakeCache.txt`, then ran a container
`make configure`: the container configured fresh, with `/rots/src` as its real `-S`
source dir and the empty named volume backing its `-B` build tree, while the poisoned
host file sat untouched throughout. `scripts/i386-battery.sh` step 0 keeps pre-cleaning
the in-volume tree as belt-and-braces for the remaining same-environment staleness class
(incidents 1 and 3), which the volume alone doesn't fix. `scripts/i386-battery.sh`'s completed-step markers
are stamped per commit, so a marker from a prior HEAD never green-lights a skip once
HEAD has moved.

**Volumes:**

| Volume | Owning service | Backs |
|---|---|---|
| `rots-build-i386` | `rots` (32-bit i386) | `/rots/build` inside the `rots` container |
| `rots-build-x64` | `rots64` (64-bit x86-64) | `/rots/build` inside the `rots64` container |

Everything else under the repo bind mount (`.:/rots`) stays shared exactly as before:
`bin/`, `src/`, `lib/`, and the flat-make trees, including `src/tests` (the i386
monolithic test runner's `make tests && ../../bin/tests` flow). Only the top-level,
non-preset CMake tree at `/rots/build` moved into a container-private volume; host
CMake preset subdirectories (`build/macos-arm64/`, `build/linux-x86-legacy/`, etc.)
were never container-shared and are unaffected.

**Clean rebuild:**

```bash
docker compose down -v                                       # primary: wipe both (and any
                                                               # other compose-managed volumes);
                                                               # resolves the project-name
                                                               # prefix itself
docker volume rm rots_live_modern_rots-build-i386             # targeted: wipe just the rots
                                                               # (i386) container's tree
docker volume rm rots_live_modern_rots-build-x64              # targeted: wipe just the rots64
                                                               # container's tree
```

Compose prefixes every declared volume name with the **project name**, which it derives from
the checkout's directory name by default (here, `rots_live_modern`) — so the volumes' real
names on disk are `rots_live_modern_rots-build-i386` / `rots_live_modern_rots-build-x64`, not
the bare `rots-build-i386` / `rots-build-x64` declared under `volumes:` in
`docker-compose.yml`. A different checkout directory name (or an explicit `-p`/
`COMPOSE_PROJECT_NAME`) yields a different prefix; `docker compose down -v` resolves this
automatically for whichever checkout it's run in, which is why it's the primary form above.
We deliberately do **not** pin a literal `name:` on these volumes in `docker-compose.yml`:
the project-derived prefix is a feature, not an accident — it keeps multiple checkouts of
this repo (e.g. separate worktrees) from sharing build volumes with each other.

**Inspect a container's build tree without configuring anything:**

```bash
docker compose run --rm rots   ls /rots/build
docker compose run --rm rots64 ls /rots/build
```

**One-time migration:** checkouts predating this change had a shared, bind-mounted
`build/`. The first container run after pulling this change reconfigures each
container's tree from scratch in its (empty) named volume — a one-time configure+build
cost, not a recurring one, and independent of whatever was in the host's `build/`
before. The host's `build/` was cleaned up as part of landing this change and now holds
**only** host CMake preset subdirectories plus whatever the host's own preset builds
regenerate — no container-generated files remain there.

**CI:** no workflow change was needed. CI runners already build fresh containers per
run (`docker compose build` + `docker compose run --rm rots ...`), so
`rots-build-i386`/`rots-build-x64` are simply created empty on every run, the same as
any other named volume on a fresh host.

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

## Library layering & the foundation acyclicity check

The source tree is being carved into layered static libraries (see
`docs/superpowers/specs/2026-07-16-library-architecture-design.md`). As of the l4-seed wave,
**eight** libraries exist and are linked into `ageland` — the original eight-library sketch's six
lower layers, plus a new L4 orchestration/authoring band (`rots_pathfind`/`rots_script`) the sketch
did not anticipate (see the spec's §3 REVISION and this doc's "L4 band" subsection below).
`rots_commands`/`rots_app`, the sketch's own original L4/L5 rows, remain entirely app-compiled —
not yet extracted, and not to be confused with the new, now-real L4 band below:

```
┌─ L4  rots_pathfind   rots_script    (new orchestration/authoring band; pathfind < script,
│                                       one sanctioned intra-band edge — see "L4 band" below)
├─ L3  rots_persist   rots_world   rots_combat   (all three L3 peer domain libs now exist; each
│                                                  seeded, none complete — certified stratified
│                                                  persist < world < combat — see their sections below)
├─ L2  rots_entity    → entity/relationship operations over the data model
├─ L1  rots_core      → the (split-up) data model + const tables
└─ L0  rots_platform  → OS/infra, zero game coupling
```

The first extracted layer is `rots_platform` (L0) — foundation TUs with **no upward dependency**
on game code — built as `librots_platform.a` and linked into `ageland`. All first-party targets
share their ABI/behavior flags through the `rots_build_flags` INTERFACE library.

- **`rots_platform_linkcheck` / CTest `PlatformLayerAcyclicity`** enforce the "no upward edge"
  property in a load-bearing way: a tiny executable force-loads the entire `librots_platform.a`
  (`-Wl,--whole-archive` on GNU ld, `-Wl,-force_load` on macOS ld64) and links it against **only**
  libc/libstdc++ — no game libraries. If any platform TU references a game symbol, it becomes an
  unresolved-symbol **link error that fails the build**. `LINK_DEPENDS` on the archive forces a
  relink whenever the archive content changes, so an upward edge introduced on an incremental build
  is caught, not silently skipped. GNU-family only (`if(NOT MSVC)`); MSVC's guarantee is structural
  (the target links only `rots_build_flags`). This superseded an earlier `nm`-denylist shell check,
  which could only catch hand-listed symbols and gave false assurance.
- `rots_platform` now has 10 member TUs: the original 7 verified-clean leaves (`rots_net.cpp`,
  `rots_crypt.cpp`, `rots_rng.cpp`, `clock.cpp`, `crashsave_schedule.cpp`, `json_utils.cpp`,
  `player_file_finalize.cpp`) plus `rots_log.cpp` and `safe_template.cpp`, added by the logging-seam
  follow-on (spec §13), plus `rots_util.cpp`, added by entity-seed Task 4's platform-helper
  relocations. `safe_template.cpp` rejoins as a genuinely clean leaf: it called `vmudlog`
  (an L2 symbol) and now that call resolves inside `rots_platform` itself.
- **`rots_util.cpp`** (entity-seed Task 4) holds `create_function`/`free_function` (the
  `CREATE`/`RELEASE` macro backers), `str_dup`/`str_cmp`/`str_cmp_nullable`,
  `rots_remove`/`rots_rename_replace`, and the `number()`/`number(int,int)` RNG wrapper family
  (plus its TESTING-only `rots_test_random_hook` seam) — all relocated verbatim from
  `utility.cpp`, each already platform-pure (no comm/db/handler header, no game type).
  `log()`/`mudlog()` moved the same wave into the existing `rots_log.cpp`, as thin forwarders
  onto `rots::log::write_stderr`/`write`.
- **The logging seam (`rots/platform/log.h` + `rots_log.cpp`)** gives `rots_platform` a sink-based
  logging facility instead of a direct call up into the game. The platform layer owns the raw
  timestamped stderr write and the notification path (`rots::log::write_stderr`/`write`, plus
  `vmudlog`, all self-contained in L0); the app layer registers the game-coupled broadcast sink
  (`comm.cpp`'s `register_mudlog_broadcast_sink()`, holding the `descriptor_list` walk / `PRF_LOG`
  gating / color framing verbatim) at boot, in `run_the_game()`, before the first `log()` call. With
  no sink registered, notification is a no-op — byte-identical to today's pre-boot behavior — which
  is the property `rots_convert` will rely on to link `rots_platform` without pulling in any game
  code.

The second extracted layer is `rots_core` (L1) — `config.cpp` (configuration defaults),
`consts.cpp` (data tables, entity-seed Task 2), and `output_seam.cpp` (the game-output seam,
entity-seed Task 3 — see "Output seam" below) — built as `librots_core.a` and linked into
`ageland` as `RotS::core`. It links `rots_build_flags` PUBLIC (same ABI-parity requirement as
`rots_platform`) and, as of Task 3, also **PUBLIC-links `RotS::platform`**: `output_seam.cpp`'s
null-sink default logs a tripwire through `rots::log::write_stderr` when nothing has registered
yet — a legal L1→L0 edge, and the *only* one in the archive (neither `config.cpp` nor `consts.cpp`
references a platform-layer symbol). `rots_core_linkcheck`/`CoreLayerAcyclicity` (below) enforces
exactly this: it force-loads `rots_core` and normal-links `RotS::platform` to resolve that one
legitimate edge, so anything else left unresolved is a real, unintended upward reference.

`consts.cpp` was originally held back from `rots_core` (db-split-era history, preserved below for
context) and has since joined it. Its first traceable upward edge, `get_guardian_type` (it read
`db.cpp`'s `mob_index` table), was relocated to `utility.cpp` (an L2/app-layer unit,
unchanged signature/declaration) ahead of the original extraction attempt — `consts.cpp` still
defines the `guardian_mob` data table itself, referenced by `utility.cpp` via a local `extern`
declaration, matching the function's pre-existing local-extern idiom. Building `rots_core` with
`consts.cpp` included then surfaced a second, structural upward edge the relocation didn't touch:
`consts.cpp`'s `skills[MAX_SKILLS]` table initializer used to embed ~69 function pointers directly
to `spell_*()` implementations defined in `mystic.cpp`/`spell_pa.cpp` (both L2/app-layer) —
confirmed by `nm -uC` on the built `consts.cpp.o`, which resolved every one of those symbols as
undefined.
Entity-seed Task 1 cut that edge at the root: `skills[]`'s function-pointer column is now populated
at boot by `assign_spell_pointers()` (`spell_pa.cpp`), not embedded in `consts.cpp`'s static
initializer, so `consts.cpp` compiles down to pure data. Entity-seed Task 2 then moved `consts.cpp`
into `ROTS_CORE_SOURCES`; `rots_core_linkcheck`/`CoreLayerAcyclicity` (below) is the acceptance
proof that the built archive resolves only libc/libstdc++ symbols (plus the one sanctioned
`RotS::platform` edge above). One consequence: `rots_convert` (full link line under
"`rots_convert`" below) now links `consts.cpp`'s REAL data tables (`race_affect[]`/`max_race_str[]`/`skills[]`/
`get_skill_array()`/`language_number`/`language_skills`/`race_abbrevs[]`/`square_root[]`/
`global_release_flag`/`get_encumb_table()`/`get_leg_encumb_table()`) instead of the verbatim data
duplicates `convert_stubs.cpp` used to carry for them — with all-null `skills[].spell_pointer`
entries, since `rots_convert` never calls `assign_spell_pointers()` either, exactly matching the
historical stub contract.

- **`rots_core_linkcheck` / CTest `CoreLayerAcyclicity`** mirror the `rots_platform_linkcheck`
  pattern (whole-archive force-load of `librots_core.a`, `LINK_DEPENDS` on the archive,
  `if(NOT MSVC)` gating, CTest registration gated on `BUILD_TESTING`) — with one deliberate
  difference: it normal-links `RotS::platform` alongside libc/libstdc++, to resolve
  `output_seam.cpp`'s one sanctioned L1→L0 edge (above); anything else left unresolved is a real
  upward reference. `ageland_tests` deliberately does **not** link `rots_core` — like
  `rots_platform`, it keeps compiling `config.cpp`, `consts.cpp`, and `output_seam.cpp` directly
  (TESTING parity: the test binary's flag set differs from the shipping build's, so it recompiles
  every layer's sources itself rather than linking the shipping libraries).
- The root `Makefile`'s `test` recipe builds `rots_platform_linkcheck` and `rots_core_linkcheck`
  by name (it builds named targets, not `all`) — omitting either leaves its CTest "Not Run"
  instead of actually exercising the check, exactly the i386-battery failure mode the first
  linkcheck target already fixed once. As of entity-seed Task 6, it also builds
  `rots_entity_linkcheck` by name for the same reason (see below).

### `rots_entity` (L2): the entity-lifecycle library

The third extracted layer is `rots_entity` (L2) — as of the placement-seam wave, **8 TUs**:
`entity_lifecycle.cpp`, `object_utils.cpp`, `environment_utils.cpp`, `char_utils.cpp`,
`char_utils_combat.cpp`, `placement.cpp`, `containment.cpp`, and `equipment.cpp` — built as
`librots_entity.a` and linked into `ageland` (and, as of entity-completion Task 6,
`rots_convert`, see below) as `RotS::entity`. The first three TUs are the `db.cpp`-split's
"unforeseen fourth TU" and its siblings (spec §4a), scrubbed by entity-seed Tasks 1-5 of every
upward edge:
consts data relocated to `rots_core`, platform helpers relocated to `rots_platform`, and the three
remaining game-coupled edges (game output, char teardown, attack-speed) inverted through
null-defaulted seams instead of direct calls (see "Output seam" and "Entity hooks" below).
`rots_entity` links `RotS::core` + `RotS::platform` PUBLIC (its two legal downward edges) plus
`rots_build_flags`; it has no `include/` directory of its own yet — its headers stay flat in
`src/`, the same way the platform/core headers did before those layers' own header carve-outs.

- **`rots_entity_linkcheck` / CTest `EntityLayerAcyclicity`** mirrors the `rots_core_linkcheck`
  pattern: force-load `librots_entity.a` and normal-link only `RotS::core` + `RotS::platform` to
  resolve its legitimate downward edges — anything else unresolved fails the build.
- **The two-STOP story:** standing up the linkcheck surfaced two rounds of residual
  `char_utils.cpp` welds carried in by Task 5's relocated bodies — first `utils::is_npc`/
  `utils::get_prof_points`/`specialization_data::set` (which pulled in the entire leaf-clean
  `specialization_info` method family, since vtables are emitted at the key function), then that
  family's own two remaining calls into `utils::get_name`/`utils::is_race_good`/
  `utils::is_race_magi` — each round adjudicated, verified leaf-clean by `nm`, and relocated into
  `entity_lifecycle.cpp` rather than stubbed, so `rots_entity`'s membership at entity-seed exit was
  still just the three TUs above, with a larger interior than the original seed.
- **`char_utils.cpp` + `char_utils_combat.cpp` joined (entity-completion Task 3).** `rots_entity`
  is now **5 TUs**: `entity_lifecycle.cpp`, `object_utils.cpp`, `environment_utils.cpp`,
  `char_utils.cpp`, `char_utils_combat.cpp`. Getting there was membership, not relocation — EC
  Tasks 1-2 had already cut both TUs' last real upward edges (`fname`/`fname_nameholder`/
  `other_side`/`other_side_num` relocated to `char_utils.cpp` itself; `attack_hit_text[]`/
  `get_hit_text` relocated to `consts.cpp`; the `wild_fighting_handler`
  construct-and-query and `big_brother::on_character_attacked_player()` calls inverted through
  two new `entity_hooks.h` hooks, wild-attack-speed-multiplier and attacked-player) — so both
  files were already `nm`-clean leaves by the time Task 3 moved them into `ROTS_ENTITY_SOURCES`.
  `EntityLayerAcyclicity` went green first attempt, no cascade. **This is the milestone that
  emptied `src/convert_stubs.cpp`:** the weld ledger the db-split wave introduced with ~40
  documented stub bodies (~1.6K lines) shrank to ~19 at entity-seed exit, to 5 (four named
  groups) at persist-split exit, to 2 bodies (the `wild_fighting_handler` pair) after EC Task 1,
  to zero stub bodies after EC Task 2's hook inversion — and EC Task 3 deleted the now-empty file
  outright (`git rm src/convert_stubs.cpp`). `rots_convert` (below) compiles down to
  `convert_main.cpp` alone, linking `RotS::platform` + `RotS::core` + `RotS::entity` +
  `RotS::persist`; the persistence boundary this whole ledger existed to document is now enforced
  structurally by those four libraries' linkchecks plus this executable's own link, not by a
  hand-maintained stand-in file. The pattern held for three full waves (entity-seed,
  persist-split, entity-completion) before running its course — see "`rots_convert`" below for
  the per-task stub-count account and git history (culminating in the "feat: char_utils +
  char_utils_combat join rots_entity; convert_stubs.cpp deleted — weld ledger ZERO" commit,
  entity-completion wave; see `git log -- src/convert_stubs.cpp`) for the ledger's own
  before/after.
- **Remaining spec §3 gap, as of entity-completion Task 3 (superseded below):** `rots_entity` was
  5 of the 6 TUs spec §3's original target-architecture table sketched for it (`char_utils,
  object_utils, environment_utils, handler, utility, char_utils_combat`) — `handler.cpp` and
  `utility.cpp` were still entirely `ROTS_SERVER_SOURCES`/app-compiled, not archived.
  `handler.cpp` carried roughly 30 named, not-yet-enumerated welds into combat/world/commands-tier
  code (recorded but unresolved as of entity-completion Task 3). `utility.cpp` had never been
  `nm`-profiled TU-wide the way `char_utils.cpp` was — its `nm` reach was app-wide, not a short
  named list — so its gap was a genuinely open question, not a known-and-counted one. Neither was
  entity-completion's scope; both were recorded follow-on for whichever future wave next touched
  the entity/app boundary. That wave was the placement-seam wave — see below.

### The placement-seam wave: a tier-line split of `handler.cpp`/`utility.cpp` (Stage 1, scoped)

Design: `docs/superpowers/specs/2026-07-19-placement-seam-design.md` (spec §7 Stage 1). Instead of
carving `handler.cpp`/`utility.cpp` whole (which would need ~10-15 new hooks serving functions
that are arguably app-tier anyway), the owner-approved decision was a **tier-line split**: pull the
entity-pure relationship core — placement, containment, equipment mutation, and assorted pure
helpers — into three new `rots_entity` TUs, and leave the session/command/combat-coupled remainder
app-compiled, calling down into the moved primitives. This carved ~60 functions across five
implementation tasks (Task 1 seam foundation, Task 2 containment/object_utils, Task 3 placement/
equipment SPLITs, Task 4 handler.cpp remainder, Task 5 utility.cpp carve), landing 34 new targeted
tests in Task 6 (ctest 1281 → 1315, both hosts; see AGENTS.md). The game-wide call-site conversion
campaign (~754 `->in_room`, ~576 `world[...]`, ~104 `next_in_room` sites) and Stage 2
(`LocationSystem`, the representation swap itself) are explicitly out of scope for this wave.

**The four-resolver world seam.** The spec anticipated one resolver; the census (and a Task 1
implementation-time STOP) surfaced the need for four, all registered by one function:

- `room_data* room_by_id(int rnum)` — **nullptr-on-invalid.** For callers that historically had
  their own explicit bounds check before indexing `world[]`; the moved bounds check becomes a
  nullptr check. Its exclusive upper bound is `top_of_world`, which excludes the live dummy room
  *at* that index — a documented caveat, not a bug: callers needing the dummy room use the TOTAL
  variant below.
- `room_data* room_by_id_total(int rnum)` — **operator[] fallback semantics.** `room_data`'s own
  `operator[]` is a graceful *total* function (log-and-return-a-fallback-room), not partial —
  Task 1's reviewer caught this and overturned the controller's initial "nullptr is a faithful UB
  substitute" framing: a plain nullptr contract would have silently *narrowed* previously-defined
  behavior for the (unchecked, in original code) callers that relied on that fallback, e.g.
  `get_char_room`. **Why two variants exist, not one:** the two resolvers preserve two genuinely
  different pre-existing contracts along the same call boundary — bounds-checked movers keep their
  checked (now nullptr-on-out-of-range) semantics via `room_by_id`; historically-unchecked movers
  keep their total, always-returns-something semantics via `room_by_id_total`. The binding
  per-site rule carried into every later task's brief: unchecked `world[...]` reads →
  `room_by_id_total`; bounds-checked reads → `room_by_id`.
- `zone_data* zone_by_id(int znum)` and `index_data* obj_index_by_id(int item_number)` —
  **nullptr-on-invalid only** (no TOTAL variant). `zone_table`/`obj_index` are raw C arrays with no
  `operator[]` fallback to preserve — genuine UB before this wave, so the nullptr contract is a
  strict improvement, not a narrowing. This asymmetry with the room resolvers is intentional and
  documented at each call site.

All four dispatch through hook storage in `placement.cpp`, with a tripwire-abort default (no safe
placeholder pointer) if queried before registration. `entity_hooks.h` declares the hook types and
setters; `db_world.cpp` implements `room_by_id_impl`/`room_by_id_total_impl`/`obj_index_by_id_impl`
and `register_world_resolver_hooks()` (declared in `db.h`); `zone_load.cpp` implements
`zone_by_id_impl` (declared in `zone.h`). `register_world_resolver_hooks()` is called once from
`run_the_game()` (alongside the other pre-`boot_db()` registrations) and once from
`gtest_main.cpp` — parity, so the test binary observes the same seam.

**SPLIT wrappers: lib primitives behind app-facing public names.** Three functions moved by
splitting rather than verbatim relocation — the public name and declaration stayed in
`handler.h`/`handler.cpp`, backed by a new status-returning primitive in the lib:

- `char_from_room(ch)` (app, `handler.cpp`) now calls `bool detach_char_from_room(ch)` (lib,
  `placement.cpp`; unlink from the occupant chain, light-count decrement, zone white/dark-power
  decrement via `zone_by_id`, clear `in_room`/`next_in_room`) and, only when it returns `true`
  (mapping the ORIGINAL's two early-return paths to `false`), runs the trailing `stop_fighting`
  loop that stayed app-side because it reaches into combat. 37 call sites unchanged.
- `equip_char(ch, obj, pos)` (app) now calls `EquipAttachOutcome attach_equipment(ch, obj, pos)`
  (lib, `equipment.cpp`) — a 5-arm enum (`HOLD_EARLY_RETURN`, `WEAPON`,
  `WEAPON_TOO_HEAVY_ONE_HAND`, `WEAPON_TOO_HEAVY_FOR_YOU`, `OTHER`) rather than a bare bool. **The
  arm design exists because of a round-2 review-caught Critical:** an earlier version of this split
  evaluated the too-heavy check (`GET_OBJ_WEIGHT(obj) > GET_BAL_STR(ch)*50 && !IS_TWOHANDED(ch)` /
  `*100`) in the app wrapper, *after* `attach_equipment()` had already run `affect_modify()`/
  `affect_total()` — but those calls can mutate the very stats the check reads (an `APPLY_STR`
  affect changes `GET_BAL_STR`; a bitvector affect can set `AFF_TWOHANDED`), an observable
  message-behavior change invisible to goldens and missed by a line-presence-only reassembly
  audit. The fix moved the check back into the primitive at the ORIGINAL's exact pre-affect
  position, and made its *outcome* (not a raw bool) the thing that crosses the call boundary, so
  the wrapper can select the right `send_to_char()` text without re-evaluating any stat comparison
  itself. `src/tests/placement_tests.cpp`'s `TooHeavyVerdictUsesPreAffectStrengthNotPostAffectRegression`
  test pins this bug class (a weapon carrying an `APPLY_STR` affect must not have that affect
  change its own too-heavy verdict).
- `unequip_char(ch, pos)` (app) now calls `obj_data* detach_equipment(ch, pos)` (lib,
  `equipment.cpp`), which — unlike the other two SPLITs — needed no new status enum: its trailing
  poison-damage/`raw_kill` block (app-tier `send_to_char`/`act`/`damage`/`raw_kill`) stayed in the
  wrapper, gated on `was_poisoned = IS_AFFECTED(ch, AFF_POISON)` captured before the call vs.
  `IS_AFFECTED(ch, AFF_POISON)` read after it returns. The primitive's own `(pos == HOLD) &&
  !CAN_WEAR(obj, ITEM_HOLD)` early return happens strictly before any state that check reads could
  change, so it VERIFIABLY self-gates: whenever that guard fires, the poison block's own condition
  (`was_poisoned != 0 && !IS_AFFECTED(ch, AFF_POISON)`) is false by construction, and no explicit
  signal needs to cross the call boundary for it — the wrapper's existing null-check on the
  returned `obj_data*` already reproduces the ORIGINAL's control flow. The `mudlog` zero-object
  guard is L0-legal and stays in the primitive.

**Three honest deferral clusters (Stage 1 scope, not oversights). ALL THREE now RETIRED — the
first two by the combat-seed wave's riders (Tasks 2-3), the poison cluster by the blocker-buster
wave's Task 3:**

1. **`obj_from_char`/`extract_obj` — RETIRED (blocker-buster Task 3).** The original STOP-check
   found a real counter-example, not a hypothetical one: `script.cpp`'s `SCRIPT_OBJ_FROM_CHAR` can
   hand `obj_from_char` an *equipped* item, which routed through `unequip_char`'s poison-damage
   block — app-tier — so moving `obj_from_char`/`extract_obj` down would have left lib code calling
   up into app. Confirmed via `script.cpp:817`/`:1461`. Blocker-buster Task 3 closed the gap with a
   **poison-notification hook** (`entity_hooks.h`'s `set_poison_removal_hook`/`poison_removal_fn`,
   the `entity_hooks.h`-pattern registered callback this section's older text anticipated):
   `containment.cpp`'s `obj_from_char()` captures `was_poisoned` before calling `detach_equipment()`
   and fires the hook — instead of calling the app-tier `unequip_char()` wrapper directly — only when
   that detach just cured the wearer's poison affect. `fight.cpp` registers the real implementation
   (a byte-for-byte replay of the wrapper's `damage()`/`raw_kill()` block) in `run_the_game()` before
   `boot_db()`, with `gtest_main.cpp` parity. A characterization test
   (`src/tests/poison_notification_tests.cpp`, commit `ddf8164`) was written and red-proofed
   **against the pre-inversion code first** — driving the real `script.cpp` `run_script()` path
   (not the documented fallback) — then re-run byte-identical (`git diff --stat` shows no changes)
   after the inversion as the identity proof. `obj_from_char()` moved to `containment.cpp` and
   `extract_obj()` to `object_utils.cpp` (both `rots_entity`, L2), verbatim; `EntityLayerAcyclicity`
   confirms the upward edge is gone. Unregistered-fire is a tripwire log with no abort (a void hook
   has nothing to dereference; see the hook's own comment for the reachability analysis — it never
   fires unregistered in `ageland` or `rots_convert`).
2. **`parse_numbered_name`/`get_char` — RETIRED (combat-seed Task 3).** `NumberedName` moved to
   `rots/platform/numbered_name.h` (see the `rots_combat` section's "NumberedName's L0-visibility
   tier decision" note below); `parse_numbered_name` → `rots_util.cpp` (L0),
   `get_char` → `entity_lifecycle.cpp` (L2), both verbatim. `handler.h` now compatibility-includes
   the new header so no caller changed.
3. **`real_time_passed`/`mud_time_passed`/`day_to_str`/`age` — RETIRED (combat-seed Task 2).** All
   four moved verbatim from `utility.cpp`/`act_info.cpp` into `consts.cpp` (L1 `rots_core`,
   co-located with `month_name[]`), the natural fit their `time_info_data`-by-value /
   `month_name[]` dependency (both L1) already pointed at. Declarations stayed in `utils.h`
   unchanged.

Beyond those three clusters, a broader **STAY-APP inventory** stayed app-compiled by design in the
placement-seam wave (per-function detail: `.superpowers/sdd/placement-census.md`): the **visibility
family** (`CAN_SEE`/`CAN_SEE_OBJ`, `get_char_room_vis`/`get_player_vis`/`get_char_vis`/`get_obj_vis`/
`get_obj_in_list_vis`/`get_object_in_equip_vis`, `generic_find`, `PERS`); the **proto family**
(`compare_obj_to_proto`, `obj_to_proto`, `check_inventory_proto`/`check_equipment_proto`/
`check_container_proto`); **session helpers** (`find_playing_char`, `echo_off`/`echo_on`); the
**zone-power pair** (`recalc_zone_power`, `report_zone_power` — world-side bookkeeping, arguably
`rots_world`'s if anything); and `get_real_OB`/`get_real_parry` (see the OB/PB/DB trio note below).
**As of the blocker-buster wave (Tasks 4/4b), the visibility family (all eleven functions except
`PERS`) and `get_real_OB`/`get_real_parry` have LEFT this inventory** — see "`rots_combat`" below
for their new `visibility.cpp` home. What remains app-compiled by design: the **proto family**,
**session helpers**, the **zone-power pair**, and `PERS` (census-confirmed unchanged — `CC_USE`/
`CC_NORM` color-global deps, `color.cpp` is app-tier). Each is app-coupled for a stated reason
(output, combat, session, world-index, or spec-handler dependency) — none is an unexamined gap.

**The OB/PB/DB trio reunites across library tiers (final state, blocker-buster Task 4).**
`get_real_dodge` moved to `char_utils_combat.cpp` (lib, `rots_entity`, L2) in placement-seam Task 5;
`get_real_OB`/`get_real_parry` — held back at that time by their `player_spec::
weapon_master_handler` dependency, then still app-tier — moved to `visibility.cpp` (lib,
`rots_combat`, L3) in blocker-buster Task 4, once `weapon_master_handler.cpp` (combat-seed Task 1)
made that dependency an intra-lib peer reference instead of an app edge. All three trio members are
now library code, split across the L2/L3 tier line rather than STAY-APP; AGENTS.md's Dead/Unused-Code
trio paragraph records this final split (see below) — do not confuse it with the *deleted*
`utils::`-namespaced weather/room-arg trio from the `combat_manager` deletion, which is unrelated
history.

### Output seam and entity hooks: the last three app-layer edges into `rots_entity`

Two dependency-inversion seams (spec §13 pattern) let `entity_lifecycle.cpp` keep calling
game-output and app-registered behavior without `rots_entity` linking upward into `comm.cpp`,
`objsave.cpp`, or `wild_fighting_handler.cpp`:

- **Output seam (`output_seam.h`/`output_seam.cpp`, entity-seed Task 3).** Five global symbols —
  `send_to_char` (both overloads), `vsend_to_char`, `act`, `track_specialized_mage`,
  `untrack_specialized_mage` — are defined in `output_seam.cpp` (joins `rots_core`, see above) as
  forwarders through a null-defaulted `rots::output::Sinks` aggregate of plain function pointers.
  `comm.cpp`'s `register_game_output_sinks()` installs the real bodies (`act_impl`,
  `track_specialized_mage_impl`, `untrack_specialized_mage_impl`, and the desc-delivery/
  descriptor-list-walk `send_to_char` bodies). A null sink logs one tripwire line and returns —
  exactly `rots_convert`'s historical `convert_stubs.cpp` stub semantics for these five symbols.
- **Entity hooks (`entity_hooks.h`, entity-seed Task 5).** Two hooks let `entity_lifecycle.cpp`
  invert its remaining app-layer edges: `char_teardown_fn` (fired by `free_char()` for every
  destroyed character; `objsave.cpp`'s `register_char_teardown_hook()` installs
  `clear_account_backed_object_bytes_for_character`) and `attack_speed_fn` (queried by
  `recalc_abilities()`'s weapon branch; `wild_fighting_handler.cpp`'s
  `register_attack_speed_multiplier_hook()` installs the real
  `player_spec::weapon_master_handler`-backed implementation). Backing storage and dispatch live
  in `entity_lifecycle.cpp` itself (unlike the output seam, there is no separate `.cpp`). Each
  null default is a provable no-op/neutral value, not just "unreachable in practice" — see the
  hook comments in `entity_lifecycle.cpp` for why.
- **Registration order.** All four registrations — `register_mudlog_broadcast_sink()`,
  `register_game_output_sinks()`, `register_char_teardown_hook()`,
  `register_attack_speed_multiplier_hook()` — happen in `run_the_game()`, in that order, before
  `boot_db()` runs. `rots_convert` never calls any of them, so it always observes the null-default
  (logged no-op) behavior — the same contract the deleted `convert_stubs.cpp` stubs used to
  hand-carry for these symbols.

### `rots_persist` (L3): the persistence library

The fourth extracted layer is `rots_persist` (L3) — 14 TUs: `db_players.cpp`, `character_json.cpp`,
`objects_json.cpp`, `exploits_json.cpp`, `account_management.cpp` (+ its six `#include`d
fragments), `account_cache.cpp`, `obj_files.cpp`, `pkill_json.cpp`, `mail_json.cpp`,
`boards_json.cpp`, `convert_exploits.cpp`, `convert_plrobjs.cpp`, `color_convert.cpp`, and
`save_benchmark.cpp` — built as `librots_persist.a` and linked into both `ageland` and
`rots_convert` (see below) as `RotS::persist`. It PUBLIC-links `RotS::entity` + `RotS::core` +
`RotS::platform` (its three legal downward edges) plus `rots_build_flags`, and PUBLIC-owns
`persist/include` (`target_include_directories(rots_persist PUBLIC persist/include)`) — unlike
`rots_entity`'s PRIVATE include (no external consumer needs `rots_entity`'s own headers), several
`ROTS_SERVER_SOURCES` files that stay app-side (`db_boot.cpp`, `db_world.cpp`, `act_info.cpp`,
`act_wiz.cpp`, `interpre.cpp`, `objsave.cpp`, `savebench.cpp`, `color.cpp`) also reach
`persist/include` headers directly and now compile alongside `RotS::persist` inside the `ageland`
target, so the include root has to flow transitively.

- **`rots_persist_linkcheck` / CTest `PersistLayerAcyclicity`** mirrors the `rots_entity_linkcheck`
  pattern: force-load `librots_persist.a` and normal-link only `RotS::entity` + `RotS::core` +
  `RotS::platform` to resolve its legitimate downward edges — anything else unresolved fails the
  build. It is the acceptance proof that persist-split PS Task 4's two inversions and nine
  relocations (below) actually closed every upward edge `nm -uC` surfaced. Both hosts' ctest
  baseline moved from 1273 to **1274** the task this check was added.

**The four-carve story (PS Task 2).** `pkill.cpp`, `mail.cpp`, `boards.cpp`, and `objsave.cpp` each
mix a pure JSON codec namespace with runtime/bridge/gameplay code that walks live game state — but
in every one of the four, the codec half turned out to already be a single contiguous, leaf-clean
block (only callees: file I/O helpers, `json_utils`, `rots::text`, `std::strerror` — zero live-game
globals, zero messaging), so each carve was a verbatim single-cut block move into a new TU, not a
function-by-function extraction:
- `pkill_json.cpp` ← `pkill.cpp:50-427` (15 definitions — 10 namespace-scope + 5 file-local,
  incl. `convert_legacy_pkill_file`'s
  verify-reparse + `.migrated` rename). `pkill.cpp` keeps the runtime/capture half (`pkill_tab`/
  rankings/`combat_list` walkers, the `pkill_read_file`/`pkill_delete_file`/`pkill_update_file`
  bridge).
- `mail_json.cpp` ← `mail.cpp:134-536`. `mail.cpp` keeps the runtime store
  (`find_char_in_index`/`persist_mail_or_log`/`index_mail`/`scan_file`/`has_mail`) and postmaster
  gameplay.
- `boards_json.cpp` ← `boards.cpp:761-1189`. `boards.cpp` keeps the display half
  (`descriptor_list` walks, `page_string`), and the persist bridge `save_board`/
  `apply_board_save_data`/`load_board` (they read `msg_storage[]`/write HTML) — the library carve
  deliberately does NOT chase these; they move only when boards' runtime half gets its own split
  (recorded follow-on).
- `obj_files.cpp` ← `objsave.cpp:92-477` (the account-backed object staging map, key/take
  helpers, path/JSON-write helpers, `Crash_get_filename`/`Crash_delete_file`/
  `Crash_delete_crashfile`/`Crash_clean_file`/`update_obj_file`, `register_char_teardown_hook`)
  plus the scattered pure tail helpers `Crash_is_unrentable`/`cost_per_day`/`secs_to_unretire`.
  `objsave.cpp` keeps `Crash_obj2record`/`Crash_collect_objects` (they read `obj_index[]`, called
  only by the G-orchestrators `Crash_crashsave`/`idlesave`/`rentsave`/`Crash_collect_followers`,
  which stay app-side) and
  the alias/rent-report helpers. The dead file-scope `FILE* fd;` (zero references) was dropped —
  the carve's one deliberate deletion, not a relocation.

Six shared anonymous-namespace helpers needed promoting out of anonymous namespace during these
carves (mail ×2, boards ×1, obj_files ×3) — each verified either needed cross-TU or safely
file-local-prototyped, never silently duplicated. Four header decl-only additions
(`pkill.h`/`mail.h`/`boards.h`/`handler.h` already declared everything else).

**`color_convert.cpp` membership (PS Task 4, not a relocation).** It already had zero comm/game
dependency — PS Task 1 carved it out of `color.cpp` as a leaf TU for exactly this purpose (see
"Testing" below) — so this task just moved its *library membership* from `ROTS_SERVER_SOURCES`/
`rots_convert`'s direct source list into `ROTS_PERSIST_SOURCES`, since its callers
(`db_players.cpp`'s `load_char`/`load_char_from_text`, `character_json.cpp`'s truecolor codec) are
now both persist-tier.

**`save_benchmark.cpp` joins; `savebench.cpp` defers.** `save_benchmark.cpp` is `nm`-clean (every
undefined symbol resolves inside the candidate set or in `RotS::entity`) and joined
`ROTS_PERSIST_SOURCES`. `savebench.cpp` does not — it calls `page_string()` (`modify.cpp`, app
layer) — and stays in `ROTS_SERVER_SOURCES`, direct-compiled into `ageland` only. This is a
recorded deferral, not an oversight: cutting `savebench.cpp`'s `page_string()` edge is follow-on
work for whichever wave next touches the app/persist boundary.

**Two persist hooks (`persist_hooks.h`, mirroring `entity_hooks.h`'s spec §13 pattern).**
`db_players.cpp`'s last two upward edges — onto `db_world.cpp` and `db_boot.cpp` — inverted via
pre-boot registration instead of relocation, since the real bodies genuinely belong on the app
side (one reads `world[]`, the other walks `combat_list`):
- **`world_room_vnum` inversion.** `save_char`'s load-room fallback now calls
  `rots::persist::dispatch_room_vnum` through a hook slot; `db_world.cpp` registers the real
  `world_room_vnum` (`return world[room_index].number;`) in `run_the_game()`, pre-boot. The null
  default is a tripwire log + `NOWHERE` — byte-identical to the converter's now-deleted
  `convert_stubs.cpp` stand-in, for the same proven-unreachable reason (see `convert_main.cpp`'s
  load-room checkpoint comment).
- **`add_exploit_record` inversion.** `rename_char`'s exploit-trail note now calls
  `rots::persist::dispatch_exploit_capture`; `db_boot.cpp` registers the real capture-not-codec
  `add_exploit_record` the same way. Null default is a loud tripwire no-op, matching the deleted
  stub's semantics (`rename_char` is unreachable on the converter's load/store/save call graph).

**Nine controller-adjudicated relocations (PS Task 4 Step 2 `nm` census)**, verbatim moves each
argued leaf-clean at its new home:
- `find_player_in_table`/`find_name`/`unaccent` — `interpre.cpp`/`utility.cpp` → `db_players.cpp`.
- `recalc_skills` — `spec_pro.cpp` → `entity_lifecycle.cpp`. This one changes what
  `rots_convert` executes, not just where: `convert_stubs.cpp` used to carry a *simplified
  hand-duplicated stand-in* (language-only, omitting the `ch->knowledge[]` recomputation) rather
  than a tripwire, because `store_to_char()` calls `recalc_skills` unconditionally. It now runs
  the real body; `ConvertEquivalence` 17/17 is the proof that `ch->knowledge[]` (a runtime-only
  derived field, never present in `char_file_u`/`char_to_store`'s output) stays output-invisible
  either way.
- `utils::set_tactics`/`utils::set_shooting`/`utils::set_casting` — `char_utils.cpp` →
  `entity_lifecycle.cpp`. `set_casting`'s body substitutes `!is_npc(ch)` for the original
  `is_pc(ch)` check (proven equivalent over `char_data`'s NPC/PC partition before the move).
- `file_to_string`/`file_to_string_alloc` — `db_boot.cpp` → `db_players.cpp`. Verbatim; these were
  the second reachable-and-duplicated stand-in pair in the old ledger (`load_player()` calls
  `file_to_string_alloc()` for every legacy text-format pfile), so this relocation is also a
  stand-in-to-real-definition swap, not just a membership change.

**Result:** the weld ledger (`convert_stubs.cpp`) shrank from ~15 entries to the 5 stub function
bodies documented in "`rots_convert`" below.

### `rots_world` (L3): the world-data library

The fifth extracted layer is `rots_world` (L3) — **4 TUs**: `db_world.cpp`, `zone_load.cpp`,
`weather.cpp`, and (l4-seed wave) `zone.cpp` itself — built as `librots_world.a` and linked into
`ageland` as `RotS::world`. It is the second of the spec's three L3 peer libraries to actually exist
as a build target (`rots_persist` was the first; `rots_combat` — the third — is now also extracted,
see "`rots_combat`" below, so all three rows of the spec's §3 L3 peer tier are seeded). `rots_world`
PUBLIC-links `RotS::persist` + `RotS::entity` + `RotS::core` + `RotS::platform` (all four legal
downward edges its TUs' undefined symbols resolve into) plus `rots_build_flags`; like `rots_entity`,
it owns no `include/` directory of its own — `persist/include`/`core/include`/`platform/include` all
flow to it transitively through `RotS::persist`'s own PUBLIC include directories.

- **`rots_world_linkcheck` / CTest `WorldLayerAcyclicity`** mirrors the `rots_persist_linkcheck`
  pattern: force-load `librots_world.a` and normal-link only `RotS::persist` + `RotS::entity` +
  `RotS::core` + `RotS::platform` to resolve its legitimate downward edges — anything else
  unresolved fails the build. It is the acceptance proof that world-seed Tasks 1-4's relocations,
  storage moves, and hook inversions actually closed every upward edge the wave's `nm` census (and
  the linkcheck's own cascade, below) surfaced. Both hosts' ctest baseline moved from 1274 to
  **1275** the task this check was added (world-seed Task 5); Task 5b then added 6 targeted
  coverage tests, for **1281**; the l4-seed wave's Step 1 (`zone.cpp` → `ROTS_WORLD_SOURCES`) needed
  **zero** new PUBLIC link — every edge `zone.cpp` carries resolves through the `world_hooks.h`/
  `entity_hooks.h` seams that wave's own Tasks 1-2 built, or into the four layers `rots_world`
  already linked (see `AGENTS.md`'s "Testing Guidelines" for the current total and per-platform skip
  counts).
- **Membership story.** `db_world.cpp` and `weather.cpp` were already `ROTS_SERVER_SOURCES`
  members scrubbed of their upward edges by world-seed Tasks 1-3: relocations and storage moves
  (`register_npc_char`/`last_control_set` → `rots_entity`, `dice` → `rots_platform`,
  `time_info`/`weather_info`/`character_list`/`object_list`/`boot_mode`/`mini_mud` storage moved to
  their steady-state owning TU), `db_world.cpp`'s scratch-buffer (`buf`/`buf1`/`buf2`) retirement in
  favor of local composition, and three `world_hooks.h` hook inversions (boot-the-shops,
  mudlle-converter, weather-MSDP) for the app-tier calls relocation alone couldn't cut.
  `zone_load.cpp` is new (world-seed wave): Task 4 carved zone-file **parsing/loading** out of
  `zone.cpp` byte-identically (264 moved lines, reviewer-diffed zero-difference against the
  pre-carve blob) — `zone_table`/`top_of_zone_table` storage moved with it. `zone.cpp` itself (the
  **zone-reset** half — `reset_zone()`/the runtime reset-command interpreter) was deliberately left
  behind at that wave, still `ROTS_SERVER_SOURCES` — and then joined `ROTS_WORLD_SOURCES` in the
  l4-seed wave (Task 3, Step 1), completing both halves of the original `zone.cpp` split. See "L4
  band" below for the four `world_hooks.h`/`entity_hooks.h` seams (`extract_char` re-home,
  `do_wear`, `is_zone_populated`, `equip_char`, pkill fame) that inversion needed under the
  no-bidirectional rule.
- **The sanctioned L3-peer edge.** `db_world.cpp` registers `world_room_vnum()` (`return
  world[room_index].number;`) as `rots_persist`'s pre-boot room-vnum hook via
  `rots::persist::set_room_vnum_hook()` (`register_room_vnum_hook()`, called from
  `run_the_game()` before `boot_db()`). This is an L3(world) → L3(persist) edge, not a layering
  violation — the l4-seed wave's spec revision certifies the L3 tier as a stratified partial order
  (`persist < world < combat`, this being the world→persist half of it, `visibility.cpp`'s
  `weather_info` reference the combat→world half — see `docs/superpowers/specs/
  2026-07-16-library-architecture-design.md`'s §3 REVISION), superseding the earlier "peer tier, not
  a strict sub-stack" framing with a certified direction. It is the reason `rots_world` links
  `RotS::persist` PUBLIC in addition to `RotS::entity`/`RotS::core`/`RotS::platform`, and
  `rots_world_linkcheck` deliberately normal-links `RotS::persist` (like `rots_core_linkcheck` does
  for its one sanctioned L1→L0 edge) to resolve it rather than treat it as an unresolved failure.
- **Task 5's linkcheck cascade.** Standing up `rots_world_linkcheck` surfaced four upward edges the
  wave's earlier `nm` census had missed, each adjudicated by the controller before the implementer
  resumed: `buf2` (`zone_load.cpp`'s malformed-zone-file error labels — converted to a local buffer,
  Task-2-style); `time_info` (read by `db_boot()` at line 147 for the boot-time report — storage
  moved to `weather.cpp`, its steady-state writer `another_hour()`'s home, mirroring the
  `weather_info` storage-move precedent); and `send_to_sector()`/`send_to_outdoor()`
  (`weather.cpp`'s per-sector weather/day-night/moon broadcast calls into `comm.cpp`'s
  `descriptor_list` walkers — inverted through a new `world_hooks.h` hook pair,
  `set_send_to_sector_hook()`/`set_send_to_outdoor_hook()`, silent-no-op default, `comm.cpp`'s
  `register_world_broadcast_hooks()` installing the real bodies pre-boot). All three adjudicated
  fixes are byte-preserving relocations/inversions, not behavior changes.
- **Honest deferrals.** `rots_world` is 4 of the spec §3 row's original ~15-TU sketch
  (`db_world, shapemdl, shapemob, shapeobj, shaperom, shapescript, shapezon, zone, script, mudlle,
  mudlle2, graph, weather, mob_csv_extract, obj2html`) — deliberately, not by oversight:
  - **`zone.cpp`** (both the parsing/loading half carved into `zone_load.cpp`, world-seed wave, and
    the reset half that stayed in `zone.cpp` itself) is now **fully resolved** — the l4-seed wave
    closed the second half; `zone` is no longer a deferral entry at all.
  - **`graph.cpp`/`mudlle.cpp`/`mudlle2.cpp`** were assumed by this row's original sketch to be
    `rots_world` members but **did not join it**: the l4-seed wave found doing so would force
    `rots_world` to PUBLIC-link `RotS::combat`, colliding with `rots_combat`'s own pre-existing
    `RotS::world` link (the first bidirectional L3-peer edge) — so they became the new
    `rots_pathfind`/`rots_script` libraries instead, one band above L3. See "L4 band" below and the
    spec's §3 REVISION; not a `rots_world` deferral any longer, but a different library entirely.
  - **`script.cpp`/the `shape*.cpp` family** (`shapemdl`/`shapemob`/`shapeobj`/`shaperom`/
    `shapescript`/`shapezon`) remain command/editor-coupled (OLC-style world-building tools,
    "Cluster B" in the world-growth census) — out of scope for both the world-seed and l4-seed
    waves, still `ROTS_SERVER_SOURCES`. A future wave's open question is whether Cluster B joins
    `rots_script` or a new sibling `rots_olc` library.
  - **`handler.cpp`/`utility.cpp`** remain app-compiled, same as reported in the `rots_entity`
    section above — closing their gap needs the spec §7 Placement/Containment seam, not a world-tier
    change; neither wave touched either file.

  All of the above are recorded follow-on for whichever future wave next touches the world/app
  boundary, not either wave's scope.
- **Known-cosmetic `ld` warning.** Since world-seed Task 5, linking `ageland` on macOS emits `ld:
  warning: ignoring duplicate libraries: 'librots_core.a', 'librots_entity.a', 'librots_persist.a',
  'librots_platform.a'` — `RotS::world`'s own PUBLIC downward links (above) pull each lower-layer
  archive in a second time alongside `ageland`'s pre-existing explicit link list, and `ld64`
  de-duplicates rather than erroring. It is cosmetic (link succeeds, output is correct — the
  archive is simply offered twice) and pre-dates this wave in kind (`rots_persist`'s own PUBLIC
  downward links already put `librots_core.a`/`librots_entity.a`/`librots_platform.a` in this same
  warning set at the persist-split wave; Task 5 just added `librots_persist.a` to it). The eventual
  fix — pruning `ageland`'s own explicit lower-layer entries now that each higher archive's PUBLIC
  links already carry them transitively — is **not done this wave**; it is a recorded optional
  follow-on, not a regression to chase down now.

### `rots_combat` (L3): the combat-handler library

The sixth extracted layer is `rots_combat` (L3) — seeded with **4 TUs** (`skill_timer.cpp`,
`battle_mage_handler.cpp`, `weapon_master_handler.cpp`, `wild_fighting_handler.cpp`, combat-seed
Task 1) and grown to **6 TUs** by the blocker-buster wave (`combat_hooks.cpp` Task 2,
`visibility.cpp` Task 4 — `ROTS_COMBAT_SOURCES` in `src/CMakeLists.txt` is the single source of
truth for the current membership) — built as `librots_combat.a` and linked into `ageland` as
`RotS::combat`. It is the third and final of the spec's three L3 peer libraries to exist as a build
target (`rots_persist` was the first, `rots_world` the second). At the 4-TU seed, `rots_combat`
linked **no** L3-peer library PUBLIC: `target_link_libraries(rots_combat PUBLIC rots_build_flags
RotS::entity RotS::core RotS::platform)` — the combat-census (`.superpowers/sdd/combat-census.md`)
found zero L3-peer edges out of that 4-TU subset (the one intra-subset peer edge,
`wild_fighting_handler.cpp` → `weapon_master_handler.cpp`, resolves inside `rots_combat` itself,
not against another L3 lib). **As of blocker-buster Task 4, `rots_combat` gains a PUBLIC
`RotS::world` link** — `visibility.cpp`'s moved `CAN_SEE(sub)` references `weather_info`
(`weather.cpp`, `rots_world`) as a legal L3-peer global (plain `extern`, no seam; the parent spec's
L3-peer rule, the same precedent `rots_world`'s own `RotS::persist` link established), the first
genuine L3-peer edge this library's subset has needed. Current link line:
`target_link_libraries(rots_combat PUBLIC rots_build_flags RotS::world RotS::entity RotS::core
RotS::platform)`. `rots_convert`'s link line is unchanged (`RotS::platform`+`RotS::core`+
`RotS::entity`+`RotS::persist`) — it does **not** link `RotS::combat`; none of the six combat TUs
sit on the persistence boundary.

- **`target_include_directories(rots_combat PRIVATE persist/include)`.**
  `weapon_master_handler.cpp` reaches `rots/persist/file_formats.h` transitively through
  `handler.h`'s `RENT_CRASH` macro include — a header-only reach, **not** a link-time dependency
  on `rots_persist` (no `rots_combat` symbol resolves into `rots_persist`, confirmed by the
  linkcheck below). PRIVATE, mirroring `rots_entity`'s identical PRIVATE `persist/include` line
  (see "`rots_entity`" above): no consumer of `RotS::combat` needs this path transitively —
  `ageland`/`rots_convert` already carry `persist/include` on their own
  `target_include_directories`.
- **`rots_combat_linkcheck` / CTest `CombatLayerAcyclicity`** mirrors the `rots_world_linkcheck`
  pattern: force-load `librots_combat.a` and normal-link only `RotS::world` + `RotS::entity` +
  `RotS::core` + `RotS::platform` (the `RotS::world` peer link added by blocker-buster Task 4, above)
  to resolve its legitimate downward/peer edges — anything else unresolved fails the build. It went
  green on the first attempt at the 4-TU seed — no STOP cascade, unlike `rots_entity`'s two rounds
  or `rots_world`'s four-edge cascade — and a positive-PASS/negative-FAIL probe (a transient upward
  call added in a scratch build, then reverted traceless) confirmed the check actually catches an
  upward edge rather than passing vacuously; it stayed green through blocker-buster's two new TUs
  (`combat_hooks.cpp`, `visibility.cpp`) with no further cascade. Both hosts' ctest baseline moved
  from 1315 to **1316** the task this check was added (combat-seed Task 1); Task 3b then added 27
  targeted coverage tests (8 `SkillTimerTest.*`, 19 `WildFightingHandler.*`) for **1343** (75 skips
  macOS / 77 rots64, freshly measured at Task 3b's gates). The blocker-buster wave then carried the
  total to **1365** both hosts (output_seam +7 tests net, the 25-cell command-dispatch table's
  discriminator tests, and the poison-notification characterization pair — see `AGENTS.md`'s
  "Testing Guidelines" for the full reconciled per-task chain and `.superpowers/sdd/task-5-report.md`
  for the citation table).
- **Coverage.** `battle_mage_handler.cpp`/`weapon_master_handler.cpp` already had dedicated suites
  before this wave (`BattleMageHandler.*` — 12 direct cases, `WeaponMasterHandler.*` — 25 direct
  cases). `skill_timer.cpp` and `wild_fighting_handler.cpp` had none — flagged by Task 1 under the
  standing coverage-gap rule and closed by the Task 3b coverage rider
  (`src/tests/skill_timer_tests.cpp`, `src/tests/wild_fighting_handler_tests.cpp` additions).
- **Membership story.** All four TUs were already `ROTS_SERVER_SOURCES` members; the combat-census
  (16-TU per-TU verdict table: `skill_timer`/`battle_mage_handler`/`weapon_master_handler`/
  `wild_fighting_handler` SEED-CLEAN, `profs` caveated SEED-WITH-SEAM, the other 11 DEFER) verified
  them SEED-CLEAN with **no** relocation, storage move, or hook inversion needed — a pure
  membership move, the same shape as entity-completion Task 3's `char_utils`/`char_utils_combat`
  join. `wild_fighting_handler.cpp`'s `register_attack_speed_multiplier_hook()`/
  `register_wild_attack_speed_multiplier_hook()` (the `entity_hooks.h` pattern) are still called
  app-side (`comm.cpp`'s `run_the_game()`, `tests/gtest_main.cpp`'s fixture setup) — a legal
  app→lib downward call, not an upward edge.
- **NumberedName's L0-visibility tier decision (combat-seed Task 3).** `NumberedName` lives in
  `rots/platform/numbered_name.h` (`rots_platform`, L0), not `rots_core` (L1): `rots_util.cpp`'s
  `parse_numbered_name` is itself an L0 TU, and `rots_core` is invisible from L0
  (`rots_platform` links only `rots_build_flags`, not `RotS::core`), so a type an L0 TU must see
  has to live in the platform tree — the same L0-visibility precedent `rots/platform/log.h`
  already established (see "The logging seam" above). `get_char` → `entity_lifecycle.cpp` (L2)
  sees the same header via `handler.h`'s compatibility include.

### The blocker-buster wave: four combat-growth enabler seams (consumer-free)

Design: `docs/superpowers/specs/2026-07-19-blocker-buster-design.md`. Plan:
`docs/superpowers/plans/2026-07-19-blocker-buster.md`. Where the combat-seed wave above seeded four
already-clean TUs, this wave built the four *enablers* the census identified as blocking every
remaining DEFER-11 TU — **enabler-only, consumer-free**: no DEFER-11 TU migrates this wave; the four
seams exist so the *next* combat wave's TU migrations have somewhere to resolve downward into
instead of welding back up into `comm.cpp`/app-command entry points.

1. **Output-seam extension (Task 1, `output_seam.h`/`output_seam.cpp`, +7 forwarders).**
   `send_to_all`/`send_to_room`/`send_to_room_except_two`, `break_spell`/`abort_delay`/
   `complete_delay`, and the content-carrying `get_from_txt_block_pool(std::string_view)` overload
   join the original five (`send_to_char`×2, `act`, track/untrack_mage). Same null-defaulted
   `rots::output::Sinks` pattern, registered by `comm.cpp`'s `register_game_output_sinks()`. Six of
   the seven are void and default to a **logged no-op** (the dominant taxonomy); the txt-block-pool
   forwarder is the one pointer-returning exception and defaults to a **tripwire log-then-abort** —
   a post-review fix: the brief's original "match the five" steer would have returned a null
   `txt_block*` into `comm.cpp`'s own `write_to_q()`, which immediately dereferences it
   (`pnew->next = ...`), so a silent null-return would have surfaced as a confusing null-deref far
   from the real cause. `output_seam.cpp` documents the 3-site contrast between the two defaults.
   Also restored a pre-existing flat-Makefile `SRCS` gap (`comm_output`/`text_view`/
   `string_view_utility` test files had never been in the i386 monolithic runner's source list).
   These seven have **no consumer this wave**; a future combat-tier TU calls them exactly like
   `send_to_char` today. `put_to_txt_block_pool` (the pool-return counterpart of
   `get_from_txt_block_pool`) deliberately needed no seam of its own: reviewer-verified to have no
   combat-row (`mobact`/`spec_pro`/`ranger`/`fight`) caller anywhere in the tree — its only callers
   are `comm.cpp` itself, `mudlle.cpp`, and `mudlle2.cpp` — so there is no upward edge to invert; the
   next implementer doesn't need to re-derive this.
2. **Command-dispatch seam (Task 2, `src/combat_hooks.h`/`combat_hooks.cpp`, 25-cell table).** An
   enum-indexed `rots::combat::combat_command` table of ACMD function pointers, modeled on
   `assign_spell_pointers()` (spell_pa.cpp, entity-seed Task 1): null-initialized at static init,
   populated once at boot by `register_combat_command_dispatch()` (`interpre.cpp` — chosen because
   that TU already forward-declares every target for its own command-interpreter table, the same
   reason `spell_pa.cpp` was `assign_spell_pointers()`'s home), called from `db_boot.cpp`'s existing
   `assign_*` registration sequence (precedent-verified: `assign_spell_pointers()` itself already
   runs inside `boot_db()`). Every cell defaults to a **logged no-op** — every ACMD is void, so a
   skipped command is a safe degraded-but-defined outcome, unlike the txt-block-pool exception
   above. **No call-site conversion this wave**: `mobact`/`spec_pro`/`ranger`/`fight` keep calling
   `do_hit()`/`do_flee()`/etc. directly (a legal same-tier `ROTS_SERVER_SOURCES` call today); the
   seam exists for when one of those four TUs itself joins `rots_combat` and its up-calls convert to
   `rots::combat::issue_command(...)`.
3. **Poison-notification hook (Task 3, `entity_hooks.h`'s `set_poison_removal_hook`).** Retires
   placement-seam deferral cluster 3 — see the "Three honest deferral clusters" note above for the
   full mechanism and the characterization-first proof.
4. **Visibility family → `rots_combat` (Tasks 4/4b, new TU `src/visibility.cpp`).** All twelve
   functions (`CAN_SEE`×2 overloads, `CAN_SEE_OBJ`, `get_char_room_vis`/`get_player_vis`/
   `get_char_vis`/`get_obj_in_list_vis`/`get_obj_vis`/`get_object_in_equip_vis`/`generic_find`,
   `get_real_OB`/`get_real_parry`) moved verbatim out of `handler.cpp`/`utility.cpp`; `PERS` stays
   app-tier (unchanged, census-confirmed correct: `color.cpp` color-global deps). `see_hiding`
   (Task 4b) was carved out of `ranger.cpp` into `visibility.cpp` (entity-pure, zero other
   `ranger.cpp` dependency) so the 3-arg `CAN_SEE(sub, obj, light_mode)` overload could finally move
   too; `generic_find`'s `search_block()` dependency relocated to `rots_util.cpp`/`rots_platform`
   (the `get_number()`/`LOWER`→`lower_ascii` precedent). `weather_info` (`weather.cpp`, `rots_world`)
   is a legal L3-peer `extern` reference (see the `RotS::world` PUBLIC link note above). The
   `world[]` resolver-variant rule applies per site (e.g. `CAN_SEE(sub)`'s unchecked
   `IS_LIGHT`/`OUTSIDE` macro expansions resolve via `room_by_id_total`, matching
   `CAN_GO()`/`can_breathe()`'s precedent in `environment_utils.cpp`).

**Census errata (record honestly for the next combat wave — re-verify census claims against
current build wiring, not just thematic/row placement, before trusting them):**

- **Census-C's target count was wrong in both directions**, not just under. Its "~19 distinct ACMD
  targets" framing conflated per-TU counts with the cross-TU union, and three of its listed names
  (`do_recover`/`do_scan`/`do_pracreset`) turned out to have no direct call site to convert (reached
  only through the general `command_interpreter`, or self-calling within their own defining TU) —
  phantoms dropped from the union. The reconciled union landed at 24 cells; a coordinator review
  then caught a genuinely missing 25th (`do_mental`, `fight.cpp`'s per-tick mental-combat up-call
  into `clerics.cpp`) that a follow-up sweep confirmed was the *only* remaining gap. True count: 25.
- **Census-A conflated "thematically a combat-row TU" with "actually in `ROTS_COMBAT_SOURCES`
  today."** It recommended moving the 3-arg `CAN_SEE(sub, obj, light_mode)` overload to
  `rots_combat` on the strength of `weather_info`/`world[]`/`act()` all being legal peer/seam
  references — true — but missed that its `see_hiding(sub)` call resolves to `ranger.cpp`, which is
  *still* a DEFER-11 `ROTS_SERVER_SOURCES` member despite being "in-row" thematically. Task 4
  STOPped and documented rather than moving it; Task 4b closed the gap by carving `see_hiding` out
  of `ranger.cpp` first.
- **`generic_find`'s `search_block()` dependency was an uncensused edge** — not in census-A's table
  at all. `search_block()` is defined in `interpre.cpp` (app-tier command interpreter), found only
  by verification (grep + `CombatLayerAcyclicity`'s deterministic link failure), not by re-reading
  the census.
- **The lesson for the next combat wave:** a census's per-function or per-TU verdict is a strong
  prior, not a build-wiring guarantee. Before moving anything on a census's say-so, re-verify the
  specific dependency's *current* `CMakeLists.txt` source-list membership (`grep` the exact
  filename against `ROTS_COMBAT_SOURCES`/`ROTS_SERVER_SOURCES`, don't trust the census's thematic
  row assignment) — this wave's two errata (census-A, `generic_find`) were both caught this way, and
  both would have failed `CombatLayerAcyclicity` at build time if landed anyway.

**The DEFER-11 growth inventory — blockers now CLEARED (per-TU remaining work for the next combat
wave).** The parent spec's `rots_combat` row still sketches 16 TUs; this wave's two prior findings
(below) are no longer live blockers — both enablers now ship:
  - **Output is fully unblocked.** `output_seam` now forwards all 12 symbols (`send_to_char`×2,
    `vsend_to_char`, `act`, track/untrack_mage, plus this wave's seven) downward into L1. Every
    `act`/`send_to_char`/`send_to_room*`/`send_to_all`/`break_spell`/`abort_delay`/`complete_delay`
    call-site in all 16 combat-row TUs now resolves into L1, not up into `comm.cpp`. The remaining
    non-output app-side surface (`close_socket`, `descriptor_list`, `_circle_shutdown`,
    `_no_specials`, `_fight_messages`, color globals) is unchanged and stays a per-TU concern, not a
    seam gap.
  - **Command up-calls are unblocked in principle.** The 25-cell `combat_hooks.h` table covers every
    real `do_*` call site the four DEFER-tier command-issuing TUs (`mobact`/`spec_pro`/`ranger`/
    `fight`) use today. Converting a TU still means literally rewriting each of its up-calls from
    `do_hit(...)` to `rots::combat::issue_command(combat_command::hit, ...)` — that per-call-site
    edit is unstarted work, not a blocker.
  - **The dominant remaining blocker is now pure migration, not enabler design.** Every DEFER-11
    TU's edges trace back to the still-app `handler.cpp`/`utility.cpp` remainder minus what this
    wave moved out (`extract_char`/`stop_riding`/the proto family/session helpers/zone-power
    pair/…, the same remainder the "`rots_entity`" section above describes) — none of that remainder
    was in scope for this wave's four enablers. **Per-TU next steps:** (1) migrate a TU's body
    verbatim into `ROTS_COMBAT_SOURCES`; (2) convert its `do_*` up-calls (if any) to
    `issue_command()`; (3) convert any remaining unseamed app-tier reference it makes (per-TU
    census re-verification required — see the errata above, do not trust the old combat-census's
    per-TU tables without re-checking current `CMakeLists.txt` membership); (4) run
    `CombatLayerAcyclicity` + the other five linkchecks to confirm no new upward edge. `profs`
    remains caveated SEED-WITH-SEAM (3 small edges, but pulls in the deeply-blocked `mystic.cpp`).
    Full per-TU evidence: `.superpowers/sdd/combat-census.md` (planning-stage) plus this wave's
    census-A/census-C corrections above.

Backlog (recorded follow-on, not this wave's scope):
- **`consts.cpp`'s `utils.h` include**, added by Task 2's time-quartet move (for `day_to_str()`'s
  `nth()` call — see "Pathed data-model includes" below / the `rots_core` section above), is
  TIER-CLEAN (`utils.h` has full L0/L1 closure) but idiom-deviant — no other `rots_core` TU
  includes the whole app-wide `utils.h`. Prefer a targeted `char* nth(int);` forward declaration
  instead, per the `get_hit_text`/`rots/platform/log.h` precedent of narrow declarations over a
  blanket header include. Reviewer-recommended future touch-up, not urgent.

### The combat-pilot wave: `clerics.cpp` + `fight.cpp` join `rots_combat` (8 TUs)

Design: `docs/superpowers/specs/2026-07-20-combat-pilot-design.md`. Plan:
`docs/superpowers/plans/2026-07-20-combat-pilot.md`. Where the blocker-buster wave above built four
consumer-free enablers, this wave proved them under real traffic: `clerics.cpp` and `fight.cpp` —
two of the original 16-TU sketch's DEFER-11 rows — landed in `ROTS_COMBAT_SOURCES`, growing
`rots_combat` from 6 to **8 TUs** (`battle_mage_handler.cpp`, `clerics.cpp`, `combat_hooks.cpp`,
`fight.cpp`, `skill_timer.cpp`, `visibility.cpp`, `weapon_master_handler.cpp`,
`wild_fighting_handler.cpp`). Both files' up-calls into the app-command/app-session/app-other
tiers now resolve through registered-hook dispatch rather than direct calls; the wave also
delivered a combat smoke-test harness and the migration playbook (`docs/superpowers/
combat-migration-playbook.md`) the remaining 9 DEFER TUs + `profs` will reuse.

**The central architectural finding: closure, not per-symbol non-blocking status, governs whether
a TU can promote standalone.** `clerics.cpp` and `fight.cpp` call each other directly
(`set_fighting`/`stop_fighting`/`check_sanctuary`/`check_hallucinate`/`die`/`appear` one way,
`weapon_willpower_damage`/`do_mental` the other) — the combat-census family's "combat-peer,
sanctioned, non-blocking" legend correctly says neither edge is an architecture violation, but it
does not mean either file can promote *alone*: a standalone `clerics.cpp` move would ask
`CombatLayerAcyclicity` to resolve `set_fighting` against a `fight.cpp` still sitting in
`ROTS_SERVER_SOURCES`, an unresolved link. The two TUs promoted together in one commit (Task 5);
Task 3 (clerics.cpp's own up-call conversions) landed two tasks earlier while clerics.cpp was
*still app-compiled*, proving conversions and membership are separable in time even when membership
itself is not separable across files. See the playbook's "The intra-subset rule" and
"Census-methodology correction" sections for the full writeup, including the two genuine census
misses (`gain_exp`, `waiting_list`) the `CombatLayerAcyclicity` linkcheck itself caught at the
membership-move gate — both traced to symbols the census had classified correctly but
provisionally, resolved with existing seams rather than new stubs.

**New hooks (all `combat_hooks.{h,cpp}` unless noted, each a registered fn-ptr with a documented
tripwire default, TDD'd with a registered-stub/unregistered-default test pair):**
- **`special()`** (Task 2) — `rots::combat::set_special_handler`/`call_special`, mirroring
  `interpre.h:99`'s real 6-parameter signature (`int in_room = NOWHERE` as the dispatch wrapper's
  own default; a function-pointer type cannot itself carry a default). NOT a 26th `combat_command`
  enum cell — `special()`'s int-returning, 6-parameter shape is categorically different from the
  25-cell ACMD table above. Tripwire default: logged, returns 0 ("no spec-proc consumed the
  event").
- **The big_brother pair** (Task 2, `entity_hooks.h`, storage in `entity_lifecycle.cpp` per that
  header's established convention) — `target_valid_fn`/`dispatch_target_valid` and
  `character_died_fn`/`dispatch_character_died`. `is_target_valid` has two genuinely different-
  bodied overloads (2-arg, 3-arg with `skill_id`); a single fn-ptr mirrors the wider 3-arg shape,
  with `inline constexpr int kNoSkillId = -1` as the sentinel a 2-arg-shaped call site (clerics.cpp)
  passes implicitly, and the registered implementation branches on the sentinel to call the exact
  right real overload rather than approximating one via the other. Tripwire default:
  `is_target_valid` logged-permissive-`true` (big_brother VETOES by returning false, so "no big
  brother installed" must default to "allow"); `on_character_died` logged no-op.
- **`extract_char`** (Task 4b) — `set_extract_char_hook`/two-overload dispatch, reproducing
  `handler.cpp`'s own 1-arg-forwards-to-2-arg-with-`-1`-sentinel shape exactly. Tripwire default:
  logged no-op.
- **The `gain_exp` family** (Task 4b, `limits.cpp`-owned) — `gain_exp`, `gain_exp_regardless`
  (wrapper pair sharing one HOOK verdict: `gain_exp`'s body only calls `gain_exp_regardless`, which
  itself calls `advance_mini_level()`'s own multi-function leveling subsystem — not a
  single-symbol relocation candidate) and `remove_fame_war_bonuses` (pulls an
  8-function/~190-line `assign_pk_bonuses`/`set_player_*` same-file cluster, flagged as a future L2
  relocation candidate as a *package*, not resolved this wave). Tripwire default: logged no-op for
  all three. **`limits.cpp` itself is the registrar** for all three
  (`register_gain_exp_hook()`/`register_gain_exp_regardless_hook()`/
  `register_remove_fame_war_bonuses_hook()`, defined in `limits.cpp`) — this infrastructure is
  ALREADY BUILT for whenever `limits.cpp` itself promotes; see the playbook's per-TU cost table.
- **The app-other trio** (Task 4b) — `Crash_crashsave` (objsave.cpp, tripwire logged no-op),
  `call_trigger` (script.cpp, tripwire logged **TRUE** — the one value-returning hook whose safe
  default is a documented non-zero constant, not a plain no-op: both fight.cpp call sites treat
  `FALSE` as "a script vetoed this event," so an unregistered hook must default to "proceed," the
  same taxonomy class as `entity_hooks.h`'s permissive-true `is_target_valid` default), `pkill_create`
  (pkill.cpp, tripwire logged no-op). Each owning TU (objsave/script/pkill, none of them combat-row
  TUs) registers its own real body via its own per-owner registrar, called from both `comm.cpp`'s
  `run_the_game()` (pre-`boot_db()`) and `tests/gtest_main.cpp` for parity.

**`RotS::persist` PUBLIC link — two real edges, not a header-only reach.** `rots_combat`'s
`target_link_libraries()` gained `RotS::persist` PUBLIC at the Task 5 membership move (dropping the
now-redundant `target_include_directories(rots_combat PRIVATE persist/include)`, since
`persist/include` now flows transitively through the PUBLIC link instead), mirroring `rots_world`'s
identical-class link for the identical edge shape (`db_world.cpp` → `rots::persist::
set_room_vnum_hook`). Two `nm`-real edges make the link load-bearing, not optional: `fight.cpp`'s
pre-existing `save_char()` call (already counted non-blocking by the census, since `rots_persist`
already existed as a peer library before this wave) and the new
`rots::persist::dispatch_exploit_capture()` call this wave's `add_exploit_record` conversion added.
The second edge required a linkage fix of its own: `dispatch_exploit_capture()` had been wrapped in
an anonymous namespace inside `db_players.cpp` (TU-local, since only that file's own `rename_char()`
had ever called it before this wave) — given external linkage and declared in `persist_hooks.h`,
the same "dispatch declared in the header, defined in the owning TU, called from a different TU"
shape `entity_hooks.h`'s dispatch functions already established.

**Combat smoke harness (Task 1, characterization-first, built BEFORE any migration).**
`rots_rng::seed_from_environment_or_time()` (`ROTS_RNG_SEED`, decimal, pins the `mt19937` stream;
unset/malformed falls back to the historical `time(0)` seeding, byte-identical default behavior) —
both `comm.cpp` seed call sites route through it. `tools/combat_smoke.py` + `scripts/
combat-golden.sh` (`capture`/`verify`, mirroring `boot-golden.sh`'s UX) script a deterministic fight
against a fixed mob (vnum 11915) in a non-`PEACEROOM` room, capturing the transcript between
explicit markers. **Determinism landed on the fallback ladder's rung (b), capture-only, named
loudly rather than silently accepted:** repeated same-seed trials showed the shared global
`rots_rng` engine's draw sequence shifts under real-time pulse-loop interleaving from other
periodic RNG consumers (door auto-close mechanics confirmed; weather/regen not ruled out), so
neither a raw transcript compare nor a normalized (combat-lines-only) compare is reliable — even a
same-host, same-seed, immediate re-run can drift. `scripts/combat-golden.sh verify` therefore
**never gates** (exits 0 regardless of transcript drift, per Task 1's finding); it is
**informational only**, read for gross-shape regressions (wrong mob, missing death, boot failure)
and not treated as a merge blocker on drift alone. Every migration task since (Task 3/Task 5) ran
`verify` and recorded informational drift consistent with RNG-consumption-order shifts from the new
hook indirection — never a logic regression, confirmed by the unchanged characterization goldens
each time. The wave's actual regression-catching burden rests on
`CharacterizationCombatTest.*`/`PoisonRemovalScriptTest.*` (gtest-level, outside real time) and the
discriminator test suites, not on this harness's transcript diff.

**Storage moves (the same technique `fight_messages` pioneered in the combat-census's own
recommendation, applied four more times this wave):**
- **`fight_messages[MAX_MESSAGES]`** — `db_boot.cpp:108` → `fight.cpp` (beside its loader
  `load_messages()`), Task 4a. `clerics.cpp`'s identical-looking extern was confirmed genuinely dead
  (zero undefined-symbol references) — no compensating edit needed there.
- **`spllog_saves`/`spllog_mage_level`/`spllog_save`** — `spell_pa.cpp` → `fight.cpp`, bundled with
  the `record_spell_damage()` relocation (Task 4a): the function alone would have left it reading
  spell_pa.cpp-owned storage, an upward read once spell_pa.cpp stays app-tier; `spell_pa.cpp`'s
  `saves_spell()`/`new_saves_spell()` (which stay in spell_pa.cpp) now `extern` the relocated
  globals and keep writing them, a legal downward write.
- **`memory_rec_pool`/`memory_rec_active`/`memory_rec_counter`** — `mobact.cpp` → `entity_lifecycle.cpp`
  (L2), bundled with the `forget()`/`remember()` package (Task 4a), the same free-list-pool shape
  the `universal_list_counter` precedent already established in that file.
- **`waiting_list`** — `db_boot.cpp` → `clerics.cpp` (Task 5, a **link-forced** fix discovered only
  at the `CombatLayerAcyclicity` gate, not planned in advance: `clerics.cpp`'s `WAIT_STATE`/
  `WAIT_STATE_BRIEF` macro expansions read/write the raw global directly, a shape no fn-ptr hook
  fits). Constant-initialized (`= 0`), no init-order break; all 16 other files' own local `extern`
  declarations were re-verified to keep linking unchanged. **Thematic-ownership caveat, recorded
  honestly rather than smoothed over:** `db_boot.cpp` never read or wrote this global, only defined
  it, and `clerics.cpp` is its definition site purely because it was the one user swept into the
  promoted pair — the name and the game concept ("who's waiting for the pulse loop") don't
  obviously belong to a cleric-spell file. Flagged as a Low-severity finding at final review and a
  candidate for a future re-home to whichever TU eventually owns the comm/delay-loop machinery,
  not a defect blocking this wave.

Test-count delta for this wave: **1365 → 1394** (Task 1 +3 `RngSeed.*`, Task 2 +8 `special`/
big_brother hook tests, Task 4b +16 hook-family tests, Task 5 +2 `ExploitCaptureHook.*`; Task 3/4a
added zero new tests — pure conversions/moves). See `AGENTS.md`'s "Testing Guidelines" for the full
reconciled per-task chain.

### The combat-trio wave: `olog_hai.cpp` + `mystic.cpp` + `profs.cpp` join `rots_combat` (11 TUs)

Design: `docs/superpowers/specs/2026-07-20-combat-trio-design.md`. Plan:
`docs/superpowers/plans/2026-07-20-combat-trio.md`. Census: `.superpowers/sdd/combat-trio-census.md`
(gitignored scratch). The playbook's **second** application (`docs/superpowers/
combat-migration-playbook.md`), and the first to promote TUs **standalone** rather than as a
mutually-dependent pair — `olog_hai.cpp` and `mystic.cpp` each closed over their own combat-peer
edges independently (zero edges to each other, zero edges to `mobact`/`spec_pro`/`mage`/`limits`),
unlike the combat-pilot wave's `clerics`↔`fight` cycle that forced a joint move. `rots_combat` grows
from 8 to **11 TUs** (`battle_mage_handler.cpp`, `clerics.cpp`, `combat_hooks.cpp`, `fight.cpp`,
`mystic.cpp`, `olog_hai.cpp`, `profs.cpp`, `skill_timer.cpp`, `visibility.cpp`,
`weapon_master_handler.cpp`, `wild_fighting_handler.cpp`). The membership commit (Task 4, `019b4c8`)
landed `CombatLayerAcyclicity` green **first try, both hosts** — no census miss, no stub, no
controller ruling needed, the corrected closure methodology (Task 0's full `nm` re-census +
closure check, run BEFORE any code moved) validated against real build wiring for the first time
since it was written.

**The profs rider — a one-directional gate, not a cycle.** The design spec's rider condition (`profs`
rides only if `scale_guardian` is standalone-relocatable OR mystic's own membership dissolves the
edge) resolved via the second path: `scale_guardian` (mystic.cpp:1584) is **not** standalone-
relocatable — Task 0's body read found a same-file helper cluster of **6** functions
(`set_guardian_stats`, `calc_guardian_hp`, `set_guardian_health`,
`tweak_aggressive_guardian_stats`/`tweak_defensive_guardian_stats`/`tweak_mystic_guardian_stats`),
one more than the design spec's stated "four" (`calc_guardian_hp`/`set_guardian_health` were omitted
from the spec's enumeration) — but once `mystic.cpp` itself joins `rots_combat`, `profs.cpp`'s call
resolves as a legal intra-lib reference, dissolving the edge without needing `scale_guardian` to move
at all. Task 0 also found a **second external caller** the design spec's source-level pass missed
entirely: `objsave.cpp:775` carries its own local `extern` declaration mirroring its existing
`get_guardian_type` call — both become ordinary downward app→lib calls once mystic promotes, no
change needed at either call site. Profs's other two blockers (`get_guardian_type`,
`add_exploit_record` ×8) were both cheap exact-signature-match relocations/conversions against
existing seams, so the rider FIRED and all three TUs land in one membership commit — chosen for
simplicity, not because the linkcheck forced it: olog_hai is independent, mystic is independent,
profs is one-directionally gated on mystic alone.

**New `combat_command` cell (Task 1) — `dismount`, the 26th.** `olog_hai.cpp`'s one direct up-call to
`do_dismount` (`ranger.cpp:208`, still-app-compiled) is the only genuine combat-peer edge either of
the two lead TUs carries outside this wave's own candidate set (re-derived from the playbook cost
table's stale "olog combat-peer=6" — see below). Registered exactly like `flee`'s real body staying
in `act_offe.cpp`: the cell's real body never has to move, only the fn-ptr registration
(`interpre.cpp`'s `register_combat_command_dispatch()`) and the discriminator pair. `combat_hooks.h`'s
enum-indexed backing array auto-scaled from 25 to 26 cells with zero other code change (the
`std::array<acmd_fn, static_cast<std::size_t>(combat_command::count)>` declaration sizes off the
sentinel). `combat_hooks.h`'s own file comment (lines 196/221) previously mis-stated "25 cells" after
this cell landed — a comment-only drift fixed in this wave's Task 5 docs sweep; line 30's "25 cells"
citation is untouched, since it correctly describes census-C's original blocker-buster-wave count
before this wave's cell existed.

**Relocations (Task 1, consumer-free before any call site converted):**
- **`one_argument`/`fill_word`/`fill[]` + `half_chop` → L0 `rots_util.cpp`.** The census confirmed
  `half_chop` (interpre.cpp:1535) has **zero function-call edges of its own** — cleaner than
  `one_argument`, and the two travel to the same destination but are not a package: `half_chop`
  relocates standalone (its own commit-worthy unit), while `one_argument`/`fill_word`/`fill[]` move
  as the census's original 3-item bundle (`fill_word` reads the `fill[]` table as its own global,
  the one piece that cannot separate). Two precedented L0-macro-inlining substitutions were required
  (L0 must not include `utils.h`): `LOWER(c)` → `lower_ascii(c)` (the `search_block()` precedent) and
  `TRUE` → `1` (a second instance of the same class, matching `combat_hooks.cpp`'s own `call_special()`
  tripwire default's literal-inlining comment) — both disclosed deviations from strict byte-for-byte
  text, not silent substitutions.
- **`saves_confuse`/`saves_insight`/`saves_leadership`/`saves_mystic`/`saves_poison` →
  `char_utils_combat.cpp` (L2).** `mystic.cpp`'s five `spell_pa.cpp`-owned combat-peer edges — the
  wave's named primary STOP-risk, entirely unenumerated by name in the design spec's own source-level
  residual list — turned out to be exactly this five-symbol family, all RELOCATE-CLEAN via the
  `saves_power` precedent (combat-pilot wave). **The L2-lateral floor**: these five are
  macro-expanded L2 calls (they read `char_data` fields the same way `saves_power` does), so their
  destination must be L2 **or above**, never below — a constraint Task 0's census called out
  explicitly so a later relocation choice doesn't accidentally place them somewhere lower-tier that
  would reintroduce an upward edge.
- **`add_follower` → `entity_lifecycle.cpp` (L2).** `handler.cpp:267`'s body was confirmed L2-clean
  by direct read (its pool helper and `stop_follower` sibling are already L2; its `act()` calls are
  already `output_seam` L1 forwarders) — zero handler-internal statics, matching the design spec's
  default exactly.
- **`get_guardian_type` → `visibility.cpp` (`rots_combat`, L3 — not L2, Task 3).** `utility.cpp:978`'s
  body reads only `mob_index` (`db_world.cpp:95`, **L3-world**) and `guardian_mob`
  (`consts.cpp:2620`, L1-core) — an L3-forcing edge, not an L2 one: `mob_index` has no L2-visible
  resolver seam (unlike `world[]`/`room_data`'s four-resolver placement-seam family), but it's already
  referenced the same way by `clerics.cpp:55`, an existing `rots_combat` member, confirming the plain
  `extern` idiom is a legal in-lib peer reference here. Landed in `visibility.cpp` (not a fresh TU) —
  that file already carries the `weather_info` L3-peer-reference precedent and the `db.h`-class
  includes the relocation needs. Its two callers (`objsave.cpp:774`, `profs.cpp:226`) both resolve as
  legal downward/intra-lib calls, unchanged.

**First-standalone-promotion data point.** Every prior `rots_combat` growth was either SEED-CLEAN (the
combat-seed wave's four TUs, zero relocation needed) or a mutually-dependent pair forced into one
commit (`clerics`+`fight`, six-symbols-one-way/two-the-other). This wave is the playbook's first
evidence that the corrected closure-check recipe (census → closure check → seams → conversions →
membership → verification, in that order, closure check run BEFORE any membership commitment) works
for TUs that are architecturally independent of each other: `olog_hai.cpp` and `mystic.cpp` share no
symbols in either direction, and each was closed over its own combat-peer edges individually before
either promoted. `profs.cpp`'s one-directional gate on `mystic.cpp` (`scale_guardian`) is a third,
intermediate case the playbook also lacked a data point for — a real coupling, but one that dissolves
the moment its one partner promotes, categorically unlike a cycle that forces joint membership by the
`CombatLayerAcyclicity` mechanism itself.

**Census overturns (Task 0, `nm`-confirmed against the playbook's own cost-table estimates — see
`docs/superpowers/combat-migration-playbook.md`'s "Per-TU cost table" and its per-row RESOLVED
updates):** olog_hai's stated "combat-peer=6" re-derived to **1** genuine edge (`do_dismount`); the
`scale_guardian` helper cluster is **6** functions, not the design spec's stated "four"; `scale_guardian`
has a **second** external caller (`objsave.cpp:775`) the design spec missed; olog_hai's `_waiting_list`
reference (4 `WAIT_STATE_FULL` sites) was never named in the design spec's residual list at all, but
resolves for free — the combat-pilot wave's Task 5 already storage-moved `waiting_list` into
`clerics.cpp`, already in-lib. None of these overturns changed either verdict (mystic promotes; the
rider fires); they correct counts the design spec's source-level pass got wrong or left unenumerated.

Test-count delta for this wave: **1394 → 1398** (Task 1 +2 `dismount` discriminator tests
(`38dca86`), Task 2 +2 `move` discriminator tests (`8338ebd`) — the `move` cell became a real
`issue_command()` consumer for the first time via `olog_hai.cpp`'s `do_overrun` conversion, a genuine
discriminator-audit gap; Task 3/Task 4 added zero new tests — pure relocations/conversions/membership
move). All three gate hosts (`macos-arm64`, `rots64`, `macos-arm64-asan`) confirmed 1398/1398 at
Task 4's final gate; ASan ran at every task boundary that touched a test file, including Task 2 —
independently re-run by the reviewer after the task's own report incorrectly claimed the
additive-only-test-change class had no ASan precedent in this wave (`trio-task-1-report.md:241`
already required ASan for the identical class of change, two new dismount tests appended to the same
pre-existing `combat_hooks_tests.cpp`; the review closed the finding by running the gate itself, zero
diagnostics). See `AGENTS.md`'s "Testing Guidelines" for the full reconciled per-task chain.

### L4 band: `rots_pathfind` + `rots_script` — the first orchestration/authoring tier

Design: `docs/superpowers/specs/2026-07-21-l4-seed-design.md`. Plan:
`docs/superpowers/plans/2026-07-21-l4-seed.md`. Census: `.superpowers/sdd/l4-census.md` (gitignored
scratch). Task reports: `.superpowers/sdd/l4-task-{0,1,2,3}-report.md`. This wave stands up the
**first library band above the L3 peer tier** — see the parent spec's §3 REVISION
(`docs/superpowers/specs/2026-07-16-library-architecture-design.md`) for the full architectural
rationale, including the certified `persist < world < combat < pathfind < script < app` order and
the no-bidirectional-links invariant this wave's `zone.cpp` adjudication turned into a named rule.
Eight libraries now exist, eight `*LayerAcyclicity` linkchecks. ctest 1398→1415 across the wave's
four implementation tasks.

- **`rots_pathfind` (L4-lower, 1 TU): `graph.cpp`.** Pathfinding/hunting — `hunt_victim()`'s driver
  calls `hit()` (fight.cpp) and `get_char_vis()` (visibility.cpp), both `rots_combat` members,
  which become legal **downward L4→L3** calls once `graph.cpp` promotes above combat — needing
  **zero edit** to either call. The only conversions: `do_say`×2/`do_move`×1 → the existing
  `combat_command::say`/`move` cells (`combat_hooks.h`, already registered since the combat-seed/
  combat-trio waves, reached downward), plus retiring two `_arg`/`_buf` global-scratch sites
  (`do_wiztrack`, `hunt_victim`) to locals. Links (mirroring `rots_combat`'s own explicit downward
  list): `RotS::combat RotS::world RotS::persist RotS::entity RotS::core RotS::platform`.
- **`rots_script` (L4-upper, 3 TUs): `mudlle.cpp`, `mudlle2.cpp`, `script_hooks.cpp`.** The
  mob-program/script engine — named for the mechanism (designers author behaviors), deliberately
  not `rots_ai`, since the quest layer is slated for its own future redesign. `mudlle.cpp` →
  `mudlle2.cpp` (14 helper symbols: `CHECK_LIST`/`compare_list`/`DOWN_LIST`/`DOWN_STACK`/
  `FROM_STACK`/`int_itemtostring`/`REMOVE_LIST`/`STACK_VALUE`/`TEXT_LIST`/`TO_LIST`×2/`TO_STACK`/
  `UP_LIST`/`UP_STACK`) is a genuine intra-band edge, one-directional — the two TUs land together
  by choice (simplicity), not because the linkcheck forces it (mirroring the combat-trio wave's
  "one-directional gate" precedent one tier up). `update_pos()`/`CAN_SEE()` (both `rots_combat`)
  and `find_first_step()` (`graph.cpp`/`rots_pathfind`) resolve without edit, the same downward-tier
  pattern as `rots_pathfind`'s own `hit()`/`get_char_vis()`. Links: `RotS::pathfind` (the sanctioned
  intra-band edge, `find_first_step`) plus the full L3 downward set.
- **`PathfindLayerAcyclicity` / `ScriptLayerAcyclicity`** mirror the six pre-existing
  `*LayerAcyclicity` checks exactly — force-load the archive, normal-link only the declared downward
  set, fail on any unresolved symbol — but their own link lists are genuinely new design, not a
  repeat of an existing pattern. `rots_pathfind_linkcheck` normal-links only `RotS::combat` (which
  transitively PUBLIC-carries `world`/`persist`/`entity`/`core`/`platform`); `rots_script_linkcheck`
  normal-links only `RotS::pathfind` (which transitively carries `combat` and everything below it)
  — but `RotS::pathfind` is listed **explicitly** on `rots_script_linkcheck`'s own link line, not
  merely inherited, specifically to keep the `find_first_step()` intra-band edge exercised. **This
  is tighter than either pre-existing L3 linkcheck, not a continuation of a precedent either already
  set**: `WorldLayerAcyclicity` normal-links four libraries explicitly (`RotS::persist RotS::entity
  RotS::core RotS::platform` — nothing to omit via transitivity, since `persist` is the deepest peer
  directly below `world`) and `CombatLayerAcyclicity` normal-links four as well (`RotS::world
  RotS::entity RotS::core RotS::platform`, omitting only the single furthest peer `RotS::persist`,
  transitively carried via `RotS::world`) — neither existing checker lists just one library, so the
  L4 pair's "list exactly one library, let transitivity carry the rest" design is this wave's own
  innovation. Both non-vacuous by positive-PASS/negative-FAIL probe (a temporary `boot_db()`-calling
  probe function appended to `graph.cpp`/`mudlle.cpp` respectively reproducibly failed the link,
  then was reverted with zero residue) — the combat-seed precedent, independently re-run by the
  reviewer on both checkers.
- **`script_hooks.h`'s storage relocation (Task 3 mid-task adjudication).** T1 placed
  `dispatch_command_interpreter()`'s backing storage inside `interpre.cpp` itself — permanently
  app-tier, since that file defines the real `command_interpreter()` body and can never promote —
  unlike `world_hooks.h`'s own hooks, whose backing storage lives in the *promoting* library
  (`db_world.cpp`) per that header's established precedent. Once Task 2 converted `mudlle.cpp`'s
  call site to `rots::script::dispatch_command_interpreter()`, that T1 placement became a live
  `rots_script → app` upward edge, invisible until `rots_script_linkcheck` first ran against it (two
  undefined symbols: `dispatch_pers`/`dispatch_command_interpreter`). **Disposition:** relocated both
  hooks' backing storage (`set_command_interpreter_hook()`/`dispatch_command_interpreter()`,
  byte-verbatim except dropping a now-redundant namespace wrapper) from `interpre.cpp` into
  `script_hooks.cpp` — which already held `dispatch_pers()`'s backing storage since T1 for the
  identical "seam header, no single owning caller" reason `combat_hooks.cpp` joined `rots_combat` —
  and added `script_hooks.cpp` to `ROTS_SCRIPT_SOURCES` alongside its two callers, making it the
  library's third member. `interpre.cpp` keeps only the registrar (`register_command_interpreter_
  hook()`, a legal `app → lib` downward call into `script_hooks.h`'s public API). This was a
  relocation with no behavior change, adjudicated in-flight per the wave's STOP contract rather than
  stubbed — the same class of linkcheck-surfaced census miss `WorldLayerAcyclicity` caught at
  `rots_world`'s own membership gate (world-seed Task 5's four-edge cascade), one tier up.
- **`extract_char`'s re-home (`combat_hooks.{h,cpp}` → L2 `entity_hooks.h`).** `zone.cpp`'s
  `extract_char(mob)` call (reset_zone case 10) needs the same inversion `fight.cpp` already used
  via `combat_hooks.h` (combat-pilot wave) — but that hook lives in `rots_combat`, unreachable from
  `rots_world`, and duplicating a second `world_hooks.h` copy would mean two independent inversions
  for one real body. Instead the existing hook re-homed **down** to L2 `entity_hooks.h`/
  `entity_lifecycle.cpp` (both `rots_world` and `rots_combat` already PUBLIC-link `RotS::entity`, so
  both L3 bands share it): the typedef/setter/dispatch moved verbatim; `handler.cpp`'s registrar
  keeps registering the real body app-side with a one-line namespace edit
  (`rots::combat::` → `rots::entity::`); `fight.cpp`'s three pre-existing call sites and the
  discriminator test suite (`CombatHooksExtractChar` → `ExtractCharHook`, re-homed into
  `entity_lifecycle_tests.cpp`, not duplicated) get the same namespace edit; `zone.cpp` dispatches
  through the same L2 entry point (`rots::entity::extract_char(mob)`, the 1-arg `-1`-sentinel
  overload — the brief's own literal `dispatch_extract_char` naming was a documentation error caught
  by a build error, not the real re-homed API's overloaded-name shape). One real gotcha found in
  transit: `entity_lifecycle.cpp`'s own internal `extract_char(ch) → extract_char(ch, -1)` forward
  needed explicit `rots::entity::` qualification, since `handler.h`'s global `::extract_char`
  overload is ADL-visible there (via `char_data`'s associated namespace) in a way it never was
  inside `combat_hooks.cpp`, which didn't transitively include `handler.h`.
- **The `zone.cpp` combat/app-edge adjudications (T0 OVERTURNED two of the design spec's own
  defaults with body-read/nm evidence, both later reviewer-confirmed).** (1) `equip_char` was
  expected entity-pure (wrapping the L2 `attach_equipment` primitive) but its own body carries a
  `damage()`/`raw_kill()` poison-coupling block identical in shape to `unequip_char`'s — the design
  spec's cited source was a caller-side observation ("`fight.cpp` calls `unequip_char` only"), not a
  body-content claim; falls to `world_hooks.h::dispatch_equip_char` (void, logged-no-op tripwire),
  registered by `fight.cpp` **unmoved** — neither half of the `equip_char`/`unequip_char` pair
  relocates, only `zone.cpp`'s call site inverts. (2) `pkill_get_good_fame`/`pkill_get_evil_fame`
  have trivial bodies but read `good_ranking`/`evil_ranking`, app-tier globals living in `pkill.cpp`
  itself (not `pkill_json.cpp`/`rots_persist`) — relocating just the accessors would compile in the
  final `ageland` link but fail `PersistLayerAcyclicity` by design; falls to a
  `world_hooks.h::dispatch_pkill_get_good_fame`/`dispatch_pkill_get_evil_fame` pair (int,
  safe-sentinel **0** default), registered together by `pkill.cpp` unmoved. Two further hooks land
  cheaply as specced: `dispatch_do_wear` (void, loud tripwire; `act_obj2.cpp` registers a new
  `wear_all_carried_items()` wrapper baking in the fixed call shape) and `dispatch_is_zone_populated`
  (bool, safe-sentinel **TRUE** default — "assume populated," degrades to "never reset" rather than
  risking a reset mid-session; `comm.cpp` registers `zone_is_populated()`, the semantic inverse of
  the retired `is_empty()`). All backing storage/dispatch for these four zone hooks lives in
  `db_world.cpp` (the "storage ahead of promotion" precedent `db_world.cpp`'s own boot-shops/
  mudlle-converter hooks already established), registrar declarations centralized in `db.h`.
- **`zone.cpp`'s `is_empty(int)` retired outright**, not just its call sites — once `comm.cpp`'s
  registered `zone_is_populated()` body proved to be the function's only remaining reason to exist
  (zero other callers tree-wide, re-verified immediately before deletion), the local
  `int is_empty(int);` forward decls and the function body itself were both deleted.
- **The `command_interpreter` hook (`script_hooks.h`, new header).** `mudlle.cpp:862`'s one live
  call (`intelligent()`'s `'C'` special-list case; the block at ~894-940 is commented-out dead text)
  invokes the **entire player-command dispatcher**, not one ACMD — what mob-programs fundamentally
  *are* — so it is neither a `combat_command` cell (that table dispatches individual `do_*`
  functions) nor an `output_seam` forwarder (no output composition). `void (*)(char_data*, char*,
  waiting_type*)`, loud-tripwire default (an unregistered mob-program command is a real error, not a
  silent best-effort push), registered by `interpre.cpp` (which already forward-declares
  `command_interpreter`) before `boot_db()`.
- **The `PERS` forwarder — the wave's named collapse condition, confirmed it does NOT fire.**
  `mudlle2.cpp:286`'s one call to `PERS()` (utility.cpp) is pointer-returning (`char*` into a
  function-local `static char name[128]`) — abort-tripwire class, the `mudlle_converter`/
  `get_from_txt_block_pool` precedent — registered in `script_hooks.h`, real body stays
  `utility.cpp`. Since `mudlle.cpp` needs `mudlle2.cpp` (the 14-helper intra-band edge above), a
  `PERS` forwarder that failed to resolve would have collapsed the wave's script side to
  `rots_pathfind` + `zone` only; it resolved cleanly (PERS's own body genuinely cannot relocate —
  its `CC_USE`/`CC_NORM` macros expand to `color.cpp` state, consistent with, not contradicting, the
  blocker-buster wave's prior "PERS body stays app-tier" finding — but the *forwarder* is a
  separate, precedented question from the *relocate* question), so Cluster A's full 3-TU scope
  survived intact.
- **`put_to_txt_block_pool`** — one new `output_seam.h` forwarder (the 8th blocker-buster-style
  addition), shared by `mudlle.cpp:808`/`mudlle2.cpp:182`; void, **safe logged no-op** default
  (distinct from `get_from_txt_block_pool`'s abort-tripwire class, since PUT never dereferences its
  argument). `comm.cpp`'s real body renamed to a file-static `put_to_txt_block_pool_impl()`,
  registered into both this new `output_seam` sink and `entity_hooks.h`'s existing PUT hook.
- **CRLF correction.** The design spec claimed all four target files (`graph.cpp`/`mudlle.cpp`/
  `mudlle2.cpp`/`zone.cpp`) were plain LF at @b6f6b76 — Task 0 measured otherwise: `zone.cpp` pure
  CRLF (703/703 lines), `graph.cpp`/`mudlle.cpp`/`mudlle2.cpp` mixed CRLF+LF. Every T1/T2 edit to
  all four used the Python binary-mode byte-edit method (never the text-mode `Edit` tool), the
  established `handler.cpp`-precedent workaround for the environment's LLVM-style formatter hook,
  which does not round-trip losslessly against this repo's WebKit `make format` style. `utility.cpp`/
  `utils.h`/`db.h`/`pkill.cpp`/`act_obj2.cpp`/`handler.h`/`handler.cpp` were also confirmed
  mixed-CRLF in transit (outside the census's original four-file scope) and handled the same way.
- **Discriminator audit found one genuine, standing gap.** `combat_command::say` had been
  registered since the combat-seed wave but had **zero** `issue_command()` callers anywhere in the
  tree until this wave's 15 new call sites (graph ×2, mudlle ×11, mudlle2 ×2) — added
  `IssueCommandReachesTheRealDoSayWhenRegistered`/`IssueCommandDefaultsToANoOpWhenSayIsUnregistered`,
  mirroring the combat-trio wave's identically-shaped `move`-cell gap.
- **Root-Makefile hand-list** updated for both new library names (the entity-seed lesson: the root
  Makefile builds named targets, not `all`, so an un-listed linkcheck target reports CTest "Not Run"
  in the i386 container rather than actually exercising the check).
- **Test-count delta for this wave: 1398 → 1415** — Task 1 +13 (script_hooks/world_hooks/output_seam
  seam tests, incl. the re-homed `extract_char` trio moved not duplicated), Task 2 +2 (the
  `combat_command::say` discriminator gap above), Task 3 +2 (`PathfindLayerAcyclicity` +
  `ScriptLayerAcyclicity` themselves). All three gate hosts (`macos-arm64`, `rots64`,
  `macos-arm64-asan`) confirmed 1415/1415 at Task 3's final gate; `ConvertEquivalence` 17/17 both
  hosts throughout. See `AGENTS.md`'s "Testing Guidelines" for the full reconciled per-task chain.

### Pathed data-model includes

`rots_core` owns `target_include_directories(rots_core PUBLIC core/include)`: every consumer that
links `RotS::core` (`ageland`, and transitively `rots_entity`/`rots_convert` via `RotS::entity`,
see "`rots_entity`" above) gets the `core/include` root transitively, so
`ageland`'s own `target_include_directories` no longer lists it directly. `rots_persist` (below)
now owns `persist/include` the same way, PUBLIC, so `ageland` and `rots_convert` also stopped
listing it directly once they linked `RotS::persist` (persist-split PS Task 4). `ageland_tests`
does not link `RotS::core`/`RotS::persist` (see above and "`rots_persist`" below — TESTING parity:
it compiles `ROTS_CORE_SOURCES`/`ROTS_PERSIST_SOURCES` directly rather than linking the shipping
archives), so it keeps `core/include` and `persist/include` as its own direct include dirs.
`rots_platform` similarly owns `target_include_directories(rots_platform PUBLIC
platform/include)`, so every consumer that links `RotS::platform` gets the `platform/include` root
transitively — the same pattern as `core/include`, one layer down. `core/include`, `persist/include`,
and `platform/include` contain nothing but a `rots/` subtree and are
the **only** directories ever added to an include path for the new header layout — `src/`
itself must never be added to `-I`/`-iquote`/`/I`: `src/limits.h` (project-specific player-rank
constants) would shadow the standard `<limits.h>`, breaking any standard header that
`#include_next`s its way through the system one (see the long comment at
`src/CMakeLists.txt` around the `ageland_tests` include setup). Headers inside the new tree
reference each other with pathed includes (`#include "rots/core/tables.h"`), and reference the
still-flat legacy headers by relative path (`"../../../../platdef.h"`) rather than adding `src/`
to a search path.

## `db.cpp` split into four translation units

`src/db.cpp` (5,803 lines) no longer exists — it was split along the persist/world seam from
spec §4a (`docs/superpowers/specs/2026-07-16-library-architecture-design.md`) into four flat
`src/` sources, all still compiled directly into `ageland`/`ageland_tests` via
`ROTS_SERVER_SOURCES` (no new library target this wave):

- **`db_world.cpp`** — room/mob/obj/zone/shop index, parse, and reset (`boot_db`'s world-loading
  half): `index_boot`, `load_rooms`/`load_mobiles`/`load_objects`, `real_room`/`real_mobile`/
  `real_object`, mudlle script loading, and the `room_data`/`room_data_extension` methods.
- **`db_players.cpp`** — pfile index, character load/store, and the crime + exploit JSON codecs:
  `build_player_index`, `load_char`/`save_char`, `store_to_char`/`char_to_store`, the
  `crime_json` namespace, and the exploit-record read/write codec. This is the persistence
  subset `rots_convert` (below) links.
- **`db_boot.cpp`** — the renamed remainder: boot orchestration (`boot_db`, invoking the world
  and player halves in order), `reset_time`, and the two **capture-side** live-game functions
  `record_crime` (walks `world[]`) and `add_exploit_record` (walks `combat_list`) — these stayed
  out of `db_players.cpp` on purpose: they *call into* the running game to observe state, whereas
  the codecs only serialize/deserialize records already captured. That capture/codec split is
  what makes `db_players.cpp` linkable into `rots_convert` at all.
- **`entity_lifecycle.cpp`** — shared char/object lifecycle (`free_char`, `make_char_data`,
  `clear_char`, `init_char`, `reset_char`, `free_obj`, …) used by both the world-loading half
  (`read_mobile` calls `clear_char`) and the persist half (store paths call `clear_char`/
  `init_char`), so it belongs to neither `db_world.cpp` nor `db_players.cpp`. It also absorbed
  the affect/derived-ability engine (`affect_modify`/`affect_total`/`affect_naked`/
  `affect_to_char`/`affect_remove`/`recalc_abilities`/the naked-stat and
  confuse-modifier helpers) and the save-file cipher (`encrypt_line`/`decrypt_line`), relocated
  verbatim from `handler.cpp`/`profs.cpp`/`utility.cpp` so `rots_convert` links one definition
  instead of a duplicate copy in `convert_stubs.cpp`. It wasn't in the original three-TU split
  plan — it's the unforeseen fourth TU, and the natural seed for a future `rots_entity` library.

`db.h` is unchanged as the stable public surface: every function it declares kept its linkage, so
no caller outside these four files needed to change. The one hard persist→world data edge
(`save_char` reading `world[ch->in_room].number`) became a seam function, `int
world_room_vnum(int room_index)`, declared in `db.h` next to `real_room` and defined in
`db_world.cpp`. Persist-split PS Task 4 later cut `db_players.cpp`'s direct call to it (and the
sibling `add_exploit_record` edge into `db_boot.cpp`, `rename_char`'s exploit-trail note) via
`persist_hooks.h`'s pre-boot-registered hook pair — see "`rots_persist`" below; `rots_convert`'s
`convert_stubs.cpp` no longer needs a stand-in definition for either symbol, since it never calls
the registration functions and gets each hook's null default instead.

## `rots_convert`: the persistence-boundary acid test

`rots_convert` is a second executable (its own small `main()`, `src/convert_main.cpp`) that
performs legacy → modern character conversion **en masse, outside MUD execution** — spec §4b.
As of entity-completion Task 3, it links:

```
rots_convert = RotS::platform + RotS::core + RotS::entity + RotS::persist + rots_build_flags
             + convert_main.cpp
```

deliberately **NO** `db_world.cpp`/`db_boot.cpp` and **NO** combat/commands/app translation unit.
**`convert_main.cpp` is now the executable's only direct source** — every other TU it needs
(down to `char_utils.cpp`/`char_utils_combat.cpp`, the last two holdouts, see below) arrives
transitively through the four static libraries, with zero stub bodies standing in for anything.
As of persist-split PS Task 4, `db_players.cpp`/`character_json.cpp`/`objects_json.cpp`/
`exploits_json.cpp`/`account_management.cpp`/`account_cache.cpp`/`obj_files.cpp`/
`pkill_json.cpp`/`mail_json.cpp`/`boards_json.cpp`/`convert_exploits.cpp`/`convert_plrobjs.cpp`/
`color_convert.cpp`/`save_benchmark.cpp` all arrive via `RotS::persist` (see "`rots_persist`"
above) instead of as direct sources — the four codec carves (`obj_files.cpp`/`pkill_json.cpp`/
`mail_json.cpp`/`boards_json.cpp`) are what let `objsave.cpp`/`pkill.cpp`/`mail.cpp`/`boards.cpp`'s
**persistence** halves join at all. Their runtime/bridge/gameplay halves stay OUT, deliberately —
G-side orchestrators (`Crash_crashsave`/`idlesave`/`rentsave`, `Crash_load`/`Crash_listrent` and
the rent/receptionist flow), `pkill_tab`/rankings/`combat_list` walkers, the mail store
(`find_char_in_index`/`persist_mail_or_log`/`index_mail`/`scan_file`/`has_mail`) and postmaster
gameplay, and boards' display half plus its `save_board`/`apply_board_save_data`/`load_board`
bridge — none are `nm`-clean against the converter's link surface, and chasing them is recorded
follow-on (see "`rots_persist`" above for the boards-bridge deferral in particular). As of
entity-seed Task 6, `entity_lifecycle.cpp`/`object_utils.cpp`/`environment_utils.cpp` arrive via
`RotS::entity` (see "`rots_entity`" above) rather than as direct sources; as of
entity-completion Task 3, `char_utils.cpp` and `char_utils_combat.cpp` join them there too — EC
Tasks 1-2 had already cut both TUs' last real combat/handler/big_brother welds (`fname`/
`fname_nameholder`/`other_side`/`other_side_num` relocated verbatim from `handler.cpp` into
`char_utils.cpp` itself; `attack_hit_text[]`/`get_hit_text` relocated verbatim from `fight.cpp`
into `consts.cpp`; `get_energy_regen()`'s `wild_fighting_handler` construct-and-query and
`char_utils_combat.cpp`'s `big_brother::on_character_attacked_player()` call inverted through
`entity_hooks.h`'s wild-attack-speed-multiplier/attacked-player hooks), so both TUs were already
`nm`-clean leaves by the time Task 3 moved their library membership — see "`rots_entity`" above
for the full account. `char_utils.cpp`/`char_utils_combat.cpp` no longer appear on this
executable's direct source list at all; they, like `entity_lifecycle.cpp`'s siblings, now arrive
purely through `RotS::entity`.

- **It calls the same code the MUD uses** (`character_json`/`objects_json`/`exploits_json`, the
  `convert_*` binary-to-JSON one-time migration converters) so mass-conversion output is
  byte-identical to in-MUD lazy conversion by construction — proven by the `ConvertEquivalence`
  suite (see "Testing" below).
- **It is CI-linked, not merely CI-tested.** `rots_convert` is added to CMake's default `all`
  target (no `EXCLUDE_FROM_ALL`), so every CI job builds it. If a future change re-welds
  `db_players.cpp`/`entity_lifecycle.cpp` to the game (combat/world/commands/session), this
  target **fails to link** and the build breaks — the converter is the executable acid-test that
  the persistence boundary holds, not a check anyone has to remember to run.
- **`src/convert_stubs.cpp` WAS the weld ledger — deleted, entity-completion Task 3.** For three
  waves it held one loud, documented stub per app/combat symbol the linked persist/entity code
  still referenced but the converter's own call graph never exercised. Each stub recorded the
  symbol, its real home, why the converter never reached it, and the follow-on that would remove
  it. Its whole point was to make the remaining persistence/game coupling enumerable and its
  shrinkage measurable, rather than hiding the coupling behind an unexplained empty function —
  and entity-seed Tasks 1-6, persist-split PS Tasks 1-4, and entity-completion Tasks 1-3 are the
  measured proof: the ledger shrank from ~40 documented stubs/~1.6K lines (db.cpp-split baseline)
  through ~19 (entity-seed exit — `send_to_char`/`act`/`vsend_to_char`/
  `track_specialized_mage`/`untrack_specialized_mage` via the output seam, `log`/`mudlog`/
  `create_function`/`free_function`/`str_dup`-family/`number()` via the platform relocations,
  `is_room_outside`/`is_light` via `rots_entity`, and more — see the file's own former header
  comment, preserved in git history, for the task-by-task account), through 5 (persist-split
  exit — four named groups: `fname`, `other_side`, `get_hit_text`, the `wild_fighting_handler`
  ctor/method pair), to **ZERO stub function bodies**.
  Entity-completion Task 1 deleted three more (`fname`/`other_side`, `get_hit_text`) the same
  way: relocating them verbatim into `char_utils.cpp`/`consts.cpp` (already-linked TUs) closed
  those welds outright. EC Task 2 deleted the last stub body —
  `player_spec::wild_fighting_handler`'s ctor + `get_attack_speed_multiplier()` — by inverting
  `char_utils.cpp`'s `get_energy_regen()` call through `entity_hooks.h`'s new
  wild-attack-speed-multiplier hook instead; `rots_convert` never registers it, so the hook's
  null default (1.0f) fires, byte-identical to the deleted stub's return value. **EC Task 3 then
  deleted the now-empty ledger file outright** (`git rm src/convert_stubs.cpp`, the "feat:
  char_utils + char_utils_combat join rots_entity; convert_stubs.cpp deleted — weld ledger ZERO"
  commit, entity-completion wave — see `git log -- src/convert_stubs.cpp`): with zero stub bodies
  remaining there was no ledger left to maintain, and the file's sole
  remaining purpose — being `rots_convert`'s third direct source alongside `convert_main.cpp` and
  `char_utils.cpp` — evaporated the same task `char_utils.cpp`/`char_utils_combat.cpp` moved to
  library membership (see "`rots_entity`" above and this section's link line, updated). The
  ledger's history is not lost — it lives in git (`git log -- src/convert_stubs.cpp`,
  `git show <commit>:src/convert_stubs.cpp` for any prior state) — it simply no longer needs a
  standing file to hold it, because there is nothing left to enumerate: every symbol
  `rots_convert` needs is now either a real cross-linked library definition or a null-defaulted
  hook default (see below). Persist-split deleted 14 stub bodies total across three tasks. PS Task 1 deleted the color
  trio's stand-ins (`nearest_ansi_color()`+`ansi_palette`, `convert_old_colormask()`,
  `sync_color_slot_foreground_from_ansi()`) when `color_convert.cpp` was carved out of
  `color.cpp` as a leaf TU (see "`color_convert.cpp` membership" above). PS Task 3 deleted
  `Crash_get_filename()`/`Crash_delete_file()`/`build_default_account_backed_object_data()`
  when `obj_files.cpp` first joined `rots_convert`. PS Task 4 deleted the remaining 8 across two
  mechanisms: the `world_room_vnum`/`add_exploit_record` inversions (see "`rots_persist`" above)
  and six of the nine controller-adjudicated relocations that had carried a stub or
  hand-duplicated stand-in here (`find_player_in_table`/`find_name`/`unaccent`/`recalc_skills`/
  `file_to_string`/`file_to_string_alloc`). `color_convert.cpp`'s PS Task 4 library-membership
  move (into `ROTS_PERSIST_SOURCES`) was already stub-free by that point — its stand-ins were PS
  Task 1's deletion, not PS Task 4's. The other three relocated symbols,
  `utils::set_tactics`/`set_shooting`/`set_casting`, never carried a `convert_stubs.cpp` stub at
  all: `char_utils.cpp` was already a direct `rots_convert` source before and after the move, so
  relocating them within already-linked TUs closed no converter-side weld — see the file's own
  header comment for the full accounting.

  Nothing remains unreachable-but-stubbed: every symbol `rots_convert` needs is now either a
  real cross-linked definition or a null-defaulted `entity_hooks.h`/`persist_hooks.h`/
  `output_seam.h` hook default (see "`rots_entity`"/"`rots_persist`" above). `char_utils.cpp` and
  `char_utils_combat.cpp` — the last two TUs still holding out for the `nm`-cleanliness reason
  noted above — joined `RotS::entity` as of entity-completion Task 3 (see "`rots_entity`" above);
  `rots_convert`'s only remaining direct source is `convert_main.cpp` itself.
- **This target is CMake-only.** It is not added to the flat `src/Makefile` / `src/tests/Makefile`,
  which compile same-directory only against a single hand-maintained `OBJFILES` list per binary —
  wiring a second multi-file executable into that pattern isn't worth it for a CI-only
  boundary check. Use a CMake preset (or the root `Makefile`'s `configure`/`build` wrappers) to
  build it.

### Testing: `ConvertEquivalence` proves the persistence boundary is behavior-preserving

`src/tests/rots_convert_equivalence_tests.cpp` is a value-parameterized GoogleTest suite
(`ConvertEquivalence.PerRaceLegacyPfileMatchesInMudConversion`) registered in CTest alongside the
rest of the unit suite. For every playable `RACE_*` constant `character.h` declares (all sixteen,
not just the chargen-selectable subset — the coverage comment in that file explains why the NPC
races `RACE_EASTERLING`/`RACE_HARAD`/`RACE_UNDEAD`/`RACE_TROLL` are included too) plus one
affect-bearing `RACE_HUMAN` variant, it builds a fixture legacy pfile, runs it through
`rots_convert` out-of-process, and asserts the result is byte-identical to running the same
character through the in-MUD conversion path — 17 cases total. `ageland_tests` depends on the
`rots_convert` target (`add_dependencies`) and receives its build path through the
`ROTS_CONVERT_EXECUTABLE` compile definition, so the test always exercises the freshly built
binary, not a stale one; if `rots_convert` isn't available in a given build configuration the test
`GTEST_SKIP()`s rather than failing. This is the suite that makes spec §4b's "byte-identical by
construction" claim load-bearing rather than aspirational.

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
   (`db_boot.cpp`, 2 sites, the capture-side `add_exploit_record`) and
   `crime_record_type.criminal/victim/witness` (`db_players.cpp`, 9 sites, the crime codec —
   both were `db.cpp` at the time of this finding; see "`db.cpp` split into four translation
   units" above). Real truncation once a server's idnum counter passes 32767 — but
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
(`db.cpp` at the time of this wave; `clear_char` now lives in `entity_lifecycle.cpp` and
`read_mobile` in `db_world.cpp` — see "`db.cpp` split into four translation units" above) — this
is required because those types carry non-trivial members
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
