# Phase 3: Windows + Platform Layer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

> **AMENDED 2026-07-09 (user pivot):** NO third-party libraries in the game — including
> Asio. Task 1's vendoring was implemented, reviewed, then **reverted** (commit 0ebc9a9)
> keeping its Asio-independent parts (platdef.h Windows scaffold, -iquote/-isystem test
> include hygiene). Task 4 is rewritten below as a **hand-rolled, platform-gated socket
> shim**. GoogleTest remains as test-only tooling (not shipped in the game binary);
> revisit only on explicit user instruction. Also amended: post-upstream-merge baselines
> (see "Post-merge amendments" at the bottom).

**Goal:** The `windows-msvc` CI job builds the game and passes the full test suite (characterization goldens included) and becomes a required job — with the networking, filesystem, timing, and signal layers made genuinely cross-platform (a hand-rolled platform-gated socket shim, std::filesystem, std::chrono, a signal shim) rather than #ifdef-riddled.

**Architecture:** Spec Phase 3, amended. Order is dependency-driven: portable subsystem migrations that benefit ALL platforms land first, each gated by the full multi-runtime battery + goldens (chrono → filesystem → sockets), then the MSVC-specific shims, then a CI-push-driven Windows bring-up loop (no local Windows machine exists — the windows-2022 runner is the only Windows environment), then CI promotion. The networking work is a portability move, not a redesign: the loop stays single-threaded, select()-driven, and tick-based — select() is the one polling primitive native to BOTH BSD sockets and Winsock, so the existing loop structure survives and only the OS-divergent calls go behind the shim.

**Tech Stack:** Hand-rolled platform layer (no third-party libraries), std::filesystem, std::chrono, GoogleTest (test-only), GitHub Actions (windows-2022/MSVC).

## Global Constraints

