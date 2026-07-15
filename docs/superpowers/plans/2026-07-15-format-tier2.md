# Tier 2 Format/Allocation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Execute the approved Tier 2 design (`docs/superpowers/specs/2026-07-15-tier2-format-design.md`): switch the live character serializer/deserializer to v2b, retire production ostringstream in message/score text and warm JSON writers, and rewrite the prompt builder tests-first — all with byte-identical output.

**Architecture:** Three components executed smallest-first (serializer switch → ostringstream groups → prompt builder). Every change is pinned by existing characterization goldens or new characterization tests written BEFORE the optimization they guard.

**Tech Stack:** C++20 `std::format_to`/`std::back_inserter`, CMake presets `macos-arm64`/`macos-arm64-asan`/`linux-x64`, GoogleTest, `tools/account_smoke.py`.

## Global Constraints

- Branch: all work on `perf/format-tier2` (already created; spec committed as `eeff274`). Never commit to master. Merge is the owner's decision.
- **NEVER use Edit/Write tools on EXISTING `.cpp`/`.h` files** — the PostToolUse clang-format hook would bury diffs under pre-existing format drift. All edits to existing sources via Python byte-edits (latin-1 read/write) run through Bash. BRAND-NEW files (e.g. `src/tests/prompt_format_tests.cpp`) may be created with Write normally.
- Never run clang-format or `make format`. Never regenerate goldens — a golden diff means the change is wrong.
- `-Wall -Wextra -Werror` clean; ~100-column convention on changed lines.
- Byte-identical output is the acceptance bar for every task.
- Conversion rules for `ostringstream` → `std::string out`:
  - `<< "literal"` → `out.append("literal")` (or fold into an adjacent format call)
  - `<< value` → `std::format_to(std::back_inserter(out), "{}", value)` — folding adjacent fragments into one format call is preferred
  - `<< std::fixed; precision(2); << x` → `std::format_to(..., "{:.2f}", x)`
  - `<< std::endl` → `append("\n")` (ostringstream flush is a no-op — byte-identical)
  - `.str()` consumers take the string directly
  - Any OTHER manipulator discovered (setw/hex/etc.) = STOP, report DONE_WITH_CONCERNS with the site, do not guess.
- Fixed-size `char[N]` struct members passed to `std::format` must keep/get `static_cast<const char*>` decay (repo rule).
- Commit messages: imperative subject ≤72 chars, body ends with
  `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`
- Per task: macos-arm64 build + full ctest. Component gates add more (named per task).

---

### Task 1: Baseline on branch

**Files:** none (verification only).

- [ ] **Step 1:** `cd src && cmake --preset macos-arm64 && cmake --build --preset macos-arm64 -j4 && ctest --preset macos-arm64`
Expected: `100% tests passed` (1228 tests, ~75 skipped). If red, STOP — report, do not fix.

No commit.

---

### Task 2: Component 2 — switch production character JSON to v2b

**Files:**
- Modify: `src/account_management_assets.cpp:28` (serialize) and `:64` (deserialize)
- Modify: `src/db.cpp:798` (deserialize)

**Interfaces:**
- Consumes: existing `character_json::serialize_character_to_json_v2b(const CharacterData&)` and `character_json::deserialize_character_from_json_v2b(std::string_view, CharacterData*, std::string*)` — same signatures as v1 (verify the exact v2b signatures in `src/character_json.h` before editing; if a v2b deserialize overload does not match the v1 call shape at a site, STOP and report).
- Produces: production save AND load paths on v2b; v1 untouched (savebench baseline).

