# Upstream Sync & Validation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Validate the merged `upstream/account-management` work (branch `merge/upstream-2026-07-10`, 5 commits) against our Phase 0–4 modernization — resolve the interaction surfaces, make the one deliberate JSON-golden change the merge implies, fix any real defects, and get to a merge-ready, four-platform-green state.

**Architecture:** Spec `docs/superpowers/specs/2026-07-10-upstream-sync-validation-design.md`. Order: the golden-changing task first (Task 1, isolated, sign-off), then the three interaction surfaces (MSDP, prac, command-table), then battery + merge. This is a validation effort — most tasks are "verify, and fix only if a real defect surfaces"; the two known concrete changes are the specialization golden (Task 1) and the prac `memcpy` over-read (Task 3).

**Tech Stack:** C++20, debian:trixie g++14.2 containers (i386 + rots64), MSVC 2022 + AppleClang 21 via CI, GoogleTest, GitHub Actions.

## Global Constraints

- **Verification flow (2026-07-10 directive):** per-task gate = local i386 container (`make test` ctest + tests-Makefile runner + `scripts/boot-golden.sh verify`) + rots64 (`ctest --preset linux-x64` + `boot-golden.sh --service rots64 verify`) — BOTH are Docker containers, both run now. **The local macOS-native leg is the ONLY thing blocked by the iCloud wedge; its `boot-golden.sh --native` confirmation is DEFERRED to post-reboot.** macOS coverage this effort comes from the **CI macos-arm64 job** (GitHub runner, unaffected by the local wedge). The four-platform remote CI is the finalization/pre-merge gate.
- **The ONE sanctioned golden change** is the `specialization` field in `src/tests/goldens/character_seed_fixture.json` (Task 1), done via `UPDATE_GOLDENS=1` with owner sign-off, its own commit. Any OTHER golden diff — boot-log, combat (`CharacterizationCombatTest`), any other JSON — is a STOP.
- USER CONSTRAINT: no third-party libraries in the game binary; legacy→JSON converters + frozen 32-bit `legacy_*_fixture.bin` fixtures intact and never regenerated; RNG owned (`rots_rng`).
- New-test ASan gate (2026-07-10): the merge's new MSDP tests get one macOS `-fsanitize=address` run.
- Never commit lib/ or log/ content; imperative subjects ≤72; backup tarball before any lib/-touching boot; `.claude/.no-autoformat` exists; i386 test binary cwd `/rots/src/tests`; blocking foreground `gh run watch` only; never `git clean`/`reset --hard`/`switch`/`stash`; no `rm -rf build` in containers.
- Work happens ON `merge/upstream-2026-07-10` (tip `d9e56a5`). Do NOT merge to master or push master until the final merge decision (owner's).
- The upstream *features* are accepted (already merged); this effort validates interaction + portability, not their product design.

**Scout anchors (verified 2026-07-10):**
- Specialization: unconditionally emitted by all 3 writers — `src/character_json.cpp:2192` (v1 `output <<`), `:2445-2446` (v2a `writer.raw/number`), `:2700-2701` (v2b); read `:1782`, validate `:393` (range `PLRSPEC_NONE .. game_types::PS_Count-1`), store↔json `:1854`/`:2004`. Golden `src/tests/goldens/character_seed_fixture.json` currently has ZERO `specialization` (confirmed) → it WILL change.
- Golden test: `src/tests/characterization_json_tests.cpp` `SerializeMatchesGolden` (kGoldenPath = `.../character_seed_fixture.json`); `UPDATE_GOLDENS=1` branch at `:215`. Run from `src/tests/`.
- prac over-read: `src/spec_pro.cpp:239` `char str[255], str2[255];`; `:298` `memcpy(str2, item.c_str(), 255)` — `item` is a `std::string` from `split()` (`:211`); if `item.size()+1 < 255` this reads past the string buffer. Also `:301` `strncmp(str2, "all", strlen(str2))`, `:304` `sprintf(str, " %s", str2)` + `strcat(arg2, str)`. `split()` uses `std::regex` on user input (`:212`).
- prac headers: commit `27ca9d8` added `<regex>`/`<iostream>` to `spec_pro.cpp` and DROPPED `#include "platform_compat.h"` + `#include "safe_template.h"`.
- MSDP buffers: `src/protocol.h:264-267` `PendingInput[4]`/`PendingInputLength`, `IacInput[MAX_PROTOCOL_BUFFER+1]`/`IacInputLength`; `MSDPSanitizeValue(const char*)` decl `:502`. Reset at `protocol.cpp:318-320`.
- Command slots (aligned, our master): `interpre.cpp:2238` overrun@245, `:2244` mob_csv@248, `:2246` convert_plrobjs@249, `:2248` convert_exploits@250, `:2250` savebench@251; `command[]` strings `:562-564`.
- Pre-existing qemu segfault: `AccountManagement.FormatsCharacterPromptWithLinkedCharacterList` (on master too; SIGSEGV under qemu-i386, suspected emulation artifact).

---

### Task 1: JSON golden update for `specialization` (the sanctioned golden change)

**Files:** `src/tests/goldens/character_seed_fixture.json` (regenerated), report only for the census/offset checks. No production code changes expected (the merge already added the field).

- [ ] **Confirm unconditional emission.** Read `src/character_json.cpp:2192,2445-2446,2700-2701` — verify all 3 writers emit `"specialization"` on every call (no `if`-gate). If ANY is conditional, STOP and report (the golden may not need to change / may change non-deterministically).
- [ ] **RED-confirm the golden is stale.** From the i386 container `src/tests/`: `./bin/tests --gtest_filter=CharacterizationJson.SerializeMatchesGolden` → EXPECT FAIL (golden lacks the new field). Capture the failure diff — it must show ONLY the added `"specialization": <n>` line.
- [ ] **Owner sign-off gate.** This regenerates a characterization golden — the deliberate, sanctioned change. Present the exact byte-diff (only the specialization line added) to the owner and get explicit sign-off before regenerating (mirror the Wave-2 zone recapture discipline).
- [ ] **Regenerate + verify.** `UPDATE_GOLDENS=1 ./bin/tests --gtest_filter=CharacterizationJson.SerializeMatchesGolden` from `src/tests/`, then re-run WITHOUT the env var → PASS. `git diff src/tests/goldens/character_seed_fixture.json` must show EXACTLY the one added line, nothing else moved.
- [ ] **validate-before-write census.** Confirm `write_account_character_file`'s new validate-before-write (store → CharacterData → validated store, range `PLRSPEC_NONE..PS_Count-1`) rejects no currently-valid character: if local `lib/` character files exist, census their `profs.specialization` values against the range; else reason from the field's domain and note it. A live char outside the range = a STOP (would fail-closed on save).
- [ ] **Cross-ABI offset.** Confirm `profs.specialization` sits at the same struct offset on i386 vs 64-bit (the frozen 32-bit fixture suites + LLP64 probe re-verify layout — run them, they must still pass).
- [ ] i386 + rots64 ctest green (goldens now match); commit `test: regenerate character JSON golden for upstream specialization field` (body: sanctioned change, owner sign-off date, the one-line diff).

---

### Task 2: MSDP surface (#275) vs our rots_net shim + MSSP

**Files:** verification only unless a defect surfaces; then `src/protocol.cpp` / `src/comm.cpp`.

- [ ] Run the merged MSDP/protocol tests (`src/tests/protocol_tests.cpp` additions) in i386 + rots64 containers; all pass. Run the new MSDP tests once under macOS ASan (per the new-test gate) — no use-after-free / buffer error.
- [ ] **Split-subnegotiation reassembly through rots_net.** Trace `process_input`/read path in `comm.cpp` + the `IacInput`/`IacInputLength` accumulation in `protocol.cpp`: verify a `IAC SB MSDP … IAC SE` sequence split across two `rots_net::read_socket` chunks reassembles (the buffer assumes chunk boundaries our hand-rolled shim may hit differently than upstream's original path). If no test covers a mid-subneg split, add one to `protocol_tests.cpp` (feed the bytes in two calls) — MSVC-safe fixture per the ASan gate. Confirm the `PendingInput[4]` prefix + single-byte IAC-tail carry compose correctly.
- [ ] **Sanitizer non-conflict.** grep for any parallel outbound-escaping we added (MSSP emitter, other telnet subneg); confirm `MSDPSanitizeValue` is the only escaper on the MSDP path and nothing double-escapes. Eyeball a room/weather name with an embedded control char through `MSDPSanitizeValue` → valid JSON string output.
- [ ] **NULL-pProtocol guards.** Confirm the new `MSDPSend*`/`MSDPFlush` NULL guards don't mask a real descriptor-lifecycle bug (are there call paths where pProtocol is legitimately NULL, or does NULL there indicate a bug our socket layer should not reach? — reason + note).
- [ ] Any defect → fix on-branch (minimal, preserve upstream's intent), re-run. i386 + rots64 green. Commit if changed (`fix: <specific MSDP interaction>`); else record "verified, no change" in the report.

---

### Task 3: prac command (#271) — memcpy over-read fix + header/portability

**Files:** `src/spec_pro.cpp` (the `memcpy` fix + any restored include); `src/tests/spec_pro_tests.cpp` or nearest home for a focused prac test if reachable.

- [ ] **Fix the `memcpy` over-read (known real defect).** `src/spec_pro.cpp:298` `memcpy(str2, item.c_str(), 255)` over-reads when `item` is shorter than 255. Replace with a bounded, NUL-terminated copy that preserves behavior for in-range input:
```cpp
// item is a std::string token from split(); copy at most 254 chars + NUL into str2[255].
const std::size_t copy_len = std::min(item.size(), sizeof(str2) - 1);
std::memcpy(str2, item.c_str(), copy_len);
str2[copy_len] = '\0';
```
  Verify the downstream `is_number(str2)` / `strncmp(str2,"all",strlen(str2))` / `sprintf(str," %s",str2)` still behave identically for normal `"N"`/`"all"`/`skill` tokens.
- [ ] **Header transitivity — CI is the gate.** `spec_pro.cpp` dropped `platform_compat.h`/`safe_template.h` and added `<regex>`/`<iostream>`. It compiled on i386; MSVC + AppleClang header transitivity differs. The `windows-msvc` + `macos-arm64` CI jobs (Task 5's push) are the real check — if either fails to compile `spec_pro.cpp`, restore the minimal include(s) it actually needs (grep the file for `platform_compat`/`safe_template` symbol use — if none, the drop is fine; a compile error means a transitive dependency was lost).
- [ ] **handle_pracs logic.** Verify the batched `"N <skill>"`/`"all <skill>"` path decrements `spells_to_learn` and clamps `knowledge` correctly per iteration (read `handle_pracs`); add a focused test if the guild SPECIAL is unit-reachable, else note it for the account/live smoke.
- [ ] i386 + rots64 ctest green; commit `fix: bound prac-command token copy; verify header portability` (body: the over-read, the header disposition).

---

### Task 4: Command-table alignment + pre-existing qemu segfault

**Files:** verification only; report.

- [ ] **Static alignment.** Confirm `interpre.cpp`'s `command[]` strings (`:562-564`: convertplrobjs@249, convertexploits@250, savebench@251) and the `COMMANDO(N, …)` slots (`:2246-2250`) are index-aligned for entries 245–252, with no off-by-one introduced by the merge. Any misalignment silently misroutes every command past the divergence → STOP + fix.
- [ ] **Level-100 roster test off-qemu.** Confirm `InterpreAccountMenu.…LevelOneHundred…` (upstream's new test, ported to `ScopedTestWorld`) passes on rots64 + CI (not just i386-under-qemu).
- [ ] **Pre-existing segfault triage.** Run `AccountManagement.FormatsCharacterPromptWithLinkedCharacterList` on rots64 (native amd64, not qemu) and confirm from the CI macOS + CI i386 jobs. If it passes off-qemu everywhere → confirmed qemu artifact, note it (it predates this merge, out of scope to fix here but recorded). If it FAILS off-qemu → real pre-existing bug, record + flag to owner (still not this effort's to fix unless the merge worsened it — confirm it fails identically on master).
- [ ] Record findings in the report; no commit unless a real misalignment fix was needed.

---

### Task 5: Battery + merge readiness

**Files:** report; no code unless a battery failure surfaces a defect.

- [ ] Full container battery: i386 (`make test` ctest + tests-Makefile runner + `boot-golden.sh verify`) + rots64 (`ctest --preset linux-x64` + `boot-golden.sh --service rots64 verify`). Backup tarball before the boots. Boot-goldens byte-identical (the boot golden did NOT change this effort — only the JSON golden did, in Task 1). Combat goldens byte-identical.
- [ ] `make smoke-account` — env-blocked (no cargo / Python≥3.10); use the manual `nanny()`-drive substitute of record (the merge touches interpre.cpp's account menu, so exercise account create → menu → character select). Document the drive.
- [ ] **Four-platform CI (finalization gate).** Push `merge/upstream-2026-07-10`; blocking `gh run watch` → all four required jobs green. The CI **macos-arm64** job is this effort's macOS coverage (local macOS-native is wedged). windows-msvc must pass — it's the real gate for Task 3's header portability. Record run URL + per-platform counts.
- [ ] **Deferred to post-reboot:** local macOS-native `boot-golden.sh --native build/macos-arm64/ageland verify` ONLY (redundant confirmation). Record it as the single open item.
- [ ] Update the ledger + write the effort's exit note (validation outcomes per surface, the sanctioned golden change, CI URL, the one deferred item). Then the final whole-branch review (Fable, `review-package master..HEAD`) and the **merge-to-master decision to the owner**.

---

## Self-review notes (write-time)

- Spec coverage: Task 1↔golden/specialization + validate-before-write + cross-ABI; 2↔MSDP surface; 3↔prac (memcpy + headers + handle_pracs); 4↔command-table + qemu segfault; 5↔battery + CI + merge. All spec sections mapped.
- No placeholders; anchors verified 2026-07-10; the two concrete changes (specialization golden, prac memcpy) have exact code/commands.
- Golden discipline: the ONE sanctioned change (JSON specialization, Task 1, sign-off) stated in Global Constraints + Task 1 + Task 5. Boot/combat goldens are STOP-on-diff.
- Deferred item (local macOS-native boot-golden) isolated to Task 5, everything else runs now.
- Type consistency: MSDPSanitizeValue(const char*), IacInput/PendingInput field names, split()/handle_pracs, command slot numbers all match the scouted source.

---

## Effort Exit

**Date:** 2026-07-11. **Final commit:** `2036e5f` (branch `merge/upstream-2026-07-10`, not merged to master — pending owner decision).

### Per-surface validation outcomes (Tasks 1-4)

- **Task 1 (specialization JSON golden):** sanctioned change, owner sign-off obtained, commit `9e05453`. All 3 writers (`character_json.cpp:2192/2445-2446/2700-2701`) unconditionally emit the field; golden diff = exactly one line (`"specialization": 0` after `two_handed`); validate-before-write census showed zero currently-valid local characters rejected; cross-ABI fixture/LLP64 suites (72/72) confirm no layout shift.
- **Task 2 (MSDP surface #275):** verified, one real pre-existing defect found and fixed by the new-test ASan gate — `ProtocolDestroy` was `free()`-ing `new`/`new[]`-allocated protocol memory (predates the merge); fixed to matching `delete`/`delete[]` (commit `017ef2c`). Split-subnegotiation reassembly, sanitizer non-conflict, and NULL-pProtocol guards all verified correct, no further change needed.
- **Task 3 (prac command #271):** memcpy over-read at `spec_pro.cpp:298` fixed (bounded copy, `std::min(item.size(), 254)` + explicit NUL), header transitivity confirmed moot (`platform_compat.h`/`safe_template.h` already present and actively used), `handle_pracs` batch logic verified correct, new `HandlePracs` test suite added and ASan-clean (commit `b44da18`).
- **Task 4 (command-table + qemu segfault):** command-table alignment (entries 245-252) confirmed correct, no off-by-one from the merge. `AccountManagement.FormatsCharacterPromptWithLinkedCharacterList`'s SIGSEGV under qemu-i386 on this Apple-Silicon host confirmed as a qemu-user emulation artifact (passes cleanly on rots64 native and on every CI runner, native hardware in all cases) — not a product defect, out of scope, no fix needed.

### Container battery (this task)

- **rots64:** `ctest --preset linux-x64` 758/758 pass, 0 fail. `boot-golden.sh --service rots64 verify` byte-identical.
- **i386:** isolated per-test `ctest` (each test its own process via `gtest_discover_tests` PRE_TEST mode) 758/758 pass, 0 fail — **including** `AccountManagement.FormatsCharacterPromptWithLinkedCharacterList`, confirming the qemu crash is specific to the *monolithic* single-process runner, not the test itself. The monolithic `tests-Makefile` runner reproduced the documented qemu-i386-only SIGSEGV at that same test twice (once before, once after this task's fixes) — tolerated per the Global Constraints, cross-confirmed passing on rots64 and (this run) on native CI i386 hardware. `boot-golden.sh --service rots verify` byte-identical. Backup tarball taken first (`../rots-lib-backup-2026-07-10-2310.tar.gz`, 59,370 entries).
- No boot/combat/JSON golden moved beyond Task 1's one sanctioned line.

### Account smoke (manual nanny()-drive substitute)

`make smoke-account` env-blocked (no cargo to build the proxy; host Python 3.9.6 < 3.10). Drove the actual `nanny()` state machine by hand over plain telnet against rots64 (native amd64, sidesteps the qemu artifact), with `ROTS_SENDMAIL_COMMAND` capturing the verification-code email inside the container. Two passes: (1) create account → verify email → account menu → create character "Usvsmoke" (Human Warrior) → full creation wizard → enter game → no crash → `info` shows "You are not specialized in anything." → `quit` (account-linked quit keeps the connection at the character menu rather than closing it) → confirmed on-disk `usvsmoke.character.json` has `"specialization": 0`. (2) Fresh reconnect → login → "Play a linked character" → reloads from disk → enter game → no crash → `info` again shows the same line, proving the account-native JSON round-trip (write on quit, read on fresh reconnect) preserves the new field with no corruption. Cleaned up: `lib/accounts/U-Z/usvsmoke1783740149@example.com/` and `lib/plrobjs/U-Z/usvsmoke.objs.json` deleted; `git status` clean; smoke container stopped+removed. Full transcript: `.superpowers/sdd/usv-task5-smoke-transcript.log`.

### Four-platform CI (finalization gate)

Five push+watch cycles were needed to reach all-green — each of the first four surfaced a **real Windows-only defect the containers could never catch**, since `windows-msvc` had never once compiled the branch's own tests to completion before this task (two earlier issues had always blocked it first):

1. **`mkdir` arity** (`interpre_account_menu_tests.cpp`, 27 sites): POSIX `mkdir(path, mode)` doesn't compile on MSVC (`_mkdir` takes no mode). Fixed via a new `rots_mkdir()` shim in `test_platform_compat.h` (commit `b2a8306`).
2. **`socketpair`/`fcntl`** (`protocol_tests.cpp`'s `ProtocolDescriptor` fixture, all 39 `ProtocolInput`/`MSDPProtocol` tests): POSIX-only APIs with no Windows equivalent, compiled unconditionally. Fixed via a portable AF_INET loopback pair for Windows (commit `83d46ef`), then corrected to keep POSIX on its original `socketpair()` path after the AF_INET version caused a macOS timing regression (commit `3ead7fb`).
3. **Dangling `output` self-pointer** (`interpre_account_menu_tests.cpp`, 25 `make_descriptor()` call sites in the upstream merge's new "linked-roster active-session/unlock" test family): a pre-existing, already-documented MSVC-Debug NRVO pitfall (Phase 3 Task 6) that the merge's new tests omitted the established workaround for. 10 tests observably failed with corrupted (but logic-correct) output text; 15 more were latently unsafe (including the Task-4-verified level-100-roster test, passing only by luck). Fixed by adding the missing re-point line to all 25 sites (commit `2036e5f`).

**Final run:** all four required jobs green — https://github.com/drelidan7/RotS_Live_Modern/actions/runs/29139211323
- Linux i386 legacy: 751/758 passed (native CI hardware, no qemu)
- Linux x64: 758/758 passed
- macOS arm64: 758/758 passed
- Windows MSVC: 754/754 passed (4 fewer total — POSIX-only suites, e.g. `rots_net_tests.cpp`'s `PREDEF_PLATFORM_LINUX`-gated tests, don't exist as compiled translation units on Windows, by design)

No golden (boot-log, combat, or JSON) moved in any of the three fix commits — all changes are test-file-only.

### Deferred item

Local macOS-native `boot-golden.sh --native build/macos-arm64/ageland verify` — the one item deferred to post-reboot (iCloud FileProvider wedge). CI's `macos-arm64` job (GitHub runner, unaffected by the local wedge) is this effort's macOS coverage in the meantime.

### Merge decision

Branch is four-platform green, container battery clean, smoke verified, no unsanctioned golden movement. **Merge-to-master is the owner's decision** (not made here per the Global Constraints).
