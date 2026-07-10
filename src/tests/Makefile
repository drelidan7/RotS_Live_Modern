CXX = g++
# -D_FILE_OFFSET_BITS=64: without it, this 32-bit test binary's opendir/readdir
# calls fail with EOVERFLOW against directories containing 64-bit inode numbers
# (routine on the container's filesystem) — see the matching comment in
# src/Makefile. Account-management tests exercise real opendir/readdir bucket
# scans (TemporaryDirectory fixtures under /tmp), so they need this too.
# -msse2 -mfpmath=sse: i386 g++ defaults to x87 (80-bit extended) float math,
# which rounds float/double expressions differently from the SSE math every
# x86-64 build uses. Several tests pin exact FP thresholds (RNG-seam scaling in
# test_random_utils.cpp, battle-mage interrupt chances) that were authored under
# SSE semantics, so the test build must use SSE too. Tests-only: the game binary
# (src/Makefile) intentionally keeps the historical x87 behavior.
# -funsigned-char: parity with src/Makefile's GAMEC flags (the live game pins
# char as unsigned on every build); without it, product .cpp files compiled
# into this test binary get the platform-default (signed) char instead.
CXXFLAGS = -std=c++20 -Wall -Wextra -D TESTING -D_FILE_OFFSET_BITS=64 -msse2 -mfpmath=sse -funsigned-char
# -idirafter .. lets the account_management_*_header_compile.cpp probes (and
# every other tests/*.cpp) use bare #include "account_management_assets.h" /
# "structs.h" (etc.) to reach the product headers one directory up; mirrors
# the -idirafter (NOT -iquote, NOT -I) that src/CMakeLists.txt uses for its
# ROTS_TEST_SOURCES list. Never a bare -I..: that would ALSO add it to the
# angle-bracket (<...>) search path, and src/limits.h (the game's player-
# rank-constants header, unrelated to the standard C <limits.h>) would then
# shadow the real system <limits.h> ahead of the toolchain's own path:
# UCHAR_MAX/CHAR_BIT come up undeclared wherever a header does a bare
# #include <limits.h> (libstdc++'s <climits> on some toolchains).
#
# NOT plain -iquote either (Phase 4 Wave 1 Task 1, trixie/g++14 bump): GCC's
# #include_next -- used internally by its own bundled <limits.h>/syslimits.h
# to chain into glibc's real <limits.h> -- does NOT respect the quote/angle
# distinction; per the GCC manual it just continues down the merged
# directory list from wherever the current file was found, and -iquote dirs
# sit at the FRONT of that merged list (highest priority), ahead of the
# standard system directories glibc's real <limits.h> lives in. Under
# C++20, gtest's own gtest-port.h (<condition_variable>) -- pulled in by
# every tests/*.cpp via gtest.h -- newly pulls in <semaphore>
# (std::counting_semaphore; unreachable pre-C++20, so this was invisible
# under the old c++1z build), whose libstdc++ implementation needs
# _POSIX_SEM_VALUE_MAX from glibc's <bits/posix1_lim.h>. With -iquote ..
# (.. containing limits.h) on the FRONT of the merged list, GCC's
# #include_next chain for <limits.h> walked into ../limits.h before ever
# reaching glibc's real one, so posix1_lim.h never loaded and every test TU
# failed with "'_POSIX_SEM_VALUE_MAX' was not declared" -- scoping the flag
# to test-only compiles (as an earlier version of this comment/fix did)
# does NOT help, because gtest.h itself is what triggers the chain, and
# every test file includes it.
#
# -idirafter instead places .. at the very END of the merged list -- after
# the standard system directories -- so #include_next resolves glibc's real
# <limits.h> first (found earlier, in its normal system-directory position)
# and only falls through to ../limits.h for a bare quoted include that
# nothing else on the list satisfies (i.e. still works as a same-priority-
# as-before fallback for genuine product headers: no other directory on the
# list has an "account_management_assets.h", "structs.h", etc., so ordering
# never mattered for those). No test source bare-quote-includes "limits.h"
# relying on this path (interpre_account_menu_tests.cpp, the one test that
# needs the project's own limits.h, uses the explicit "../limits.h" relative
# form, unaffected either way); every product .cpp that bare-quote-includes
# "limits.h" lives one directory up (in src/, not src/tests/), so it's built
# via its own explicit OBJFILES rule using plain $(CXXFLAGS) (no -idirafter
# needed at all -- its implicit "directory of the including file" quote
# entry, always first on every compiler, already finds ../limits.h). This
# is a build-flag fix only -- no file's resolved header set changes -- so it
# cannot affect runtime behavior or goldens.
TEST_CXXFLAGS = $(CXXFLAGS) -idirafter ..
# -lpthread: libgtest.a's ThreadLocal support needs pthread_{get,set}specific/key_*;
# the static lib doesn't pull it in transitively on this toolchain, so link it explicitly.
LDFLAGS = -lgtest -lpthread

