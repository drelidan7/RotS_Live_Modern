# Backlog Cleanup Wave Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land the prioritized backlog — DO_SAY security hardening, the alias-load leak, the buf-aliasing cluster conversion, instrumented gtest on macOS ASan CI, and an evidence-based MSVC-narrowing disposition — netting NEGATIVE suppressions tree-wide.

**Architecture:** Severity-ordered ladder (security first, CI-cycle-bound MSVC work last); every conversion pinned byte-for-byte before the switch; dual local gate per task; i386 battery + 6-job CI at exit.

**Tech Stack:** C++20, std::format, safe_template (Wave 2), GoogleTest, ASan/UBSan, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-07-13-backlog-cleanup-design.md`. **Branch:** `modernization/backlog-cleanup` off master (`c7183ed` or later).

## Global Constraints

Every task's requirements implicitly include this section.

- **Goldens STOP-on-diff** (boot/combat/JSON), zero sanctioned changes. **RNG discipline** (no `number()` call added/removed/moved). **-Werror//WX green throughout** — any new warning on any of the four compilers is a build failure; conversions must be warning-clean everywhere.
- **Dual local gate per task** (verbatim from Phase 5's plan): macOS native (`cd src && cmake --build --preset macos-arm64 -j4 && ctest --preset macos-arm64; cd ..` + `scripts/boot-golden.sh --native build/macos-arm64/ageland verify`) and rots64 (`docker compose run --rm --pull never rots64 bash -lc 'cd /rots/src && cmake --preset linux-x64 && cmake --build --preset linux-x64 -j"$(nproc)" && ctest --preset linux-x64'` + `scripts/boot-golden.sh --service rots64 verify`). Expected 0 failures both legs, both print `boot log matches golden`. Background >10-min container steps and STOP with a note. NO i386/`rots` commands per task (exit-only). NO per-task CI push (T5 is the sanctioned exception — CI-cycle-based by design).
- **Characterization contract:** pins PASS against unchanged source BEFORE any conversion; new/extended test files get the macOS ASan run (`cmake --preset macos-arm64 -B ../build/macos-arm64-asan -DCMAKE_CXX_FLAGS=-fsanitize=address -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address` from src/, build ageland_tests, run the suite filter).
- **Standing fixture rules** (Phase 4/5): in-place descriptor resets w/ MSVC comment, value-init `{}`, `tmpabilities.str = 100`, content-emptiness checks, platform shims, char[N]→std::format casts, `make format` scope discipline (it reformats the whole tree — commit only intended hunks), clang-format-hook recovery (restore + script re-apply), CRLF preservation in scripted edits, qemu kill+rerun, `--pull never` for BuildKit hangs.
- **Suppression discipline:** this wave REMOVES suppressions (5 deprecation pragmas, aliasing-cluster pragmas, -Wrestrict pair, 1 sanitize.supp entry); any NEW suppression needs comment+ledger and is expected only in T5's narrowing disposition.
- **Registration rule:** new test files go in BOTH src/CMakeLists.txt (alphabetized) and src/tests/Makefile SRCS with header-dependency lines (-MMD covers rebuilds, but SRCS membership is still manual).

---

### Task 1: DO_SAY template hardening (SECURITY)

**Files:**
- Modify: `src/safe_template.h`, `src/safe_template.cpp` (extend validation to the one-%s-with-argument class)
- Modify: `src/script.cpp` (5 sites: SCRIPT_DO_SAY, SCRIPT_DO_YELL, SCRIPT_SEND_TO_CHAR, SCRIPT_SEND_TO_ROOM, SCRIPT_SEND_TO_ROOM_X — `sprintf(output, curr->text, txt1)`; remove their `-Wdeprecated-declarations` pragmas)
- Create-or-extend: `src/tests/safe_template_tests.cpp` (the Wave 2 suite exists — EXTEND it) + a small `ScriptDoSay` pin block (new tests in the same file or `script.cpp`-covering file if one exists — read the tree first)
- Modify: `docs/security-notes.md` (entry → resolved)

**Interfaces:**
- Consumes: `safe_template::expand_checked(const char* tmpl, ...)` (src/safe_template.h:48 — read the REAL signature first; the Wave 2 spec says: template + expected conversion signature → validated expansion or safe fallback + syslog).
- Produces: the with-args validation entry point later callers can reuse; script.cpp's five sites hardened.

- [ ] **Step 1:** Read safe_template.{h,cpp} completely + the five script.cpp sites in context + the Wave 2 plan §Task 2 (its behavior contract is the precedent). Determine the exact expansion semantics needed: `curr->text` expects EXACTLY ONE `%s` consuming `txt1` (verify against the sites — if any passes different args, record it).
- [ ] **Step 2:** Write pins FIRST: well-formed template (`"Hello %s!"` + txt1) must produce byte-identical output through the CURRENT sprintf path — capture via the existing test conventions; malformed cases (zero %s, two %s, %d, %n, stray %) get tests asserting the SAFE-FALLBACK contract (raw template via "%s" or neutral default + syslog — match Wave 2's chosen fallback exactly; read what expand_checked already does). Run against unchanged source: well-formed pins PASS; malformed pins FAIL (they assert the new behavior) — this half IS fail-first TDD, deliberately.
- [ ] **Step 3:** Extend safe_template (validation for exactly-one-%s-with-arg, reusing the existing scanner; keep the API style). Route the five sites through it; delete the pragmas. All pins green.
- [ ] **Step 4:** ASan the touched suite. Dual local gate. Security-notes update.
- [ ] **Step 5:** Commits: `test: pin script DO_SAY-family template expansion (backlog T1)`, `fix: harden script DO_SAY-family templates via safe_template (backlog T1)` (+ docs). Trailer on all:
```
Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01W2FhYpeUrffvBxb6q3EGKi
```

### Task 2: Small-fix bundle

**Files:**
- Modify: `src/objsave.cpp` (Crash_alias_load ownership — read the alias-list lifecycle: who allocates, where free_char should release; the fix likely belongs in `src/db.cpp:free_char` or the alias-list teardown it calls)
- Modify: `src/tests/sanitize.supp` (REMOVE the Crash_alias_load leak entry — the proof the fix works)
- Modify: `src/interpre.cpp` (~:694 `int operator==(target_data t2)` → `const target_data&` — verify zero callers first with grep; it stays dead code, just no longer a copy trap)
- Modify: `src/tests/gtest_main.cpp` (world_singleton first-call-wins comment)
- Modify: `src/fight.cpp:2041`, `src/shapemob.cpp:98` (comment-only: FIXME → `INVESTIGATE (owner, 2026-07-13): behavior ruling pending more information; preserved byte-for-byte.` Keep the evidence citations. At fight.cpp:2041 ALSO append the owner observation: `Live-game whips display whip/lash (not crush/pound) messages — the fallthrough may be dead code or the message path may not flow from this assignment; verify message-path provenance before ruling.`)

- [ ] **Step 1:** Alias-leak lifecycle read → minimal ownership fix → prove with the linux sanitize preset run of the covering tests (`DbLoader.*`/objsave tests) with the supp entry removed: LeakSanitizer clean.
- [ ] **Step 2:** The three comment/declaration edits (operator== const&, gtest_main comment, INVESTIGATE rewrites). interpre.cpp whitespace nits ONLY if a clean one-hunk edit.
- [ ] **Step 3:** Dual local gate. Commits: `fix: release Crash_alias_load alias list on free_char (backlog T2)` (+ supp removal in same commit), `docs: INVESTIGATE dispositions + minor comment fixes (backlog T2)`. Trailers as T1.

### Task 3: buf-aliasing cluster conversion

**Files:**
- Modify: `src/act_info.cpp` (the pragma-wrapped display cluster: show_char_to_char, list_char_to_char, get_char_position_line, get_char_flag_line, show_mount_to_char remnants, do_look case 8 — remove the pragma push/pop pairs)
- Modify: `src/utility.cpp` (show_room_affection, show_room_weather — remove the two `-Wrestrict` pragmas)
- Modify: `src/tests/act_info_format_tests.cpp` (extend ActInfoPerception or add `ActInfoDisplayCluster` suite — pins FIRST)

- [ ] **Step 1:** Read the whole cluster. These are `sprintf(buf, "%s...", buf, ...)` self-referencing accumulators feeding act()/send_to_char via shared global buf — Wave 4's materialize-then-strcpy idiom is the proven conversion (`std::string tmp = std::format(...); strcpy(buf, tmp.c_str());` or full std::string accumulation where buf isn't consumed downstream). Map each function's downstream consumers before choosing per catalog items 2/3.
- [ ] **Step 2:** Pins FIRST (PASS pre-conversion): position lines per POSITION_*, flag lines (hide/invis combos), mount line, room-affection/weather lines (fixture-reachable — ScopedTestWorld + weather globals with RAII restore), do_look case 8 shallow render already partially pinned (extend). Commit tests.
- [ ] **Step 3:** Convert function-by-function, suite between each. Remove ALL the cluster's pragmas. Verify tree-wide: `grep -rn "deprecated-declarations\|Wrestrict" src/*.cpp` → only add_prompt's documented PRF_DISPTEXT branch remains (its pragma is for the dynamic-format sprintf — it STAYS, it's not part of this cluster; confirm the spec's zero-format-pragmas claim against reality and reconcile in your report: the spec's "zero" means zero ALIASING/deprecation pragmas outside that one justified dynamic-format site — state the final census).
- [ ] **Step 4:** ASan the suite. Dual local gate (combat + boot goldens explicitly — do_look case 8 renders boot-adjacent room output). Commits: tests then conversion, trailers as T1.

### Task 4: macOS ASan gtest via FetchContent

**Files:**
- Modify: `src/CMakeLists.txt` (gtest provisioning: when the sanitize flags are on for macos-arm64-asan, FetchContent GoogleTest instead of find_package brew gtest — follow the windows-2022 FetchContent precedent already in the file; read it first)
- Modify: `.github/workflows/ci.yml` (macos sanitize job: drop `detect_container_overflow=0`, restore full ASAN_OPTIONS; adjust the brew gtest install step if it becomes unnecessary for that job)

- [ ] **Step 1:** Read the existing FetchContent branch (Windows) + the preset wiring. Implement: a cache option (e.g. `ROTS_FETCH_GTEST=ON` set by the asan preset) selecting FetchContent; default stays find_package.
- [ ] **Step 2:** Local proof: full macOS asan-preset build + ctest with container-overflow ON — clean (the T6-era gtest FP gone because gtest is now instrumented).
- [ ] **Step 3:** ci.yml update; actionlint. Dual local gate (normal presets unperturbed). Commit: `ci: FetchContent-instrumented gtest for macOS ASan job; re-enable container-overflow (backlog T4)`. Trailer as T1.

### Task 5: MSVC narrowing revisit (LAST; CI-cycle-based)

**Files:**
- Modify: per-triage source files; `src/CMakeLists.txt` (suppression narrowing if the evidence supports it); `docs/BUILD.md` (the documented disposition)

- [ ] **Step 1: Sample.** Temporarily remove `/wd4244 /wd4267` locally? No local MSVC — instead push a census branch commit removing them WITH /WX off for the MSVC job? NO — simpler sanctioned procedure: one CI cycle with `/wd4244 /wd4267` removed AND `/WX` temporarily off (commit clearly labeled census-only), harvest the full C4244/C4267 site list from the log, then revert the census commit. Stratify the ~1,167 sites by file/context class (combat math, table indices, time, protocol bytes, sizes).
- [ ] **Step 2: Classify.** Sample ≥60 sites across strata (read each in source): real-truncation-risk (value can exceed target range on reachable inputs → latent bug) vs benign (domain provably fits). Extrapolate per stratum; every REAL finding gets read fully, not sampled.
- [ ] **Step 3: Fix the real class** (each a disclosed fix with domain reasoning; combat-math changes need explicit value-preservation argument or they're behavior changes — escalate, don't slip). Local dual gate.
- [ ] **Step 4: Disposition.** If reals were concentrated (e.g. one subsystem): narrow the global /wd to per-file pragmas around the benign clusters. If diffuse-and-benign: keep global /wd with the sampling evidence written into BUILD.md's Warnings policy. Either way: BUILD.md documents the evidence, sample size, and decision.
- [ ] **Step 5:** Final CI cycle: 6/6 green with /WX restored. Commits: census (reverted), fixes, disposition+docs. Trailers as T1.

### Task 6: Exit

- [ ] i386 battery (make test + monolithic zero-SIGSEGV + boot golden; qemu kill+rerun as needed). Push; 6-job CI green. Docs: test-count baselines if pins grew the suite; security-notes verified. Exit note in this plan (suppression-net-negative tally, per-task outcomes, T5 evidence summary, INVESTIGATE items carried). Final whole-branch review (most capable model). Merge decision to the owner.

---

## Self-review notes (write-time)

- Spec coverage: T1-T6 ↔ spec tasks 1-6 one-to-one; INVESTIGATE amendment + owner whip observation carried into T2's exact comment text; the spec's "zero format-suppression pragmas" claim is reconciled honestly in T3 Step 3 (add_prompt's justified dynamic-format pragma remains — census stated rather than overstated).
- T1's malformed-case tests are deliberately fail-first (they pin NEW contract behavior) while well-formed pins are pass-first characterization — both stated explicitly to avoid the reviewer flagging the inversion.
- T5's census procedure avoids a broken-window push (census commit clearly labeled and reverted; /WX only off for that one labeled cycle).
- Anchors verified this session: safe_template.h:48 expand_checked; script.cpp five SCRIPT_* sites (T3 pragma ledger); interpre.cpp:694 operator==; fight.cpp:2041/shapemob.cpp:98 FIXMEs; sanitize.supp Crash_alias_load entry; Windows FetchContent precedent in CMakeLists.
