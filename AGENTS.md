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
- build/: CMake build artifacts and test scaffolding; not checked in. Host `build/`
  holds only host CMake preset subdirectories (`macos-arm64/`, `linux-x86-legacy/`,
  …) — the `rots`/`rots64` containers keep their own CMake trees in private named
  volumes (`rots-build-i386`/`rots-build-x64`) that a host command can never reach or
  poison, and vice versa; see `docs/BUILD.md` "Container build isolation".
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
- `rots_entity` (L2 static library seeded by the entity-seed wave, grown to 5 TUs by entity-completion Task 3, then to 8 TUs by the placement-seam wave: `entity_lifecycle.cpp`/`object_utils.cpp`/`environment_utils.cpp`/`char_utils.cpp`/`char_utils_combat.cpp`/`placement.cpp`/`containment.cpp`/`equipment.cpp`, linked as `RotS::entity`) and its `EntityLayerAcyclicity` linkcheck are CI-linked the same way as `rots_platform`/`rots_core` — see docs/BUILD.md "Library layering". The placement-seam wave carved the entity-pure relationship core (placement/containment/equipment, ~60 functions) out of `handler.cpp`/`utility.cpp` behind a four-resolver world seam (`room_by_id`/`room_by_id_total`/`zone_by_id`/`obj_index_by_id`); the session/command/combat-coupled remainder of both files stays app-compiled by design (Stage 1 scope — see docs/BUILD.md), not as an unexamined gap. `rots_convert`'s only direct source is now `convert_main.cpp`; the entity-completion wave deleted `src/convert_stubs.cpp` (the weld ledger reached zero stub bodies).
- `rots_persist` (L3 static library stood up by the persist-split wave: `db_players.cpp` + the JSON/account codecs + `obj_files.cpp`/`pkill_json.cpp`/`mail_json.cpp`/`boards_json.cpp`/`color_convert.cpp`/`save_benchmark.cpp`, linked as `RotS::persist`) and its `PersistLayerAcyclicity` linkcheck are CI-linked the same way — see docs/BUILD.md "Library layering".
- `rots_world` (L3 static library stood up by the world-seed wave: `db_world.cpp`/`zone_load.cpp`/`weather.cpp`, linked as `RotS::world`, PUBLIC-linking `RotS::persist` too for the one sanctioned L3-peer edge `db_world.cpp` → `rots::persist::set_room_vnum_hook`; grown to **4 TUs** by the l4-seed wave, which rode `zone.cpp` itself — the reset half `zone_load.cpp` was carved out of, world-seed wave — into `ROTS_WORLD_SOURCES`) and its `WorldLayerAcyclicity` linkcheck are CI-linked the same way. It is 4 of the spec's ~15-TU sketch for the row — `zone.cpp` is now fully resolved (both halves); `graph.cpp`/`mudlle.cpp`/`mudlle2.cpp` did NOT join `rots_world` as the row's original sketch assumed (see `rots_pathfind`/`rots_script` below); the `shape*`/`script` OLC-tool family remains deferred follow-on. The spec §7 Placement seam itself landed as a Stage-1-scoped tier-line split (placement-seam wave): `placement.cpp`/`containment.cpp`/`equipment.cpp` joined `rots_entity` (not `rots_world`), while `handler.cpp`/`utility.cpp`'s session/command/combat-coupled remainder stays app-compiled by design — see docs/BUILD.md "Library layering".
- `rots_pathfind` (L4-band, new — l4-seed wave: `graph.cpp`, 1 TU, linked as `RotS::pathfind`) and `rots_script` (L4-band, new — l4-seed wave: `mudlle.cpp`/`mudlle2.cpp`/`script_hooks.cpp`, 3 TUs, linked as `RotS::script`, PUBLIC-linking `RotS::pathfind` for the sanctioned intra-band `find_first_step` edge; **grown to 4 TUs by the behavior wave**, which landed `mobact.cpp` — see below) stand up the **first library band above the L3 peer tier** — see `docs/superpowers/specs/2026-07-16-library-architecture-design.md`'s §3 REVISION for the full architectural rationale (certified `persist < world < combat < pathfind < script < app` order; the no-bidirectional-links invariant). Their `PathfindLayerAcyclicity`/`ScriptLayerAcyclicity` linkchecks are CI-linked the same way — **eight** libraries and **eight** `*LayerAcyclicity` linkchecks total now exist. See docs/BUILD.md's "L4 band" subsection for the seam/hook/adjudication account (the `equip_char`/`pkill_get_*_fame` OVERTURNs, the `extract_char` re-home from `combat_hooks.h` down to L2 `entity_hooks.h`, the `command_interpreter`/`PERS` hooks). i386 verification for both new libraries/linkchecks is now measured (Task 5 finalization battery, `log/i386-battery/step1-20260721T044424Z.log`): **1415/1415** via the ctest flow, including both new `*LayerAcyclicity` linkchecks — see the Testing Guidelines section below for the full reconciled i386 numbers and monolithic-runner cross-check.
- `rots_combat` (L3 static library seeded by the combat-seed wave — `skill_timer.cpp`/`battle_mage_handler.cpp`/`weapon_master_handler.cpp`/`wild_fighting_handler.cpp` — and grown to **6 TUs** by the blocker-buster wave: the 4 seed TUs plus `combat_hooks.cpp` (Task 2) and `visibility.cpp` (Task 4), linked as `RotS::combat`, PUBLIC-linking `RotS::world`/`RotS::entity`/`RotS::core`/`RotS::platform` — `visibility.cpp`'s `weather_info` reference is the one L3-peer edge this subset needs, added alongside it) and its `CombatLayerAcyclicity` linkcheck are CI-linked the same way; it is the third and final L3 peer to exist as a build target. It is 6 of the spec's 16-TU sketch for the row (4 SEED-CLEAN TUs per the combat-census, plus the 2 new blocker-buster enabler TUs not in the original 16-TU sketch) — `profs` (caveated) and the 11 DEFER TUs (`spec_ass`/`olog_hai`/`clerics`/`mage`/`mystic`/`mobact`/`spell_pa`/`limits`/`ranger`/`spec_pro`/`fight`) are deferred follow-on. The combat-seed wave's two blockers (app-side output, and a command-dispatch seam for the mob-AI/spec-proc TUs) are now CLEARED: the blocker-buster wave shipped all four combat-growth enablers **consumer-free** — `output_seam` gained 7 forwarders (`send_to_all`/`send_to_room`/`send_to_room_except_two`/`break_spell`/`abort_delay`/`complete_delay`/the txt-pool content overload), `combat_hooks.h` added a 25-cell boot-registered ACMD dispatch table (`assign_spell_pointers` precedent), the poison-notification hook (`entity_hooks.h`) retired placement-seam deferral cluster 3 (`obj_from_char`/`extract_obj` completed their moves to `rots_entity`), and the full 12-function visibility family (`CAN_SEE`×2/`CAN_SEE_OBJ`/`get_char_room_vis`/`get_player_vis`/`get_char_vis`/`get_obj_in_list_vis`/`get_obj_vis`/`get_object_in_equip_vis`/`generic_find`/`get_real_OB`/`get_real_parry`, plus the carved-out `see_hiding`) joined `rots_combat`'s new `visibility.cpp`. The remaining blocker for the 11 DEFER TUs is now pure per-TU migration (body move + up-call conversion + census re-verification), not enabler design — see docs/BUILD.md "Library layering" (the `rots_combat` section's blocker-buster subsection) and `.superpowers/sdd/combat-census.md`/`.superpowers/sdd/blocker-census.md`. The combat-seed wave also completed the placement-seam wave's two remaining deferral riders: the time quartet (`real_time_passed`/`mud_time_passed`/`day_to_str`/`age`) rejoined `rots_core` (`consts.cpp`), and `NumberedName` extracted to `rots/platform/numbered_name.h` (L0 platform tier, not the plan's core sketch — see docs/BUILD.md's `rots_combat` section, "NumberedName's L0-visibility tier decision") so `parse_numbered_name`→`rots_util.cpp` and `get_char`→`entity_lifecycle.cpp` could complete.
  **The combat-pilot wave grew `rots_combat` 6 → 8 TUs**, landing `clerics.cpp` and `fight.cpp` —
  two of the 11 DEFER TUs above — as a joint membership move (Task 5; the two files call each
  other directly, so a standalone promotion of either would have left `CombatLayerAcyclicity`
  unable to resolve the edge — see `docs/superpowers/combat-migration-playbook.md`'s "intra-subset
  rule"). `clerics`/`fight` leave the list above: the DEFER TUs are now **9** (`spec_ass`/
  `olog_hai`/`mage`/`mystic`/`mobact`/`spell_pa`/`limits`/`ranger`/`spec_pro`), plus the still-
  caveated `profs`. New seams landed consumer-free then proven under real conversions: a
  `special()` registered hook (int-returning, matches `interpre.h:99`'s real 6-parameter
  signature — NOT a 26th `combat_command` cell); a big_brother `is_target_valid`/
  `on_character_died` hook pair (`entity_hooks.h`, a `kNoSkillId` sentinel unifying
  `is_target_valid`'s two differently-bodied overloads behind one fn-ptr); an `extract_char` hook
  (reproducing its 1-arg-forwards-to-2-arg sentinel shape); the `gain_exp`/`gain_exp_regardless`/
  `remove_fame_war_bonuses` trio (registered by `limits.cpp` itself — ALREADY BUILT for whenever
  `limits.cpp` promotes); and an app-other trio (`Crash_crashsave`/`call_trigger`/`pkill_create`,
  `call_trigger`'s tripwire defaulting to logged TRUE since its callers treat FALSE as
  script-veto). `rots_combat` gained a PUBLIC `RotS::persist` link (two real edges: `fight.cpp`'s
  pre-existing `save_char()` plus the new `rots::persist::dispatch_exploit_capture()` this wave's
  `add_exploit_record` conversion added, the second requiring `dispatch_exploit_capture` to gain
  external linkage out of a `db_players.cpp`-local anonymous namespace). The wave also shipped a
  combat smoke-test harness (`ROTS_RNG_SEED` + `tools/combat_smoke.py` + `scripts/
  combat-golden.sh`) that landed on the fallback ladder's **capture-only rung (b)** — same-seed
  transcript drift proved real under the shared global `rots_rng` engine's real-time pulse-loop
  interleaving, so `verify` is informational only, never a merge-gating comparison — and the
  migration playbook (`docs/superpowers/combat-migration-playbook.md`) the remaining DEFER-9+profs
  rows will reuse, including a per-TU cost table. See docs/BUILD.md "Library layering" (the
  `rots_combat` section's combat-pilot subsection) for the full write-up.
  **The combat-trio wave grew `rots_combat` 8 → 11 TUs**, landing `olog_hai.cpp`, `mystic.cpp`, and
  `profs.cpp` in ONE membership commit — the playbook's first **standalone** promotions (olog_hai and
  mystic each closed over their own combat-peer edges independently, zero edges to each other) rather
  than a cycle-forced joint move like `clerics`/`fight`; `profs` rode as a census-gated rider whose
  one real coupling (`scale_guardian`, owned by `mystic.cpp`) is a one-directional gate that dissolves
  once mystic promotes, not a cycle. `olog_hai`/`mystic` leave the DEFER list above: the DEFER TUs are
  now **7** (`spec_ass`/`mage`/`mobact`/`spell_pa`/`limits`/`ranger`/`spec_pro`) — `profs`'s
  SEED-WITH-SEAM caveat is RESOLVED, it has joined. New this wave: a 26th `combat_command` cell
  (`dismount`); `one_argument`/`fill_word`/`fill[]`/`half_chop` relocated to L0 `rots_util.cpp`;
  the `saves_confuse`/`saves_insight`/`saves_leadership`/`saves_mystic`/`saves_poison` five-pack and
  `add_follower` relocated to L2 (`char_utils_combat.cpp`/`entity_lifecycle.cpp` respectively);
  `get_guardian_type` relocated to `rots_combat` itself (`visibility.cpp`, L3 not L2 — it reads
  `mob_index`, an L3-world global, so `rots_entity` cannot host it). `CombatLayerAcyclicity` went
  green on the first build attempt, both hosts — no census miss, unlike the pilot wave's two
  (`gain_exp`/`waiting_list`). Full write-up, including the census's overturns of the playbook's own
  stale cost-table estimates (olog_hai's "combat-peer=6" re-derived to 1; the `scale_guardian` helper
  cluster is 6 functions not 4; a second, spec-missed external caller of `scale_guardian` at
  `objsave.cpp:775`): docs/BUILD.md "Library layering" (the `rots_combat` section's combat-trio
  subsection) and `docs/superpowers/combat-migration-playbook.md`'s per-TU cost table (olog_hai/
  mystic/profs rows now marked RESOLVED).
  **The behavior wave grew `rots_combat` 11 → 12 TUs**, landing `limits.cpp` (a standard L3
  DEFER-row promotion, `CombatLayerAcyclicity` green first try, both hosts) — and, in the **same**
  wave, grew `rots_script` 3 → 4 TUs by landing `mobact.cpp` (`ScriptLayerAcyclicity` green first
  try, both hosts), answering the l4-seed wave's own "still undecided" downstream note: the mob-AI
  driver's tier is `rots_script`, not `rots_combat` — the driver homes with the engine it invokes.
  `limits`/`mobact` leave the DEFER list above: the DEFER TUs are now **5** (`spec_ass`/`mage`/
  `spell_pa`/`ranger`/`spec_pro`). **The wave's defining coupling**: `limits.cpp:1398` calls
  `one_mobile_activity()` (`mobact.cpp`) — once `limits` is `rots_combat` (L3) and `mobact` is
  `rots_script` (L4), this is an `L3 → L4` upward edge across a tier boundary that never moves, so
  unlike every prior hook (which exist only because two sides don't share a library *yet*) it is a
  **permanent** inversion — the codebase's first. New `combat_hooks.h::dispatch_one_mobile_activity`
  hook (registered by mobact, dispatched by limits); two `Crash_*` sibling cells
  (`Crash_idlesave`/`Crash_extract_objs`); a new `script_hooks.h::dispatch_virt_program_number`
  cell (the design spec's pre-authorized rider gate firing for the first time, closed at 1 of the
  pre-authorized ≤3 same-shape edges); two new `entity_hooks.h` hooks (`char_from_room`, and a
  second big_brother pair `on_character_afked`/`on_corpse_decayed`); three new `output_seam`
  entries (`close_socket` — a symbol takeover, not merely a forwarder — plus
  `no_specials_active()`/`request_circle_shutdown()` accessors). Relocations: `affect_remove_notify`
  → L2 `entity_lifecycle.cpp`; `recalc_zone_power`/`report_zone_power` → `rots_world`/
  `db_world.cpp`; `pkill_get_rank_by_character` → `rots_persist`/`db_players.cpp`; `saves_spell` →
  L3 `rots_combat`/`fight.cpp` (an OVERTURN of the design spec's own L2 default — its body writes
  `fight.cpp`-owned `spllog_*` globals, and `rots_entity` does not link `RotS::combat`). The
  cleanup-commit verdict was **STAYED**, not deleted: `fight.cpp` (six call sites, not the stale
  in-file comment's "five") and `clerics.cpp` (one site) still dispatch through the
  `gain_exp`/`gain_exp_regardless`/`remove_fame_war_bonuses` hooks rather than calling limits.cpp's
  now-intra-lib globals directly, so nothing could be removed — comment-only cleanup commit. Full
  write-up: docs/BUILD.md "Library layering" (the `rots_combat` section's behavior-wave subsection)
  and `docs/superpowers/combat-migration-playbook.md`'s "The behavior wave" section (mobact/limits
  rows now marked RESOLVED).

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
- `scripts/i386-battery.sh`'s completed-step markers are stamped per commit
  (`git rev-parse HEAD`); a marker left over from a prior wave's HEAD never
  green-lights a skip once HEAD has moved, so a fresh branch always re-runs the full
  battery at least once (see `scripts/i386-battery.sh` and docs/BUILD.md "Container
  build isolation").
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
- C/C++: a GoogleTest suite (`cd src/tests && make tests && ../../bin/tests`, or `ctest --test-dir build`) covers ~1415 tests as of the l4-seed wave (see the "L4-seed wave" entry at the end of the per-wave chain below for the reconciled +17 delta and its i386-pending status); historically, 1365 total on macOS native, freshly re-measured for the blocker-buster wave's Task 5 docs gate — **75 skips**, unchanged from the combat-seed wave's Task 3b measurement; `rots64`'s 1365/1365 total is confirmed by every one of this wave's per-task gate reports (Tasks 1-4b all landed with both hosts matching exactly) but its skip count was not independently re-run for this docs-only task — carried forward at **77** from the combat-seed measurement, since no blocker-buster task touched a POSIX/32-bit-fixture-gated test; the i386 container's count/skips are now measured for this wave's finalization battery (`log/i386-battery/`, `step1-20260720T051230Z.log`/`step2-20260720T052803Z.log`): **1365 total / 6 skips** via the ctest flow (`100% tests passed, 0 tests failed out of 1365`, with the same 6 tests — `AccountManagement.FormatsOutOfRangeSummaryTimestampsAsInvalid`/`LeavesStoredObjectPathUnchangedWhenCanonicalObjectWriteFails`/`MigrationFailsClosedWhenLegacyFileRetirementFails`/`MigrationRestoresRetiredFilesWhenExploitRetirementFails`, `DbLoader.FailsClosedWhenAccountNativeObjectOrExploitJsonCannotBeRead`, and `OlogHaiHelpers.HeavyFightingAndRidingAdjustOverrunDamage` — listed under "did not run"). This reconciles cleanly against the flat-Makefile monolithic runner's own summary of 23 skips: subtracting its 17 `PerRace/ConvertEquivalence.*` skips (monolithic-only by design — `rots_convert` is a CMake-only target per its own CMakeLists.txt comment, so it's simply absent from the flat build, whereas the CMake ctest flow builds it and those 17 pass there) leaves the identical 6-test remainder both ways, per the combat-seed wave's established reconciliation method; Windows count tracked separately in CI, where a handful of POSIX-only cases don't build/run), including characterization goldens (`src/tests/goldens/`, `docs/superpowers/goldens/`) that pin existing behavior byte-for-byte. Smoke tests (build + boot, see `/build-and-smoke`) remain the final gate — verify server boots, accepts connections, and changed features behave as expected. Totals move per wave (mirroring the platform skip counts above): (Backlog Cleanup wave: 1015 → 1057, T1 +8, T3 +34. RAII Lifecycle-Audit wave: 1057 → 1071, T4 +12 `ActCommAlias.*`, T6 +2 `DbLoaderFactory.*`; T3/T5 added no new tests. Entity-seed wave: 1071 → 1270. Persist-split wave: 1270 → 1274, T4 adds `PersistLayerAcyclicity`. World-seed wave: 1274 → 1281, T5 +1 `WorldLayerAcyclicity`, T5b +6 targeted coverage tests (weather-MSDP broadcast behavior + target_data pool-hook semantics). Placement-seam wave: 1281 → 1315, T6 +34 `placement_tests.cpp` (char_to_room/detach_char_from_room occupant-chain + light + zone-power coverage; attach_equipment/detach_equipment slot/encumbrance/affect/light coverage; the marquee `TooHeavyVerdictUsesPreAffectStrengthNotPostAffectRegression` regression test) — 1315/1315 both hosts, ASan 1315 clean. Combat-seed wave: 1315 → 1316, T1 +1 `CombatLayerAcyclicity`; 1316 → 1343, T3b +27 (8 `SkillTimerTest.*` + 19 `WildFightingHandler.*` coverage-rider tests, closing the skill_timer.cpp/wild_fighting_handler.cpp coverage-gap flags from T1) — 1343/1343 both hosts, ASan 1343 clean. **Blocker-buster wave (reconciled chain, see `.superpowers/sdd/task-5-report.md` for the full per-report citation table): 1343 → 1353, T1 +11 new `output_seam` seam tests − 1 removed post-review (the nullptr-default txt-pool test, retired once the default became a tripwire abort per the no-death-test rule) = net +10 (commits `f74cde7`..`136f7cf`) → 1363, T2 +8 initial `CombatHooksDispatch.*` discriminator tests (`a030215`, landing 1361) + 2 more for the review-caught `do_mental` 25th cell (`41ef52a`) = net +10 (commits `136f7cf`..`41ef52a`) → 1365, T3 +2 (`PoisonRemovalScriptTest.*` — the characterization-first positive/negative-control pair, commit `ddf8164` before the inversion, unchanged after it) (commits `41ef52a`..`b0445bf`) → 1365, T4 +0 (pure moves, no new tests; commit `565286f`) → 1365, T4b +0 (pure moves, no new tests; commit `34ebf3c`)** — 1365/1365 both hosts, ASan 1365 clean at every task's gate. Note: the wave brief's own running arithmetic guessed T2's delta as "+9"; the reconciled figure from the task-2 report's gate section is **+10** (8 initial + 2 `do_mental` fix) — see the task-5 report for the correction. **Combat-pilot wave (reconciled chain, see `.superpowers/sdd/progress.md`'s "WAVE: combat-pilot" block and `.superpowers/sdd/pilot-task-{1,2,3,4a,4b,5}-report.md` for the per-task gate reports): 1365 → 1368, CP T1 +3 `RngSeed.*` (commits `418ec5b`..`3c13432`) → 1376, CP T2 +8 (3 `CombatHooksSpecial.*` + 5 big_brother hook tests, commits `3c13432`..`2f5af34`) → 1376, CP T3 +0 (clerics.cpp conversions — existing T2 discriminators already covered every converted shape, commit `7a30a52`) → 1376, CP T4a +0 (byte-verbatim moves/storage-moves only, no new tests, commit `af0cc72`) → 1392, CP T4b +16 (`extract_char`/`gain_exp` family/app-other trio hook tests, commit `134d803`) → 1394, CP T5 +2 (`persist_hooks_tests.cpp`'s `ExploitCaptureHook.*` — the one real discriminator-audit gap, consumer-free until this task, commit `649b473`)** — 1394/1394 both hosts (macOS native, `rots64`), ASan 1394 clean at every task's gate. Skips carried forward unchanged from the blocker-buster wave: **75** (macOS) / **77** (`rots64`) — no combat-pilot task touched a POSIX/32-bit-fixture-gated test. **The i386 container's count/skips are now measured for this wave's finalization battery** (`log/i386-battery/`, `step1-20260720T194119Z.log`/`step2-20260720T195722Z.log`): **1394 total / 6 skips** via the ctest flow (`100% tests passed, 0 tests failed out of 1394`, with the same 6 tests — `AccountManagement.FormatsOutOfRangeSummaryTimestampsAsInvalid`/`LeavesStoredObjectPathUnchangedWhenCanonicalObjectWriteFails`/`MigrationFailsClosedWhenLegacyFileRetirementFails`/`MigrationRestoresRetiredFilesWhenExploitRetirementFails`, `DbLoader.FailsClosedWhenAccountNativeObjectOrExploitJsonCannotBeRead`, and `OlogHaiHelpers.HeavyFightingAndRidingAdjustOverrunDamage` — listed under "did not run") — matching the wave's own macOS/`rots64` total of 1394. This reconciles cleanly against the flat-Makefile monolithic runner's own summary (1365 passed + 23 skipped = 1388 of 1394 gtest-visible cases; the remaining 6 are the CMake ctest-only acyclicity linkcheck tests, absent from the gtest binary): subtracting its 17 `PerRace/ConvertEquivalence.*` skips (monolithic-only by design — `rots_convert` is a CMake-only target per its own CMakeLists.txt comment, so it's simply absent from the flat build, whereas the CMake ctest flow builds it and those 17 pass there) leaves the identical 6-test remainder both ways, per the blocker-buster wave's established reconciliation method. **Combat-trio wave (reconciled chain, see `.superpowers/sdd/progress.md`'s "WAVE: combat-trio" block and `.superpowers/sdd/trio-task-{0,1,2,3,4}-report.md` for the per-task gate reports): 1394 → 1396, CT T1 +2 `dismount` discriminator tests (`IssueCommandReachesTheRealDoDismountWhenRegistered`/`IssueCommandDefaultsToANoOpWhenDismountIsUnregistered`, commit `38dca86`, part of the four-commit Task 1 span `1663f59`..`1e36bde`) → 1398, CT T2 +2 `move` discriminator tests (`IssueCommandReachesTheRealDoMoveWhenRegistered`/`IssueCommandDefaultsToANoOpWhenMoveIsUnregistered` — a genuine discriminator-audit gap, `move` became a real `issue_command()` consumer for the first time via `olog_hai.cpp`'s `do_overrun` conversion, commit `8338ebd`) → 1398, CT T3 +0 (profs rider work — `get_guardian_type` relocation, `buf` retirement, `add_exploit_record` conversions, all pure relocation/conversion with an audited-zero discriminator gap; commits `558372b`..`9237f97`) → 1398, CT T4 +0 (pure membership move, `019b4c8`)** — 1398/1398 both hosts (macOS native, `rots64`), ASan 1398/1398 clean (CT T1's own gate ran ASan directly; CT T2's report incorrectly claimed additive-only test changes had no ASan precedent in this wave and skipped the gate — the review closed the finding by independently running `macos-arm64-asan`, 1398/1398, zero diagnostics, before approving; see `.superpowers/sdd/trio-task-2-review.md` Finding 1). Skips carried forward unchanged from the combat-pilot wave: **75** (macOS) / **77** (`rots64`) — no combat-trio task touched a POSIX/32-bit-fixture-gated test. **The i386 container's count/skips are now measured for this wave's finalization battery** (`log/i386-battery/`, `step1-20260720T232422Z.log`/`step2-20260720T234035Z.log`): **1398 total / 6 skips** via the ctest flow (`100% tests passed, 0 tests failed out of 1398`, same 6 did-not-run tests as every prior wave's measurement), reconciling against the monolithic runner (1369 passed + 23 skipped = 1392 of 1398 gtest-visible cases; the remaining 6 are the CMake ctest-only acyclicity linkchecks absent from the flat binary; 23 − 17 monolithic-only `PerRace/ConvertEquivalence.*` skips leaves the identical 6-test remainder both ways), boot golden matches (`step3-20260721T000055Z.log`) — the blocker-buster reconciliation method, third consecutive wave. **L4-seed wave (reconciled chain, see `.superpowers/sdd/progress.md`'s "WAVE: l4-seed" block and `.superpowers/sdd/l4-task-{0,1,2,3}-report.md` for the per-task gate reports): 1398 → 1411, T1 +13 (`script_hooks_tests.cpp` — 3 new; `world_hooks_tests.cpp` +8 for the four new zone hooks; `output_seam_forwarders_tests.cpp` +2 for `put_to_txt_block_pool`; the re-homed `extract_char` discriminator trio moved into `entity_lifecycle_tests.cpp`, not duplicated, so it adds 0 to this delta) → 1413, T2 +2 (`IssueCommandReachesTheRealDoSayWhenRegistered`/`IssueCommandDefaultsToANoOpWhenSayIsUnregistered` — a genuine discriminator-audit gap, the `say` cell had been registered since the combat-seed wave but had zero real callers until this task's 15 new call sites) → 1415, T3 +2 (`PathfindLayerAcyclicity`/`ScriptLayerAcyclicity` themselves)** — 1415/1415 both hosts (macOS native, `rots64`), ASan 1415 clean at every task's gate, `ConvertEquivalence` 17/17 throughout. T0 was read-only (no test delta). Skips carried forward unchanged from the combat-trio wave: **75** (macOS) / **77** (`rots64`) — no l4-seed task touched a POSIX/32-bit-fixture-gated test. **The i386 container's count/skips are now measured for this wave's finalization battery** (`log/i386-battery/`, `step1-20260721T044424Z.log`/`step2-20260721T050109Z.log`): **1415 total / 6 skips** via the ctest flow (`100% tests passed, 0 tests failed out of 1415`, same 6 did-not-run tests as every prior wave's measurement), reconciling against the monolithic runner (1384 passed + 23 skipped = 1407 of 1415 gtest-visible cases; the remaining 8 are the CMake ctest-only acyclicity linkchecks absent from the flat binary, up from 6 in prior waves now that `PathfindLayerAcyclicity`/`ScriptLayerAcyclicity` exist alongside the original six; 23 − 17 monolithic-only `PerRace/ConvertEquivalence.*` skips leaves the identical 6-test remainder both ways), boot golden matches (`step3-20260721T052142Z.log`) — the blocker-buster reconciliation method, fourth consecutive wave. **Behavior wave (reconciled chain, see `.superpowers/sdd/progress.md`'s "WAVE: behavior" block and `.superpowers/sdd/bw-task-{0,1,2,3}-report.md` for the per-task gate reports): 1415 → 1434, BW T1 +19 (6 `combat_hooks_tests.cpp` for `one_mobile_activity`/`Crash_idlesave`/`Crash_extract_objs`, 2 `entity_lifecycle_tests.cpp` for `char_from_room`, 4 `big_brother_hooks_tests.cpp`, 1 `script_hooks_tests.cpp` for `virt_program_number`, 6 `output_seam_forwarders_tests.cpp` — commits `6d1395c`..`ae37553`) → 1446, BW T2 +12 (six genuine discriminator-audit gaps for long-registered-but-never-caller-tested `combat_command` cells — `assist`/`rescue`/`wear`/`sleep`/`rest`/`sit`, each a registered/unregistered pair, the l4-seed `say`-cell gap recurring — commit `cab2e1c`, part of the three-commit Task 2 span `b9d1f12`..`bc91fed`) → 1446, BW T3 +0 (two pure membership-move commits plus a comment-only cleanup commit; `79dadb4`..`d361388`)** — 1446/1446 both hosts (macOS native, `rots64`), ASan 1446 clean at every task's gate, `ConvertEquivalence` 17/17 throughout, both boot goldens byte-identical at every commit (informational native-boot smoke after Task 3's membership commits: 338 zone resets, zero STUB/tripwire-hook warnings — matching the l4-seed wave's own baseline exactly, no drift). T0 was read-only (no test delta). Skips carried forward unchanged from the l4-seed wave: **75** (macOS) / **77** (`rots64`) — no behavior-wave task touched a POSIX/32-bit-fixture-gated test. **The i386 container's count/skips are now measured for this wave's finalization battery** (`log/i386-battery/`, `step1-20260721T090230Z.log`/`step2-20260721T091917Z.log`): **1446 total / 6 skips** via the ctest flow (`100% tests passed, 0 tests failed out of 1446`, same 6 did-not-run tests as every prior wave's measurement), reconciling against the monolithic runner (1415 passed + 23 skipped = 1438 of 1446 gtest-visible cases; the remaining 8 are the CMake ctest-only acyclicity linkchecks absent from the flat binary; 23 − 17 monolithic-only `PerRace/ConvertEquivalence.*` skips leaves the identical 6-test remainder both ways), boot golden matches (`step3-20260721T093947Z.log`) — the standing reconciliation method, fifth consecutive wave.)
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
  deleted alongside it. The **live** OB/PB/DB trio (`get_real_OB`, `get_real_parry`,
  `get_real_dodge`, all taking a single `char_data*`, global namespace, declared in
  `src/utils.h`) — do not confuse these with the deleted `utils::`-namespaced trio above that took
  `weather_data`/`room_data` arguments — **splits across library tiers, and has now REUNITED
  entirely in library code (final state, blocker-buster Task 4):** `get_real_dodge` moved into
  **`src/char_utils_combat.cpp`** (`rots_entity`, L2 lib) in the placement-seam wave (Task 5)
  because it's entity-pure combat-stat arithmetic. `get_real_OB` and `get_real_parry` stayed in
  `src/utility.cpp` (app-compiled) at that time because both call into
  `player_spec::weapon_master_handler` — an app-tier spec-handler edge `get_real_dodge` doesn't
  share — but the combat-seed wave later made `weapon_master_handler.cpp` a `rots_combat` (L3)
  member, turning that dependency into an intra-lib peer reference; the blocker-buster wave then
  moved both functions verbatim into **`src/visibility.cpp`** (`rots_combat`, L3 lib), retiring
  their STAY-APP comments. All three trio members are now library code — `get_real_dodge` in L2,
  `get_real_OB`/`get_real_parry` in L3 — none app-compiled any longer; each carries a one-line
  comment at its definition citing this history. `char_utils_combat.cpp` itself is still live: it
  also defines `get_engaged_characters`/`is_victim_player`/`get_controlling_player`/
  `on_attacked_character`, used by `fight.cpp`/`clerics.cpp`/`ranger.cpp`.
- The deleted implementation differed materially from the live one (e.g., its damage formula
  added the strength term *outside* the random factor, where the live formula folds it *inside*;
  there is no live "accurate hit" system — the guaranteed-hit mechanism is `frenzy`). When
  documenting or modifying combat, follow the `fight.cpp` / `utility.cpp` versions.
- Heuristic: before relying on a combat helper, grep for its callers (`grep -rn 'funcname(' src/`).
  A helper with no caller outside its own file (or only called by other dead code) is dead —
  that's how `combat_manager` and the OB/PB/DB trio above were identified before deletion.