- Full battery gates every task: 32-bit container (runner 579 pass / 7 skip of 586 + ctest 586 + boot-golden), rots64 (ctest + boot-golden --service), macOS native (ctest 515/71 + boot-golden --native). Goldens byte-identical always — a golden diff is a STOP, never a recapture.
- Gameplay/network behavior preservation: telnet byte streams (negotiation, MCCP, MXP), the `-x` proxy 4-byte header protocol (comm.cpp:173-205), prompt/output pacing, and disconnect semantics must be indistinguishable to a client. protocol.cpp's single write seam (`write_to_descriptor` call at protocol.cpp:51) must keep working unchanged.
- USER CONSTRAINT: the legacy→JSON conversion/salvage path stays intact and buildable everywhere (filesystem migration touches the converters — their tests are the gate).
- Windows-runtime reality: the CI runner has no world data, so the Phase 3 Windows exit is **build + full unit suite + goldens green on CI**; live boot verification on Windows is explicitly deferred until a Windows environment with world data exists (deviation from the spec's "boots" wording — noted in self-review; a committed minimal test world is the future enabler, out of scope here).
- Dependency policy (AMENDED 2026-07-09): **no third-party libraries in the game** — the spec's Asio choice is superseded by user decision. Networking is hand-rolled and platform-gated behind `platdef.h`. GoogleTest stays, test-only.
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

### Task 1: ~~Vendor Asio +~~ platdef Windows scaffold — DONE, then partially reverted (pivot)

Historical: implemented as specified (25be602 + review fix 7097c8b), review-approved, then
the Asio vendoring was reverted per the 2026-07-09 no-third-party pivot (0ebc9a9).
**Surviving deliverables:** `src/platdef.h` Windows branch (WIN32_LEAN_AND_MEAN +
winsock2.h/ws2tcpip.h, `SocketType = SOCKET`), and the test-build include hygiene
(`-iquote ..` so src/limits.h can't shadow `<limits.h>`). No action remaining.

---

### Task 2: Chrono timing migration

**Files:** `src/comm.cpp` (game_loop timing :683-772 region), `src/clock.cpp`, `src/fight.cpp:2732` region, plus the BSD `sigmask()` sites.

Replace `gettimeofday`/`timeval` arithmetic with `std::chrono::steady_clock` (identical pulse math: 250ms target per pulse, sleep = target − elapsed, clamped ≥0); the sleep-select (:765) becomes `std::this_thread::sleep_for`; the poll-select stays permanently as the readiness poll (Task 4 keeps select(), which is native to both BSD sockets and Winsock — it only moves behind the shim where its fd types diverge). Replace `sigmask()/sigsetmask()` with a small `platdef`-declared shim: POSIX = `sigprocmask` with the equivalent mask set, Windows = no-op — AND make the remaining select/read paths EINTR-tolerant (retry loops) so the mask becomes belt-and-braces rather than load-bearing. `perform_violence`'s delta-time (fight.cpp) converts with EXACT semantics (same units/rounding — combat timing is golden-covered).

TDD where testable (clock.cpp elapsed-seconds has tests? check; add one for the chrono version). Full battery ×3 — the boot golden and combat goldens are the real gate. Commit `refactor: game-loop timing on std::chrono`.

---

### Task 3: Filesystem migration

**Files:** dirent users (`db.cpp:587-813`, `account_management.cpp` bucket scans, `convert_plrobjs.cpp:42-61`, `convert_exploits.cpp:42-61`) → `std::filesystem::directory_iterator`; `system("mv/cp/rm/touch ...")` sites (db.cpp ×6, boards/mail/objsave/pkill/account files) → `std::filesystem::{rename,copy_file,remove}`/ofstream-touch with per-site error handling MATCHING current semantics (a failed shell `mv` today is silent/logged — do not introduce new abort paths); `chmod` in the two tooling files → `std::filesystem::permissions` or drop if dead.

Constraints: iteration ORDER of directory scans may differ from readdir — check each consumer for order sensitivity (the player-index build sorts? verify; converters report-order changes are cosmetic but note them). The converters' tests + the strict/recovery sweeps' idempotence tests are the gate; run the full battery ×3 + boot-golden (the boot log's file-open lines come from world files opened by name, not dir scans — should be untouched; verify).

Commit `refactor: directory scans and file ops on std::filesystem`.

---

### Task 4: Hand-rolled platform-gated socket layer (AMENDED 2026-07-09 — replaces the Asio design)

**Files:** New `src/rots_net.h`/`src/rots_net.cpp` (the shim — small, single-responsibility); `src/comm.cpp` (call sites only: init_socket/pnew_connection/nonblock/close_socket/process_input read path/process_output/write_to_descriptor/the two selects/populate_descriptor_host); `src/platdef.h` (already provides the includes + `SocketType`); descriptor_data's fd field becomes `SocketType` (structs.h or comm.h — smallest change that compiles everywhere).

Design (binding) — the existing single-threaded select() loop structure is PRESERVED on all platforms; only OS-divergent calls go behind `rots_net`:
- **Lifetime:** `rots_net::startup()`/`shutdown()` — WSAStartup(2,2)/WSACleanup on Windows, no-ops on POSIX. Called once from main()/comm.cpp init and exit paths.
- **Handle type:** `SocketType` everywhere comm.cpp holds an fd (int on POSIX, SOCKET on Windows); `rots_net::kInvalidSocket` (-1 / INVALID_SOCKET); comparisons `>= 0` become explicit validity checks.
- **Close:** `rots_net::close_socket(SocketType)` — ::close vs ::closesocket.
- **Nonblocking:** `rots_net::set_nonblocking(SocketType)` — fcntl O_NONBLOCK vs ioctlsocket FIONBIO. Replaces `nonblock()` (:1950).
- **I/O:** `rots_net::read_socket`/`write_socket` — POSIX ::read/::write vs Winsock ::recv/::send (Windows cannot read()/write() a SOCKET). Signatures mirror read/write; return ssize_t-style.
- **Error mapping:** `rots_net::last_error()` + `rots_net::error_is_would_block(e)` / `error_is_interrupted(e)` — errno/EAGAIN/EWOULDBLOCK/EINTR vs WSAGetLastError()/WSAEWOULDBLOCK/WSAEINTR. Every EAGAIN/EWOULDBLOCK/EINTR test in comm.cpp (write_to_descriptor :1604-1617, process_input :1683, accept, proxy header :173-205) routes through these predicates — semantics per-site identical to today.
- **select():** stays as THE readiness mechanism (native on Winsock). fd_set fills use SocketType; the nfds first arg is computed on POSIX and ignored by Winsock (pass it anyway — portable). The Task-2 sleep_for stays as the pulse throttle.
- **DNS:** `gethostbyaddr` → `getnameinfo` (portable POSIX+Winsock, replaces an obsolete API with no new dependency), still synchronous, still guarded by `nameserver_is_slow`.
- **Listener/options:** setsockopt SO_REUSEADDR/SO_LINGER replicated verbatim (:1313/:1324) — both exist on Winsock (char* cast on the option value, which the shim or a small helper handles).
- **Proxy header:** finish_proxy_header_if_ready keeps byte-for-byte semantics (read up to 4 raw bytes pre-protocol, would-block→try-next-tick) via rots_net::read_socket + the error predicates.
- **No behavior change on POSIX:** on Linux/macOS every shim function must compile down to exactly the call it replaced (inline/thin wrappers); the battery + goldens + network smokes prove indistinguishability.
- **Unit tests:** rots_net gets its own gtest file (loopback socketpair-style tests: nonblocking set, would-block classification, read/write round-trip, close idempotence) — runnable on all three POSIX runtimes now, and on Windows CI when the preset goes green.

Verification: full battery ×3; PLUS live network smokes on macOS native AND 32-bit container: plain telnet session (login, look, quit), a second simultaneous connection, disconnect-mid-session, and the **proxy path**: run the Rust proxy (`cargo run -p proxy` on the host) against a `-x` server and connect through it — the 4-byte header must parse (first end-to-end proxy test since Phase 0; document the invocation). MCCP/MXP negotiation bytes eyeballed via a raw dump against a pre-change capture (the negotiation preamble on connect must be byte-identical).

Commit `refactor: platform-gated socket shim (hand-rolled, no third-party)`.

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

- **Spec coverage (Phase 3):** networking → Task 4 (hand-rolled platform-gated shim, single-threaded select-per-tick, proxy contract preserved; spec's Asio superseded by the 2026-07-09 no-third-party user decision); std::filesystem → Task 3; chrono → Task 2; signal shim → Task 5; passwords → reality-adjusted scope (rots_crypt landed in 2b; Phase 3 keeps scrubbing + the documented legacy-check disposition — the spec's "crypt() removal + first-login migration" was written before the account system's real shape was known; the live check at interpre.cpp:3333 is migration-critical and OS-independent, so it stays). Exit criterion adjusted for Windows boot (no environment) — documented as a deviation, not silently dropped.
- **Order rationale:** chrono/filesystem before the socket shim so each battery isolates one subsystem; MSVC shims after so the Windows loop starts from the smallest error surface; every task independently battery-gated on the three live runtimes.
- **Known adaptation points:** the ~19-error syntax hole (find during bring-up); LLP64 fixture decision (explicit either way); wizlist-reload command existence (Task 5 check).

---

## Post-merge amendments (2026-07-09, upstream account-management merge)

The upstream merge (plan: `2026-07-09-upstream-account-management-merge.md`) lands before
Task 2 and changes this plan's ground truth:
- **Task 2 (chrono):** comm.cpp's heartbeat now calls `AutosaveTimer::tick` via new
  `crashsave_schedule.cpp` (already steady-clock-friendly; `stopwatch.h` is in-repo prior
  art for steady_clock timing). The plan's comm.cpp scout-anchor line numbers have
  drifted — re-anchor before dispatching the implementer.
- **Task 3 (filesystem):** `player_file_finalize.cpp` (rename/remove + readdir sibling
  cleanup) joins the dirent/file-op migration list.
- **Task 5 (MSVC/POSIX census):** new POSIX surface from the merge —
  `player_finalize_tests.cpp`, `save_benchmark_tests.cpp` (`<dirent.h>`, `<unistd.h>`,
  `<sys/stat.h>`) and `player_file_finalize.cpp` itself.
- **Battery baselines:** post-merge, post-Asio-revert totals supersede all counts stated
  above; the ledger records the authoritative numbers after merge task M2.
- **Type consistency:** `rots_asprintf`/`rots_strcasecmp` naming follows `rots_rng`/`rots_crypt` precedent; battery definitions match the ledger's current numbers (579/7 of 586; macOS 515/71).

---

## Phase 3 exit (2026-07-09, Task 7)

**Exit date:** 2026-07-09. **Final CI run (all four jobs REQUIRED and green):**
https://github.com/drelidan7/RotS_Live_Modern/actions/runs/29059169576
(commit `db42da3`; jobs: Linux i386 legacy, Linux x64, macOS arm64, Windows MSVC —
all `(required)`, all success; windows-msvc's `continue-on-error` removed in `3212788`).

**Exit battery (actuals):**
- 32-bit container: root-Makefile `make test` ctest **645/645 passed, 0 failed, 7 skipped**;
  tests-Makefile monolithic runner **638 passed / 7 skipped / 0 failed** (645 ran);
  `boot-golden.sh verify` — boot log matches golden.
- rots64 (`linux-x64` preset): ctest **645/645 passed, 0 failed, 73 skipped**;
  `boot-golden.sh --service rots64 verify` — boot log matches golden.
- macOS native (`macos-arm64` preset): ctest **645/645 passed, 0 failed, 71 skipped**;
  `boot-golden.sh --native build/macos-arm64/ageland verify` — boot log matches golden.
- Windows (windows-msvc, CI): ctest **641/641 passed, 0 failed, 20 ctest-listed skips**
  (6 AccountManagement, 8 BoardsJson LLP64-guarded, 3 InterpreAccountMenu, 1 DbLoader,
  1 OlogHaiHelpers, 1 SpellParser — all pre-existing platform guards; verified against the
  final run log. Task 6's report stated "40 skips"; that figure does not match Task 6's
  own cited CI run log — run 29055050418 already shows exactly 20 in ctest's "did not
  run" census, byte-identical names to Task 7's, with zero gtest `[ SKIPPED ]` lines
  anywhere in the log. Root cause of the original miscount is unresolved; **20 is the
  verified number of record**, consistent across three independent green runs
  (29055050418, 29056541362, 29059169576)).

**Spec deviation (recorded for the Phase 5 exit review): Windows boot verification
deferred.** The spec's "boots" wording is satisfied on Linux i386, Linux x64, and macOS
arm64 (boot-goldens above); on Windows the Phase 3 exit is **build + full unit suite +
characterization goldens green on CI**, per this plan's Global Constraints — the
windows-2022 runner has no `lib/world/` and no Windows workstation exists in this project.
Future enabler: a Windows host (or long-running-capable runner) with world data staged,
plus a Windows port of `scripts/boot-golden.sh` (bash/POSIX-tools today). A committed
minimal test world remains the cleanest path and stays out of scope for Phase 3.

**Asio→hand-rolled pivot, as implemented.** Task 1 vendored Asio per the original spec;
the 2026-07-09 user decision (no third-party libraries in the game) reverted the vendoring
(commit `0ebc9a9`), keeping platdef.h's Windows scaffold and the test include hygiene.
Task 4 delivered the replacement: `src/rots_net.{h,cpp}`, a hand-rolled, platform-gated
socket shim (startup/close/nonblocking/read/write/error-predicates; select() retained as
the readiness primitive on all platforms; getnameinfo for DNS) — single-threaded
select-per-tick loop preserved, proxy 4-byte-header contract intact, POSIX behavior
byte-identical per the batteries and network smokes. GoogleTest remains test-only tooling,
FetchContent-provisioned on Windows CI only, never linked into the game binary.