OBJFILES = act_comm.o act_info.o act_move.o act_obj1.o act_obj2.o act_offe.o \
	act_othe.o act_soci.o act_wiz.o ban.o battle_mage_handler.o big_brother.o boards.o char_utils.o char_utils_combat.o clerics.o clock.o color.o combat_manager.o \
	comm.o config.o consts.o convert_exploits.o convert_plrobjs.o crashsave_schedule.o db.o delayed_command_interpreter.o fight.o graph.o handler.o interpre.o environment_utils.o account_cache.o account_management.o character_json.o exploits_json.o json_utils.o objects_json.o \
	limits.o mail.o mystic.o mage.o mobact.o modify.o mudlle.o mudlle2.o mob_csv_extract.o obj2html.o object_utils.o objsave.o olog_hai.o\
	pkill.o player_file_finalize.o profs.o protocol.o ranger.o save_benchmark.o savebench.o script.o shapemdl.o shapemob.o shapeobj.o shaperom.o shapezon.o shapescript.o shop.o \
	signals.o skill_timer.o spec_ass.o spec_pro.o spell_pa.o utility.o wait_functions.o weapon_master_handler.o  \
	wild_fighting_handler.o weather.o zone.o rots_rng.o rots_crypt.o rots_net.o


# Dependencies for the main mud

account_management.o : ../account_management.cpp ../account_management.h ../account_management_types.h ../account_management_identity.h ../account_management_storage.h ../account_management_assets.h ../account_management_migration.h ../account_management_presentation.h ../account_management_internal.cpp ../account_management_identity.cpp ../account_management_storage.cpp ../account_management_assets.cpp ../account_management_migration.cpp ../account_management_presentation.cpp ../json_utils.h
	$(CXX) -c $(CXXFLAGS) ../account_management.cpp

account_cache.o : ../account_cache.cpp ../account_cache.h ../account_management.h
	$(CXX) -c $(CXXFLAGS) ../account_cache.cpp

crashsave_schedule.o : ../crashsave_schedule.cpp ../crashsave_schedule.h
	$(CXX) -c $(CXXFLAGS) ../crashsave_schedule.cpp

player_file_finalize.o : ../player_file_finalize.cpp ../player_file_finalize.h
	$(CXX) -c $(CXXFLAGS) ../player_file_finalize.cpp

save_benchmark.o : ../save_benchmark.cpp ../save_benchmark.h ../account_cache.h ../account_management.h ../character_json.h ../db.h ../stopwatch.h ../structs.h
	$(CXX) -c $(CXXFLAGS) ../save_benchmark.cpp

savebench.o : ../savebench.cpp ../savebench.h ../save_benchmark.h ../account_management.h ../comm.h ../db.h ../structs.h ../utils.h
	$(CXX) -c $(CXXFLAGS) ../savebench.cpp

character_json.o : ../character_json.cpp ../character_json.h ../json_utils.h ../structs.h
	$(CXX) -c $(CXXFLAGS) ../character_json.cpp

exploits_json.o : ../exploits_json.cpp ../exploits_json.h ../json_utils.h ../db.h
	$(CXX) -c $(CXXFLAGS) ../exploits_json.cpp

json_utils.o : ../json_utils.cpp ../json_utils.h
	$(CXX) -c $(CXXFLAGS) ../json_utils.cpp

objects_json.o : ../objects_json.cpp ../objects_json.h ../json_utils.h ../structs.h
	$(CXX) -c $(CXXFLAGS) ../objects_json.cpp

clock.o : ../clock.cpp ../clock.h
	$(CXX) -c $(CXXFLAGS) ../clock.cpp

comm.o : ../comm.cpp ../structs.h ../utils.h ../comm.h ../interpre.h ../handler.h ../db.h \
	../limits.h ../clock.h
	$(CXX) -c $(CXXFLAGS) $(COMMFLAGS) ../comm.cpp
