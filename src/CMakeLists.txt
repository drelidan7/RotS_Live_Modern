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
# char_utils_combat.cpp IS listed: it defines live functions used by
# fight.cpp / clerics.cpp / ranger.cpp (its dead OB/PB/DB helpers were deleted in the same wave).
#
# account_management.cpp / character_json.cpp / exploits_json.cpp / json_utils.cpp /
# objects_json.cpp arrive with the account-management merge (JSON persistence +
# accounts); the other account_management_*.cpp files are #included into
# account_management.cpp, not compiled separately.
#
# rots_crypt.cpp is a vendored, portable SHA-512-crypt ($6$) — account_management*.cpp
# call rots_crypt() instead of libc crypt() so credential hashes are byte-identical
# on every platform, not just glibc.
set(ROTS_SERVER_SOURCES
    account_cache.cpp account_management.cpp act_comm.cpp act_info.cpp act_move.cpp act_obj1.cpp
    act_obj2.cpp act_offe.cpp act_othe.cpp act_soci.cpp
    act_wiz.cpp ban.cpp battle_mage_handler.cpp big_brother.cpp
    boards.cpp char_utils.cpp char_utils_combat.cpp character_json.cpp clerics.cpp
    clock.cpp color.cpp comm.cpp config.cpp convert_exploits.cpp convert_plrobjs.cpp
    consts.cpp crashsave_schedule.cpp db.cpp delayed_command_interpreter.cpp exploits_json.cpp environment_utils.cpp
    fight.cpp graph.cpp handler.cpp interpre.cpp
    json_utils.cpp limits.cpp mage.cpp mail.cpp mob_csv_extract.cpp
    mobact.cpp modify.cpp mudlle.cpp mudlle2.cpp
    mystic.cpp obj2html.cpp object_utils.cpp objects_json.cpp objsave.cpp
    olog_hai.cpp pkill.cpp player_file_finalize.cpp profs.cpp protocol.cpp
    ranger.cpp rots_crypt.cpp rots_net.cpp rots_rng.cpp safe_template.cpp save_benchmark.cpp savebench.cpp script.cpp shapemdl.cpp shapemob.cpp
    shapeobj.cpp shaperom.cpp shapescript.cpp shapezon.cpp
    shop.cpp signals.cpp skill_timer.cpp spec_ass.cpp
    spec_pro.cpp spell_pa.cpp utility.cpp wait_functions.cpp
    weapon_master_handler.cpp weather.cpp wild_fighting_handler.cpp zone.cpp
)

add_executable(ageland ${ROTS_SERVER_SOURCES})

# C++20, ISO dialect (-std=c++20, not gnu++20) to match the Makefile's -std=c++20.
target_compile_features(ageland PRIVATE cxx_std_20)
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
target_compile_options(ageland PRIVATE
    $<$<BOOL:${ROTS_LEGACY_32BIT}>:-m32>
    $<${ROTS_GNULIKE}:-funsigned-char -fstrict-aliasing -Wall -Wextra -Werror -g>
    $<$<CXX_COMPILER_ID:MSVC>:/J /W4 /WX /wd4244 /wd4267 /wd4456 /wd4458 /wd4459 /wd4702 /wd4127 /wd4310 /Zc:strictStrings->
)
target_link_options(ageland PRIVATE
    $<$<BOOL:${ROTS_LEGACY_32BIT}>:-m32>
    # -rdynamic: GNU spelling; the clang driver translates it on macOS. MSVC: none.
    $<${ROTS_GNULIKE}:-rdynamic>
)
# ws2_32: the Winsock2 import library backing every socket call rots_net.cpp /
# comm.cpp make on Windows (WSAStartup, socket/bind/listen/accept, send/recv,
# select, ...). POSIX needs no equivalent (BSD sockets live in libc). Not a
# third-party library -- it is the OS's own socket API, same standing as libc.
target_link_libraries(ageland PRIVATE $<$<PLATFORM_ID:Windows>:ws2_32>)
# _FILE_OFFSET_BITS is a glibc knob; harmless but meaningless off Linux/MSVC.
# _CRT_SECURE_NO_WARNINGS (Phase 3 Task 5, MSVC bring-up): the game calls plain
# localtime()/asctime()/strcpy()-family CRT functions all over (db.cpp, utility.cpp,
# etc.); MSVC's CRT deprecates those in favor of the *_s() bounds-checked variants
# and turns the deprecation into a hard warning (effectively an error under /W-heavy
# configs) without this. A real localtime_s()/strcpy_s() conversion is out of scope
# for this phase (Phase 5 owns warning hygiene per the plan) -- this only silences
# the deprecation so the existing, behaviorally-unchanged calls compile.
# _CRT_NONSTDC_NO_WARNINGS (Phase 5 Task 9, /W4 campaign): companion knob for
# the POSIX-name deprecations (C4996 on strdup/unlink/getpid/...); MSVC wants
# the underscore-prefixed ISO names, but the plain POSIX spellings are the
# portable ones every other platform uses. Same silence-not-rewrite rationale
# as _CRT_SECURE_NO_WARNINGS above.
target_compile_definitions(ageland PRIVATE
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:_FILE_OFFSET_BITS=64>
    $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS>
)

# Per-configuration optimization (the common flags above apply to every config).
target_compile_options(ageland PRIVATE
    $<${ROTS_GNULIKE}:$<$<CONFIG:Debug>:-O0 -DDEBUG>>
    $<${ROTS_GNULIKE}:$<$<CONFIG:Release>:-O2 -DNDEBUG>>
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
        tests/exploits_json_tests.cpp
        tests/fight_proc_tests.cpp
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
        tests/player_finalize_tests.cpp
        tests/pod_persistence_json_tests.cpp
        tests/protocol_tests.cpp
        tests/rots_asprintf_tests.cpp
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

    add_executable(ageland_tests ${ROTS_SERVER_SOURCES} ${ROTS_TEST_SOURCES})

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
    # list has an "act_info.h", "structs.h", etc., so ordering never mattered
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
    target_compile_options(ageland_tests PRIVATE
        $<$<BOOL:${ROTS_LEGACY_32BIT}>:-m32>
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
    if(ROTS_SUPPRESS_TEST_WARNINGS)
        target_compile_options(ageland_tests PRIVATE $<${ROTS_GNULIKE}:-w>)
    else()
        target_compile_options(ageland_tests PRIVATE $<${ROTS_GNULIKE}:-Wall -Wextra -Werror>)
    endif()
    # SSE float math for the tests (mirrors src/tests/Makefile): i386 g++ defaults to
    # x87 extended-precision FP, which rounds differently from the SSE math of every
    # x86-64 build; several tests pin exact FP thresholds authored under SSE semantics.
    # No-op on x86-64 hosts (SSE is already the default there).
    # -msse2/-mfpmath=sse are x86-only spellings — gcc/clang reject them outright on
    # arm64 ("unknown FP unit 'sse'"), so gate on the target processor too. The
    # Linux-container src/tests/Makefile path stays x86-only already (it's the -m32
    # legacy ABI), so it doesn't need — and doesn't get — this guard.
    target_compile_options(ageland_tests PRIVATE
        $<$<AND:${ROTS_GNULIKE},${ROTS_X86}>:-msse2 -mfpmath=sse>
    )
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
