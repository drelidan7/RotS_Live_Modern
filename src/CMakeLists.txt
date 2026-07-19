# RotS — CMake build (alternate to src/Makefile).
#
#   Out-of-source build:  cmake -S src -B build && cmake --build build
#   Produces:             build/ageland   (the Makefile build produces ../bin/ageland)
#
# Intermediate objects (.o) already live entirely under the build tree
# (build/CMakeFiles/ageland.dir/), never beside the sources — this is CMake's
# out-of-source model, the equivalent of the Makefile's $(OBJDIR) = ../intermediate.
#
# This MUST stay flag-compatible with src/Makefile so both build systems produce an
# equivalent binary — see the "Flag parity" block below. The toolchain (g++/make/
# cmake) lives in the i386 container; see docs/BUILD.md.

cmake_minimum_required(VERSION 3.18...3.28)
project(RotS LANGUAGES CXX)

# --- Build-configuration axes (set by CMakePresets.json / CI; defaults preserve
# --- the historical 32-bit container build) -----------------------------------
# ON: build the historical 32-bit binary (-m32 compile+link). The i386 container
# and the root Makefile rely on this default. Presets for 64-bit platforms turn
# it OFF (those builds are expected to fail until Phase 2 of the modernization
# plan lands the 64-bit port).
option(ROTS_LEGACY_32BIT "Build the legacy 32-bit (-m32) binary" ON)
# OFF skips GoogleTest discovery and the ageland_tests target entirely — for
# platforms where GTest isn't installed yet (e.g. the windows-msvc preset).
option(ROTS_BUILD_TESTS "Build the ageland_tests GoogleTest binary" ON)
# ON (set by the macos-arm64-asan preset only, Backlog T4): FetchContent-provision
# GoogleTest from source instead of find_package()'s prebuilt brew package, so
# gtest itself compiles under this configure's active CMAKE_CXX_FLAGS —
# including the asan preset's -fsanitize=address,undefined — and comes out
# ASan/UBSan-instrumented. See the GoogleTest provisioning block below for why
# that matters (an uninstrumented static lib linked into an instrumented test
# binary is exactly the "mixed instrumentation" scenario that false-positives
# on ASan's container-overflow check). Off by default: every other preset
# already has a fast prebuilt system GTest via find_package(), and rebuilding
# GTest from source there would only add configure/build time for no benefit.
option(ROTS_FETCH_GTEST "FetchContent-provision GoogleTest from source instead of find_package()" OFF)

# Default to Debug: matches the Makefile, which always builds with -g and no -O.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# --- Sources -----------------------------------------------------------------
# Explicit list (CMake recommends against file(GLOB): it cannot tell when files are
# added/removed, so the build silently goes stale). Keep in sync with OBJFILES in
# src/Makefile.
#
# combat_manager.cpp was proven-dead code and was DELETED in Phase 4 Wave 1 (the
# live melee path is fight.cpp::hit(), see AGENTS.md's dead-code section).
# char_utils.cpp / char_utils_combat.cpp are NOT listed here (entity-completion
# Task 3): both moved into ROTS_ENTITY_SOURCES below -- see that set's comment
# for the membership rationale. char_utils_combat.cpp still defines live
# functions used by fight.cpp / clerics.cpp / ranger.cpp (its dead OB/PB/DB
# helpers were deleted in Phase 4 Wave 1); those callers reach it through
# RotS::entity now instead of a same-target same-executable compile.
#
# account_management.cpp / character_json.cpp / exploits_json.cpp / json_utils.cpp /
# objects_json.cpp arrive with the account-management merge (JSON persistence +
# accounts); the other account_management_*.cpp files are #included into
# account_management.cpp, not compiled separately.
#
# rots_crypt.cpp is a vendored, portable SHA-512-crypt ($6$) — account_management*.cpp
# call rots_crypt() instead of libc crypt() so credential hashes are byte-identical
# on every platform, not just glibc.
# Foundation layer (spec §3, library architecture): 7 verified-clean-leaf TUs
# with zero upward symbol references (confirmed via `nm -u` on the built
# archive — see the rots_platform_linkcheck load-bearing test below).
# Extracted into the rots_platform STATIC library.
# NOTE: signals.cpp is deliberately NOT here — it references descriptor_list /
# calls hupsig()/unrestrict_game(), i.e. it depends UP on the game, so it stays
# in ROTS_SERVER_SOURCES (app layer).
# NOTE: safe_template.cpp REJOINED here via the logging seam (spec §13
# payoff): it was excluded pending a platform-level logging seam (see prior
# history in git blame) because it called vmudlog(), and vmudlog() used to be
# defined in utility.cpp (an L2/app-layer unit) — a real upward edge, not a
# clean leaf. vmudlog() now resolves inside rots_platform itself
# (rots_log.cpp), so that edge is gone and the archive is self-contained
# again — confirmed via `nm -uC` on the built archive, see the
# rots_platform_linkcheck load-bearing test below. safe_template.cpp now
# reaches "rots/platform/log.h" as its only pathed rots/ header (its null-arg
# guard is inlined), so it
# is a genuinely clean leaf — no core/include reach, no qualifier needed.
# rots_util.cpp JOINS HERE (entity-seed Task 4, platform-helper relocations):
# create_function()/free_function() (the CREATE/RELEASE macro backers),
# str_dup()/str_cmp()/str_cmp_nullable(), rots_remove()/rots_rename_replace(),
# and the number()/number(int,int) RNG wrapper family (plus its TESTING-only
# rots_test_random_hook seam) all relocated verbatim from utility.cpp — each
# was already platform-pure (no comm/db/handler header, no game type) and
# only needed two trivial utils.h macro expansions inlined (LOWER, the CREATE
# expansion inside str_dup) to drop the utils.h include, the same "L0 must not
# include utils.h" deviation rots_log.cpp already uses for nz(). log()/
# mudlog() moved the same wave, but into the existing rots_log.cpp (see that
# file) since they're pure forwarders onto its write_stderr()/write().
set(ROTS_PLATFORM_SOURCES
    rots_net.cpp rots_crypt.cpp rots_rng.cpp clock.cpp
    crashsave_schedule.cpp json_utils.cpp player_file_finalize.cpp
    rots_log.cpp safe_template.cpp rots_util.cpp
)

# L1: core data-model library (spec §3). config.cpp is pure data (config
# defaults) with no upward symbol references — verified via `nm -uC` on the
# built archive, see the rots_core_linkcheck load-bearing test below.
#
# consts.cpp JOINS HERE (entity-seed Task 2; the contingency noted in prior
# history below is now resolved). get_guardian_type was consts.cpp's one
# traceable upward edge and was relocated to utility.cpp (an L2/app-layer
# unit) ahead of the original extraction attempt, but that rots_core_linkcheck
# build then surfaced a second, structural one — the `skills[MAX_SKILLS]`
# table (consts.cpp:382) used to embed ~69 function pointers directly to
# spell_*() implementations defined in mystic.cpp/spell_pa.cpp (both
# L2/app-layer). Entity-seed Task 1 cut that edge at the root: skills[]'s
# function-pointer column is now populated at boot by
# assign_spell_pointers() (spell_pa.cpp), not embedded in consts.cpp's static
# initializer, so consts.cpp itself compiles down to pure data (all-null
# pointers as far as this archive is concerned — assign_spell_pointers()
# never runs inside rots_core_linkcheck or rots_convert, both of which never
# call it). rots_core_linkcheck (below) is the acceptance proof: the built
# archive resolves only libc/libstdc++ symbols (plus rots_platform, see
# output_seam.cpp below). See docs/BUILD.md's "Library layering" section and
# the spec's §3 caveats for the recorded evidence.
#
# output_seam.cpp JOINS HERE (entity-seed Task 3, spec §13 pattern): it
# defines the five global send_to_char/act/track_specialized_mage output-path
# symbols as forwarders through a null-defaulted rots::output::Sinks
# aggregate that comm.cpp registers at boot (register_game_output_sinks()).
# Unlike config.cpp/consts.cpp, it has one legitimate downward edge — it logs
# a tripwire through rots::log::write_stderr (rots_platform) when no sink is
# registered yet, matching rots_convert's deleted convert_stubs.cpp stub
# semantics for these five symbols — so rots_core now PUBLIC-links
# RotS::platform (a legal L1→L0 edge; see the target_link_libraries call
# below and rots_core_linkcheck's matching update). It never dereferences a
# game type: char_data*/obj_data* pass through opaquely.
set(ROTS_CORE_SOURCES
    config.cpp consts.cpp output_seam.cpp
)