char_utils.o : ../char_utils.cpp ../base_utils.h ../char_utils.h ../object_utils.h \
	../environment_utils.h ../structs.h ../handler.h
	$(CXX) -c $(CXXFLAGS) ../char_utils.cpp
environment_utils.o : ../environment_utils.cpp ../structs.h ../environment_utils.h
	$(CXX) -c $(CXXFLAGS) ../environment_utils.cpp
object_utils.o : ../object_utils.cpp ../structs.h ../object_utils.h
	$(CXX) -c $(CXXFLAGS) ../object_utils.cpp
char_utils_combat.o : ../char_utils_combat.cpp ../structs.h ../base_utils.h ../char_utils.h ../object_utils.h  ../comm.h ../char_utils_combat.h
	$(CXX) -c $(CXXFLAGS) ../char_utils_combat.cpp
combat_manager.o : ../combat_manager.cpp ../combat_manager.h ../base_utils.h
	$(CXX) -c $(CXXFLAGS) ../combat_manager.cpp
big_brother.o : ../big_brother.cpp ../big_brother.h ../base_utils.h ../singleton.h
	$(CXX) -c $(CXXFLAGS) ../big_brother.cpp
battle_mage_handler.o : ../battle_mage_handler.cpp ../warrior_spec_handlers.h ../structs.h
	$(CXX) -c $(CXXFLAGS) ../battle_mage_handler.cpp
wild_fighting_handler.o : ../wild_fighting_handler.cpp ../warrior_spec_handlers.h ../structs.h
	$(CXX) -c $(CXXFLAGS) ../wild_fighting_handler.cpp
weapon_master_handler.o : ../weapon_master_handler.cpp ../warrior_spec_handlers.h ../structs.h
	$(CXX) -c $(CXXFLAGS) ../weapon_master_handler.cpp
skill_timer.o : ../skill_timer.cpp ../skill_timer.h ../singleton.h
	$(CXX) -c $(CXXFLAGS) ../skill_timer.cpp
olog_hai.o : ../olog_hai.cpp ../structs.h ../utils.h ../comm.h ../interpre.h ../handler.h \
	../db.h ../spells.h ../limits.h
	$(CXX) -c $(CXXFLAGS) ../olog_hai.cpp
delayed_command_interpreter.o : ../delayed_command_interpreter.cpp ../delayed_command_interpreter.h
	$(CXX) -c $(CXXFLAGS) ../delayed_command_interpreter.cpp
wait_functions.o : ../wait_functions.cpp ../structs.h ../base_utils.h ../char_utils.h ../object_utils.h  ../comm.h
	$(CXX) -c $(CXXFLAGS) ../wait_functions.cpp
act_comm.o : ../act_comm.cpp ../structs.h ../utils.h ../comm.h ../interpre.h ../handler.h \
	../db.h
	$(CXX) -c $(CXXFLAGS) ../act_comm.cpp
act_info.o : ../act_info.cpp ../structs.h ../utils.h ../comm.h ../interpre.h \
	../handler.h ../db.h ../spells.h ../limits.h
	$(CXX) -c $(CXXFLAGS) ../act_info.cpp
act_move.o : ../act_move.cpp ../structs.h ../utils.h ../comm.h ../interpre.h \
	../handler.h ../db.h ../spells.h
	$(CXX) -c $(CXXFLAGS) ../act_move.cpp
act_obj1.o : ../act_obj1.cpp ../structs.h ../utils.h ../comm.h ../interpre.h ../handler.h \
	../db.h ../spells.h
	$(CXX) -c $(CXXFLAGS) ../act_obj1.cpp
act_obj2.o : ../act_obj2.cpp ../structs.h ../utils.h ../comm.h ../interpre.h ../handler.h \
	../db.h ../spells.h ../limits.h
	$(CXX) -c $(CXXFLAGS) ../act_obj2.cpp
act_offe.o : ../act_offe.cpp ../structs.h ../utils.h ../comm.h ../interpre.h \
	../handler.h ../db.h ../spells.h ../limits.h
	$(CXX) -c $(CXXFLAGS) ../act_offe.cpp
act_othe.o : ../act_othe.cpp ../structs.h ../utils.h ../comm.h ../interpre.h ../handler.h \
	../db.h ../spells.h ../limits.h
	$(CXX) -c $(CXXFLAGS) ../act_othe.cpp
