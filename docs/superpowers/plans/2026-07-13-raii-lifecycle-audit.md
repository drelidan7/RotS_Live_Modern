# RAII Lifecycle-Audit Wave Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bring `char_data`/`obj_data` lifecycles under RAII where the audit proves it safe — instance-block ownership plus greenlit local/string boundaries — while the world graph stays raw and prototype-shared strings are handled per an owner ruling.

**Architecture:** An audit-GATED wave: T1 is a read-only ownership map that classifies every pointer field and SIZES the rest of the wave; T2 is an owner-ruling checkpoint on prototype-shared strings; T3+ convert only greenlit boundaries, safest-first, each characterization-pinned with the goldens + ASan/UBSan CI as the net. The exact T3+ roster is finalized from T1's greenlit set (this plan specifies T1/T2 fully and the conversion-task TEMPLATE + known-likely conversions; the executing coordinator instantiates the concrete conversion tasks once T1 lands).

**Tech Stack:** C++20, `std::string`/`std::unique_ptr` (owner-explicit), GoogleTest, ASan/UBSan CI, the existing `CREATE`/`CREATE1`/`RELEASE` macros (`utils.h:208-245`) and `free_char`/`free_obj`/`clear_char` (`db.cpp`).

**Spec:** `docs/superpowers/specs/2026-07-13-raii-lifecycle-audit-design.md`. **Branch:** `modernization/raii-audit` off master (`c7550ba` or later).

## Global Constraints

Every task's requirements implicitly include this section.

- **Goldens STOP-on-diff** (boot/combat/JSON), zero sanctioned changes. **RNG discipline.** **-Werror//WX green on all four compilers throughout** — RAII changes are the likeliest to trip libc++-vs-libstdc++/MSVC/ASan divergence; the sanitizer CI jobs are the safety net, and **every conversion task runs the macOS ASan gate; the CI ASan jobs must stay green.**
- **The world graph stays raw.** Non-owning cross-links (people lists, contents chains, `affected` chains, room back-pointers, next_in_room/next_content, prototype back-refs) NEVER become owning. The audit classifies them; they are OUT of scope for conversion.
- **No ownership code before the gate.** No conversion task starts until `ownership-map.md` (T1) is committed AND T2's prototype-string ruling is recorded. Instance-owned string FIELDS are not touched until T2 lands.
- **Characterization-first:** output-visible conversions get byte-pins that PASS against unchanged source before the change. Instance-ownership/teardown changes get ASan/leak proof (LeakSanitizer via the linux sanitize preset) — the behavioral contract is "same allocations freed, same order, no double-free, no leak."
- **Dual local gate per task** (macOS native + rots64: full ctest + boot goldens; commands verbatim from the Backlog plan's Global Constraints). i386 battery + 7-job CI at exit. Background >10-min container steps and STOP with a note.
- **Standing fixture rules** (Phase 4/5/Backlog): in-place descriptor resets, value-init, `tmpabilities.str=100`, platform shims, char[N]→std::format casts, the `test_char_cleanup.h` RAII guards for fixture chars, `make format` scope discipline, clang-format-hook recovery (scripted edits + CRLF preservation), qemu kill+rerun, `--pull never`.
- **Suppression discipline:** net-neutral-or-negative; any new suppression needs comment+ledger.

---

### Task 1: Ownership audit (READ-ONLY — the gate)

**Files:**
- Create: `docs/superpowers/ownership-map.md` (the deliverable — committed)
- Read-only: `src/structs.h` (char_data/obj_data field declarations), `src/db.cpp` (`clear_char` :3607, `free_char` :3363, `free_obj` :3411, `read_mobile` :1458, `read_object` :1771), `src/handler.cpp` (extract_char/extract_obj, the 4 free_char/free_obj callers), `src/objsave.cpp` (Crash_alias_load, obj load), `src/utils.h:208-245` (CREATE/CREATE1/RELEASE), and every `free_char`/`free_obj` caller (`grep -rn 'free_char\|free_obj' src/`).

**Interfaces:**
- Produces: `ownership-map.md` — the classification that T3+ consume as their greenlight source; the prototype-shared-string count + free-path analysis that T2 consumes.

- [ ] **Step 1: Enumerate every pointer field** of `char_data` (incl. nested `char_special_data`/`char_player_data`/`char_ability_data`) and `obj_data`/`obj_flag_data` from structs.h. For each: name, type, one-line role.
- [ ] **Step 2: Trace each field's lifecycle** across clear_char (init), read_mobile/read_object (prototype construction + instance copy), the copy/instantiation path (how an NPC instance gets mob_proto's pointers), and free_char/free_obj (teardown). Quote the exact free logic per field.
- [ ] **Step 3: Classify each field** into exactly one bucket, with the evidence (file:line) for the call:
  - **OWNING** — this object allocates and unconditionally frees it (e.g. poofIn/poofOut, PC-only strings, skills/knowledge arrays, profs).
  - **NON-OWNING** — world-graph cross-link, freed by nobody-through-this-pointer; stays raw forever (people/contents/affected/next_* chains, in_room, prototype back-refs).
  - **CONDITIONAL** — freed only under a runtime test (the IS_NPC/prototype-shared strings — name/title/short_descr/long_descr/description); quote the conditional and note the commented-out NPC branch in free_char.
  - **LOCAL-SCRATCH** — allocated and freed within one function, never stored on the instance (the fixed-buffer families: output[500] and the act_info group the Backlog review flagged; other function-local buffers).