- [ ] **Step 1:** Python byte-edit the three call sites, appending `_v2b` to the function name only. Verify with:
`grep -n '_v2b' src/account_management_assets.cpp src/db.cpp` → 3 hits at the expected lines; `git diff --stat` → 2 files, 3 lines changed.
- [ ] **Step 2:** Build + full ctest (macos-arm64). Expected: 100% passed — `JsonPerf.*` equivalence and `CharacterizationJson.*` goldens are the key gates.
- [ ] **Step 3:** Account smoke gate: from repo root run `make smoke-account`. Expected: smoke flow completes (account create/verify/login/migrate assertions pass). KNOWN RISK: this host historically lacked the Rust proxy / py>=3.10 setup for the script (Phase 0 ledger). If it fails for ENVIRONMENT reasons (tooling missing, not assertion failure), capture the exact error, run `ctest --preset macos-arm64 -R 'AccountManagement|DbLoader|CharacterJson|JsonPerf'` as the fallback gate, and report DONE_WITH_CONCERNS naming the environment gap. An ASSERTION failure = BLOCKED.
- [ ] **Step 4:** Commit:
```
perf: switch live character JSON codec to v2b

Production save (account_management_assets) and load
(account_management_assets, db) now use the equivalence-tested
serialize/deserialize_character_from_json_v2b variants. v1 remains as
the savebench comparison baseline.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
```

---

### Task 3: Component 3 group 1 — report_exposed_data interface to std::string&

**Files:**
- Modify: `src/structs.h` (all `report_exposed_data(std::ostringstream&)` declarations, e.g. structs.h:1503)
- Modify: `src/char_utils.cpp` (~10 `message_writer` sites: implementations + the wrapper functions that build/`.str()` them, lines ~1528-1800)
- Test: `src/tests/char_utils_tests.cpp` (extend ONLY if the two `{:.2f}` sites lack byte pins)

**Interfaces:**
- Produces: `void report_exposed_data(std::string& message_writer) const` on every spec-data struct that had the ostringstream overload; wrapper functions return the built `std::string` directly.

