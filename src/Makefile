# RotS server build (32-bit Linux; see docs/BUILD.md and CLAUDE.md).
#
# The flags below are MANDATORY and must not be dropped — they affect the ABI and
# runtime behavior, so the binary must keep building with them:
#   -m32             32-bit build (save-file format / ABI); built in the i386 container
#   -funsigned-char  the code assumes `char` is unsigned
#   -fstrict-aliasing  matches the historical build
#   -std=c++20       (Phase 4 Wave 1; previously -std=c++17, and before that
#     the identical-under-GCC -std=c++1z spelling)
#   -D_FILE_OFFSET_BITS=64  32-bit glibc's legacy readdir()/stat() use a 32-bit
#     ino_t/off_t; against modern filesystems with 64-bit inode numbers (ext4,
#     overlay2 — e.g. the Docker container's own /tmp and lib/ trees) readdir()
#     fails with EOVERFLOW on any directory containing such an inode. This macro
#     switches libc to the 64-bit-safe variants transparently; it only affects
#     libc's own struct stat/dirent, not any of the game's own on-disk structs
#     (char_file_u, obj_file_elem, ...), so it does not change save-file format.
# Warnings are treated as errors (-Wall -Wextra -Werror) on GNU-family compilers
# (Phase 5 Task 8): the tree reached a zero warning census under Tasks 1-7, so the
# historical blanket -w suppression is gone. User CPPFLAGS/CXXFLAGS/LDFLAGS/LIBS are
# appended to — never replace — the required flags. See docs/BUILD.md's "Warnings
# policy" section for the pinned-toolchain rationale; the i386 (-m32, g++14
# container) leg of this Makefile itself only gets exercised under -Werror at
# Phase 5 Task 10 (finalization) — any target-specific warning there is fixed then.
#

CXX      ?= g++
BIN       = ../bin/ageland

# Intermediate build artifacts (.o and auto-generated .d) go here, out of the
# source tree — kept beside ../bin rather than mixed in with the .cpp files.
OBJDIR    = ../intermediate

# --- Flags -------------------------------------------------------------------
# REQ_* hold the mandatory flags so an environment override of CXXFLAGS/LDFLAGS
# can add to, but cannot strip, them (e.g. -m32 is always present).
# -msse2 -mfpmath=sse -ffp-contract=off: shared deterministic-FP policy (Phase 1
# FP unification; see docs/BUILD.md "FP determinism" and src/tests/Makefile /
# src/CMakeLists.txt's ROTS_FP_OPTIONS) -- flips this i386 shipping build from
# x87 to the same SSE math ageland_tests has always used, and bans FMA fusion.
REQ_CXXFLAGS = -m32 -std=c++20 -Wall -Wextra -Werror -fstrict-aliasing -funsigned-char -g -D_FILE_OFFSET_BITS=64 -msse2 -mfpmath=sse -ffp-contract=off
REQ_LDFLAGS  = -m32 -rdynamic

# -MMD -MP: emit a per-object header-dependency file (.d) plus phony targets for
# each header, so editing a header rebuilds exactly the objects that include it and
# deleting/renaming one never breaks the build.
DEPFLAGS = -MMD -MP

ALL_CPPFLAGS = $(DEPFLAGS) -Icore/include -Ipersist/include -Iplatform/include $(CPPFLAGS)
ALL_CXXFLAGS = $(REQ_CXXFLAGS) $(CXXFLAGS)
ALL_LDFLAGS  = $(REQ_LDFLAGS) $(LDFLAGS)

# --- Sources -----------------------------------------------------------------
# combat_manager.cpp was proven-dead code and was DELETED in Phase 4 Wave 1 (the
# live melee path is fight.cpp::hit() — see AGENTS.md's dead-code section).
# NOTE: char_utils_combat.cpp IS built — it defines live functions used by
# fight.cpp / clerics.cpp / ranger.cpp (its dead OB/PB/DB helpers were deleted in
# the same wave). Keep this list in sync with the source list in CMakeLists.txt.
OBJNAMES = account_cache.o account_management.o act_comm.o act_info.o act_move.o act_obj1.o act_obj2.o act_offe.o \
	act_othe.o act_soci.o act_wiz.o ban.o battle_mage_handler.o big_brother.o boards.o char_utils.o char_utils_combat.o character_json.o clerics.o clock.o color.o color_convert.o \
	comm.o config.o consts.o output_seam.o convert_exploits.o convert_plrobjs.o crashsave_schedule.o db_boot.o db_players.o db_world.o delayed_command_interpreter.o entity_lifecycle.o exploits_json.o fight.o graph.o handler.o interpre.o environment_utils.o \
	json_utils.o limits.o mail.o mystic.o mage.o mobact.o modify.o mudlle.o mudlle2.o mob_csv_extract.o obj2html.o object_utils.o objects_json.o objsave.o olog_hai.o \
	pkill.o pkill_json.o player_file_finalize.o profs.o protocol.o ranger.o rots_crypt.o rots_log.o rots_net.o rots_rng.o rots_util.o safe_template.o save_benchmark.o savebench.o script.o shapemdl.o shapemob.o shapeobj.o shaperom.o shapezon.o shapescript.o shop.o \
	signals.o skill_timer.o spec_ass.o spec_pro.o spell_pa.o utility.o wait_functions.o weapon_master_handler.o \
	wild_fighting_handler.o weather.o zone.o