- [ ] **Step 4: Per-boundary verdict** — for each OWNING/LOCAL-SCRATCH field a GREENLIGHT-CONVERT with the target type (`std::string` for owned char*; `std::unique_ptr`/`std::vector` for arrays); for each CONDITIONAL an ESCALATE (feeds T2); for each NON-OWNING an explicit HOLD-RAW-FOREVER with the reason. Also: an INSTANCE-OWNERSHIP verdict — is a `unique_ptr<char_data, free_char_deleter>` factory feasible given clear_char's placement-new-over-CREATE()-storage contract? (Read clear_char's own comment on this.)
- [ ] **Step 5: Quantify the escalation** — count the CONDITIONAL (prototype-shared) string sites, list every read site of those fields (so T2 knows the blast radius of a type change), and the exact free-path logic. State a preliminary keep-raw-conditional vs model-sharing recommendation with reasoning.
- [ ] **Step 6: Size the wave** — a proposed T3+ task list derived from the greenlit set (e.g. "T3 fixed-buffer families; T4 unconditional-owned strings→std::string; T5 skills/knowledge arrays→vector; T6 instance-ownership factory"), each with its risk tier. This is the coordinator's input for instantiating concrete tasks.
- [ ] **Step 7: Commit** `docs: char_data/obj_data ownership map (RAII audit T1)` + trailer:
```
Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01W2FhYpeUrffvBxb6q3EGKi
```
No code changed; no gate needed beyond "doc committed." (Reviewer verifies the classifications against source — this is the highest-leverage review of the wave.)

---

### Task 2: Prototype-string escalation checkpoint (COORDINATION — no code)

- [ ] **Step 1:** The coordinator reads T1's Step 5 quantification and presents the owner: the shared-string site count, the read-site blast radius, the keep-raw-vs-model recommendation, and the trade-offs (keep-raw = zero risk to the proven conditional, instance-block ownership still lands around it; model-sharing = type-enforced but rewrites the hottest struct's string storage + every read site).
- [ ] **Step 2:** Owner rules. Record the ruling in this plan (amendment block) and the ledger. The default if kept-raw: instance-ownership converts the BLOCK, leaving the conditional string members raw with their existing free logic untouched.
- [ ] **Step 3:** Only after the ruling is recorded do the instance-ownership conversion tasks (T6-class) become eligible. The safest conversion tasks (fixed-buffer, unconditional strings) may proceed in parallel — they don't touch conditional fields.

---

### Conversion-task TEMPLATE (T3+ — instantiated from T1's greenlit set)

Each greenlit boundary becomes one task shaped like this. The coordinator fills the boundary specifics from `ownership-map.md`.