# L2: entity layer (spec §3; entity-seed Task 6 payoff). entity_lifecycle.cpp/
# object_utils.cpp/environment_utils.cpp are the three seed TUs Tasks 1-5
# scrubbed of every upward edge (consts data relocated to rots_core, platform
# helpers relocated to rots_platform, output/specialization/teardown/attack-
# speed edges inverted through null-defaulted hooks, pulse/top_idnum storage
# relocated here) -- verified via `nm -uC` on the built archive, see the
# rots_entity_linkcheck load-bearing test below. No headers of its own live
# under an include/ dir yet -- they stay flat in src/ (the same way the
# platform/core headers did before those layers' own carve-outs); a later
# wave can relocate them if/when rots_entity grows a real public surface.
#
# char_utils.cpp / char_utils_combat.cpp JOIN HERE (entity-completion Task 3,
# spec §10 step 4, membership not relocation): entity-completion Tasks 1-2
# already cut both TUs' last upward edges (fname()/other_side()/
# get_hit_text() relocated to char_utils.cpp/consts.cpp respectively;
# player_spec::wild_fighting_handler's ctor/get_attack_speed_multiplier()
# inverted through entity_hooks.h's wild-attack-speed-multiplier hook), so
# both are now nm-clean leaves the same way entity_lifecycle.cpp/
# object_utils.cpp/environment_utils.cpp are -- verified via `nm -uC` on the
# built archive, see the rots_entity_linkcheck load-bearing test below.
# char_utils.cpp was already a direct rots_convert source (it wasn't nm-clean
# for the library until now); it stays a rots_convert dependency, but arrives
# transitively through RotS::entity instead of a second same-executable
# compile -- see the rots_convert target's own comment below for why that
# executable's source list shrinks to convert_main.cpp alone this task.
set(ROTS_ENTITY_SOURCES
    entity_lifecycle.cpp object_utils.cpp environment_utils.cpp
    char_utils.cpp char_utils_combat.cpp
    placement.cpp containment.cpp equipment.cpp
)

# L3: persist layer (spec §3/§10 step 4; persist-split PS Task 4 payoff).
# db_players.cpp's last two upward edges (world_room_vnum()/
# add_exploit_record(), both defined in db_world.cpp/db_boot.cpp -- app
# layer) were cut this task via persist_hooks.h's pre-boot-registered hooks
# (mirroring entity_hooks.h's spec Sec13 pattern); its remaining nm-surfaced
# upward edges (recalc_skills(); utils::set_tactics()/set_shooting()/
# set_casting(); file_to_string()/file_to_string_alloc();
# find_player_in_table()/find_name()/unaccent()) were relocated verbatim
# into entity_lifecycle.cpp/db_players.cpp itself -- see each function's own
# "relocated" comment at its new home for the leaf-cleanliness argument.
# color_convert.cpp JOINS here (membership, not relocation): it already has
# zero comm/game dependency (carved for exactly this purpose by PS Task 1)
# and exists to serve persistence conversion (db_players.cpp's
# load_char()/load_char_from_text(), character_json.cpp's truecolor codec);
# color.cpp's remaining app-side callers (do_color et al.) call down into it
# through the unchanged color.h declarations. save_benchmark.cpp joins too
# (nm-clean: every undefined symbol resolves in-set or in rots_entity);
# savebench.cpp does NOT (page_string(), modify.cpp/app layer) and stays in
# ROTS_SERVER_SOURCES below -- verified via `nm -uC` on the built archive,
# see the rots_persist_linkcheck load-bearing test below.
set(ROTS_PERSIST_SOURCES
    account_cache.cpp account_management.cpp boards_json.cpp character_json.cpp
    color_convert.cpp convert_exploits.cpp convert_plrobjs.cpp db_players.cpp
    exploits_json.cpp mail_json.cpp objects_json.cpp obj_files.cpp
    pkill_json.cpp save_benchmark.cpp
)

# L3 peer: world layer (spec §3/§10 step 4; world-seed wave Task 5 payoff).
# db_world.cpp/zone_load.cpp/weather.cpp are the three TUs Tasks 1-4 scrubbed
# of every upward edge: relocations, storage moves (register_npc_char, dice,
# entity lists, weather_info, boot flags -- Task 1), scratch-global
# retirement (db_world composes locally -- Task 2), three hook inversions
# (shop-boot/mudlle-convert/weather-MSDP, Task 3), and the zone-file
# parse/load carve out of zone.cpp into zone_load.cpp (Task 4, which also
# defines the zone_table/top_of_zone_table storage) -- verified via `nm -uC`
# on the built archive, see the rots_world_linkcheck load-bearing test below.
#
# db_world.cpp's one legitimate remaining downward edge lands OUTSIDE the
# entity/core/platform stack: register_room_vnum_hook() (db_world.cpp,
# called from run_the_game() before boot_db()) registers world_room_vnum()
# as persist_hooks.h's pre-boot room-vnum hook via
# rots::persist::set_room_vnum_hook() -- a sanctioned L3-peer edge (world ->
# persist, both L3), NOT a layering violation, mirroring the
# entity_hooks.h/persist_hooks.h pattern the persist-split wave already
# established (db_players.cpp's own upward edges into world/boot). This is
# why rots_world PUBLIC-links RotS::persist below, in addition to
# RotS::entity/RotS::core/RotS::platform.
set(ROTS_WORLD_SOURCES
    db_world.cpp zone_load.cpp weather.cpp
)

# L3 peer: combat layer (spec §3/§10 step 4; combat-seed wave Task 1 payoff).
# skill_timer.cpp/battle_mage_handler.cpp/weapon_master_handler.cpp/
# wild_fighting_handler.cpp are the 4-of-16 combat-row TUs the combat-census
# (.superpowers/sdd/combat-census.md) verified SEED-CLEAN: every undefined
# symbol resolves downward into L0 rots_platform, L1 rots_core (incl. the
# existing output_seam), L2 rots_entity, or within the subset itself
# (wild_fighting_handler.cpp -> weapon_master_handler.cpp is the one
# intra-subset peer edge, and both TUs are included) -- verified via `nm -uC`
# on the built archive, see the rots_combat_linkcheck load-bearing test below.
# No handler.cpp/utility.cpp (still app-compiled), no command/session/
# app-output-beyond-seam, and no world/persist edges -- unlike rots_world,
# this layer does NOT need a peer-library PUBLIC link. The remaining 11
# combat-row TUs (profs.cpp, spec_ass.cpp, olog_hai.cpp, clerics.cpp,
# mage.cpp, mystic.cpp, mobact.cpp, spell_pa.cpp, limits.cpp, ranger.cpp,
# spec_pro.cpp, fight.cpp) are DEFERRED per the census's per-TU blocker
# analysis and stay in ROTS_SERVER_SOURCES below.
#
# wild_fighting_handler.cpp's register_attack_speed_multiplier_hook()/
# register_wild_attack_speed_multiplier_hook() (entity_hooks.h pattern) are
# still called app-side -- comm.cpp's run_the_game() and
# tests/gtest_main.cpp's test-fixture setup both call them before boot -- a
# legal app(ROTS_SERVER_SOURCES)->lib(RotS::combat) downward call, not an
# upward edge; the declarations remain visible via warrior_spec_handlers.h.
set(ROTS_COMBAT_SOURCES
    battle_mage_handler.cpp skill_timer.cpp weapon_master_handler.cpp
    wild_fighting_handler.cpp
)

# ageland's remaining app-layer sources (spec §3): combat, commands,
# comm/session handling, and every TU not yet carved into a rots_* library.
# db_world.cpp/zone_load.cpp/weather.cpp LEFT this list for
# ROTS_WORLD_SOURCES above (world-seed wave Task 5).
# battle_mage_handler.cpp/skill_timer.cpp/weapon_master_handler.cpp/
# wild_fighting_handler.cpp LEFT this list for ROTS_COMBAT_SOURCES above
# (combat-seed wave Task 1).
set(ROTS_SERVER_SOURCES
    act_comm.cpp act_info.cpp act_move.cpp act_obj1.cpp
    act_obj2.cpp act_offe.cpp act_othe.cpp act_soci.cpp
    act_wiz.cpp ban.cpp big_brother.cpp
    boards.cpp clerics.cpp
    color.cpp comm.cpp
    db_boot.cpp delayed_command_interpreter.cpp
    fight.cpp graph.cpp handler.cpp interpre.cpp
    limits.cpp mage.cpp mail.cpp mob_csv_extract.cpp
    mobact.cpp modify.cpp mudlle.cpp mudlle2.cpp
    mystic.cpp obj2html.cpp objsave.cpp
    olog_hai.cpp pkill.cpp profs.cpp protocol.cpp
    ranger.cpp savebench.cpp script.cpp shapemdl.cpp shapemob.cpp
    shapeobj.cpp shaperom.cpp shapescript.cpp shapezon.cpp
    shop.cpp signals.cpp spec_ass.cpp
    spec_pro.cpp spell_pa.cpp utility.cpp wait_functions.cpp
    zone.cpp
)

add_executable(ageland ${ROTS_SERVER_SOURCES})

# Pathed data-model headers (spec §9a). ONLY these two leaf dirs go on the
# include path — never src/ itself (src/limits.h would shadow <limits.h>).
# They contain nothing but rots/, so they cannot shadow any system header.
# core/include flows to ageland transitively via RotS::core's PUBLIC
# target_include_directories (see the rots_core definition below);
# persist/include now flows the same way via RotS::persist's PUBLIC
# target_include_directories (see the rots_persist definition below,
# persist-split PS Task 4) -- this target no longer needs a direct entry of
# its own for either.

set_target_properties(ageland PROPERTIES CXX_EXTENSIONS OFF)