# Objects (and their .d files) live under $(OBJDIR), not next to the sources.
OBJFILES = $(addprefix $(OBJDIR)/, $(OBJNAMES))
DEPFILES = $(OBJFILES:.o=.d)

# --- Targets -----------------------------------------------------------------
.DEFAULT_GOAL := all

.PHONY: all ageland clean format setup run

all: $(BIN)

# Backwards-compatible alias (older docs/scripts invoke `make ageland`).
ageland: $(BIN)

# Link step. Rotate any existing binary to <bin>~ first (historical behavior).
$(BIN): $(OBJFILES)
	@mkdir -p $(@D)
	@if [ -f $@ ]; then mv -f $@ $@~; fi
	$(CXX) $(ALL_LDFLAGS) -o $@ $(OBJFILES) $(LIBS)

# One pattern rule builds every object into $(OBJDIR); the directory is created on
# demand, and header prerequisites come from the .d files included at the bottom.
# With -o pointing into $(OBJDIR), -MMD writes each .d alongside its .o there too.
$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(ALL_CPPFLAGS) $(ALL_CXXFLAGS) -c $< -o $@

# Remove the intermediate dir; also sweep any stray objects an older in-tree build left in src/.
clean:
	rm -rf $(OBJDIR)
	rm -f *.o *.d

# NOTE (2026-07-09 upstream/account-management merge): upstream's Makefile still
# carries a hand-maintained "Dependencies for the main mud" block of per-object
# rules using $(CC)/$(CFLAGS) (neither defined in this Makefile). This repo's
# Makefile was deliberately modernized (2994a77) to drop that block in favor of
# compiler-generated header dependencies (-MMD -MP -> $(DEPFILES), included at
# the bottom of this file) driven by the single generic pattern rule above.
# Reintroducing upstream's block verbatim would silently shadow that pattern
# rule for every object it lists and build them with undefined $(CC)/$(CFLAGS)
# instead of $(CXX)/$(ALL_CXXFLAGS) — dropping the mandatory -m32/-std=c++20/etc.
# flags for exactly those objects. Intentionally not carried forward; the new
# account/savebench/finalize objects get correct dependency tracking for free
# from the existing pattern rule + auto-generated .d files.

format:
	clang-format -i -style=WebKit *.cpp
	clang-format -i -style=WebKit *.h

setup:
	@mkdir -p ../bin ../log ../lib/misc \
		../lib/players/A-E \
		../lib/players/F-J \
		../lib/players/K-O \
		../lib/players/P-T \
		../lib/players/U-Z \
		../lib/players/ZZZ \
		../lib/accounts/A-E \
		../lib/accounts/F-J \
		../lib/accounts/K-O \
		../lib/accounts/P-T \
		../lib/accounts/U-Z \
		../lib/accounts/ZZZ \
		../lib/account_characters/A-E \
		../lib/account_characters/F-J \
		../lib/account_characters/K-O \
		../lib/account_characters/P-T \
		../lib/account_characters/U-Z \
		../lib/account_characters/ZZZ \
		../lib/plrobjs/A-E \
		../lib/plrobjs/F-J \
		../lib/plrobjs/K-O \
		../lib/plrobjs/P-T \
		../lib/plrobjs/U-Z \
		../lib/plrobjs/ZZZ \
		../lib/exploits/A-E \
		../lib/exploits/F-J \
		../lib/exploits/K-O \
		../lib/exploits/P-T \
		../lib/exploits/U-Z \
		../lib/exploits/ZZZ \
		../mobs
	@touch \
		../log/lastdeath \
		../lib/last_cmds \
		../lib/misc/pklist

run: $(BIN) setup
	@cd .. && ./bin/ageland -p 3791

# Auto-generated header dependencies (silently ignored when absent, e.g. first build).
-include $(DEPFILES)
