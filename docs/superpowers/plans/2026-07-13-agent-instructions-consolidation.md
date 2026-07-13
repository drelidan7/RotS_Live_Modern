# Agent Instructions Consolidation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make tracked `AGENTS.md` the portable instruction authority, keep host-specific guidance in ignored local files, and reduce Claude files to import shims.

**Architecture:** `AGENTS.md` owns repository-wide rules. This checkout uses ignored `AGENTS.local.md` for Apple Silicon commands and ignored `CLAUDE.local.md` to import it; tracked `CLAUDE.md` imports tracked `AGENTS.md`.

**Tech Stack:** Markdown, Git local excludes, CMake/CTest/Docker command documentation.

## Global Constraints

- Preserve valid parent-`RotS_Live` workflow references even when their implementation is outside this checkout.
- Keep machine-specific commands out of tracked files.
- Do not duplicate policy between `AGENTS.md` and `CLAUDE.md`, or between `AGENTS.local.md` and `CLAUDE.local.md`.
- Exclude both local files through `.git/info/exclude`, not tracked `.gitignore`.
- Preserve repository safety, persistence, golden, RNG, and platform rules.
- Do not change source code, build configuration, runtime data, or historical SDD reports.

---

## File Structure

- Modify `AGENTS.md`: tracked, agent-neutral authority.
- Modify `CLAUDE.md`: tracked import shim for `AGENTS.md`.
- Create `AGENTS.local.md`: ignored machine-specific guidance.
- Create `CLAUDE.local.md`: ignored import shim for `AGENTS.local.md`.
- Modify `.git/info/exclude`: local exclusions.

### Task 1: Create the local instruction layer

**Files:**
- Create: `AGENTS.local.md`
- Create: `CLAUDE.local.md`
- Modify: `.git/info/exclude`

**Interfaces:**
- Consumes: machine-specific cadence currently in `CLAUDE.md`.
- Produces: ignored local guidance usable by every agent plus a Claude-compatible import.

- [ ] **Step 1: Add local exclusions**

Add exactly:

```gitignore
# Project-local AI instructions
AGENTS.local.md
CLAUDE.local.md
```

Run `git check-ignore -v AGENTS.local.md CLAUDE.local.md`.

Expected: both paths resolve to `.git/info/exclude`.

- [ ] **Step 2: Create `AGENTS.local.md`**

Create these sections:

```markdown
# Project-local agent instructions

This ignored file supplements tracked `AGENTS.md` with machine-specific guidance.

## Apple Silicon verification cadence

- Per production change: native macOS arm64 CTest plus boot golden, and `rots64` Linux x64 CTest plus boot golden.
- Run qemu-i386 only at branch or wave finalization; on this host it takes 60–90+ minutes and can hang.
- Never tolerate a qemu-i386 monolithic-runner SIGSEGV; clean stale objects and investigate.
- Run every new or substantially rewritten test file once under the `macos-arm64-asan` preset.

## Commands

Native macOS: configure/build/test with `macos-arm64`, then run `scripts/boot-golden.sh --native build/macos-arm64/ageland verify`.

Linux x64: run the `linux-x64` configure/build/CTest sequence through `docker compose run --rm rots64`, then `scripts/boot-golden.sh --service rots64 verify`.

Sanitizer: configure/build `macos-arm64-asan`, then run `ctest --preset macos-arm64-asan`.
A future task may use a concrete `-R` filter only when that task names the exact added or rewritten
suite.
```

The implementation must expand each summary into the exact commands already present in tracked `AGENTS.md`/current `CLAUDE.md`; no new workflow is invented.

- [ ] **Step 3: Create `CLAUDE.local.md`**

```markdown
# Local Claude Code instructions

Machine-specific instructions for this checkout live in `AGENTS.local.md`. Read them:

@AGENTS.local.md
```

- [ ] **Step 4: Verify local isolation**

Run:

```bash
git check-ignore -v AGENTS.local.md CLAUDE.local.md
git status --short
```

Expected: both files are ignored and absent from Git status.

### Task 2: Consolidate tracked cross-agent guidance

**Files:**
- Modify: `AGENTS.md`
- Modify: `CLAUDE.md`

**Interfaces:**
- Consumes: portable rules currently present only in `CLAUDE.md`.
- Produces: one tracked authority and one tracked Claude import shim.

- [ ] **Step 1: Add instruction precedence to `AGENTS.md`**