**Files:** the owner struct's field decl (structs.h), its free/clear/copy sites (db.cpp + callers), and the characterization test file (extend the relevant `*_format_tests.cpp` or add a `raii_lifecycle_tests.cpp` for pure-teardown behavior).

- [ ] **Step 1:** Restate the boundary from ownership-map.md (bucket, current alloc/free, target type). Confirm it's GREENLIT (not conditional/non-owning).
- [ ] **Step 2: Characterization.** Output-visible field (e.g. a string that reaches a display) → byte-pins on that output, PASS pre-conversion. Pure-teardown boundary (e.g. an array freed in free_char with no output) → a leak/lifecycle test: build a fixture instance, exercise alloc, run the teardown, assert (via LeakSanitizer under the linux sanitize preset) zero leak and (via a deliberate double-free-detection reasoning or ASan) no double-free. Commit tests.
- [ ] **Step 3: Convert.** Owned char* → `std::string` (drop the RELEASE; the member's destructor now frees it — verify free_char no longer RELEASEs it and clear_char's placement-new value-inits it correctly). Owned array → `std::vector`/`std::unique_ptr`. Instance block → the `unique_ptr<T, deleter>` factory (deleter runs the EXISTING free_char/free_obj body; callers that `free_char(ch)` become `.reset()`/scope exit — do this incrementally, one allocation site + its frees per task). NEVER touch a conditional/non-owning field.
- [ ] **Step 4:** macOS ASan gate on the touched suite; dual local gate (goldens are the outer net — a teardown-order change that alters output fails here). 
- [ ] **Step 5:** Commit `refactor: <boundary> to RAII (RAII Tn)` + trailer. Update ownership-map.md's verdict for that boundary to CONVERTED.

**Known-likely instantiations** (audit confirms/adjusts): (a) fixed-buffer families output[500]+act_info group → local `std::string`; (b) poofIn/poofOut/PC-title unconditional-owned char* → `std::string`; (c) skills/knowledge arrays → `std::vector`/owning ptr; (d) char_data/obj_data instance block → RAII factory + deleter (gated on T2). Instance ownership (d) is the moderate-depth deliverable and the riskiest — sequence it LAST among conversions.

---

### Task (exit): Exit

- [ ] Finalize ownership-map.md (every boundary marked CONVERTED / HELD-RAW / ESCALATED-DEFERRED; the held/deferred set is the next backlog). i386 battery (make test + monolithic zero-SIGSEGV + boot golden; qemu kill+rerun). Push; 7-job CI green (ASan/UBSan jobs are the RAII net — they must be green). Docs: test-count baselines if pins grew the suite; BUILD.md if a lifecycle lesson emerged. Exit note in this plan (converted vs held vs escalated ledger, the T2 ruling, instance-ownership outcome). Final whole-branch review (most capable model — ownership correctness is the review focus). Merge decision to the owner.

---

## Self-review notes (write-time)

- Spec coverage: T1↔audit (the gate), T2↔escalation checkpoint, conversion TEMPLATE + known-likely list↔"greenlit boundaries only, safest-first, instance ownership last", exit↔finalized map + battery. The world-graph-stays-raw and no-code-before-gate constraints are in Global Constraints AND the template's Step 3 ("NEVER touch a conditional/non-owning field").
- Audit-gated honesty: T3+ are deliberately a template + known-likely list rather than fully-enumerated tasks, because the spec's decision 3 makes the audit size the wave — enumerating exact conversion tasks pre-audit would be fabricated precision. The coordinator instantiates concrete tasks from T1's Step 6 output; this is stated, not hidden.
- Anchors verified this session: clear_char db.cpp:3607 (placement-new comment), free_char :3363 (conditional string frees + commented NPC branch), free_obj :3411, read_mobile :1458, read_object :1771, CREATE/RELEASE utils.h:208-245, poofIn/poofOut structs.h:1150/1152, free_alias_list precedent (Backlog T2).
- Type consistency: the deleter/factory naming (`unique_ptr<char_data, free_char_deleter>`), target types (std::string/std::vector), and the CONVERTED/HELD-RAW/ESCALATED verdict vocabulary are used identically across T1, the template, and exit.
