# Agent Instructions Consolidation Design

**Date:** 2026-07-13

## Goal

Make `AGENTS.md` the tracked, agent-neutral source of repository guidance while preserving
machine-specific instructions in local, untracked files and retaining compatibility with Claude
Code.

## Tracked instruction files

### `AGENTS.md`

`AGENTS.md` remains the authoritative repository guide for every AI agent type. It will absorb the
parts of `CLAUDE.md` that are independent of a particular agent product or local machine:

- repository verification requirements and finalization policy;
- supported build targets and required CI jobs;
- warnings-as-errors and C++20/`std::format` policy;
- server port and proxy-header behavior;
- world-data and runtime-data boundaries;
- characterization goldens and deterministic RNG requirements; and
- current JSON persistence behavior and retained legacy migration paths.

Existing parent-depot references remain when they communicate valid repository context. A
Claude-only command or action is not copied as a universal requirement; its portable behavior is
described through concrete commands or a parent-depot workflow note instead.

`AGENTS.md` will also direct agents to read `AGENTS.local.md` when that file exists. The local file
is supplementary and cannot override repository safety boundaries.

### `CLAUDE.md`

`CLAUDE.md` becomes a minimal compatibility shim. It identifies `AGENTS.md` as authoritative and
includes it with Claude Code's `@AGENTS.md` syntax. It contains no duplicated operational policy.

## Local instruction files

### `AGENTS.local.md`

`AGENTS.local.md` contains guidance that is specific to this checkout or development machine:

- the Apple Silicon macOS and `rots64` per-change verification cadence;
- the expensive qemu-i386 finalization-only cadence and stale-object/SIGSEGV lesson;
- the local macOS AddressSanitizer gate for new or substantially rewritten tests; and
- host-specific command details that do not apply to every clone.

The file is excluded through this checkout's `.git/info/exclude`, matching the parent
`RotS_Live` depot's local-instruction convention. It is not committed.

### `CLAUDE.local.md`

`CLAUDE.local.md` is also excluded through `.git/info/exclude`. It contains only a short
Claude-compatible reference to `AGENTS.local.md`, using `@AGENTS.local.md`, so local guidance is
not duplicated.

## Information-preservation rules

- Do not remove a reference solely because its implementation or action lives in the parent
  `RotS_Live` depot.
- Do not copy machine-specific commands into tracked files.
- Do not duplicate the same operational rule in `AGENTS.md` and `CLAUDE.md`, or in
  `AGENTS.local.md` and `CLAUDE.local.md`.
- Preserve historical rationale only when it changes how an agent should work today; otherwise
  link to the existing build or historical documentation.

## Verification

The completed change must satisfy all of the following:

1. `AGENTS.md` contains every agent-neutral rule formerly present only in `CLAUDE.md`.
2. `CLAUDE.md` is a thin reference to `AGENTS.md`.
3. `AGENTS.local.md` contains the machine-specific guidance and is ignored locally.
4. `CLAUDE.local.md` refers to `AGENTS.local.md` and is ignored locally.
5. `git status --short` shows only the intended tracked documentation changes, never either local
   file.
6. A focused diff review finds no loss of parent-depot context or repository safety rules.
