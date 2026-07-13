# Phase 5 — Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Zero warnings under `-Werror` on all four platforms (`/W4 /WX` on MSVC), ASan/UBSan CI jobs green and required, `.clang-tidy` checked in — with byte-identical observable behavior throughout.

**Architecture:** Nine-task category ladder (real-bug warning classes first → mechanical classes → the const/`string_view` campaign → tail → sanitizer CI → clang-tidy → GNU `-Werror` flip → MSVC `/W4` campaign last). Behavior gate is the existing suite + goldens; progress gate is the per-flag census reaching zero on AppleClang AND g++14 before the flip.

**Tech Stack:** C++20, AppleClang 21 / g++14 / MSVC, GitHub Actions CI, ASan/UBSan.

**Spec:** `docs/superpowers/specs/2026-07-12-phase-5-hardening-design.md`. **Branch:** `modernization/phase-5-hardening` off master (`2b63d21` or later).

## Global Constraints

Every task's requirements implicitly include this section.

- **Zero sanctioned golden changes** (boot / combat / JSON — STOP-on-diff). **Byte-identical observable behavior**, except individually disclosed fixes on provably-broken/UB paths (Task 1 dispositions). **RNG discipline:** no `number()`/`rots_rng` call added/removed/moved.
- **No third-party libraries.** `-funsigned-char` stays pinned everywhere (GNU `-funsigned-char`, MSVC `/J`) — Task 1's `-Wchar-subscripts` fixes must respect that chars are unsigned on every platform.
- **Per-task DUAL local gate** (macOS native + rots64: full ctest + boot goldens, commands as in Wave 4's plan §Dual local gate). i386 battery + four-platform CI at finalization only. Background >10-min container steps and stop with a note.
- **Census procedure (used by every category task):** from `src/`: `rm -rf ../build/warn-probe && cmake --preset macos-arm64 -B ../build/warn-probe -DROTS_SUPPRESS_TEST_WARNINGS=OFF > /dev/null 2>&1 && cmake --build ../build/warn-probe --target ageland_tests -j4 > /tmp/census.log 2>&1; grep -oE "\[-W[a-z-]+\]" /tmp/census.log | sort | uniq -c | sort -rn` — run from the REPO's src/ (presets live there). g++14 census: grep the rots64 build log the same way (warning tags are `[-Wname]` there too). Record per-flag counts in every task report.
- **Suppression discipline:** a warning may be suppressed (pragma / `/wd` / attribute) ONLY with an adjacent comment stating why the code is intentional, plus a ledger entry in the task report. Blanket `-Wno-*` flags are allowed only where this plan names them explicitly.
- **Standing fixture/test rules** from Phase 4 (in-place descriptor resets, value-init, content-emptiness checks, platform shims, char[N]→std::format casts, `make format` scope discipline, clang-format-hook recovery, qemu kill+rerun) all apply.
- **Transform idiom catalog** (Wave 3 plan §catalog) applies verbatim wherever this phase converts sprintf-family sites (Task 3).
- **string_view boundary rule (Task 4):** tables → `const char* const[]`; parameters/locals consumed by length-aware C++ code → `std::string_view`; NUL-terminated C-string consumers keep `const char*`; NEVER pass `string_view::data()` to an API assuming NUL termination.

---

### Task 1: Real-bug warning classes (~140 instances)

**Files:** Modify: whatever the census names for `-Warray-bounds` (51), `-Wfortify-source` (51), `-Wchar-subscripts` (9, e.g. `src/shapemob.cpp:1709`, `src/shapeobj.cpp:1629`), `-Wdangling-else` (19, e.g. `src/fight.cpp:565`), `-Wlogical-op-parentheses` (14), `-Wparentheses-equality` (6, e.g. `src/act_wiz.cpp:179`), `-Wundefined-var-template` (38).

- [ ] **Step 1: Extract the full instance list** from the census log (file:line per flag). Build a disposition table BEFORE changing anything.
- [ ] **Step 2: Disposition each instance.** Latent bug → minimal fix (a behavior change on a broken path is a DISCLOSED DELTA with the reasoning recorded); intentional → restructure so the compiler sees intent (explicit braces for dangling-else, explicit parens, `int` cast for char subscripts — safe under `-funsigned-char` but make the cast explicit: `static_cast<unsigned char>(c)`), or last-resort pragma with comment. `-Warray-bounds`/`-Wfortify-source` overlaps with real overflow bugs — treat each as guilty until proven benign; anything you can't prove benign gets escalated in your report, not guessed. `-Wundefined-var-template` is likely one template pattern repeated 38× — fix the declaration once.
- [ ] **Step 3: Census re-run** — all seven flags at ZERO on AppleClang; grep the rots64 build log for the same flags (g++ names may differ: `-Wparentheses` covers two clang flags) — zero there too.
- [ ] **Step 4: Dual local gate.** Full suite + boot goldens green ×2; combat goldens explicitly green (fight.cpp touched).
- [ ] **Step 5: Commit** `fix: resolve real-bug warning classes (Phase 5 T1)` with the disposition table in the body.

### Task 2: -Wunused-parameter (~1,154)

**Files:** Modify: `src/interpre.h:27` (ACMD macro), `:36` (SPECIAL macro), stragglers per census.

- [ ] **Step 1:** Add `[[maybe_unused]]` to every parameter in the ACMD and SPECIAL macro parameter lists (read the current definitions first; the attribute goes on each declared parameter). Rebuild; census — expect the count to collapse (most do_* functions ignore `cmd`/`wait_list`).
- [ ] **Step 2:** Remaining instances: prefer unnamed parameters (`char_data*` instead of `char_data* ch`) when the name is truly unused; `[[maybe_unused]]` where the parameter is used only in some `#ifdef`/platform branch.
- [ ] **Step 3:** Census zero for the flag on both compilers. **Step 4:** Dual gate. **Step 5:** Commit `fix: eliminate -Wunused-parameter via macro [[maybe_unused]] (Phase 5 T2)`.

### Task 3: -Wdeprecated-declarations = finish the sprintf conversion tree-wide (~428 sites)

All 428 are AppleClang deprecating `sprintf` itself — a census of the modules no Phase 4 wave targeted: objsave.cpp 39, protocol.cpp 31, act_obj1.cpp 31, spec_pro.cpp 29, act_move.cpp 28, shop.cpp 26, act_comm.cpp 23, boards.cpp 18, interpre.cpp 16, ranger.cpp 15, mudlle.cpp 15, act_info.cpp 15 (Wave 3's DOCUMENTED justified skips), handler.cpp 14, act_obj2.cpp 13, limits.cpp 11, mail.cpp 9, tail ~30.

**Files:** Modify: the files above.

- [ ] **Step 1: Partition.** (a) Mechanical conversions per the Wave 3 idiom catalog (the overwhelming majority — same %s/%d one-shot and staging shapes as Phase 4). (b) Wave 3/4's documented justified-skip sites (act_info.cpp's buf-aliasing display cluster + dynamic-format branches; any equivalent elsewhere): these stay UNCONVERTED — wrap each cluster in `#pragma clang diagnostic push/ignored "-Wdeprecated-declarations"/pop` with a comment pointing at the existing justification; the aliasing-cluster conversion remains a backlog item with its own future characterization effort. (c) Sites feeding on-disk formats (objsave.cpp rent-file paths, boards.cpp, mail.cpp persistence): convert ONLY the message/log composition; anything composing bytes that reach a file gets the D2 treatment — read the surrounding persistence code first and if a site writes file content, skip it with a pragma + comment and list it in the report (candidate for a future round-trip-pinned effort).
- [ ] **Step 2: Convert partition (a), file-sized commits, largest files first.** Full suite + macOS boot golden between files (these modules include player-facing paths pinned only by the suite/goldens — the boot golden covers zone/shop/board loading; act_comm's say/tell paths have Wave 2-era pins).
- [ ] **Step 3:** Census: `-Wdeprecated-declarations` at zero. **Step 4:** Dual gate. **Step 5:** Commits per file batch: `refactor: convert <files> sprintf to std::format (Phase 5 T3)`; pragma ledger in the final commit body.

### Task 4: Const/string_view campaign (-Wwritable-strings, ~3,270)

**Files:** Modify: string-table definitions (`src/consts.cpp` and headers declaring them), their consumers tree-wide, per the boundary rule.

- [ ] **Step 1: Map the tables.** Census file distribution + `grep -n "char\* *[a-z_]*\[\]" src/*.cpp src/*.h`. The bulk: `consts.cpp` tables (`dirs`, `room_bits`, `item_types`, `equipment_types`, spell/skill names…) declared `char* x[]`, consumed by sprintbit/sprinttype (`const char**`-compatible?— read their signatures in utility.cpp first), format calls, and comparisons.
- [ ] **Step 2: Convert by table cluster, module-sized commits.** Table → `const char* const x[]`; update extern declarations (utils.h/db.h); consumers: reads compile unchanged; writes through a table pointer DO NOT EXIST (if the compiler finds one, that's a real finding — escalate, don't cast away). Function parameters that receive literals (`char* argument`-style where never mutated) → `const char*`; where the callee only reads length-aware → `std::string_view` per the boundary rule. Locals holding literals → `std::string_view` or `const char*`.
- [ ] **Step 3:** Where a ripple crosses into a mutating consumer legitimately (e.g. one_argument writes into its buffer), the LITERAL side gets fixed instead (the caller passes a mutable buffer copy — check each; never const_cast a literal into a mutating API).
- [ ] **Step 4:** Census zero for `-Wwritable-strings` (both compilers). **Step 5:** Dual gate after each batch AND at end. **Step 6:** Commits `refactor: const-correct <cluster> string tables (Phase 5 T4)`; every `.data()` use listed in the report (named review risk).

### Task 5: Tail sweep to zero (GNU-family)

**Files:** per census: `-Wunused-but-set-variable` (47), `-Wignored-qualifiers` (40), `-Wunused-variable` (29), `-Wdeprecated-copy-with-user-provided-copy` (8), everything remaining on AppleClang AND everything additional in the g++14 census (take a fresh full rots64 census — g++ has classes clang lacks: `-Wmaybe-uninitialized`, `-Wstringop-*`).

- [ ] **Step 1:** Fresh census both compilers; disposition table. **Step 2:** Fix to zero (unused-but-set: delete or use; ignored-qualifiers: drop the meaningless top-level const from return types; deprecated-copy: default or delete the copy members per rule-of-five). **Step 3:** Dual gate. **Step 4:** Commit `fix: warning tail sweep to zero on AppleClang+g++14 (Phase 5 T5)`.

### Task 6: Sanitizer CI (ASan+UBSan) + backlog memory fixes

**Files:** Modify: `.github/workflows/ci.yml` (two new jobs), `src/tests/json_utils_tests.cpp:83/:96` (dangling-temporary fixes), test fixture files with `profs` leaks (locate via LeakSanitizer), `src/CMakePresets.json` (add `macos-arm64-asan` + `linux-x64-sanitize` presets so CI and local runs share config).

- [ ] **Step 1:** Add presets: `linux-x64-sanitize` (inherits linux-x64; `-fsanitize=address,undefined -fno-sanitize-recover=all -g`) and macOS equivalent. Build + full ctest locally under both; fix what surfaces: the two JsonUtils dangling-temporary tests (bind the temporary to a named local), fixture `profs` leaks (free in fixture teardown), and disposition anything new (product bug → fix with disclosure; test bug → fix; false positive → suppression file `src/tests/sanitize.supp` with comments if genuinely needed).
- [ ] **Step 2:** Sanitized boot-golden: run `scripts/boot-golden.sh --native <sanitized binary> verify` — byte-identical output under ASan/UBSan proves the memory fixes are output-neutral.
- [ ] **Step 3:** CI jobs: `sanitize-linux` (ubuntu-24.04, linux-x64-sanitize preset, full ctest) and `sanitize-macos` (macos-14, asan preset, full ctest), both REQUIRED, modeled on the existing job structure (`.github/workflows/ci.yml:31+`). Do NOT push yet (finalization pushes; but a temporary branch push to validate the yml is allowed ONCE here if the yml can't be validated locally — note it in the report).
- [ ] **Step 4:** Dual gate (normal presets — sanitizer work must not perturb them). **Step 5:** Commits: `test: fix ASan-visible dangling temporaries and fixture leaks (Phase 5 T6)`, `ci: add required ASan/UBSan jobs (Phase 5 T6)`.

### Task 7: .clang-tidy check-in (advisory)

**Files:** Create: `.clang-tidy` (repo root). Modify: `.github/workflows/ci.yml` (advisory job).

- [ ] **Step 1:** Author `.clang-tidy`: `Checks: 'bugprone-*,performance-*,modernize-use-nullptr,modernize-use-override,modernize-loop-convert,-bugprone-easily-swappable-parameters,-bugprone-narrowing-conversions'` (start curated-small; WarningsAsErrors: '' — advisory), `HeaderFilterRegex: '^.*/src/.*'`. Run `clang-tidy` over 3 representative files with the CMake compile database (`-p build/macos-arm64`) to confirm the config parses and the noise level is sane; tune the check list against the codebase idioms (document each disabled check with a comment in the file).
- [ ] **Step 2:** CI: advisory job (non-required, `continue-on-error: true`) running clang-tidy over changed files (`git diff --name-only origin/master...HEAD -- 'src/*.cpp'`) with the compile database.
- [ ] **Step 3:** Dual gate untouched (config-only). **Step 4:** Commit `ci: check in curated .clang-tidy and advisory CI job (Phase 5 T7)`.

### Task 8: GNU-family -Werror flip

**Files:** Modify: `src/CMakeLists.txt:113` (game target: replace `-w` with `-Wall -Wextra -Werror`), `:157` (`ROTS_SUPPRESS_TEST_WARNINGS` default OFF), `:350-353` (tests: add `-Werror`), `src/Makefile:17-32` (REQ_CXXFLAGS: `-w` → `-Wall -Wextra -Werror`), `src/tests/Makefile:26` (add `-Werror`), `docs/BUILD.md` (new "Warnings policy" section: -Werror everywhere, the pinned-toolchain rationale, the -funsigned-char resolution, the 32-bit-retirement future-phase trigger), `CLAUDE.md` (drop the "warnings are expected" gotcha line).

- [ ] **Step 1:** Flip the flags (all build paths listed above — grep for `-w ` tree-wide to catch stragglers). **Step 2:** Full clean builds: macOS native + rots64 (game AND tests targets) — must be warning-free, hence green under -Werror; fix any straggler the probes missed (different TU sets). **Step 3:** Docs. **Step 4:** Dual gate. **Step 5:** Commit `build: enforce -Wall -Wextra -Werror on GNU-family; remove -w (Phase 5 T8)`.

### Task 9: MSVC /W4 campaign → /WX

**Files:** Modify: `src/CMakeLists.txt` (MSVC branch: `/W4`, later `/WX`; per-class `/wd` suppressions with comments), possibly source files per triage.

- [ ] **Step 1: Census run.** Push the branch (first CI exercise of the phase; earlier tasks all local). Add `/W4` (no `/WX`) to the MSVC compile options first — CI logs are the census. Extract per-class counts (`grep -oE "warning C[0-9]+" | sort | uniq -c`).
- [ ] **Step 2: Triage table.** Real-fix classes (uninitialized, format mismatches, narrowing with behavioral risk) vs noise classes for this codebase (e.g. C4100 unused-param if [[maybe_unused]] doesn't satisfy MSVC — verify; C4244/C4267 int conversions: sample 20 — if the pattern is pervasive-and-benign legacy int narrowing, a documented `/wd4244 /wd4267` is the sanctioned call THIS phase with a ledger note that a future effort may revisit; anything security-adjacent is a real fix).
- [ ] **Step 3: Iterate:** fix → push → watch, batching fixes per cycle (CI cycles are the cost; batch aggressively). **Step 4:** Flip `/WX`; full run green. **Step 5:** Commit(s) `build: MSVC /W4 clean with documented suppressions; enforce /WX (Phase 5 T9)` with the triage table in the body.

### Task 10: Exit

- [ ] Docs final pass (test counts if suites changed; BUILD.md verified). Dual gate + i386 battery (new flags exercise g++14-i386 — fix target-specific warnings surfaced there before exit; the battery build itself now enforces -Werror). Four-platform CI green INCLUDING the two new required sanitizer jobs. Exit note in this plan (census before/after per compiler, disposition/suppression ledgers, disclosed deltas, backlog). Final whole-branch review (most capable model). Merge decision to the owner.

---

## Self-review notes (write-time)

- Spec coverage: ladder tasks 1-9 ↔ spec ladder 1-9 one-to-one; exit ↔ verification section. Decisions 2/3 (funsigned-char doc, 32-bit future-phase trigger) land in Task 8's BUILD.md step. Census procedure + suppression discipline are Global Constraints. The spec's "sanitizer jobs green before the -Werror flip lands" ordering is preserved (T6 before T8).
- Judgment-heavy steps are framed as disposition/triage with escalation rules rather than fake precision (Task 1 instance dispositions, Task 9 MSVC classes) — counts and exemplar sites are real (census 2026-07-12); exact instance lists are extracted in-task from the same logged procedure.
- Type consistency: preset names (`linux-x64-sanitize`, `macos-arm64-asan`), census command, and flag lists are used identically across tasks; Task 8's file:line anchors verified this session (CMakeLists 113/157/350; Makefile 17/32; tests/Makefile 26).
- Task 3 partition (c) protects on-disk formats — objsave/boards/mail composition sites feeding files get pragma+skip, mirroring D2's lesson rather than repeating its risk without its round-trip pin.

---

## Phase 5 Exit (2026-07-13)

**Final commit:** `b802b8b` (branch `modernization/phase-5-hardening`, ~35 commits off master `2e7782f`).
**Merge decision:** owner's (pending at write time; final CI run on the exit commits in flight).

### The headline

**Zero warnings, enforced as errors, on every platform:** GNU-family `-Wall -Wextra -Werror` (AppleClang 21, g++14 x64, g++14 -m32) and MSVC `/W4 /WX`. Baseline was 5,203 AppleClang warnings + 1,948 MSVC /W4 sites. `-w` is deleted from every build path.

### Battery actuals (exit)

- macOS native: 1015/0/71 + boot golden; rots64: 1015/0/73 + boot golden — both under -Werror, green at every task.
- i386 (first -m32 -Werror exercise): build zero stragglers; ctest 1015/0/7; monolithic 1008 pass/0 fail, ZERO SIGSEGVs; boot golden byte-identical. (Two qemu hangs across the phase, killed+reran per guidance.)
- CI matrix: SIX required jobs (4 platforms + Linux ASan/UBSan+LSan + macOS ASan/UBSan) green with /WX at run 29210731880; T9 completed in 3 CI cycles (≤4 budget).
- Boot/combat/JSON goldens byte-identical throughout; zero sanctioned golden changes; RNG discipline verified per task.

### Real bugs fixed this phase (all individually disclosed in task reports/commits)

Stack overflows: shapescript SCRIPTPARAMCHANGE input[3]→[4] (Wave 2 backlog CLOSED); player-name 7-byte overflow. Boot crashes (real world data): fread_string stack-underflow; affect_modify negative-shift; singleton construction order. Leaks: store_to_char double-allocation (every account char selection). Output engine: handler.cpp null-GET_NAME std::format crash. Logic: sprintbit multichar-sentinel OOB + consts affected_bits missing-comma (compounding pair); 3× copy-pasted sscanf format-string bug; fscanf %ld-into-int 64-bit stack write; do_blinding disabled guard; do_gen_com imm_side wild-pointer read; self-compare/no-op-operator trio; mudlle inverted logic; obj2html struct-through-%s UB; structs.h rule-of-three; 23 MSVC-found uninitialized locals (6 verified UB-path). Plus ~140 test-fixture memory fixes (test_char_cleanup.h RAII) and the JsonUtils dangling-temporary tests.

### Deliverables beyond fixes

sprintf conversion COMPLETE tree-wide (T3's 401 + prior waves; 27 ledgered pragma skips incl. the Wave-2 DO_SAY security-hardening remainder — still OWED, see backlog); const/string_view campaign (all string tables `const char* const`, ~25 signatures widened, mutable_arg helper, 4 write-through-literal bugs fixed with real storage); ASan/UBSan CI presets + 2 required jobs; sanitize.supp (3 ledgered entries); curated .clang-tidy + advisory CI job (parallelized after a 30-min-timeout root-cause); BUILD.md Warnings policy (-funsigned-char pin = accepted resolution; 32-bit retirement = own future phase on owner's live-data confirmation); tests-Makefile -MMD carried from Wave 4.

### Suppression ledger (all documented in-code + reports)

GNU: zero blanket flags; 3 pragma clusters (act_info aliasing 15, utility 7, script.cpp DO_SAY 5) + 3 -Wrestrict/-Wnonnull-compare singles. MSVC: /wd4244 /wd4267 (1,167 benign narrowing sites — future-revisit ledgered), /wd4456/4458/4459 (shadowing, GNU-parity), /wd4702, /wd4127, /wd4310 (/J-blind FP), _CRT_NONSTDC_NO_WARNINGS. macOS ASan CI: detect_container_overflow=0 (brew gtest uninstrumented — revisit on FetchContent).

### Backlog (carried, prioritized candidates first)

(1) script.cpp DO_SAY template-hardening (SECURITY: builder-authored format strings — safe_template treatment owed since Wave 2). (2) Crash_alias_load production leak (alias lifecycle). (3) The 2 FIXME likely-missing-break fallthroughs (fight.cpp:2041 whip→bludgeon, shapemob.cpp:98 easterling language) — behavior-fix candidates. (4) buf-aliasing display cluster conversion. (5) target_data operator== by-value (dead-code latent leak). (6) MSVC narrowing /wd4244/4267 revisit. (7) RAII lifecycle-audit wave. (8) 32-bit retirement phase (owner-triggered). (9) gtest FetchContent for macOS sanitize job.

### Phase 5 status vs parent spec

Warnings clean + enforced: DONE (beyond spec — all four platforms, not just GNU). Sanitizers in CI: DONE (required, not just present). -funsigned-char: RESOLVED by pin (sanctioned option). clang-tidy: DONE (advisory). 32-bit retirement: MOVED to its own phase. **The parent modernization spec's Phase 5 exit criteria are met**; remaining items live on the backlog above.

### Exit-note amendment (final-review M1-M5, 2026-07-13)

- Suppression ledger addendum: the script.cpp tautological-compare pragma (T5) and C4804 fix-vs-suppress pair (T9) are ledgered in their task reports; recorded here for completeness (M1).
- Backlog additions from review-logged Minors (M2-M4): gtest_main nullptr-world first-call-wins semantics (comment recommended); interpre.cpp incidental whitespace-format spots (Wave 3 carry); report-accuracy nits are recorded in the ledger only.
- Correction (M5): T9's /WX validation cited run 29210731880 — its six REQUIRED jobs were green; the run-level conclusion reads "cancelled" solely because the ADVISORY clang-tidy job hit its (since-fixed) 30-min timeout. Job-level claims stand; final validation is run 29214544984 on tip e4caad0.
- Re-homed: Phase 3's "consolidate Windows operational gaps" recommendation now lives on the parent-spec backlog (Windows boot smoke still deferred pending a Windows host + world data).