Add a top-level section stating that `AGENTS.md` is authoritative, agents must read `AGENTS.local.md` when present, local instructions cannot override safety rules, and this child depot may reference valid workflows/actions supplied by parent `RotS_Live` or local tooling.

- [ ] **Step 2: Consolidate verification and platform policy**

Preserve in agent-neutral form:

- host-appropriate local CTest, sanitizer, and boot-golden gates;
- no remote CI push after every small change;
- finalization requires the canonical i386 battery and six blocking jobs (`legacy-32bit`, `linux-x64`, `sanitize-linux`, `macos-arm64`, `sanitize-macos`, `windows-msvc`);
- `clang-tidy-advisory` is non-blocking;
- any i386/MSVC regression and any monolithic-runner SIGSEGV blocks merge;
- new/substantially rewritten tests require a focused sanitizer run;
- GNU-family `-Wall -Wextra -Werror`, MSVC `/W4 /WX`, C++20, and sanctioned `std::format`;
- Windows CI has no world-data boot; i386 remains shipping/legacy guard until production migration is confirmed.

- [ ] **Step 3: Consolidate runtime and persistence rules**

Preserve in focused sections:

- `-p <port>` sets the port and `-x` enables the proxy four-byte client-IP header;
- direct clients must not connect to an `-x` server;
- `proxy/` supplies that header and `--cloudflare` uses `CF-Connecting-IP`;
- world/player/object/exploit/log data is external or ignored and must not be committed;
- the first fresh-install character becomes a level-100 Implementor;
- object/board/mail/pkill/crime/exploit live persistence is JSON;
- retained binary decoders are one-time migration paths, including lazy exploit migration;
- account-native characters use JSON; unlinked characters still use line-oriented text `save_player`.

- [ ] **Step 4: Preserve goldens and RNG rules**

Confirm the final guide still bans direct `rand()`/`random()`, requires `rots_rng`/`mt19937`, stops on unintended golden movement, and permits legacy fixture regeneration only in i386.

- [ ] **Step 5: Replace `CLAUDE.md`**

Use exactly:

```markdown
# Claude Code instructions

Repository-wide instructions for every AI agent live in `AGENTS.md`. Read them:

@AGENTS.md

Machine-specific instructions, when present, are loaded separately through the ignored
`CLAUDE.local.md` compatibility file.
```

- [ ] **Step 6: Review transfer and commit**

Run:

```bash
git show HEAD:CLAUDE.md
rg -n 'verification|windows-msvc|Werror|/WX|-p <port>|four-byte|level-100|C\+\+20|std::format|JSON|legacy|mt19937|rand\(\)|random\(\)' AGENTS.md AGENTS.local.md
git diff --check
git diff -- AGENTS.md CLAUDE.md
git status --short
```

Expected: portable topics are in `AGENTS.md`, host commands are only local, `CLAUDE.md` duplicates no policy, and local files remain absent from status.

Commit:

```bash
git add AGENTS.md CLAUDE.md
git diff --cached --check
git commit -m "docs: consolidate cross-agent repository guidance"
```

### Task 3: Final instruction-layer verification

**Files:**
- Verify: `AGENTS.md`, `CLAUDE.md`, `AGENTS.local.md`, `CLAUDE.local.md`, `.git/info/exclude`

**Interfaces:**
- Consumes: both instruction layers.
- Produces: evidence that imports resolve and local guidance cannot be staged accidentally.

- [ ] **Step 1: Verify imports and exclusions**

Run:

```bash
test -f AGENTS.md && test -f CLAUDE.md && test -f AGENTS.local.md && test -f CLAUDE.local.md
rg -n '^@AGENTS\.md$' CLAUDE.md
rg -n '^@AGENTS\.local\.md$' CLAUDE.local.md
git check-ignore -v AGENTS.local.md CLAUDE.local.md
```

Expected: every command exits zero and both imports target existing files.

- [ ] **Step 2: Verify tracked scope**

Run:

```bash
git diff --check
git status --short --branch
git ls-files AGENTS.local.md CLAUDE.local.md
```

Expected: no diff errors, no uncommitted instruction changes after the tracked commit, and no output from `git ls-files` for either local file.

- [ ] **Step 3: Report handoff**

Report the tracked sections added, both shim relationships, local exclusion evidence, preservation of parent-depot context, verification output, and commit IDs.