# --- Flag parity with src/Makefile (behavior-affecting — keep in sync) --------
# The historical flags, now split along two axes:
#   * ROTS_LEGACY_32BIT gates -m32 (compile AND link).
#   * Compiler guards keep GCC/Clang spellings away from MSVC, which fails hard
#     on unknown flags. MSVC gets /J — the -funsigned-char equivalent — because
#     unsigned char is a load-bearing behavior assumption (spec Phase 5 audits
#     it away; until then every compiler must pin it). MSVC also gets
#     /Zc:strictStrings- (Phase 4 Wave 1 Task 1, C++20 bump): MSVC's default
#     string-literal-to-char* conversion strictness is tied to /std level, and
#     /std:c++20 newly enables it, turning this codebase's pervasive
#     (pre-C++11-era) `char *p = "literal";` idiom -- hundreds of sites across
#     the old C-heritage sources -- into hard C2440/C2664 errors that never
#     fired under /std:c++17. /Zc:strictStrings- restores the same permissive
#     behavior the c++17 build already relied on: purely a compiler-conformance
#     switch (the flag MSVC itself provides for exactly this legacy-code
#     scenario), not a rewrite -- the pointed-to data and every call site are
#     unchanged, so it cannot affect runtime behavior or goldens.
# GNU-family flags: -funsigned-char / -fstrict-aliasing / -Wall -Wextra -Werror / -g
# are identical on GCC, Clang, and AppleClang. Phase 5 Task 8 flips the historical
# -w (suppress everything) to -Wall -Wextra -Werror once the GNU-family warning
# census (Tasks 1-7) reached zero on every locally-built configuration; see
# docs/BUILD.md's "Warnings policy" section for the pinned-toolchain rationale and
# the i386 (-m32, g++14 container) exposure, which is deferred to Task 10.
# MSVC: Phase 5 Task 9 flips the historical /w (suppress everything) to /W4 the
# same way Task 8 did for GNU-family -- census first (no /WX), then triage: real
# bugs fixed in source (uninitialized locals C4700/C4701/C4703, format-string
# mismatches C4477, bool arithmetic C4804/C4805, function-pointer tests C4550,
# constant truncation C4309, int-to-pointer C4312, and the specialize_name
# extern-type-mismatch LNK2001), and the remaining classes suppressed per-class
# below, each with a rationale. Applied to both the ageland and ageland_tests
# targets (the latter compiles every ROTS_SERVER_SOURCES file too, so it hits
# the same warnings).
#
# Per-class /wd suppression ledger (Phase 5 Task 9 census, run 29208716185):
#   /wd4244 /wd4267  int-conversion narrowing (893 + 174 unique sites at the
#     Backlog Cleanup T5 recount): pervasive benign legacy narrowing (int ->
#     sh_int/byte/char assignments across the whole C-heritage codebase;
#     size_t -> int lengths). Revisited rigorously in Backlog Cleanup Task 5
#     (census run 29222339494, full site list, 84-site stratified read, every
#     suspected-real site read in full): two latent-real classes fixed in
#     source (target_data::ch_num widened to int; random_exit's romfl local),
#     the idnum-into-sh_int sites in the frozen exploit/crime record formats
#     escalated as format-migration work, everything else domain-provably
#     benign and diffuse across ~59 files -- so the global suppression stays.
#     See docs/BUILD.md "Warnings policy" for the full evidence and
#     disposition. A future typed-fields effort remains the real fix.
#   /wd4456 /wd4458 /wd4459  declaration shadowing (278 sites): parity with the
#     GNU-family bar, where -Wshadow is deliberately NOT part of the Task 8
#     -Wall -Wextra -Werror set. Same hygiene class, same deferral.
#   /wd4702  unreachable code (228 sites): legacy return-then-break switch arms
#     and defensive tails; GNU has no equivalent enabled, and C4702 is
#     notoriously optimizer/version-dependent on MSVC.
#   /wd4127  constant conditional (73 sites, mostly tests): do/while(0) macro
#     idiom and sizeof-based compile-time checks; intentional.
#   /wd4310  cast truncates constant (85 sites): telnet protocol byte constants
#     ((char)IAC == (char)0xFF etc. in protocol.cpp/utility.cpp). char is
#     pinned unsigned via /J, so the values are preserved -- but MSVC's
#     C4310/C4309 checks ignore /J (they assume signed char), making every one
#     of these a false positive here.
#   -D_FILE_OFFSET_BITS=64  32-bit glibc's legacy readdir()/stat() use a 32-bit
#     ino_t/off_t; against modern filesystems with 64-bit inode numbers (ext4,
#     overlay2) readdir() fails with EOVERFLOW. Switches libc to the 64-bit-safe
#     variants transparently — does not change any of the game's own on-disk
#     structs, so save-file format is unaffected.
set(ROTS_GNULIKE "$<CXX_COMPILER_ID:GNU,Clang,AppleClang>")
# True on x86/x86-64 targets (32-bit legacy ABI and any 64-bit x86 host); false on
# arm64 (Apple Silicon, etc.), where x86-only flags like -msse2/-mfpmath=sse don't
# exist and the compiler errors out rather than ignoring them. Computed once at
# configure time (CMAKE_SYSTEM_PROCESSOR doesn't vary per-build-config), then used
# as a plain 0/1 inside generator expressions below.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|i[3-6]86|x86)$")
    set(ROTS_X86 1)
else()
    set(ROTS_X86 0)
endif()
# Single source of truth for the deterministic-FP flag set (Phase 1 FP
# unification; see docs/BUILD.md "FP determinism"), shared by BOTH the
# ageland and ageland_tests targets so shipping and test binaries compute
# floating point identically:
#   -msse2 -mfpmath=sse  GNU-family x86 only (incl. i386): x87 80-bit extended
#     precision rounds intermediates differently from SSE's IEEE double path;
#     several tests pin exact FP thresholds authored under SSE semantics, and
#     the shipping i386 build historically used x87 -- this flips it to SSE.
#     Gated on ROTS_X86 too: gcc/clang reject these outright on arm64
#     ("unknown FP unit 'sse'").
#   -ffp-contract=off  ALL GNU-family targets (x86 AND arm64): without it,
#     -O2+ may fuse a*b+c into a single FMA instruction, which rounds once
#     instead of twice and diverges from -O0/other platforms that don't fuse.
#     Valid and needed on arm64 too (NOT x86-gated), where FMA is common.
#   /fp:precise  MSVC: disables the compiler's own contraction/reassociation
#     optimizations, the MSVC analogue of the two GNU flags above.
set(ROTS_FP_OPTIONS
    $<$<AND:${ROTS_GNULIKE},${ROTS_X86}>:-msse2 -mfpmath=sse>
    $<${ROTS_GNULIKE}:-ffp-contract=off>
    $<$<CXX_COMPILER_ID:MSVC>:/fp:precise>
)
# --- Shared build-flags interface --------------------------------------------
# The shipping/game flag set (ageland's exact flags), in one place. ageland and
# every rots_* library (from Task 2 on) inherit it via
#   target_link_libraries(<target> PRIVATE rots_build_flags)   # PUBLIC for libs
# so the -m32 ABI / FP-determinism / unsigned-char behavior contracts hold
# identically across the whole shipping link. INTERFACE (not PUBLIC/PRIVATE)
# because the library has no sources of its own — it only imposes requirements.
# NOTE: ageland_tests deliberately does NOT consume this — its flag set differs
# (TESTING, -idirafter include handling, no -g/-fstrict-aliasing/per-config-opt)
# and is left exactly as-is. This block must be a verbatim copy of ageland's
# current options/definitions/link-options so ageland is provably unchanged.
add_library(rots_build_flags INTERFACE)
target_compile_features(rots_build_flags INTERFACE cxx_std_20)
# ISO dialect, not GNU (matches the Makefile's -std=c++20). Applied to every
# consumer because CXX_EXTENSIONS is a target property, not an INTERFACE one; a
# consuming target must also set it (see the ageland/ageland_tests setters,
# left in place below).
target_compile_options(rots_build_flags INTERFACE
    $<$<BOOL:${ROTS_LEGACY_32BIT}>:-m32>
    $<${ROTS_GNULIKE}:-funsigned-char -fstrict-aliasing -Wall -Wextra -Werror -g>
    $<$<CXX_COMPILER_ID:MSVC>:/J /W4 /WX /wd4244 /wd4267 /wd4456 /wd4458 /wd4459 /wd4702 /wd4127 /wd4310 /Zc:strictStrings->
    ${ROTS_FP_OPTIONS}
    $<${ROTS_GNULIKE}:$<$<CONFIG:Debug>:-O0 -DDEBUG>>
    $<${ROTS_GNULIKE}:$<$<CONFIG:Release>:-O2 -DNDEBUG>>
)
target_compile_definitions(rots_build_flags INTERFACE
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:_FILE_OFFSET_BITS=64>
    $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS>
)
target_link_options(rots_build_flags INTERFACE
    $<$<BOOL:${ROTS_LEGACY_32BIT}>:-m32>
)
# --- L0: foundation library --------------------------------------------------
add_library(rots_platform STATIC ${ROTS_PLATFORM_SOURCES})
add_library(RotS::platform ALIAS rots_platform)
set_target_properties(rots_platform PROPERTIES CXX_EXTENSIONS OFF)
# PUBLIC: rots_platform needs the parity flags to BUILD, and every consumer must
# link with the same -m32/FP set — so the requirement propagates.
target_link_libraries(rots_platform PUBLIC rots_build_flags $<$<PLATFORM_ID:Windows>:ws2_32>)
# tests/product .cpp reach most of these headers via bare quoted includes
# ("rots_net.h"); the sources live in src/, so the compiler's own "directory
# of the including file" entry already resolves them for TUs in src/. No
# extra include dir needed for those while the headers stay in src/ (they
# relocate in a later plan).
# platform/include is the first pathed header root for this layer (spec §13,
# rots/platform/log.h): wired exactly like rots_core's core/include below.
# PUBLIC: every consumer of rots_platform (ageland via RotS::platform) needs
# this on its include path to reach "rots/platform/log.h".
target_include_directories(rots_platform PUBLIC platform/include)