act_soci.o : ../act_soci.cpp ../structs.h ../utils.h ../comm.h ../interpre.h \
	../handler.h ../db.h ../spells.h
	$(CXX) -c $(CXXFLAGS) ../act_soci.cpp
act_wiz.o : ../act_wiz.cpp ../structs.h ../utils.h ../comm.h ../interpre.h \
	../handler.h ../db.h ../spells.h ../limits.h ../profs.h
	$(CXX) -c $(CXXFLAGS) ../act_wiz.cpp
handler.o : ../handler.cpp ../structs.h ../utils.h ../comm.h ../db.h ../handler.h ../interpre.h
	$(CXX) -c $(CXXFLAGS) ../handler.cpp
db.o : ../db.cpp ../structs.h ../utils.h ../db.h ../comm.h ../handler.h ../limits.h ../spells.h \
        ../interpre.h ../big_brother.h ../skill_timer.h
	$(CXX) -c $(CXXFLAGS) ../db.cpp
ban.o : ../ban.cpp ../structs.h ../utils.h ../comm.h ../interpre.h ../handler.h ../db.h
	$(CXX) -c $(CXXFLAGS) ../ban.cpp
interpre.o : ../interpre.cpp ../structs.h ../comm.h ../interpre.h ../db.h ../utils.h \
	../limits.h ../spells.h ../handler.h ../profs.h
	$(CXX) -c $(CXXFLAGS) ../interpre.cpp
utility.o : ../utility.cpp ../structs.h ../utils.h ../comm.h
	$(CXX) -c $(CXXFLAGS) ../utility.cpp
spec_ass.o : ../spec_ass.cpp ../structs.h ../db.h ../interpre.h ../utils.h
	$(CXX) -c $(CXXFLAGS) ../spec_ass.cpp
spec_pro.o : ../spec_pro.cpp ../structs.h ../utils.h ../comm.h ../interpre.h \
	../handler.h ../db.h ../spells.h ../limits.h
	$(CXX) -c $(CXXFLAGS) ../spec_pro.cpp
limits.o : ../limits.cpp ../structs.h ../limits.h ../utils.h ../spells.h ../comm.h ../db.h ../handler.h          ../profs.h
	$(CXX) -c $(CXXFLAGS) ../limits.cpp
fight.o	: ../fight.cpp ../structs.h ../utils.h ../comm.h ../handler.h ../interpre.h ../db.h ../spells.h ../limits.h
	$(CXX) -c $(CXXFLAGS) ../fight.cpp
weather.o : ../weather.cpp ../structs.h ../utils.h ../comm.h ../handler.h ../interpre.h ../db.h
	$(CXX) -c $(CXXFLAGS) ../weather.cpp
shop.o : ../shop.cpp ../structs.h ../comm.h ../handler.h ../db.h ../interpre.h ../utils.h
	$(CXX) -c $(CXXFLAGS) ../shop.cpp
mystic.o : ../mystic.cpp ../structs.h ../utils.h ../comm.h ../spells.h ../handler.h ../limits.h \
	../interpre.h ../db.h
	$(CXX) -c $(CXXFLAGS) ../mystic.cpp
mage.o  : ../mage.cpp ../structs.h ../utils.h ../comm.h ../spells.h ../handler.h ../limits.h \
        ../interpre.h ../db.h
	${CXX} -c ${CXXFLAGS} ../mage.cpp
spell_pa.o : ../spell_pa.cpp ../structs.h ../utils.h ../comm.h ../db.h ../interpre.h \
	../spells.h ../handler.h
	$(CXX) -c $(CXXFLAGS) ../spell_pa.cpp
mobact.o : ../mobact.cpp ../utils.h ../structs.h ../db.h ../comm.h ../interpre.h ../handler.h
	$(CXX) -c $(CXXFLAGS) ../mobact.cpp
modify.o : ../modify.cpp ../structs.h ../utils.h ../interpre.h ../handler.h ../db.h ../comm.h
	$(CXX) -c $(CXXFLAGS) ../modify.cpp
consts.o : ../consts.cpp ../structs.h ../limits.h
	$(CXX) -c $(CXXFLAGS) ../consts.cpp
objsave.o : ../objsave.cpp ../structs.h ../comm.h ../handler.h ../db.h ../interpre.h \
	../utils.h ../spells.h
	$(CXX) -c $(CXXFLAGS) ../objsave.cpp
