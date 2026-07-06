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

cmake_minimum_required(VERSION 3.13...3.28)
project(RotS LANGUAGES CXX)

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
    ranger.cpp script.cpp shapemdl.cpp shapemob.cpp
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
#   -m32             32-bit build (ABI / save-file format); required, and must be
#                    passed at BOTH compile and link time
#   -funsigned-char  the code assumes `char` is unsigned
#   -fstrict-aliasing  matches the historical build
#   -w               warnings intentionally suppressed (legacy tree)
#   -g               debug symbols (always on, like the Makefile)
#   -rdynamic        export symbols for runtime backtraces (link)
target_compile_options(ageland PRIVATE -m32 -funsigned-char -fstrict-aliasing -w -g)
target_link_options(ageland PRIVATE -m32 -rdynamic)

# Per-configuration optimization (the common flags above apply to every config).
target_compile_options(ageland PRIVATE
    $<$<CONFIG:Debug>:-O0 -DDEBUG>
    $<$<CONFIG:Release>:-O2 -DNDEBUG>
)

# account_management*.cpp calls libc crypt() for password hashing; link libcrypt
# (mirrors LIBS += -lcrypt in src/Makefile).
target_link_libraries(ageland PRIVATE crypt)

# --- Tests (ported from upstream/account-management) --------------------------
# GoogleTest suite, discovered via CTest so `ctest --test-dir build` (or the root
# `make test`) runs it. This is IN ADDITION to the older src/tests/Makefile entry
# point (see CLAUDE.md / AGENTS.md) — both build the same test sources against
# GoogleTest; use whichever fits the workflow.
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
target_compile_definitions(ageland_tests PRIVATE TESTING)
if(ROTS_SUPPRESS_TEST_WARNINGS)
    target_compile_options(ageland_tests PRIVATE -w)
else()
    target_compile_options(ageland_tests PRIVATE -Wall -Wextra)
endif()
target_link_libraries(ageland_tests PRIVATE GTest::gtest crypt)
# test_random_utils.cpp wraps number() for deterministic RNG in tests.
target_link_options(ageland_tests PRIVATE
    -Wl,--wrap=_Z6numberv
    -Wl,--wrap=_Z6numberii
)

if(BUILD_TESTING)
    gtest_discover_tests(ageland_tests
        DISCOVERY_MODE PRE_TEST
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
    )
endif()