- [ ] **Step 1 (pin first):** Check `src/tests/char_utils_tests.cpp` for byte-exact pins of the two damage-report functions using `std::fixed`+`precision(2)` (`char_utils.cpp:1745`, `:1792` — "Average"/"DPS"/percent lines). If pins exist, run them and record. If not: ADD characterization tests (to the existing test file via careful append — this file is a test file; check `clang-format act_info…` drift rule does not apply to appends done via Python; keep the file's existing style) pinning e.g. `Count/Total/Max/Average` formatting with a value that exercises 2-decimal rendering (`10/3` → `3.33`) and the percent tail. Run: RED not expected (they pin current behavior) — they must PASS against unconverted code.
- [ ] **Step 2:** Convert: change every declaration in structs.h and implementation/caller in char_utils.cpp from ostringstream to `std::string&` per the Global conversion rules (`{:.2f}` for the two fixed/precision sites; `\n` for endl). `#include <format>`/`<iterator>` added to char_utils.cpp if absent; `<sstream>`/`<iomanip>` includes stay if other code in the TU still uses them (check before removing; removal optional).
- [ ] **Step 3:** Verify no ostringstream remains in char_utils.cpp: `grep -c 'ostringstream' src/char_utils.cpp` → 0; structs.h declaration updated: `grep -n 'report_exposed_data' src/structs.h src/char_utils.cpp`.
- [ ] **Step 4:** Build + full ctest. Expected 100% (step-1 pins prove byte identity).
- [ ] **Step 5:** Commit (`perf: build spec-data reports with format_to instead of ostringstream`, standard footer).

---

### Task 4: Component 3 group 1 — remaining message-text singles

**Files:**
- Modify: `src/act_info.cpp:211` (inventory writer), `src/act_othe.cpp:1353`, `src/ranger.cpp:2824`, `src/mystic.cpp:637`

- [ ] **Step 1:** Convert each per Global rules (Python byte-edits). Check each file for other ostringstream uses first — convert every production site in these four files (act_info may include `<iomanip>` legacy — leave includes used elsewhere).
- [ ] **Step 2:** `grep -c 'ostringstream' src/act_info.cpp src/act_othe.cpp src/ranger.cpp src/mystic.cpp` → all 0.
- [ ] **Step 3:** Build + full ctest → 100%.
- [ ] **Step 4:** Commit (`perf: replace ostringstream with format_to in player message paths`, footer).

---

### Task 5: Component 3 group 2 — JSON writers A (boards, pkill, crime, mail)

**Files:**
- Modify: `src/boards.cpp:1012`, `src/pkill.cpp:227`, `src/db.cpp:4226` (crime), `src/mail.cpp:384` (and any sibling ostringstream sites in those files' serialization paths)

- [ ] **Step 1:** Convert per Global rules. These build JSON documents — the golden suites pin their bytes exactly (`BoardsJson.*`, `PkillJson.*`, `CrimeJson.*`, `MailJson.*`, incl. `GoldenRoundTripsByteStable`).
- [ ] **Step 2:** `grep -c 'ostringstream'` on the four files → 0 (production sites).
- [ ] **Step 3:** Build + full ctest → 100%.
- [ ] **Step 4:** Commit (`perf: build board/pkill/crime/mail JSON with format_to`, footer).

---

### Task 6: Component 3 group 2 — JSON writers B + component gate

**Files:**
- Modify: `src/objects_json.cpp` (3 sites), `src/exploits_json.cpp`, `src/account_management.cpp`, `src/account_management_storage.cpp`, `src/account_management_presentation.cpp` (3 each)

- [ ] **Step 1:** Convert per Global rules (`ObjectsJson.*`, `ExploitsJson.*`, `AccountManagement.*` suites + goldens pin bytes).
- [ ] **Step 2:** `grep -rc 'ostringstream' src --include='*.cpp' --include='*.h' | grep -v ':0$'` → remaining hits ONLY in: `src/character_json.cpp` (v1 baseline, intentional), cold converters (`convert_plrobjs.cpp`, `convert_exploits.cpp`, `mob_csv_extract.cpp`, `save_benchmark.cpp`/`savebench.cpp` if any), and `src/tests/*`. Anything else = missed site.
- [ ] **Step 3:** Build + full ctest → 100%.
- [ ] **Step 4 (Component 3 gate):** rots64: `docker compose run --rm --pull never rots64 bash -lc 'cd /rots/src && cmake --preset linux-x64 && cmake --build --preset linux-x64 -j"$(nproc)" && ctest --preset linux-x64'` → 100%; then `scripts/boot-golden.sh --service rots64 verify` and `scripts/boot-golden.sh --native build/macos-arm64/ageland verify` → both match.
- [ ] **Step 5:** Commit (`perf: build object/exploit/account JSON with format_to`, footer).

---

### Task 7: Component 1 — extract build_prompt (pure refactor)

**Files:**
- Modify: `src/comm.cpp` (prompt block, currently ~1032-1113 inside the game-loop descriptor sweep; locate by searching `give the people some prompts`)
- Modify: `src/comm.h` (declaration)

**Interfaces:**
- Produces: `void build_prompt(descriptor_data* point, std::string& out);` declared in comm.h — fills `out` with the prompt text exactly as the inline block produced in `prompt`/`pptr`. Call site: a function-local `static std::string prompt_buffer;` replaces the old char-buffer usage; after building, the existing WAITWHEEL check gates `write_to_descriptor(point->descriptor, prompt_buffer.c_str())`.

- [ ] **Step 1:** Move the block verbatim (Python byte-edit: cut lines, paste into a new function above the game loop; inside the function, the existing `strcpy(prompt, std::format(...))` chain writes into a local `char prompt[MAX_INPUT_LENGTH]` and finishes with `out.assign(prompt)` — i.e., extraction does NOT yet change the composition mechanics). Keep `opponent`/`tank` locals inside the function. Preserve the exact final semantics: empty-prompt case (`prompt[0] = 0`) yields empty `out`, and the write is skipped/`pptr` semantics preserved exactly as before (verify against the original: an empty prompt still got written if tmpflag — preserve whatever the original did, byte-for-byte).
- [ ] **Step 2:** Build + full ctest → 100%; `scripts/boot-golden.sh --native build/macos-arm64/ageland verify` → matches.
- [ ] **Step 3:** Commit (`refactor: extract prompt composition into build_prompt`, footer).

---

### Task 8: Component 1 — prompt characterization tests (new file)

**Files:**
- Create: `src/tests/prompt_format_tests.cpp` (Write tool OK — new file)
- Modify: `src/CMakeLists.txt` (add to test sources) and `src/tests/Makefile` (add to SRCS) — Python byte-edits

**Interfaces:**
- Consumes: `build_prompt(descriptor_data*, std::string&)` from Task 7.

- [ ] **Step 1:** Write tests pinning EXACT bytes for: (a) plain prompt (terminator only), (b) invis level (`i<N>` head), (c) HP/Mind/Move display combinations per the PRF/display flags the block checks, (d) maul-mode variant, (e) combat with tank and opponent (PERS-rendered names, `A:(...)` section, `,` separators), (f) the `>` vs `]` terminators. Follow existing test conventions: fixtures like `src/tests/comm_output_tests.cpp` (descriptor setup) and `test_char_cleanup.h`/`test_world.h` helpers; every class-scoped member commented (user convention); no `rand()`.
- [ ] **Step 2:** Register the file in BOTH build systems; build; run `./build/macos-arm64/ageland_tests --gtest_filter='PromptFormat*'` (or ctest -R) → all pass against the EXTRACTED, unoptimized code.
- [ ] **Step 3:** Full ctest → 100%. ASan gate (new test file): `cmake --preset macos-arm64-asan && cmake --build --preset macos-arm64-asan -j4 && ctest --preset macos-arm64-asan` → clean.
- [ ] **Step 4:** Commit (`test: pin build_prompt output byte-for-byte`, footer).

---

### Task 9: Component 1 — rewrite build_prompt single-pass

**Files:**
- Modify: `src/comm.cpp` (build_prompt internals only)

- [ ] **Step 1:** Replace the internal `char prompt[]` + `strcpy(prompt, std::format("{} …", prompt))` chain with direct appends to `out`: `out.clear(); out.reserve(128);` then `std::format_to(std::back_inserter(out), …)`/`append` per fragment — each old fragment's NEW text only (drop the `{}` self-prefix), preserving every literal space/bracket byte. The function stays stateless; the call site's static buffer (Task 7) is unchanged.
- [ ] **Step 2:** `ctest -R PromptFormat` (or gtest filter) → all pass UNCHANGED. Full ctest → 100%. Boot golden (native) → matches.
- [ ] **Step 3:** Commit (`perf: compose prompts single-pass with format_to`, footer).

---

### Task 10: Final verification + report

**Files:** none.

- [ ] **Step 1:** macos-arm64: full ctest + `scripts/boot-golden.sh --native build/macos-arm64/ageland verify`.
- [ ] **Step 2:** rots64: full ctest + `scripts/boot-golden.sh --service rots64 verify`.
- [ ] **Step 3:** ASan preset full run (covers the branch's new tests): `ctest --preset macos-arm64-asan` → clean.
- [ ] **Step 4:** Residual scan: production `ostringstream` only in character_json v1 + cold converters; no `+= std::format` regressions (`grep -rcE '\+=\s*std::format' src --include='*.cpp'` → only the known test site).
- [ ] **Step 5:** Report to owner: per-component results, smoke-account outcome, merge decision + i386 finalization battery surfaced (owner's call). Do NOT merge or push.

---

## Self-review notes
- Spec coverage: Component 2 = Task 2; Component 3 group 1 = Tasks 3-4, group 2 = Tasks 5-6 (+gate); Component 1 = Tasks 7-9 (extract → pin → rewrite, strictly ordered); verification = Task 10. Helper explicitly dropped per spec.
- Known risks named in-plan: smoke-account environment gap (fallback defined), stream-manipulator discovery rule (STOP), v2b signature check before edit, empty-prompt semantics preservation in extraction.
- No placeholders: every step has exact commands, counts, paths, or an explicit STOP rule.