# --- L1: core data-model library ----------------------------------------------
add_library(rots_core STATIC ${ROTS_CORE_SOURCES})
add_library(RotS::core ALIAS rots_core)
set_target_properties(rots_core PROPERTIES CXX_EXTENSIONS OFF)
# PUBLIC RotS::platform: output_seam.cpp's tripwire default reaches
# rots::log::write_stderr (rots/platform/log.h) -- a legal L1->L0 edge (see
# the ROTS_CORE_SOURCES comment above). PUBLIC so it also carries
# platform/include to any consumer that links only RotS::core.
target_link_libraries(rots_core PUBLIC rots_build_flags RotS::platform)
# The carved data-model headers (spec §5/§9a). PUBLIC: every consumer of the
# core data model gets the pathed include root by linking RotS::core.
target_include_directories(rots_core PUBLIC core/include)

# --- L2: entity layer ---------------------------------------------------------
add_library(rots_entity STATIC ${ROTS_ENTITY_SOURCES})
add_library(RotS::entity ALIAS rots_entity)
set_target_properties(rots_entity PROPERTIES CXX_EXTENSIONS OFF)
# PUBLIC RotS::core + RotS::platform: entity_lifecycle.cpp/object_utils.cpp/
# environment_utils.cpp's legitimate downward edges land in both layers (data
# model + platform helpers/logging) -- see the ROTS_ENTITY_SOURCES comment
# above. PUBLIC so both layers' include roots (core/include, platform/
# include) flow transitively to any consumer that links only RotS::entity.
target_link_libraries(rots_entity PUBLIC rots_build_flags RotS::core RotS::platform)
# entity_lifecycle.cpp reaches "rots/persist/file_formats.h". PRIVATE: rots_entity
# has no include/ dir of its own (its headers stay flat in src/, see the
# ROTS_ENTITY_SOURCES comment above) -- no consumer of RotS::entity needs this
# path transitively; ageland/rots_convert already carry persist/include on
# their own target_include_directories below.
target_include_directories(rots_entity PRIVATE persist/include)

# --- L3: persist layer ---------------------------------------------------------
add_library(rots_persist STATIC ${ROTS_PERSIST_SOURCES})
add_library(RotS::persist ALIAS rots_persist)
set_target_properties(rots_persist PROPERTIES CXX_EXTENSIONS OFF)
# PUBLIC RotS::entity + RotS::core + RotS::platform: db_players.cpp/
# character_json.cpp/etc.'s legitimate downward edges land in all three
# layers (entity helpers, data model, platform helpers/logging) -- see the
# ROTS_PERSIST_SOURCES comment above. PUBLIC so all three layers' include
# roots flow transitively to any consumer that links only RotS::persist.
target_link_libraries(rots_persist PUBLIC rots_build_flags RotS::entity RotS::core RotS::platform)
# db_players.cpp/character_json.cpp/objects_json.cpp/color_convert.cpp/etc.
# reach "rots/persist/file_formats.h". PUBLIC (unlike rots_entity's PRIVATE
# above): several ROTS_SERVER_SOURCES files that stay app-side (db_boot.cpp,
# db_world.cpp, act_info.cpp, act_wiz.cpp, interpre.cpp, objsave.cpp,
# savebench.cpp, color.cpp) also reach persist/include headers directly and
# now compile as part of the `ageland` target alongside RotS::persist, so
# this needs to flow transitively to any consumer that links RotS::persist
# -- see the ageland target_include_directories comment below for how this
# retires that target's own former direct persist/include entry.
target_include_directories(rots_persist PUBLIC persist/include)

# --- L3 peer: world layer -------------------------------------------------------
add_library(rots_world STATIC ${ROTS_WORLD_SOURCES})
add_library(RotS::world ALIAS rots_world)
set_target_properties(rots_world PROPERTIES CXX_EXTENSIONS OFF)
# PUBLIC RotS::persist + RotS::entity + RotS::core + RotS::platform:
# db_world.cpp/zone_load.cpp/weather.cpp's legitimate downward edges land in
# all four layers -- see the ROTS_WORLD_SOURCES comment above. RotS::persist
# specifically carries the one sanctioned L3-peer edge (db_world.cpp calls
# rots::persist::set_room_vnum_hook, an L3 world -> L3 persist edge, not a
# layering violation -- see that comment for the full rationale). PUBLIC so
# all four layers' include roots flow transitively to any consumer that
# links only RotS::world.
target_link_libraries(rots_world PUBLIC rots_build_flags RotS::persist RotS::entity RotS::core RotS::platform)
# No target_include_directories() of its own needed: persist/include,
# core/include, and platform/include all already flow transitively via
# RotS::persist's own PUBLIC target_include_directories (see that target
# above), which db_world.cpp's "rots/persist/file_formats.h" and every TU's
# "rots/core/*.h" reach the same way ageland already did before this task.

# --- L3 peer: combat layer -------------------------------------------------------
add_library(rots_combat STATIC ${ROTS_COMBAT_SOURCES})
add_library(RotS::combat ALIAS rots_combat)
set_target_properties(rots_combat PROPERTIES CXX_EXTENSIONS OFF)
# PUBLIC RotS::entity + RotS::core + RotS::platform: battle_mage_handler.cpp/
# skill_timer.cpp/weapon_master_handler.cpp/wild_fighting_handler.cpp's
# legitimate downward edges land in all three layers -- see the
# ROTS_COMBAT_SOURCES comment above. Unlike rots_world, no RotS::persist (or
# RotS::world) PUBLIC link: the census found zero L3-peer edges out of this
# 4-TU subset (the one intra-subset peer edge, wild_fighting_handler.cpp ->
# weapon_master_handler.cpp, resolves within rots_combat itself). PUBLIC so
# all three layers' include roots flow transitively to any consumer that
# links only RotS::combat.
target_link_libraries(rots_combat PUBLIC rots_build_flags RotS::entity RotS::core RotS::platform)
# weapon_master_handler.cpp reaches "rots/persist/file_formats.h" transitively
# through handler.h's RENT_CRASH macro include (a header-only reach, NOT a
# link-time dependency on rots_persist -- no rots_combat symbol resolves into
# rots_persist, per the census). PRIVATE, mirroring rots_entity's identical
# PRIVATE persist/include above: no consumer of RotS::combat needs this path
# transitively; ageland/rots_convert already carry persist/include on their
# own target_include_directories.
target_include_directories(rots_combat PRIVATE persist/include)

target_link_libraries(ageland PRIVATE
    RotS::platform
    RotS::core
    RotS::entity
    RotS::persist
    RotS::world
    RotS::combat
    rots_build_flags
    $<$<PLATFORM_ID:Windows>:ws2_32>
)
target_link_options(ageland PRIVATE
    # -rdynamic: GNU spelling; the clang driver translates it on macOS. MSVC: none.
    $<${ROTS_GNULIKE}:-rdynamic>
)

