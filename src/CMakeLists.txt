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

# Default to Debug: matches the Makefile, which always builds with -g and no -O.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# --- Sources -----------------------------------------------------------------
# Explicit list (CMake recommends against file(GLOB): it cannot tell when files are
# added/removed, so the build silently goes stale). Keep in sync with OBJFILES in
# src/Makefile.
#
# combat_manager.cpp is intentionally omitted — dead code (never instantiated or
# invoked; the live melee path is fight.cpp::hit(), see CLAUDE.md).
# char_utils_combat.cpp IS listed: it also defines live functions used by
# fight.cpp / clerics.cpp / ranger.cpp; only its get_real_ob/parry/dodge helpers are dead.
#
# account_management.cpp / character_json.cpp / exploits_json.cpp / json_utils.cpp /
# objects_json.cpp arrive with the account-management merge (JSON persistence +
# accounts); the other account_management_*.cpp files are #included into
# account_management.cpp, not compiled separately.
set(ROTS_SERVER_SOURCES
    account_management.cpp act_comm.cpp act_info.cpp act_move.cpp act_obj1.cpp
    act_obj2.cpp act_offe.cpp act_othe.cpp act_soci.cpp
    act_wiz.cpp ban.cpp battle_mage_handler.cpp big_brother.cpp
    boards.cpp char_utils.cpp char_utils_combat.cpp character_json.cpp clerics.cpp
    clock.cpp color.cpp comm.cpp config.cpp
    consts.cpp db.cpp delayed_command_interpreter.cpp exploits_json.cpp environment_utils.cpp
    fight.cpp graph.cpp handler.cpp interpre.cpp
    json_utils.cpp limits.cpp mage.cpp mail.cpp mob_csv_extract.cpp
    mobact.cpp modify.cpp mudlle.cpp mudlle2.cpp
    mystic.cpp obj2html.cpp object_utils.cpp objects_json.cpp objsave.cpp
    olog_hai.cpp pkill.cpp profs.cpp protocol.cpp
    ranger.cpp rots_rng.cpp script.cpp shapemdl.cpp shapemob.cpp
    shapeobj.cpp shaperom.cpp shapescript.cpp shapezon.cpp
    shop.cpp signals.cpp skill_timer.cpp spec_ass.cpp
    spec_pro.cpp spell_pa.cpp utility.cpp wait_functions.cpp
    weapon_master_handler.cpp weather.cpp wild_fighting_handler.cpp zone.cpp
)

add_executable(ageland ${ROTS_SERVER_SOURCES})

# C++17, ISO dialect (-std=c++17, not gnu++17) to match the Makefile's -std=c++1z.
target_compile_features(ageland PRIVATE cxx_std_17)
set_target_properties(ageland PROPERTIES CXX_EXTENSIONS OFF)

# --- Flag parity with src/Makefile (behavior-affecting — keep in sync) --------
# The historical flags, now split along two axes:
#   * ROTS_LEGACY_32BIT gates -m32 (compile AND link).
#   * Compiler guards keep GCC/Clang spellings away from MSVC, which fails hard
#     on unknown flags. MSVC gets /J — the -funsigned-char equivalent — because
#     unsigned char is a load-bearing behavior assumption (spec Phase 5 audits
#     it away; until then every compiler must pin it).
# GNU-family flags: -funsigned-char / -fstrict-aliasing / -w / -g are identical
# on GCC, Clang, and AppleClang.
#   -D_FILE_OFFSET_BITS=64  32-bit glibc's legacy readdir()/stat() use a 32-bit
#     ino_t/off_t; against modern filesystems with 64-bit inode numbers (ext4,
#     overlay2) readdir() fails with EOVERFLOW. Switches libc to the 64-bit-safe
#     variants transparently — does not change any of the game's own on-disk
#     structs, so save-file format is unaffected.
set(ROTS_GNULIKE "$<CXX_COMPILER_ID:GNU,Clang,AppleClang>")
target_compile_options(ageland PRIVATE
    $<$<BOOL:${ROTS_LEGACY_32BIT}>:-m32>
    $<${ROTS_GNULIKE}:-funsigned-char -fstrict-aliasing -w -g>
    $<$<CXX_COMPILER_ID:MSVC>:/J /w>
)
target_link_options(ageland PRIVATE
    $<$<BOOL:${ROTS_LEGACY_32BIT}>:-m32>
    # -rdynamic: GNU spelling; the clang driver translates it on macOS. MSVC: none.
    $<${ROTS_GNULIKE}:-rdynamic>
)
# _FILE_OFFSET_BITS is a glibc knob; harmless but meaningless off Linux/MSVC.
target_compile_definitions(ageland PRIVATE
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:_FILE_OFFSET_BITS=64>
)

# Per-configuration optimization (the common flags above apply to every config).
target_compile_options(ageland PRIVATE
    $<${ROTS_GNULIKE}:$<$<CONFIG:Debug>:-O0 -DDEBUG>>
    $<${ROTS_GNULIKE}:$<$<CONFIG:Release>:-O2 -DNDEBUG>>
)

# account_management*.cpp calls libc crypt(): a separate libcrypt on Linux,
# inside libc on macOS (no -lcrypt exists there), absent on Windows (the
# Windows build stays red on this symbol until Phase 3 replaces crypt()).
if(UNIX AND NOT APPLE)
    target_link_libraries(ageland PRIVATE crypt)
