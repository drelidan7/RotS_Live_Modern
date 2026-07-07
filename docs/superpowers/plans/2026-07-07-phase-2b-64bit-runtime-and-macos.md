# Phase 2b: 64-Bit Runtime + macOS Port Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver the spec's Phase 2 exit: 64-bit Linux and native macOS arm64 builds that boot against the real data, pass the full suite, and match the characterization goldens — with the linux-x64 and macos-arm64 CI jobs promoted to required and Docker no longer needed for Mac development.

**Architecture:** Second half of Phase 2 (first half, JSON-only persistence, is merged as of f9b849e). Order is dictated by evidence: the two NAMED BLOCKERS from Phase 2a's final review come first (the account-staged binary bridge — which is already the concrete cause of the linux-x64 CI regression on master — and the exploits memcpy decoder + 5,728-file lazy tail), then cross-platform enablers (portable RNG test seam, portable `$6$` crypt), then the macOS compile fixes, then runtime verification on both new platforms, then CI promotion.

**Tech Stack:** C++17, CMake presets (Phase 1), GoogleTest, Docker (i386 legacy + new amd64 runtime container), vendored public-domain SHA-512-crypt, GitHub Actions.

## Global Constraints

- The 32-bit container battery gates every task: monolithic runner (currently 551 pass / 7 skip / 0 fail, 558 total), ctest 558/558, `scripts/boot-golden.sh verify` exit 0.
- Gameplay behavior unchanged; characterization goldens (combat/JSON/boot) must pass untouched on EVERY platform — that is the phase's core claim. The six `legacy_*_fixture.bin` goldens are 32-bit-frozen (tests already skip/hard-fail correctly on 64-bit — do not weaken those guards).
- Live-data safety rules from Phase 2a stand: backup tarball before any lib/-mutating step; converters non-destructive; never commit lib/ or log/ content.
- Existing account password hashes are `$6$<16-char-salt>$` glibc SHA-512-crypt strings — they MUST verify byte-identically forever on every platform (that is Task 4's acceptance bar).
- `.claude/.no-autoformat` must exist before editing C++; role comments on new file/class-scope variables; commit subjects imperative ≤72.
- Branch pushes to origin are pre-authorized for CI verification on `modernization/**` (established Phase 1); master pushes remain a user decision at merge time.

**Research anchors (verified 2026-07-07):**
- Blocker 1 evidence: master CI run (post-f9b849e) linux-x64 job fails exactly in `AccountManagement.{WritesAndReadsAccountNativeObjectFile, AccountNativeObjectWriteRejectsLegacyObjectFileWithoutFollowerSection, WritesDefaultAccountNativeObjectFile, WritesCanonicalObjectPathWhenSafeLegacyRelativePathIsStored, ...}` — the byte-oriented `write_account_object_file`/`read_account_object_file` API round-trips native-struct binary (`account_management_assets.cpp:147-174`, `object_save_data_to_binary` native `append_pod`) that the explicit-offset 32-bit decoder then rejects on LP64.
- Blocker 2: `exploits_json::exploit_records_from_binary` (`exploits_json.cpp:119-141`) is the last memcpy decoder (guarded by `static_assert(sizeof(exploit_record)==80)`); 5,728 legacy `.exploits` files await lazy conversion.
- RNG seam: `-Wl,--wrap=_Z6numberv -Wl,--wrap=_Z6numberii` (GNU ld only) in `src/tests/Makefile` and `src/CMakeLists.txt`; seam implementation `src/tests/test_random_utils.cpp` (queue + `__wrap_`/`__real_` pairs).
- Crypt: `$6$` generation at `account_management.cpp:175-176`, verify at `account_management_identity.cpp:174`, `#include <crypt.h>` at `account_management.cpp:9`; macOS crypt() cannot do `$6$`.
- macOS compile seeds (`.superpowers/phase2-seed-*.txt` + Phase 1 CI logs): `register` storage class (18 uses: act_info×4, act_wiz×2, db×2, comm×3, handler×2, interpre×4, modify×1); `MIN`/`MAX` undeclared (17 files use them; the utils.h definitions at :146-147 are commented out — glibc leaks them via sys/param.h, macOS/MSVC don't); `crypt.h` not found; `singleton.h:20/22` calls virtual member functions `on_instance_destroyed()`/`on_instance_not_created()` from the static `instance()` (clang rejects at definition; GCC only diagnoses on instantiation); `-mfpmath=sse` invalid on arm64 (needs an x86-only arch guard).
- docker-compose.yml has one service `rots` (i386 bullseye); repo bind-mounted at /rots.

---

### Task 1: Retire the account-staged binary bridge (Blocker 1 — fixes red linux-x64 CI)

**Files:**
- Modify: `src/account_management_assets.cpp` / `.h` (the byte-oriented object-file API), `src/interpre.cpp` (staging consumer ~:3092), `src/db.cpp` (`load_object_save_bytes_for_character` and callers), `src/objsave.cpp` (`open_account_backed_object_stream` if it still consumes bytes)
- Modify: `src/tests/account_management_tests.cpp`, `src/tests/interpre_account_menu_tests.cpp`, `src/tests/db_loader_tests.cpp` (call-site migration to the struct API)

**Interfaces:**
- Produces: `bool account::read_account_object_data(const std::string& accounts_root, const std::string& account_name, const std::string& character_name, objects_json::ObjectSaveData* data, std::string* error_message)` and the matching `write_account_object_data(..., const objects_json::ObjectSaveData&, ...)` — the byte-vector variants are deleted, not deprecated. Every consumer passes `ObjectSaveData` (or the JSON string) end to end; `object_save_data_to_binary` becomes test-only (encode side of round-trip tests) with a comment saying so.

- [ ] **Step 1 (RED):** Convert the five failing AccountManagement tests (plus any sibling using byte vectors) to the struct API — they will fail to compile because `read_account_object_data`/`write_account_object_data` don't exist. That compile failure is the red state; capture it.
- [ ] **Step 2 (GREEN):** Implement the struct API (JSON in the file, `serialize_objects_to_json`/`deserialize_objects_from_json` at the boundary — no binary hop anywhere), migrate all production consumers (staging path decodes legacy `.obj` bytes ONCE via the portable decoder, then everything downstream is `ObjectSaveData`), delete the byte-vector API, and confirm `grep -n 'object_save_data_to_binary' src/*.cpp` shows only objects_json.cpp's definition + test files.
- [ ] **Step 3:** Full 32-bit battery (runner, ctest, boot-golden) — all green; numbers reported.
- [ ] **Step 4:** Push the branch; confirm the linux-x64 CI job is GREEN again (this task's whole point). Record the run URL.
- [ ] **Step 5:** Commit: `refactor: account object staging passes ObjectSaveData, not binary`

---

### Task 2: Exploits — explicit-offset decoder + batch sweep (Blocker 2)

**Files:**
- Modify: `src/exploits_json.cpp` (decoder + encoder to explicit-offset, keep the size static_assert), `src/db.cpp` (lazy path unchanged semantics)
- Create: `convertexploits` imp command mirroring `convert_plrobjs.cpp` (same file or sibling `src/convert_exploits.cpp` — follow the existing wiring), tests in `src/tests/pod_persistence_json_tests.cpp` (extend)

**Interfaces:**
- Consumes: the Phase 2a converter contract verbatim (decode → serialize → re-decode → equality → atomic write → `.migrated`; non-destructive skips; report).
- Produces: `int convert_all_legacy_exploits(const char* exploits_root, std::string* report)`; the lazy per-login path remains for stragglers.

- [ ] **Step 1 (TDD):** Explicit-offset decode/encode for `exploit_record` (fields per db.h:222 — int/sh_int/char[30]; verify offsets against a container `offsetof` probe and the frozen `legacy_exploits_fixture.bin`); existing tests must pass unchanged (they are the oracle).
- [ ] **Step 2 (TDD):** The sweep command against synthesized fixtures (valid + corrupt + already-converted), then full battery.
- [ ] **Step 3:** BACKUP (`tar czf ../rots-lib-backup-$(date +%Y-%m-%d-%H%M).tar.gz lib/`, verify >25MB), run the live sweep over `lib/exploits/` (expect ~5,728 conversions; the 3 known stray top-level flat files skip — report exact numbers; ANY unexpected skip = stop and report).
- [ ] **Step 4:** Boot twice (steady state), boot-golden verify, spot-check one converted character's exploits in-game (`exploits` command or equivalent — find it in act_info/act_wiz), commit: `feat: exploits explicit-offset codec + batch conversion sweep`

---

### Task 3: Portable RNG test seam (replace GNU-ld --wrap)

**Files:**
- Modify: `src/utility.cpp` (seam hook in `number()` overloads), `src/tests/test_random_utils.cpp`/`.h`, `src/tests/Makefile` (drop wrap flags), `src/CMakeLists.txt` (drop wrap flags)

**Interfaces:**
- Produces: same public test API (`clear_test_random_values`, `push_test_random_value`) working on ALL platforms. Design: a TESTING-only hook —

```cpp
// utility.cpp (file scope)
#ifdef TESTING
// Test seam: when non-null, number()/number(int,int) consume queued values
// instead of the PRNG. Set only by test_random_utils.cpp; never in production
// builds (the symbol does not exist without -DTESTING).
double (*rots_test_random_hook)() = nullptr;
#endif
```

with `number()` checking the hook first under `#ifdef TESTING`, and `test_random_utils.cpp` installing a hook that pops the queue (returning the clamped value; empty queue → fall through to the real PRNG, preserving current `__wrap_` fallback semantics — read the existing wrap functions and reproduce their EXACT fallthrough behavior including the int-range mapping in `__wrap__Z6numberii`).

- [ ] **Step 1 (RED):** Remove the wrap flags from both test builds; the seam-dependent tests (proc tests that queue values) now get real randomness and fail/flake — run one such test to demonstrate, then
- [ ] **Step 2 (GREEN):** Implement the hook; the seam tests pass again. Full battery (all 558) + verify the production build (`ageland` target) contains no hook symbol (`nm` or successful build without TESTING).
- [ ] **Step 3:** Commit: `test: portable RNG seam replaces GNU-ld --wrap`

---

### Task 4: Portable `$6$` SHA-512-crypt

**Files:**
- Create: `src/rots_crypt.h`, `src/rots_crypt.cpp` (vendored public-domain SHA-512-crypt — Drepper reference algorithm — wrapped as `const char* rots_crypt(const char* key, const char* setting)`; thread-safety note: static buffer like libc crypt is acceptable, the game is single-threaded)
- Modify: `src/account_management.cpp` (:9 include, :176 call), `src/account_management_identity.cpp` (:174), `src/act_wiz.cpp` (its crypt call — check its salt style: if legacy DES two-char salts, `rots_crypt` must ALSO handle classic DES or that call keeps libc crypt behind `#ifndef _WIN32` with a Phase 3 note — investigate and pick, documenting the choice)
- Create: `src/tests/rots_crypt_tests.cpp`; Modify: build lists

**Interfaces:**
- Produces: `rots_crypt(key, setting)` — for `$6$...` settings, output byte-identical to glibc; used by all account credential paths on every platform.

- [ ] **Step 1 (TDD, the acceptance bar):** In the 32-bit container, generate reference vectors with glibc: 3 fixed (password, salt) pairs → capture glibc `crypt()` output strings. Write tests asserting `rots_crypt` reproduces them exactly, plus a round-trip test through `generate_hash_for_secret`/`verify_password`. RED (no implementation) → GREEN.
- [ ] **Step 2:** Wire the three call sites; delete `#include <crypt.h>`; keep `-lcrypt` only if act_wiz's DES path still needs it (per the Step-4 investigation above). Full battery — and CRITICALLY: log into an existing account in the container (real hash verifies through the new code path).
- [ ] **Step 3:** Commit: `feat: portable SHA-512-crypt for account credentials`

---

### Task 5: 64-bit/macOS compile fixes — macos-arm64 preset goes green locally

**Files:**
- Modify: `src/utils.h` (MIN/MAX proper definitions — `#ifndef MIN` guarded), the 7 files with `register` (18 deletions), `src/singleton.h` (make the two handlers static — they are empty stubs; fix the `instance()` return while there if it doesn't compile under clang; keep behavior identical for the GCC build), `src/CMakeLists.txt` (SSE flags gated to x86: wrap `-msse2 -mfpmath=sse` in a `CMAKE_SYSTEM_PROCESSOR MATCHES "x86|i.86|AMD64|x86_64"` condition or equivalent genex)
- Possibly small includes (`unistd.h` etc.) as AppleClang errors dictate — each fix minimal, listed in the report

**Interfaces:** none new — this task ends when `cd src && cmake --preset macos-arm64 && cmake --build --preset macos-arm64 -j8 && ctest --preset macos-arm64` all succeed natively on this Mac (brew-install googletest if absent). The characterization goldens passing on macOS is the headline claim — combat transcript + character JSON must match the committed goldens bit-for-bit (mt19937 + SSE-double math make this expected; any mismatch is a STOP-and-report finding, not a golden update).

- [ ] Step 1: Iterate compile fixes against the macos-arm64 build until clean (each category its own micro-commit or one grouped commit — implementer's judgment, subjects imperative). 32-bit battery re-run after EACH category that touches shared code (register/MIN/MAX/singleton are shared).
- [ ] Step 2: `ctest --preset macos-arm64` — expect: full suite green, with the six legacy-fixture suites SKIPPED (by design, sizeof(long)!=4... **note: macOS arm64 long IS 8 bytes, skip fires correctly**) and characterization goldens PASSING.
- [ ] Step 3: Full 32-bit battery + commit(s): `fix: 64-bit/macOS compile portability (register, MIN/MAX, singleton, SSE guard)`

---

### Task 6: 64-bit Linux runtime container + cross-ABI boot verification

**Files:**
- Create: `Dockerfile.x64` (FROM debian:bookworm; g++ make cmake libgtest-dev libcrypt-dev python3 telnet procps ca-certificates), compose service `rots64` in `docker-compose.yml` (same /rots bind mount, image `rots-dev:bookworm-x64`)
- Modify: `scripts/boot-golden.sh` (accept `ROTS_SERVICE` env or `--service rots64` to choose the container; default `rots` unchanged), `scripts/rots-docker.sh` (optional `ROTS_SERVICE` passthrough — keep default behavior identical)

**Interfaces:**
- Produces: `docker compose run --rm rots64 ...` builds (via `cmake --preset linux-x64` inside, or plain cmake -DROTS_LEGACY_32BIT=OFF) and boots the 64-bit binary against the SAME lib/ data.

- [ ] Step 1: Container + build + full suite inside rots64 (expect: suite green with legacy-fixture suites skipped; characterization goldens PASS — same reasoning as Task 5).
- [ ] Step 2: BACKUP, then boot the 64-bit binary against the real lib/ — `scripts/boot-golden.sh --service rots64 verify` must MATCH the committed golden (the world-load lines are ABI-independent text; a mismatch is a real 64-bit behavior difference — STOP and report, never recapture to paper over).
- [ ] Step 3: 64-bit runtime smoke: telnet in, existing character loads with inventory (JSON), boards/mail read, rent cycle, exploits view. Everything the 32-bit smoke covered in Phase 2a Task 7.
- [ ] Step 4: Boot the 32-bit binary again afterward (same lib/) — proves bidirectional data compatibility (JSON is ABI-neutral; this is the demonstration). Commit: `feat: 64-bit Linux runtime container + cross-ABI verification`

---

### Task 7: macOS native runtime

- [ ] Step 1: Native boot on this Mac from the repo root (`build/macos-arm64/ageland` — check the binary's working-directory expectations; the game opens `lib/` relative paths — run from repo root or use its `-d` dir flag per comm.cpp usage), against the same lib/. Listens on :1024; telnet smoke as Task 6 Step 3.
- [ ] Step 2: `scripts/boot-golden.sh --native` mode (new: run the local binary instead of docker; same normalize/compare) — must match the committed golden. Same STOP-on-mismatch rule.
- [ ] Step 3: Docs update happens in Task 9; here just record evidence + commit script change: `feat: native boot-golden mode for macOS`

---

### Task 8: Bounded 64-bit hazard audit

- [ ] Step 1: Run and triage these specific greps (the suite+boots already passed, so this hunts LATENT runtime paths): (a) `grep -rn '%d' src/*.cpp | grep -iE 'long|time\(' | head -50` — %d fed a long is UB on LP64 (log/sprintf paths); (b) `grep -rnE '\(int\)\s*\(?[a-z_]*ptr|\(long\)\s*[a-z_]*ptr' src/*.cpp` — pointer↔int casts; (c) `grep -rn 'time(0)\|time(NULL)' src/*.h src/*.cpp | grep -v 'time_t\|long\|rots_rng'` — time() into int. Fix what is REACHABLE at runtime (each fix minimal + listed); defer cosmetic hits to a committed list `docs/superpowers/phase4-seed-64bit-cosmetic.md`.
- [ ] Step 2: Full 32-bit battery + one rots64 suite run + commit: `fix: 64-bit runtime hazards (formats, casts) + phase-4 seed list`

---

### Task 9: CI promotion, docs, exit checklist

- [ ] Step 1: `.github/workflows/ci.yml`: remove `continue-on-error` from linux-x64 AND macos-arm64 (both now required); update job display names and the header comment; windows-msvc stays allowed-to-fail (Phase 3).
- [ ] Step 2: Docs: BUILD.md matrix rows (linux-x64/macos-arm64 → green/required; note Docker now OPTIONAL for Mac dev — native build is primary, container remains for the 32-bit legacy guard), CLAUDE.md first gotcha rewrite (Docker no longer mandatory; the 32-bit container is the legacy-format guard until Phase 5), AGENTS.md build commands.
- [ ] Step 3: Exit battery — ALL FOUR environments: 32-bit container (runner+ctest+boot-golden), rots64 (suite+boot-golden --service rots64), macOS native (ctest preset + boot-golden --native), plus push branch → CI fully green including the two newly-required jobs. This is the spec's Phase 2 exit criterion, verbatim: "64-bit Linux and native macOS arm64 builds boot, pass tests, and match the Phase 0 characterization goldens. Docker is no longer required for Mac dev."
- [ ] Step 4: Commit docs; final ledger entry.

---

## Plan Self-Review Notes

- **Spec coverage (Phase 2 second half):** "64-bit hazard audit" → Tasks 1/2 (the two remaining ABI-coupled paths) + Task 8 (bounded latent audit); "Drop -m32 from the 64-bit presets" → done in Phase 1; exit criteria → Task 9 Step 3 verbatim. macOS test seam and portable crypt are discovered prerequisites, documented as such.
- **Deviation, justified:** spec Phase 3 assigned password work; reality: account credentials are glibc-`$6$`-crypt and macOS can't verify them — Task 4 pulls the portable-crypt slice forward (Phase 3 keeps the act_wiz legacy-DES removal and Windows).
- **Adaptation points (resolve in-step):** act_wiz crypt salt style (Task 4); singleton.h exact clang errors (Task 5); ageland's working-dir/-d flag for native boot (Task 7); exploit_record offsets vs the frozen fixture (Task 2).
- **Type consistency:** `read_account_object_data`/`write_account_object_data` (Task 1) shape mirrors Phase 2a's `write_player_objects_json`; `--service rots64`/`--native` boot-golden modes consistent across Tasks 6/7/9; converter contract wording identical to Phase 2a.
