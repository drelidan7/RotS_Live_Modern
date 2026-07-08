# Phase 3: Windows + Platform Layer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The `windows-msvc` CI job builds the game and passes the full test suite (characterization goldens included) and becomes a required job — with the networking, filesystem, timing, and signal layers made genuinely cross-platform (Asio, std::filesystem, std::chrono, a signal shim) rather than #ifdef-riddled.

**Architecture:** Spec Phase 3. Order is dependency-driven: portable subsystem migrations that benefit ALL platforms land first, each gated by the full multi-runtime battery + goldens (chrono → filesystem → Asio), then the MSVC-specific shims, then a CI-push-driven Windows bring-up loop (no local Windows machine exists — the windows-2022 runner is the only Windows environment), then CI promotion. The Asio port is a portability move, not a concurrency redesign: the loop stays single-threaded and tick-driven (`io_context::poll()` once per pulse).

**Tech Stack:** Standalone Asio (header-only, vendored), std::filesystem, std::chrono, GoogleTest, GitHub Actions (windows-2022/MSVC).

## Global Constraints

- Full battery gates every task: 32-bit container (runner 579 pass / 7 skip of 586 + ctest 586 + boot-golden), rots64 (ctest + boot-golden --service), macOS native (ctest 515/71 + boot-golden --native). Goldens byte-identical always — a golden diff is a STOP, never a recapture.
- Gameplay/network behavior preservation: telnet byte streams (negotiation, MCCP, MXP), the `-x` proxy 4-byte header protocol (comm.cpp:173-205), prompt/output pacing, and disconnect semantics must be indistinguishable to a client. protocol.cpp's single write seam (`write_to_descriptor` call at protocol.cpp:51) must keep working unchanged.
- USER CONSTRAINT: the legacy→JSON conversion/salvage path stays intact and buildable everywhere (filesystem migration touches the converters — their tests are the gate).
- Windows-runtime reality: the CI runner has no world data, so the Phase 3 Windows exit is **build + full unit suite + goldens green on CI**; live boot verification on Windows is explicitly deferred until a Windows environment with world data exists (deviation from the spec's "boots" wording — noted in self-review; a committed minimal test world is the future enabler, out of scope here).
- Dependency policy deviation (justified): the spec said Asio "pinned via FetchContent", but the i386 container builds offline via Make — so Asio is **vendored** at a pinned release under `third_party/asio/` (header-only, one directory, LICENSE included), serving all four build paths identically.
- Never commit lib/ or log/ content; `.claude/.no-autoformat` before C++ edits; imperative subjects ≤72; backups before any live-data-touching boot.

**Scout anchors (verified 2026-07-07):**
- Game loop: `comm.cpp` `game_loop():683`, fd_sets :733-741, poll-select :758 + sleep-select :765 (the pulse throttle; OPT_USEC=250000 :46), `gettimeofday` :703/:745 (+ `clock.cpp:8,15`, `fight.cpp:2732`), BSD `sigmask()/sigsetmask()` :728/:756/:772 (4.3BSD, non-POSIX — needs sigprocmask shim or EINTR-safe removal), pulse++ :1018 rollover :1091.
- Sockets: `init_socket():1289` (bind-failure runs `system("touch ../.killscript")` :1334), accept `pnew_connection():1346`, `nonblock():1950` (fcntl O_NONBLOCK), `close_socket():1863`, `process_input():1664` (read :1683), `process_output():1512`, `write_to_descriptor():1604`; blocking `gethostbyaddr` in `populate_descriptor_host():133-148` (guarded by `nameserver_is_slow`); proxy header `finish_proxy_header_if_ready():173-205`.
- Signals (`signals.cpp:33-67`): SIGUSR1/USR2 reload handlers; HUP/INT/TERM → `hupsig` (Emergency_save+exit); ILL/FPE/BUS → `diesig`; PIPE ignored; SIGVTALRM handler dead (setitimer commented out). No fork/hotboot anywhere.
- POSIX-isms: `bzero` (~14 files), `asprintf` (9 sites: act_info.cpp:3694, fight.cpp:524/528/532, pkill.cpp:1067/1070/1073, spec_pro.cpp:811/1056, utility.cpp:1700), `strcasecmp` direct (color.cpp ×6, db.cpp ×2, script.cpp ×1), dirent (db.cpp:587-813, account_management.cpp bucket scans, both converters :42-61), `system("mv/cp/rm/touch ...")` (db.cpp:2785/2993/3000/5273/5293/5297 + boards/mail/objsave/pkill/account files), `platdef.h` is the existing shim seam (SocketType=int).
- Legacy character passwords: `CRYPT(a,b)` is a no-op macro (interpre.h:26); the ONLY live consumer is the legacy-character account-linking check at interpre.cpp:3333 (CON_ACCTLEGPWD, plain strncmp, no OS dependency); all other CRYPT paths are unreachable (CON_NME:2710 always routes to account email; `!*d->account_name` branches vestigial). Account-native chars persist the `"*ACCOUNT*"` sentinel (interpre.cpp:2288/2334). → Phase 3 password scope is therefore SMALL: rots_crypt secret scrubbing + a documented decision to leave the legacy-import check as-is (it verifies ownership of pre-account characters against the frozen legacy files; changing those files would break the live-server migration path).
- **LLP64 trap:** Windows `long` is 4 bytes — the `sizeof(long) != 4` GTEST_SKIP guards DO NOT skip on Windows, and the `#if !defined(__LP64__) && !defined(_WIN64)` static_asserts DO compile away there. The legacy-fixture native-struct builders may genuinely produce 32-bit-compatible layouts on LLP64 (obj_file_elem has no pointers; long members are 4 bytes) — Task 5 must VERIFY record sizes on MSVC and either let the fixture suites run (if layouts match: bonus coverage) or extend the guards to `defined(_WIN64)`; never let them fail ambiguously.
- Current MSVC error census (master CI): missing-function-header/`;` syntax errors (~19, likely a platform #if hole), `bzero` (18), `time_t`/`time`/`localtime`/`asctime` visibility errors (~50, include/using-declaration issues in the Windows branch of headers), `asprintf` (10), `strcasecmp` (8), `dirent.h` (4), `write`/`close`/`chmod` (unistd).

---

### Task 1: Vendor Asio + platdef Windows scaffold

**Files:** Create `third_party/asio/` (pinned standalone release, headers + LICENSE + a README noting version/provenance); modify `src/Makefile`, `src/tests/Makefile`, `src/CMakeLists.txt` (include path `-Ithird_party/asio/include` / `target_include_directories ... SYSTEM`); extend `src/platdef.h` with the Windows branch skeleton (winsock2.h/ws2tcpip.h, `SocketType`, and a `rots_platform` note that Asio owns sockets after Task 4).

**Interfaces produced:** `#include <asio.hpp>` compiles in all four build paths (prove with a temporary smoke TU compiled then deleted, or a permanent trivial test asserting `asio::io_context` constructs). `ASIO_STANDALONE` defined project-wide for the affected targets.

Steps: fetch a pinned Asio release (implementer records exact version+sha in the README); wire includes; smoke-compile in 32-bit container, rots64, macOS; full battery; commit `build: vendor standalone Asio for the platform layer`.

---

### Task 2: Chrono timing migration

**Files:** `src/comm.cpp` (game_loop timing :683-772 region), `src/clock.cpp`, `src/fight.cpp:2732` region, plus the BSD `sigmask()` sites.

Replace `gettimeofday`/`timeval` arithmetic with `std::chrono::steady_clock` (identical pulse math: 250ms target per pulse, sleep = target − elapsed, clamped ≥0); the sleep-select (:765) becomes `std::this_thread::sleep_for` (Asio timer arrives in Task 4 — don't pre-couple); the poll-select stays for now (Task 4 removes it). Replace `sigmask()/sigsetmask()` with a small `platdef`-declared shim: POSIX = `sigprocmask` with the equivalent mask set, Windows = no-op — AND make the remaining select/read paths EINTR-tolerant (retry loops) so the mask becomes belt-and-braces rather than load-bearing. `perform_violence`'s delta-time (fight.cpp) converts with EXACT semantics (same units/rounding — combat timing is golden-covered).

TDD where testable (clock.cpp elapsed-seconds has tests? check; add one for the chrono version). Full battery ×3 — the boot golden and combat goldens are the real gate. Commit `refactor: game-loop timing on std::chrono`.

---

### Task 3: Filesystem migration

**Files:** dirent users (`db.cpp:587-813`, `account_management.cpp` bucket scans, `convert_plrobjs.cpp:42-61`, `convert_exploits.cpp:42-61`) → `std::filesystem::directory_iterator`; `system("mv/cp/rm/touch ...")` sites (db.cpp ×6, boards/mail/objsave/pkill/account files) → `std::filesystem::{rename,copy_file,remove}`/ofstream-touch with per-site error handling MATCHING current semantics (a failed shell `mv` today is silent/logged — do not introduce new abort paths); `chmod` in the two tooling files → `std::filesystem::permissions` or drop if dead.

Constraints: iteration ORDER of directory scans may differ from readdir — check each consumer for order sensitivity (the player-index build sorts? verify; converters report-order changes are cosmetic but note them). The converters' tests + the strict/recovery sweeps' idempotence tests are the gate; run the full battery ×3 + boot-golden (the boot log's file-open lines come from world files opened by name, not dir scans — should be untouched; verify).

Commit `refactor: directory scans and file ops on std::filesystem`.

---

### Task 4: Asio connection layer

**Files:** `src/comm.cpp` (the socket surface: init_socket/pnew_connection/nonblock/close_socket/process_input read path/write_to_descriptor/the two selects/populate_descriptor_host/finish_proxy_header_if_ready), `src/platdef.h`; descriptor_data gains an Asio socket handle (structs.h or comm.h — smallest change that compiles everywhere).

Design (binding):
- `asio::io_context` owned by comm.cpp; **one `io_context::poll()` per game-loop iteration** replaces the poll-select; the Task-2 sleep_for stays as the pulse throttle (or an asio steady_timer — implementer's choice, semantics identical).
- Listener: `asio::ip::tcp::acceptor` with async_accept re-armed per accept; SO_REUSEADDR + linger options replicated (:1313/:1324).
- Per-descriptor: async_read_some into the existing input buffering (feeding `ProtocolInput` exactly as :1686-1699 does); writes stay SYNCHRONOUS non-blocking (`asio::error::would_block` handled like the current EAGAIN path in write_to_descriptor :1604-1617) to preserve output-ordering/pacing semantics — do NOT introduce write queues protocol.cpp can't see.
- Proxy header: replicate finish_proxy_header_if_ready byte-for-byte semantics (read up to 4 raw bytes pre-protocol, EWOULDBLOCK→try-next-tick) on the Asio socket.
- DNS: `gethostbyaddr` → `asio::ip::tcp::resolver` SYNCHRONOUS reverse resolve, still guarded by `nameserver_is_slow` (behavior-identical; async DNS is a Phase-4 nicety, YAGNI now).
- `system("touch ../.killscript")` on bind failure was replaced in Task 3; keep the same failure behavior.
- Retire: fcntl nonblock (Asio non_blocking(true)), fd_set code, the raw ::read/::write on sockets. platdef's socket includes shrink accordingly.

Verification: full battery ×3; PLUS live network smokes on macOS native AND 32-bit container: plain telnet session (login, look, quit), a second simultaneous connection, disconnect-mid-session, and the **proxy path**: run the Rust proxy (`cargo run -p proxy` on the host) against a `-x` server and connect through it — the 4-byte header must parse (this is the first end-to-end proxy test since Phase 0; document the invocation). MCCP/MXP negotiation bytes eyeballed via a raw dump against a pre-change capture (the negotiation preamble on connect must be byte-identical).

Commit `refactor: connection layer on standalone Asio`.

---

### Task 5: MSVC shims + POSIX-ism cleanup + crypt scrubbing

**Files:** `src/platdef.h` (+ a tiny `src/platform_compat.h` if cleaner), the bzero/asprintf/strcasecmp sites, `src/signals.cpp`, `src/rots_crypt.cpp`, headers with the time_t/localtime MSVC issues, `src/tests/*` fixture guards.

- `bzero(p,n)` → `memset(p,0,n)` (mechanical, ~14 files); `strcasecmp` direct uses → the existing `str_cmp` wrapper where semantics match, else a platdef `rots_strcasecmp` (=_stricmp on MSVC); `asprintf` (9 sites) → a small `rots_asprintf` in utility.cpp (vsnprintf-size-then-malloc, same ownership contract) or conversion to std::string formatting where trivially safe — pick per site, list in report.
- signals shim: `signal_setup()` splits — POSIX keeps the full set; Windows registers SIGINT/SIGTERM via `signal()` + `SetConsoleCtrlHandler` mapping CTRL_CLOSE/CTRL_C to the same `hupsig` semantics (Emergency_save + clean exit); USR1/USR2/HUP-reload become no-ops on Windows (document: wizlist reload via an in-game command already exists? check — if not, note it as a Windows operational gap).
- MSVC header hygiene: fix the time_t/localtime/asctime visibility errors (correct includes in the Windows platdef branch; no `localtime_s` conversion yet — deprecation silencing via `_CRT_SECURE_NO_WARNINGS` on the MSVC target is acceptable for Phase 3, Phase 5 owns warning hygiene); hunt the ~19 "missing function header" syntax errors (likely one #if hole in a header — find it during bring-up).
- **LLP64 fixture decision** (from the trap note): probe `sizeof(rent_info/obj_file_elem/follower_file_elem/exploit_record/board record/mail block constants)` under MSVC x64 via a static_assert TU or CI log; if layouts match the frozen 32-bit sizes, let the fixture suites RUN on Windows (record the reasoning in a comment beside the guards); if any mismatch, change every `sizeof(long) != 4` guard to also skip on `_WIN64` and extend the `#if` static_asserts accordingly. Either way the outcome must be deliberate and commented.
- rots_crypt scrubbing (parked Phase-2b minor): zeroize `p_bytes`/`s_bytes`/`alt_result` and the working contexts before return (`std::fill` + a note that a dedicated secure-zero is Phase-5 hardening); the result buffer stays (it's the return value).
- Password documentation: one paragraph in docs/BUILD.md or a new docs/security-notes.md recording the legacy-character-password reality (no-op CRYPT, single live strncmp check at account-linking, sentinel for account-native chars, legacy files frozen for migration) — the parked finding gets a documented disposition instead of silent deferral.

Battery ×3 after each mechanical category. Commit(s) grouped by category.

---

### Task 6: Windows bring-up (CI-iteration loop)

No local Windows machine: iterate via branch pushes. Loop: push → `gh run watch` → `gh run view --log-failed` (windows job) → fix the next error category → repeat. The windows-msvc job stays allowed-to-fail during this task, so pushes are safe. Enable `ROTS_BUILD_TESTS=ON` for the windows preset ONCE the game target compiles (provision GTest in the windows CI job — FetchContent for gtest on Windows only, or vcpkg, implementer picks the lightest that caches well; update the preset/job accordingly). Exit: windows-2022 job GREEN — configure, build, full ctest including the characterization goldens (JSON goldens platform-neutral; combat golden needs mt19937+`/J`+SSE — all in place; any golden mismatch on MSVC = STOP and report, it would be a real semantic divergence).

Budget note: each iteration costs a CI round-trip (~10-20 min). Batch fixes per push (fix EVERY instance of the current top error categories before pushing). Keep a per-push log in the report.

Commit(s): whatever the loop needs, imperative, grouped.

---

### Task 7: CI promotion, docs, exit

- windows-msvc → required (remove continue-on-error; update display name + header comment: matrix fully required).
- Docs: BUILD.md matrix (windows row green/required; note the deferred-boot caveat and what enabling a Windows boot would take — world data + a Windows host); CLAUDE.md gotcha updates (three-platform native reality); AGENTS.md commands; presets displayName for windows.
- Exit battery: full battery ×3 locally + CI run with ALL FOUR jobs required and green. Record run URL.
- Spec-deviation note (Windows boot deferred) recorded in the plan/ledger for the Phase 5 exit review.

---

## Plan Self-Review Notes

- **Spec coverage (Phase 3):** Asio port → Task 4 (single-threaded poll-per-tick, proxy contract preserved); std::filesystem → Task 3; chrono → Task 2; signal shim → Task 5; passwords → reality-adjusted scope (rots_crypt landed in 2b; Phase 3 keeps scrubbing + the documented legacy-check disposition — the spec's "crypt() removal + first-login migration" was written before the account system's real shape was known; the live check at interpre.cpp:3333 is migration-critical and OS-independent, so it stays). Exit criterion adjusted for Windows boot (no environment) — documented as a deviation, not silently dropped.
- **Order rationale:** chrono/filesystem before Asio so each battery isolates one subsystem; MSVC shims after Asio so the Windows loop starts from the smallest error surface; every task independently battery-gated on the three live runtimes.
- **Known adaptation points:** the ~19-error syntax hole (find during bring-up); LLP64 fixture decision (explicit either way); wizlist-reload command existence (Task 5 check); Asio version pin (Task 1 records it).
- **Type consistency:** `rots_asprintf`/`rots_strcasecmp` naming follows `rots_rng`/`rots_crypt` precedent; battery definitions match the ledger's current numbers (579/7 of 586; macOS 515/71).