endif()

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

    option(ROTS_SUPPRESS_TEST_WARNINGS "Suppress compiler warnings when building the ageland_tests target" ON)

    # Explicit list, matching the style above — no file(GLOB).
    set(ROTS_TEST_SOURCES
        tests/account_management_assets_header_compile.cpp
        tests/account_management_identity_header_compile.cpp
        tests/account_management_migration_header_compile.cpp
        tests/account_management_presentation_header_compile.cpp
        tests/account_management_storage_header_compile.cpp
        tests/account_management_tests.cpp
        tests/account_management_types_header_compile.cpp
        tests/act_wiz_tests.cpp
        tests/battle_mage_handler_tests.cpp
        tests/char_utils_tests.cpp
        tests/character_json_tests.cpp
        tests/characterization_combat_tests.cpp
        tests/characterization_json_tests.cpp
        tests/CharPlayerDataBuilder.cpp
        tests/color_tests.cpp
        tests/damage_tests.cpp
        tests/db_loader_tests.cpp
        tests/exploits_json_tests.cpp
        tests/fight_proc_tests.cpp
        tests/gtest_main.cpp
        tests/interpre_account_menu_tests.cpp
        tests/json_utils_tests.cpp
        tests/mage_tests.cpp
        tests/obj_flag_data_tests.cpp
        tests/ObjFlagDataBuilder.cpp
        tests/object_utils_tests.cpp
        tests/objects_json_tests.cpp
        tests/olog_hai_tests.cpp
        tests/protocol_tests.cpp
        tests/rots_rng_tests.cpp
        tests/spell_pa_tests.cpp
        tests/startup_options_tests.cpp
        tests/test_random_utils.cpp
        tests/test_random_utils_tests.cpp
        tests/weapon_master_handler_tests.cpp
    )

    find_package(GTest REQUIRED)
    include(GoogleTest)

    add_executable(ageland_tests ${ROTS_SERVER_SOURCES} ${ROTS_TEST_SOURCES})

    # Same C++ standard as ageland (see above) — required because these targets are
    # defined per-target here rather than via a project-wide CMAKE_CXX_STANDARD.
    target_compile_features(ageland_tests PRIVATE cxx_std_17)
    set_target_properties(ageland_tests PROPERTIES CXX_EXTENSIONS OFF)

    target_include_directories(ageland_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests"
    )
    target_compile_options(ageland_tests PRIVATE
        $<$<BOOL:${ROTS_LEGACY_32BIT}>:-m32>
    )
    # _FILE_OFFSET_BITS=64: same EOVERFLOW-avoidance rationale as the ageland target
    # above — account-management tests do real opendir/readdir bucket scans under
    # TemporaryDirectory fixtures, so they need the 64-bit-safe libc variants too.
    # TESTING stays unconditional; _FILE_OFFSET_BITS is a glibc knob, meaningless off Linux/MSVC.
    target_compile_definitions(ageland_tests PRIVATE TESTING)
    target_compile_definitions(ageland_tests PRIVATE
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:_FILE_OFFSET_BITS=64>
    )
    # gtest_discover_tests (below) runs the binary with WORKING_DIRECTORY set to the
    # repo root, not src/tests/ — so the characterization tests' plain relative
    # "goldens/..." lookups (src/tests/Makefile's cwd) would miss under ctest. Anchor
    # them to an absolute path instead; characterization_combat_tests.cpp /
    # characterization_json_tests.cpp fall back to the relative path when this is
    # undefined (the Makefile build, which runs with cwd already at src/tests/).
    target_compile_definitions(ageland_tests PRIVATE ROTS_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/goldens")
    if(ROTS_SUPPRESS_TEST_WARNINGS)
        target_compile_options(ageland_tests PRIVATE $<${ROTS_GNULIKE}:-w>)
    else()
        target_compile_options(ageland_tests PRIVATE $<${ROTS_GNULIKE}:-Wall -Wextra>)
    endif()
    # SSE float math for the tests (mirrors src/tests/Makefile): i386 g++ defaults to
    # x87 extended-precision FP, which rounds differently from the SSE math of every
    # x86-64 build; several tests pin exact FP thresholds authored under SSE semantics.
    # No-op on x86-64 hosts (SSE is already the default there).
    target_compile_options(ageland_tests PRIVATE $<${ROTS_GNULIKE}:-msse2 -mfpmath=sse>)
    target_link_libraries(ageland_tests PRIVATE GTest::gtest)
    if(UNIX AND NOT APPLE)
        target_link_libraries(ageland_tests PRIVATE crypt)
    endif()
    # Mirror the compile-time -m32 so a multilib host building legacy tests links 32-bit too.
    target_link_options(ageland_tests PRIVATE
        $<$<BOOL:${ROTS_LEGACY_32BIT}>:-m32>
    )
    # test_random_utils.cpp wraps number() for deterministic RNG in tests.
    # -Wl,--wrap is GNU ld only: ld64 (macOS) and MSVC link.exe have no
    # equivalent, so the RNG seam does not exist off Linux yet — Phase 2 must
    # replace it (e.g. a link-time indirection) before macOS tests can pass.
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_options(ageland_tests PRIVATE
            -Wl,--wrap=_Z6numberv
            -Wl,--wrap=_Z6numberii
        )
    endif()

    if(BUILD_TESTING)
        gtest_discover_tests(ageland_tests
            DISCOVERY_MODE PRE_TEST
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
        )
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