boards.o : ../boards.cpp ../structs.h ../utils.h ../comm.h ../db.h ../boards.h ../interpre.h \
	../handler.h ../json_utils.h
	$(CXX) -c $(CXXFLAGS) ../boards.cpp
signals.o : ../signals.cpp ../utils.h ../structs.h
	$(CXX) -c $(CXXFLAGS) ../signals.cpp
graph.o : ../graph.cpp ../structs.h ../utils.h ../comm.h ../interpre.h ../handler.h ../db.h \
	../spells.h
	$(CXX) -c $(CXXFLAGS) ../graph.cpp
config.o : ../config.cpp ../structs.h
	$(CXX) -c $(CXXFLAGS) ../config.cpp
ranger.o : ../ranger.cpp ../structs.h ../utils.h ../comm.h ../interpre.h ../handler.h ../db.h\
	 ../spells.h ../script.h
	 $(CXX) -c $(CXXFLAGS) ../ranger.cpp
script.o : ../script.cpp ../structs.h ../utils.h ../comm.h ../interpre.h ../protos.h ../script.h
	$(CXX) -c $(CXXFLAGS) ../script.cpp
shapemob.o : ../shapemob.cpp ../structs.h ../utils.h ../comm.h ../interpre.h ../protos.h
	$(CXX) -c $(CXXFLAGS) ../shapemob.cpp
shapeobj.o : ../shapeobj.cpp ../structs.h ../utils.h ../comm.h ../interpre.h ../protos.h
	$(CXX) -c $(CXXFLAGS) ../shapeobj.cpp
shaperom.o : ../shaperom.cpp ../structs.h ../utils.h ../comm.h ../interpre.h ../protos.h
	$(CXX) -c $(CXXFLAGS) ../shaperom.cpp
shapezon.o : ../shapezon.cpp ../structs.h ../utils.h ../comm.h ../interpre.h ../protos.h
	$(CXX) -c $(CXXFLAGS) ../shapezon.cpp
shapemdl.o : ../shapemdl.cpp ../structs.h ../utils.h ../comm.h ../interpre.h ../protos.h
	$(CXX) -c $(CXXFLAGS) ../shapemdl.cpp
shapescript.o : ../shapescript.cpp ../structs.h ../utils.h ../comm.h ../interpre.h ../protos.h
	$(CXX) -c $(CXXFLAGS) ../shapescript.cpp
mudlle.o   : ../mudlle.cpp ../structs.h ../utils.h ../comm.h ../interpre.h ../protos.h ../mudlle.h
	$(CXX) -c $(CXXFLAGS) ../mudlle.cpp
mudlle2.o   : ../mudlle2.cpp ../structs.h ../utils.h ../comm.h ../interpre.h ../protos.h ../mudlle.h
	$(CXX) -c $(CXXFLAGS) ../mudlle2.cpp
profs.o   : ../profs.cpp ../structs.h ../utils.h ../comm.h ../interpre.h ../limits.h ../profs.h ../comm.h
	$(CXX) -c $(CXXFLAGS) ../profs.cpp
clerics.o : ../clerics.cpp ../structs.h ../utils.h ../comm.h ../handler.h ../interpre.h ../db.h ../spells.h ../limits.h
	$(CXX) -c $(CXXFLAGS) ../clerics.cpp
mail.o    : ../mail.cpp ../structs.h ../utils.h ../comm.h ../interpre.h ../db.h ../handler.h ../mail.h ../json_utils.h
	$(CXX) -c $(CXXFLAGS) ../mail.cpp
zone.o: ../zone.cpp ../zone.h ../structs.h ../utils.h
	$(CXX) -c $(CXXFLAGS) ../zone.cpp
color.o: ../color.cpp ../color.h
	$(CXX) -c $(CXXFLAGS) ../color.cpp
obj2html.o: ../obj2html.cpp
	$(CXX) -c $(CXXFLAGS) ../obj2html.cpp
mob_csv_extract.o: ../mob_csv_extract.cpp ../mob_csv_extract.h
	$(CXX) -c $(CXXFLAGS) ../mob_csv_extract.cpp
convert_plrobjs.o: ../convert_plrobjs.cpp ../convert_plrobjs.h ../objects_json.h
	$(CXX) -c $(CXXFLAGS) ../convert_plrobjs.cpp
convert_exploits.o: ../convert_exploits.cpp ../convert_exploits.h ../exploits_json.h
	$(CXX) -c $(CXXFLAGS) ../convert_exploits.cpp
