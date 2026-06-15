#usage : cmake -S . -B /../build
# this will create a build solution in the build directory
# .gitignore exludes everything under /build.

cmake_minimum_required(VERSION 3.10)
project(RotS)

# Set default build type to Debug if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

file(GLOB SOURCES "*.cpp")
file(GLOB HEADERS ".h")

# --- Dead code exclusion -----------------------------------------------------
# The combat_manager class (combat_manager.cpp) is compiled but never instantiated
# or invoked anywhere in the live game; the real melee path is fight.cpp::hit()
# (see CLAUDE.md). Nothing outside combat_manager.{cpp,h} references its symbols,
# so it is dropped from the build.
#
# NOTE: char_utils_combat.cpp is intentionally NOT removed. Its get_real_ob/parry/
# dodge(weather, room) helpers are dead (only combat_manager called them), but the
# same translation unit also defines LIVE functions — on_attacked_character
# (fight.cpp, clerics.cpp) and get_engaged_characters (ranger.cpp) — so removing the
# file would break the link. Only the helpers inside it are dead, not the unit.
list(REMOVE_ITEM SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/combat_manager.cpp")
# -----------------------------------------------------------------------------

add_executable(ageland ${SOURCES} ${HEADERS})

set_target_properties(ageland PROPERTIES CXX_STANDARD 17)

# Add debug flags
target_compile_options(ageland PRIVATE 
    $<$<CONFIG:Debug>:-g -O0 -DDEBUG>
    $<$<CONFIG:Release>:-O2 -DNDEBUG>
)

# Enable debug symbols for all configurations
target_compile_options(ageland PRIVATE -g)