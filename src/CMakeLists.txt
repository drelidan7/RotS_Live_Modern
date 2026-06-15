# RotS — CMake build (alternate to src/Makefile).
#
#   Out-of-source build:  cmake -S src -B build && cmake --build build
#   Produces:             build/ageland   (the Makefile build produces ../bin/ageland)
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
add_executable(ageland
    act_comm.cpp act_info.cpp act_move.cpp act_obj1.cpp
    act_obj2.cpp act_offe.cpp act_othe.cpp act_soci.cpp
    act_wiz.cpp ban.cpp battle_mage_handler.cpp big_brother.cpp
    boards.cpp char_utils.cpp char_utils_combat.cpp clerics.cpp
    clock.cpp color.cpp comm.cpp config.cpp
    consts.cpp db.cpp delayed_command_interpreter.cpp environment_utils.cpp
    fight.cpp graph.cpp handler.cpp interpre.cpp
    limits.cpp mage.cpp mail.cpp mob_csv_extract.cpp
    mobact.cpp modify.cpp mudlle.cpp mudlle2.cpp
    mystic.cpp obj2html.cpp object_utils.cpp objsave.cpp
    olog_hai.cpp pkill.cpp profs.cpp protocol.cpp
    ranger.cpp script.cpp shapemdl.cpp shapemob.cpp
    shapeobj.cpp shaperom.cpp shapescript.cpp shapezon.cpp
    shop.cpp signals.cpp skill_timer.cpp spec_ass.cpp
    spec_pro.cpp spell_pa.cpp utility.cpp wait_functions.cpp
    weapon_master_handler.cpp weather.cpp wild_fighting_handler.cpp zone.cpp
)

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