pkill.o: ../pkill.cpp ../pkill.h ../json_utils.h
	$(CXX) -c $(CXXFLAGS) ../pkill.cpp
protocol.o: ../protocol.cpp ../protocol.h
	$(CXX) -c $(CXXFLAGS) ../protocol.cpp
rots_rng.o : ../rots_rng.cpp ../rots_rng.h
	$(CXX) -c $(CXXFLAGS) ../rots_rng.cpp
rots_crypt.o : ../rots_crypt.cpp ../rots_crypt.h
	$(CXX) -c $(CXXFLAGS) ../rots_crypt.cpp
rots_net.o : ../rots_net.cpp ../rots_net.h ../platdef.h
	$(CXX) -c $(CXXFLAGS) ../rots_net.cpp


SRCS = account_management_tests.cpp account_cache_tests.cpp account_management_assets_header_compile.cpp account_management_identity_header_compile.cpp account_management_migration_header_compile.cpp account_management_presentation_header_compile.cpp account_management_storage_header_compile.cpp account_management_types_header_compile.cpp act_wiz_tests.cpp battle_mage_handler_tests.cpp CharPlayerDataBuilder.cpp boards_json_tests.cpp characterization_combat_tests.cpp characterization_json_tests.cpp character_json_tests.cpp char_utils_tests.cpp clock_tests.cpp color_tests.cpp convert_exploits_tests.cpp convert_plrobjs_tests.cpp cpp20_format_smoke_tests.cpp crashsave_schedule_tests.cpp damage_tests.cpp db_loader_tests.cpp exploits_json_tests.cpp fight_proc_tests.cpp interpre_account_menu_tests.cpp json_perf_tests.cpp json_utils_tests.cpp llp64_probe_tests.cpp mage_tests.cpp mail_json_tests.cpp ObjFlagDataBuilder.cpp objects_json_layout_tests.cpp objects_json_tests.cpp objsave_json_tests.cpp olog_hai_tests.cpp obj_flag_data_tests.cpp object_utils_tests.cpp player_finalize_tests.cpp pod_persistence_json_tests.cpp protocol_tests.cpp rots_asprintf_tests.cpp rots_crypt_tests.cpp rots_net_tests.cpp rots_rng_tests.cpp save_benchmark_tests.cpp shapemob_tests.cpp spell_pa_tests.cpp startup_options_tests.cpp test_random_utils.cpp test_random_utils_tests.cpp weapon_master_handler_tests.cpp gtest_main.cpp

OBJS = $(SRCS:.cpp=.o)
EXECUTABLE = ../../bin/tests

# Test TUs build via the generic .cpp.o rule, which knows nothing about
# headers; without this line, editing the shared fixture header leaves stale
# objects behind (make considers the .o up to date) and the two suites can
# silently run different fixture code.
characterization_combat_tests.o damage_tests.o : damage_test_context.h
characterization_json_tests.o : ../character_json.h
objects_json_layout_tests.o convert_plrobjs_tests.o : ../objects_json.h legacy_rent_fixture.h
objsave_json_tests.o : ../objects_json.h
convert_plrobjs_tests.o : ../convert_plrobjs.h
convert_exploits_tests.o : ../convert_exploits.h ../exploits_json.h
boards_json_tests.o : ../boards.h
mail_json_tests.o : ../mail.h
pod_persistence_json_tests.o : ../pkill.h ../db.h ../exploits_json.h ../account_management.h
account_cache_tests.o : ../account_cache.h ../account_management.h
crashsave_schedule_tests.o : ../crashsave_schedule.h
clock_tests.o : ../clock.h
json_perf_tests.o : ../json_utils.h ../character_json.h
player_finalize_tests.o : ../player_file_finalize.h
save_benchmark_tests.o : ../save_benchmark.h ../account_cache.h ../account_management.h ../character_json.h

tests: $(EXECUTABLE)

$(EXECUTABLE): $(OBJFILES) $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJFILES) $(OBJS) -o $(EXECUTABLE) $(LDFLAGS)

.cpp.o:
	$(CXX) $(TEST_CXXFLAGS) -c $< -o $@

clean:
	rm -f *.o $(EXECUTABLE)

ageland: ../bin/ageland

../bin/ageland: $(OBJFILES)

all: ageland $(EXECUTABLE)

run: $(EXECUTABLE)
	$(EXECUTABLE)