# --- rots_convert: the standalone character converter -------------------------
# (db.cpp-split Task 3, spec Sec4b; re-plumbed onto RotS::entity in
# entity-seed Task 6, then onto RotS::persist in persist-split PS Task 4;
# convert_stubs.cpp deleted outright in entity-completion Task 3) -- a SECOND
# executable, its own main(), that links RotS::platform + RotS::core +
# RotS::entity + RotS::persist (db_players.cpp: player-index/load/store/
# save, the JSON codecs, the account system, the legacy exploit/plrobj
# binary-to-JSON one-time converters, color_convert.cpp, obj_files.cpp,
# char_utils.cpp/char_utils_combat.cpp -- see the ROTS_PERSIST_SOURCES/
# ROTS_ENTITY_SOURCES comments above) plus exactly ONE TU it still
# direct-compiles: convert_main.cpp (its own main()) -- deliberately NO
# db_world.cpp/db_boot.cpp and NO combat/commands/app translation unit. This
# is the executable acid-test that the persistence boundary holds: if a
# future change re-welds db_players.cpp/entity_lifecycle.cpp/char_utils.cpp
# to the game (combat/world/commands/session), either the growing library's
# own linkcheck (rots_entity_linkcheck/rots_persist_linkcheck) fails first,
# or -- for an edge those linkchecks don't cover -- this target itself fails
# to link. It is therefore added to `all` (no EXCLUDE_FROM_ALL) so every CI
# job builds it -- see
# docs/superpowers/specs/2026-07-16-library-architecture-design.md Sec4b/
# Sec11 and docs/superpowers/plans/2026-07-17-db-split-and-rots-convert.md
# Task 3. objsave.cpp's G half (Crash_load/Crash_listrent and the rent/
# receptionist flow) plus boards.cpp/mail.cpp/pkill.cpp's runtime/bridge/
# gameplay halves are still deliberately OUT this wave -- their P (codec)
# halves already arrive via RotS::persist (obj_files.cpp/pkill_json.cpp/
# mail_json.cpp/boards_json.cpp), so those welds are resolved, not
# follow-on; the G halves stay app-side by design.
#
# The weld-ledger era is over. convert_stubs.cpp -- the file-scoped record of
# every symbol substituted for an excluded app/combat home, and why the
# converter's own call graph never reached it -- reached ZERO surviving stub
# bodies at the end of entity-completion Task 2 (every remaining section was
# already a DELETED-stub marker comment, not a live stand-in) and is deleted
# outright this task (git history preserves its full per-symbol reachability
# analysis for anyone tracing why a given inversion/relocation was safe). The
# persistence/combat boundary is no longer enforced by a hand-maintained
# ledger file at all -- it is enforced purely by the four libraries' own
# linkchecks (rots_platform_linkcheck / rots_core_linkcheck /
# rots_entity_linkcheck / rots_persist_linkcheck, each verifying its layer
# has no upward symbol reference beyond its declared lower layers) plus this
# executable's own link succeeding against exactly those four libraries and
# convert_main.cpp -- see each linkcheck's own comment further below.
#
# This target is CMake-only this wave (not added to the flat src/Makefile /
# src/tests/Makefile, which compile same-directory only against a single
# hand-maintained OBJFILES list per binary) -- see docs/BUILD.md.
add_executable(rots_convert
    convert_main.cpp
)
set_target_properties(rots_convert PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(rots_convert PRIVATE
    RotS::platform
    RotS::core
    RotS::entity
    RotS::persist
    rots_build_flags
    $<$<PLATFORM_ID:Windows>:ws2_32>
)

# --- Tests (ported from upstream/account-management) --------------------------
# GoogleTest suite, discovered via CTest so `ctest --test-dir build` (or the root
# `make test`) runs it. This is IN ADDITION to the older src/tests/Makefile entry
# point (see CLAUDE.md / AGENTS.md) — both build the same test sources against
# GoogleTest; use whichever fits the workflow.
#
# Gated on ROTS_BUILD_TESTS: OFF skips GTest discovery and the ageland_tests
# target entirely, for platforms where GTest isn't installed yet (e.g. the
# windows-msvc preset).
if(ROTS_BUILD_TESTS)
    include(CTest)

    option(ROTS_SUPPRESS_TEST_WARNINGS "Suppress compiler warnings when building the ageland_tests target" OFF)

    # Explicit list, matching the style above — no file(GLOB).
    set(ROTS_TEST_SOURCES
        tests/account_cache_tests.cpp
        tests/account_management_assets_header_compile.cpp
        tests/account_management_identity_header_compile.cpp
        tests/account_management_migration_header_compile.cpp
        tests/account_management_presentation_header_compile.cpp
        tests/account_management_storage_header_compile.cpp
        tests/account_management_tests.cpp
        tests/account_management_types_header_compile.cpp
        tests/act_comm_alias_tests.cpp
        tests/act_format_tests.cpp
        tests/act_info_format_tests.cpp
        tests/act_wiz_format_tests.cpp
        tests/act_wiz_tests.cpp
        tests/battle_mage_handler_tests.cpp
        tests/boards_json_tests.cpp
        tests/char_utils_tests.cpp
        tests/character_json_tests.cpp
        tests/characterization_combat_tests.cpp
        tests/characterization_json_tests.cpp
        tests/CharPlayerDataBuilder.cpp
        tests/clock_tests.cpp
        tests/color_tests.cpp
        tests/comm_act_tests.cpp
        tests/comm_output_tests.cpp
        tests/convert_exploits_tests.cpp
        tests/convert_plrobjs_tests.cpp
        tests/cpp20_format_smoke_tests.cpp
        tests/crashsave_schedule_tests.cpp
        tests/damage_tests.cpp
        tests/db_loader_tests.cpp
        tests/db_save_roundtrip_tests.cpp
        tests/entity_lifecycle_tests.cpp
        tests/exploits_json_tests.cpp
        tests/fight_proc_tests.cpp
        tests/fp_determinism_smoke_tests.cpp
        tests/gtest_main.cpp
        tests/interpre_account_menu_tests.cpp
        tests/json_perf_tests.cpp
        tests/json_utils_tests.cpp
        tests/llp64_probe_tests.cpp
        tests/mage_tests.cpp
        tests/mail_json_tests.cpp
        tests/obj_flag_data_tests.cpp
        tests/ObjFlagDataBuilder.cpp
        tests/object_utils_tests.cpp
        tests/objects_json_layout_tests.cpp
        tests/objects_json_tests.cpp
        tests/objsave_json_tests.cpp
        tests/olog_hai_tests.cpp
        tests/placement_tests.cpp
        tests/platform_log_tests.cpp
        tests/player_finalize_tests.cpp
        tests/pod_persistence_json_tests.cpp
        tests/prompt_format_tests.cpp
        tests/protocol_tests.cpp
        tests/rots_asprintf_tests.cpp
        tests/rots_convert_equivalence_tests.cpp
        tests/rots_crypt_tests.cpp
        tests/rots_net_tests.cpp
        tests/rots_rng_tests.cpp
        tests/safe_template_tests.cpp
        tests/string_view_utility_tests.cpp
        tests/save_benchmark_tests.cpp
        tests/scoped_allocation_counter.cpp
        tests/scoped_allocation_counter_tests.cpp
        tests/shape_format_tests.cpp
        tests/shapemob_tests.cpp
        tests/spec_pro_tests.cpp
        tests/spell_pa_tests.cpp
        tests/spell_registry_tests.cpp
        tests/startup_options_tests.cpp
        tests/test_random_utils.cpp
        tests/test_random_utils_tests.cpp
        tests/text_view_tests.cpp
        tests/utility_format_tests.cpp
        tests/weapon_master_handler_tests.cpp
        tests/zone_tests.cpp
    )

    # GoogleTest provisioning (Phase 3 Task 6, extended Backlog T4): Linux/macOS
    # CI installs GTest as a system package (apt/brew) ahead of configure, so
    # find_package() there is a fast no-op. windows-2022 CI has no such package
    # manager entry for GTest (no system-wide vcpkg baseline in the runner
    # image), so pull it via FetchContent there instead -- source-built
    # against the exact same MSVC/CRT the game target uses (avoiding any ABI
    # mismatch a pre-built binary package could introduce), and cached by the
    # CI workflow's actions/cache step keyed on this file's hash so a cache
    # hit skips re-downloading/rebuilding GoogleTest on every run.
    #
    # ROTS_FETCH_GTEST generalizes the same "source-build against this
    # configure's own flags" rationale to the macos-arm64-asan preset
    # (Backlog T4): brew's libgtest.a is a prebuilt, uninstrumented static
    # library. Linking it into an ASan-instrumented ageland_tests binary is a
    # mixed-instrumentation build, which is documented to false-positive on
    # ASan's container-overflow check (it fired inside gtest's own JSON
    # test-list printer during ctest's PRE_TEST discovery) -- see
    # https://github.com/google/sanitizers/wiki/AddressSanitizerContainerOverflow
    # and the now-removed detect_container_overflow=0 override this replaced
    # in .github/workflows/ci.yml's sanitize-macos job. FetchContent instead
    # compiles gtest itself under this configure's CMAKE_CXX_FLAGS (the
    # -fsanitize=address,undefined the asan preset sets), so it comes out
    # instrumented like every other translation unit and the mismatch --
    # hence the false positive -- goes away, letting container-overflow
    # checking stay on (its default) for this job.
    if(WIN32 OR ROTS_FETCH_GTEST)
        include(FetchContent)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
            URL_HASH SHA256=1f357c27ca988c3f7c6b4bf68a9395005ac6761f034046e9dde0896e3aba00e4
        )
        FetchContent_MakeAvailable(googletest)
    else()
        find_package(GTest REQUIRED)
    endif()
    include(GoogleTest)

    # ageland_tests deliberately does NOT link rots_core/rots_entity/
    # rots_persist/rots_world/rots_combat (TESTING parity, same as the
    # rots_platform pattern above): it keeps compiling config.cpp/consts.cpp/
    # output_seam.cpp, entity_lifecycle.cpp/object_utils.cpp/
    # environment_utils.cpp, db_players.cpp/character_json.cpp/etc., (as of
    # world-seed Task 5) db_world.cpp/zone_load.cpp/weather.cpp, and (as of
    # combat-seed Task 1) battle_mage_handler.cpp/skill_timer.cpp/
    # weapon_master_handler.cpp/wild_fighting_handler.cpp directly via
    # ROTS_CORE_SOURCES/ROTS_ENTITY_SOURCES/ROTS_PERSIST_SOURCES/
    # ROTS_WORLD_SOURCES/ROTS_COMBAT_SOURCES instead (all now live only in
    # those lists — see each set's own comment above).
    add_executable(ageland_tests ${ROTS_SERVER_SOURCES} ${ROTS_PLATFORM_SOURCES} ${ROTS_CORE_SOURCES} ${ROTS_ENTITY_SOURCES} ${ROTS_PERSIST_SOURCES} ${ROTS_WORLD_SOURCES} ${ROTS_COMBAT_SOURCES} ${ROTS_TEST_SOURCES})

    # Same C++ standard as ageland (see above) — required because these targets are
    # defined per-target here rather than via a project-wide CMAKE_CXX_STANDARD.
    target_compile_features(ageland_tests PRIVATE cxx_std_20)
    set_target_properties(ageland_tests PROPERTIES CXX_EXTENSIONS OFF)

    # tests/*.cpp reach headers one level up via bare quoted includes
    # ("act_info.h" etc.), so src/ needs to be on their quote search path; they
    # also reach shared test fixtures via bare quoted includes ("damage_test_
    # context.h" etc.), so src/tests/ needs to be too. Never target_include_
    # directories()'s default -I for src/: that would ALSO add it to the
    # angle-bracket <...> search path, and src/limits.h (a project header —
    # player rank constants — unrelated to the standard C <limits.h>) would
    # then shadow the real system <limits.h> ahead of the toolchain's own path.
    # That's invisible under glibc/libstdc++ (CHAR_BIT etc. come from compiler
    # builtins), but AppleClang's libc++ headers (<climits>, <string>,
    # <bitset>, ...) do a bare `#include <limits.h>` and break with "use of
    # undeclared identifier 'CHAR_BIT'" the moment src/ shadows it.
    #
    # src/tests/ is plain -iquote (no name collision there, so highest quote
    # priority is fine and simplest). src/ is -idirafter, NOT -iquote (Phase 4
    # Wave 1 Task 1, trixie/g++14 bump): GCC's #include_next -- used
    # internally by its own bundled <limits.h>/syslimits.h to chain into
    # glibc's real <limits.h> -- does NOT respect the quote/angle distinction;
    # per the GCC manual it just continues down the merged directory list from
    # wherever the current file was found, and -iquote dirs sit at the FRONT
    # of that merged list (highest priority), ahead of the standard system
    # directories glibc's real <limits.h> lives in. Under C++20, gtest's own
    # gtest-port.h (<condition_variable>) and this project's comm.cpp (<thread>)
    # both newly pull in <semaphore> (std::counting_semaphore; unreachable
    # pre-C++20, so this was invisible under the old c++17/c++1z build), whose
    # libstdc++ implementation needs _POSIX_SEM_VALUE_MAX from glibc's
    # <bits/posix1_lim.h>. With src/ on the FRONT of the merged list via
    # -iquote, GCC's #include_next chain for <limits.h> walked into
    # src/limits.h before ever reaching glibc's real one, so posix1_lim.h
    # never loaded and every test TU (they all pull in gtest.h) failed with
    # "'_POSIX_SEM_VALUE_MAX' was not declared". -idirafter instead places
    # src/ at the very END of the merged list -- after the standard system
    # directories -- so #include_next resolves glibc's real <limits.h> first
    # (found earlier, in its normal system-directory position) and only
    # falls through to src/limits.h for a bare quoted include that nothing
    # else on the list satisfies (i.e. still works as a same-priority-as-
    # before fallback for genuine project headers: no other directory on the
    # list has an "act_info.h", "utils.h", etc., so ordering never mattered
    # for those). No test source bare-quote-includes "limits.h" relying on
    # this path specifically (the one test file that reaches the project's
    # own limits.h -- interpre_account_menu_tests.cpp -- uses the explicit
    # "../limits.h" relative form, which resolves directly off the including
    # file's own directory and is unaffected either way); every product .cpp
    # that bare-quote-includes "limits.h" lives IN src/, so its own implicit
    # "directory of the including file" quote entry (always first, before any
    # -iquote/-idirafter path, on every compiler) finds src/limits.h
    # regardless of this flag. This is a build-flag fix only -- no file's
    # resolved header set changes -- so it cannot affect runtime behavior or
    # goldens.
    # NOTE: no embedded quotes around the paths below — CMake doesn't
    # shell-parse a genex's expansion, so a literal `"` would travel through
    # as part of the token (e.g. an -idirafter argument of literal `"/path"`,
    # quote characters included) instead of being stripped as shell-style
    # grouping.
    set_source_files_properties(${ROTS_TEST_SOURCES} PROPERTIES
        COMPILE_OPTIONS "$<${ROTS_GNULIKE}:-idirafter;${CMAKE_CURRENT_SOURCE_DIR};-iquote;${CMAKE_CURRENT_SOURCE_DIR}/tests>"
    )
    # Pathed data-model headers (spec §9a), same two leaf dirs as ageland above.
    # Additive to (not a replacement for) the -idirafter/-iquote handling above.
    # platform/include: ageland_tests compiles ROTS_PLATFORM_SOURCES directly
    # (it does not link the rots_platform library, see the TESTING-parity note
    # above ROTS_PLATFORM_SOURCES' add_executable line), so it needs this on
    # its own include path too, rather than inheriting it transitively via
    # target_link_libraries the way ageland does through RotS::platform.
    target_include_directories(ageland_tests PRIVATE core/include persist/include platform/include)
    target_compile_options(ageland_tests PRIVATE
        $<$<BOOL:${ROTS_LEGACY_32BIT}>:-m32>
        ${ROTS_FP_OPTIONS}
    )
    # -funsigned-char: same parity rationale as the ageland target above (char
    # signedness is a load-bearing behavior assumption the live game pins on every
    # compiler). Without this, product .cpp files compiled into ageland_tests get
    # the platform-default signedness (signed on x86 GCC/Clang) instead of the
    # game's pinned unsigned, which can silently diverge on any char-comparison /
    # sign-extension test that exercises high-bit (>=0x80) byte values.
    target_compile_options(ageland_tests PRIVATE
        $<${ROTS_GNULIKE}:-funsigned-char>
    )
    # /Zc:strictStrings- / /W4 + per-class /wd suppressions: same rationale and
    # suppression ledger as the ageland target above -- this target ALSO
    # compiles every ROTS_SERVER_SOURCES file (act_comm.cpp, act_info.cpp,
    # etc.), so it hits the identical MSVC /std:c++20 string-literal-to-char*
    # strictness and the same /W4 warning classes (Phase 5 Task 9).
    target_compile_options(ageland_tests PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX /wd4244 /wd4267 /wd4456 /wd4458 /wd4459 /wd4702 /wd4127 /wd4310 /Zc:strictStrings->
    )
    # _FILE_OFFSET_BITS=64: same EOVERFLOW-avoidance rationale as the ageland target
    # above — account-management tests do real opendir/readdir bucket scans under
    # TemporaryDirectory fixtures, so they need the 64-bit-safe libc variants too.
    # TESTING stays unconditional; _FILE_OFFSET_BITS is a glibc knob, meaningless off Linux/MSVC.
    target_compile_definitions(ageland_tests PRIVATE TESTING)
    target_compile_definitions(ageland_tests PRIVATE
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:_FILE_OFFSET_BITS=64>
        $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS>
    )
    # gtest_discover_tests (below) runs the binary with WORKING_DIRECTORY set to the
    # repo root, not src/tests/ — so the characterization tests' plain relative
    # "goldens/..." lookups (src/tests/Makefile's cwd) would miss under ctest. Anchor
    # them to an absolute path instead; characterization_combat_tests.cpp /
    # characterization_json_tests.cpp fall back to the relative path when this is
    # undefined (the Makefile build, which runs with cwd already at src/tests/).
    target_compile_definitions(ageland_tests PRIVATE ROTS_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/goldens")
    # ROTS_CONVERT_EXECUTABLE: absolute path to the rots_convert binary (db.cpp-split
    # Task 4), consumed by tests/rots_convert_equivalence_tests.cpp's ConvertEquivalence/
    # PerRace.* suite to shell out to the real, separately-built executable and compare
    # its output against this same binary's own in-process (real, non-stubbed) conversion
    # path. add_dependencies ensures `rots_convert` is actually built before ageland_tests
    # runs, since this compile definition only carries a path string -- it creates no
    # build-graph edge on its own. That test suite falls back to a ROTS_CONVERT_EXECUTABLE
    # environment variable (and skips cleanly if neither resolves to a file that exists)
    # for the flat src/tests/Makefile build, which does not build rots_convert at all (see
    # the rots_convert target's own comment above).
    add_dependencies(ageland_tests rots_convert)
    target_compile_definitions(ageland_tests PRIVATE
        ROTS_CONVERT_EXECUTABLE="$<TARGET_FILE:rots_convert>")
    if(ROTS_SUPPRESS_TEST_WARNINGS)
        target_compile_options(ageland_tests PRIVATE $<${ROTS_GNULIKE}:-w>)
    else()
        target_compile_options(ageland_tests PRIVATE $<${ROTS_GNULIKE}:-Wall -Wextra -Werror>)
    endif()
    # SSE float math and FP contraction for the tests now come from the shared
    # ROTS_FP_OPTIONS applied above (mirrors src/tests/Makefile) -- see the
    # comment at ROTS_FP_OPTIONS's definition near ROTS_GNULIKE/ROTS_X86 for
    # the full per-flag rationale. No-op on x86-64 hosts for -msse2/-mfpmath=sse
    # (SSE is already the default there); -ffp-contract=off applies everywhere,
    # including arm64.
    # ws2_32: same Winsock2 import library rationale as the ageland target
    # above (the tests link the full server sources, plus
    # startup_options_tests.cpp's own loopback socket fixtures).
    target_link_libraries(ageland_tests PRIVATE GTest::gtest $<$<PLATFORM_ID:Windows>:ws2_32>)
    # Mirror the compile-time -m32 so a multilib host building legacy tests links 32-bit too.
    target_link_options(ageland_tests PRIVATE
        $<$<BOOL:${ROTS_LEGACY_32BIT}>:-m32>
    )
    # test_random_utils.cpp installs a TESTING-only function-pointer hook
    # (rots_test_random_hook, defined in utility.cpp) that number()/
    # number(int,int) consult before drawing from the PRNG. This replaced a
    # GNU-ld-only `-Wl,--wrap` seam so the RNG queue works on every platform
    # (ld64/macOS and MSVC link.exe have no --wrap equivalent).

    if(BUILD_TESTING)
        gtest_discover_tests(ageland_tests
            DISCOVERY_MODE PRE_TEST
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
        )
    endif()

    # Load-bearing foundation-layer acyclicity check (spec §11). Force-load the
    # entire rots_platform archive into a tiny exe linked against ONLY
    # libc/libstdc++ (rots_build_flags carries the -m32/FP/etc. ABI flags) and NO
    # game libraries: any upward edge from L0 becomes an unresolved-symbol LINK
    # ERROR, failing the build. Supersedes the old nm-denylist (which could only
    # catch hand-listed hubs and gave false assurance). GNU ld and macOS ld64 use
    # different force-load spellings; both are non-MSVC. MSVC's guarantee stays
    # structural (rots_platform links only rots_build_flags), so this is skipped
    # there.
    if(NOT MSVC)
        add_executable(rots_platform_linkcheck tests/platform_linkcheck_main.cpp)
        target_link_libraries(rots_platform_linkcheck PRIVATE rots_build_flags)
        add_dependencies(rots_platform_linkcheck rots_platform)
        if(APPLE)
            target_link_options(rots_platform_linkcheck PRIVATE
                -Wl,-force_load,$<TARGET_FILE:rots_platform>)
        else()
            target_link_options(rots_platform_linkcheck PRIVATE
                -Wl,--whole-archive $<TARGET_FILE:rots_platform> -Wl,--no-whole-archive)
        endif()
        # add_dependencies (above) only orders the build; it does not tell the
        # generated Make/Ninja rules that this executable's LINK step consumes
        # librots_platform.a's contents (the archive path only appears inside a
        # raw linker flag string, invisible to CMake's normal
        # target_link_libraries dependency tracking). Without LINK_DEPENDS, an
        # incremental rebuild that changes only a rots_platform source (e.g. a
        # platform TU gains an upward edge) recompiles the archive but leaves
        # this executable's last-linked binary untouched, silently defeating
        # the whole point of this check. LINK_DEPENDS makes the archive file
        # itself a link prerequisite, so any change to it forces a relink.
        set_target_properties(rots_platform_linkcheck PROPERTIES
            LINK_DEPENDS "$<TARGET_FILE:rots_platform>")
        # The target above always builds under ROTS_BUILD_TESTS, so the
        # build-time link enforcement fires regardless. Gate only the CTest
        # REGISTRATION on BUILD_TESTING, mirroring gtest_discover_tests above, so
        # a `-DBUILD_TESTING=OFF` configure behaves consistently across all tests.
        if(BUILD_TESTING)
            add_test(NAME PlatformLayerAcyclicity COMMAND rots_platform_linkcheck)
        endif()

        # Load-bearing core-layer acyclicity check (spec §11), mirroring the
        # rots_platform_linkcheck block above. rots_core (config.cpp +
        # consts.cpp + output_seam.cpp, entity-seed Tasks 2-3 — see the
        # ROTS_CORE_SOURCES comment above) is verified to have no upward
        # symbol references beyond the one legal downward edge into
        # rots_platform (output_seam.cpp's rots::log::write_stderr tripwire).
        # Allowed lower layers are rots_platform + libc/libstdc++; the checker
        # force-loads rots_core itself (so every rots_core symbol is pulled
        # in and any OTHER unresolved reference fails the link) and normal-
        # links RotS::platform to resolve that one legitimate edge — a
        # strictly tighter guarantee than "any lower layer is allowed".
        add_executable(rots_core_linkcheck tests/core_linkcheck_main.cpp)
        target_link_libraries(rots_core_linkcheck PRIVATE rots_build_flags RotS::platform)
        add_dependencies(rots_core_linkcheck rots_core)
        if(APPLE)
            target_link_options(rots_core_linkcheck PRIVATE
                -Wl,-force_load,$<TARGET_FILE:rots_core>)
        else()
            target_link_options(rots_core_linkcheck PRIVATE
                -Wl,--whole-archive $<TARGET_FILE:rots_core> -Wl,--no-whole-archive)
        endif()
        # LINK_DEPENDS: same rationale as rots_platform_linkcheck above — the
        # archive path only appears inside a raw linker flag string, invisible
        # to CMake's normal target_link_libraries dependency tracking, so an
        # incremental rebuild that changes only a rots_core source needs this
        # to force a relink instead of silently reusing a stale binary.
        set_target_properties(rots_core_linkcheck PROPERTIES
            LINK_DEPENDS "$<TARGET_FILE:rots_core>")
        if(BUILD_TESTING)
            add_test(NAME CoreLayerAcyclicity COMMAND rots_core_linkcheck)
        endif()

        # Load-bearing entity-layer acyclicity check (spec §11), mirroring the
        # rots_core_linkcheck block above. rots_entity (entity_lifecycle.cpp +
        # object_utils.cpp + environment_utils.cpp, entity-seed Task 6, plus
        # char_utils.cpp + char_utils_combat.cpp, entity-completion Task 3 —
        # see the ROTS_ENTITY_SOURCES comment above) is verified to have no
        # upward symbol references beyond its legal downward edges into
        # rots_core and rots_platform. The checker force-loads rots_entity
        # itself (so every rots_entity symbol is pulled in and any OTHER
        # unresolved reference fails the link) and normal-links RotS::core +
        # RotS::platform to resolve those legitimate lower-layer edges — a
        # strictly tighter guarantee than "any lower layer is allowed".
        add_executable(rots_entity_linkcheck tests/entity_linkcheck_main.cpp)
        target_link_libraries(rots_entity_linkcheck PRIVATE rots_build_flags RotS::core RotS::platform)
        add_dependencies(rots_entity_linkcheck rots_entity)
        if(APPLE)
            target_link_options(rots_entity_linkcheck PRIVATE
                -Wl,-force_load,$<TARGET_FILE:rots_entity>)
        else()
            target_link_options(rots_entity_linkcheck PRIVATE
                -Wl,--whole-archive $<TARGET_FILE:rots_entity> -Wl,--no-whole-archive)
        endif()
        # LINK_DEPENDS: same rationale as rots_platform_linkcheck/
        # rots_core_linkcheck above — the archive path only appears inside a
        # raw linker flag string, invisible to CMake's normal
        # target_link_libraries dependency tracking, so an incremental
        # rebuild that changes only a rots_entity source needs this to force
        # a relink instead of silently reusing a stale binary.
        set_target_properties(rots_entity_linkcheck PROPERTIES
            LINK_DEPENDS "$<TARGET_FILE:rots_entity>")
        if(BUILD_TESTING)
            add_test(NAME EntityLayerAcyclicity COMMAND rots_entity_linkcheck)
        endif()

        # Load-bearing persist-layer acyclicity check (spec §11), mirroring the
        # rots_entity_linkcheck block above (persist-split PS Task 4).
        # rots_persist (db_players.cpp + character_json.cpp + objects_json.cpp +
        # exploits_json.cpp + account_management.cpp + account_cache.cpp +
        # obj_files.cpp + pkill_json.cpp + mail_json.cpp + boards_json.cpp +
        # convert_exploits.cpp + convert_plrobjs.cpp + save_benchmark.cpp +
        # color_convert.cpp — see the ROTS_PERSIST_SOURCES comment above) is
        # verified to have no upward symbol references beyond its legal
        # downward edges into rots_entity, rots_core, and rots_platform. The
        # checker force-loads rots_persist itself (so every rots_persist
        # symbol is pulled in and any OTHER unresolved reference fails the
        # link) and normal-links RotS::entity + RotS::core + RotS::platform
        # to resolve those legitimate lower-layer edges — a strictly tighter
        # guarantee than "any lower layer is allowed".
        add_executable(rots_persist_linkcheck tests/persist_linkcheck_main.cpp)
        target_link_libraries(rots_persist_linkcheck PRIVATE rots_build_flags RotS::entity RotS::core RotS::platform)
        add_dependencies(rots_persist_linkcheck rots_persist)
        if(APPLE)
            target_link_options(rots_persist_linkcheck PRIVATE
                -Wl,-force_load,$<TARGET_FILE:rots_persist>)
        else()
            target_link_options(rots_persist_linkcheck PRIVATE
                -Wl,--whole-archive $<TARGET_FILE:rots_persist> -Wl,--no-whole-archive)
        endif()
        # LINK_DEPENDS: same rationale as rots_entity_linkcheck above — the
        # archive path only appears inside a raw linker flag string, invisible
        # to CMake's normal target_link_libraries dependency tracking, so an
        # incremental rebuild that changes only a rots_persist source needs
        # this to force a relink instead of silently reusing a stale binary.
        set_target_properties(rots_persist_linkcheck PROPERTIES
            LINK_DEPENDS "$<TARGET_FILE:rots_persist>")
        if(BUILD_TESTING)
            add_test(NAME PersistLayerAcyclicity COMMAND rots_persist_linkcheck)
        endif()

        # Load-bearing world-layer acyclicity check (spec §11), mirroring the
        # rots_persist_linkcheck block above (world-seed wave Task 5).
        # rots_world (db_world.cpp + zone_load.cpp + weather.cpp — see the
        # ROTS_WORLD_SOURCES comment above) is verified to have no upward
        # symbol references beyond its legal downward edges into rots_persist
        # (the one sanctioned L3-peer edge — db_world.cpp calls
        # rots::persist::set_room_vnum_hook), rots_entity, rots_core, and
        # rots_platform. The checker force-loads rots_world itself (so every
        # rots_world symbol is pulled in and any OTHER unresolved reference
        # fails the link) and normal-links RotS::persist + RotS::entity +
        # RotS::core + RotS::platform to resolve those legitimate
        # lower-layer/peer edges — a strictly tighter guarantee than "any
        # lower layer is allowed".
        add_executable(rots_world_linkcheck tests/world_linkcheck_main.cpp)
        target_link_libraries(rots_world_linkcheck PRIVATE rots_build_flags RotS::persist RotS::entity RotS::core RotS::platform)
        add_dependencies(rots_world_linkcheck rots_world)
        if(APPLE)
            target_link_options(rots_world_linkcheck PRIVATE
                -Wl,-force_load,$<TARGET_FILE:rots_world>)
        else()
            target_link_options(rots_world_linkcheck PRIVATE
                -Wl,--whole-archive $<TARGET_FILE:rots_world> -Wl,--no-whole-archive)
        endif()
        # LINK_DEPENDS: same rationale as rots_persist_linkcheck above — the
        # archive path only appears inside a raw linker flag string, invisible
        # to CMake's normal target_link_libraries dependency tracking, so an
        # incremental rebuild that changes only a rots_world source needs
        # this to force a relink instead of silently reusing a stale binary.
        set_target_properties(rots_world_linkcheck PROPERTIES
            LINK_DEPENDS "$<TARGET_FILE:rots_world>")
        if(BUILD_TESTING)
            add_test(NAME WorldLayerAcyclicity COMMAND rots_world_linkcheck)
        endif()

        # Load-bearing combat-layer acyclicity check (spec §11), mirroring the
        # rots_world_linkcheck block above (combat-seed wave Task 1).
        # rots_combat (battle_mage_handler.cpp + skill_timer.cpp +
        # weapon_master_handler.cpp + wild_fighting_handler.cpp — see the
        # ROTS_COMBAT_SOURCES comment above) is verified to have no upward
        # symbol references beyond its legal downward edges into rots_entity,
        # rots_core, and rots_platform. Unlike rots_world_linkcheck, no
        # RotS::persist/RotS::world normal-link: the census found zero
        # L3-peer edges out of this 4-TU subset. The checker force-loads
        # rots_combat itself (so every rots_combat symbol is pulled in and
        # any OTHER unresolved reference fails the link) and normal-links
        # RotS::entity + RotS::core + RotS::platform to resolve those
        # legitimate lower-layer edges — a strictly tighter guarantee than
        # "any lower layer is allowed".
        add_executable(rots_combat_linkcheck tests/combat_linkcheck_main.cpp)
        target_link_libraries(rots_combat_linkcheck PRIVATE rots_build_flags RotS::entity RotS::core RotS::platform)
        add_dependencies(rots_combat_linkcheck rots_combat)
        if(APPLE)
            target_link_options(rots_combat_linkcheck PRIVATE
                -Wl,-force_load,$<TARGET_FILE:rots_combat>)
        else()
            target_link_options(rots_combat_linkcheck PRIVATE
                -Wl,--whole-archive $<TARGET_FILE:rots_combat> -Wl,--no-whole-archive)
        endif()
        # LINK_DEPENDS: same rationale as rots_world_linkcheck above — the
        # archive path only appears inside a raw linker flag string, invisible
        # to CMake's normal target_link_libraries dependency tracking, so an
        # incremental rebuild that changes only a rots_combat source needs
        # this to force a relink instead of silently reusing a stale binary.
        set_target_properties(rots_combat_linkcheck PROPERTIES
            LINK_DEPENDS "$<TARGET_FILE:rots_combat>")
        if(BUILD_TESTING)
            add_test(NAME CombatLayerAcyclicity COMMAND rots_combat_linkcheck)
        endif()
    endif()
endif()

# --- Utility targets (ported from upstream/account-management) ----------------
# `setup` mirrors the src/Makefile `setup` target (which the upstream branch
# extended with the accounts/account_characters buckets): creates the runtime
# directory tree and placeholder files the server expects at boot. Invoked by
# the root Makefile as `cmake --build build --target setup`.
set(ROTS_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(ROTS_LIB_DIR "${ROTS_REPO_ROOT}/lib")
add_custom_target(setup
    COMMAND "${CMAKE_COMMAND}" -E make_directory
        "${ROTS_REPO_ROOT}/bin"
        "${ROTS_REPO_ROOT}/log"
        "${ROTS_LIB_DIR}/misc"
        "${ROTS_LIB_DIR}/players/A-E"
        "${ROTS_LIB_DIR}/players/F-J"
        "${ROTS_LIB_DIR}/players/K-O"
        "${ROTS_LIB_DIR}/players/P-T"
        "${ROTS_LIB_DIR}/players/U-Z"
        "${ROTS_LIB_DIR}/players/ZZZ"
        "${ROTS_LIB_DIR}/accounts/A-E"
        "${ROTS_LIB_DIR}/accounts/F-J"
        "${ROTS_LIB_DIR}/accounts/K-O"
        "${ROTS_LIB_DIR}/accounts/P-T"
        "${ROTS_LIB_DIR}/accounts/U-Z"
        "${ROTS_LIB_DIR}/accounts/ZZZ"
        "${ROTS_LIB_DIR}/account_characters/A-E"
        "${ROTS_LIB_DIR}/account_characters/F-J"
        "${ROTS_LIB_DIR}/account_characters/K-O"
        "${ROTS_LIB_DIR}/account_characters/P-T"
        "${ROTS_LIB_DIR}/account_characters/U-Z"
        "${ROTS_LIB_DIR}/account_characters/ZZZ"
        "${ROTS_LIB_DIR}/plrobjs/A-E"
        "${ROTS_LIB_DIR}/plrobjs/F-J"
        "${ROTS_LIB_DIR}/plrobjs/K-O"
        "${ROTS_LIB_DIR}/plrobjs/P-T"
        "${ROTS_LIB_DIR}/plrobjs/U-Z"
        "${ROTS_LIB_DIR}/plrobjs/ZZZ"
        "${ROTS_LIB_DIR}/exploits/A-E"
        "${ROTS_LIB_DIR}/exploits/F-J"
        "${ROTS_LIB_DIR}/exploits/K-O"
        "${ROTS_LIB_DIR}/exploits/P-T"
        "${ROTS_LIB_DIR}/exploits/U-Z"
        "${ROTS_LIB_DIR}/exploits/ZZZ"
        "${ROTS_REPO_ROOT}/mobs"
    COMMAND "${CMAKE_COMMAND}" -E touch
        "${ROTS_REPO_ROOT}/log/lastdeath"
        "${ROTS_LIB_DIR}/last_cmds"
        "${ROTS_LIB_DIR}/misc/pklist"
    COMMENT "Creating runtime directories and placeholder files"
)

# `format` mirrors the src/Makefile `format` target: WebKit-style clang-format
# over the server sources. Only defined when clang-format is available (the
# i386 container image does not install it; run via the root `make format`
# on a host that has it).
find_program(CLANG_FORMAT_EXECUTABLE clang-format)
if(CLANG_FORMAT_EXECUTABLE)
    file(GLOB ROTS_FORMAT_FILES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    )
    add_custom_target(format
        COMMAND "${CLANG_FORMAT_EXECUTABLE}" -i -style=WebKit ${ROTS_FORMAT_FILES}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Formatting source files with clang-format"
    )
endif()
