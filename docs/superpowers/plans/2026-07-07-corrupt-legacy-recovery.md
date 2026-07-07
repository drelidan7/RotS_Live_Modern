# Corrupt Legacy File Recovery Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Salvage what is recoverable from the 1,172 corrupt legacy files the Phase 2a/2b sweeps skipped (273 rent `.obj` + 899 exploits), convert the salvage to JSON, and preserve every original byte — per the user's 2026-07-07 decision to attempt recovery rather than accept-as-is.

**Architecture:** One recovery tool extending the existing converter pattern (`convert_plrobjs`/`convert_exploits`), with an explicit, tested sanitization policy. Recovery is a sanctioned, *documented* behavior change for corrupt data only: well-formed files and records are untouched. Originals are renamed (`.salvaged-from`), never deleted — the live production server's copies still need this same recovery later (USER CONSTRAINT: the whole conversion+recovery path ships and stays).

**Tech Stack:** Existing explicit-offset decoders + JSON codecs; GoogleTest; the 32-bit container as reference environment.

## Global Constraints

- Everything from the established converter contract applies: atomic temp+rename writes; originals renamed only AFTER the salvaged JSON is written and verified; failures leave files untouched; per-file report; backup tarball before the live run (`tar czf ../rots-lib-backup-$(date +%Y-%m-%d-%H%M).tar.gz lib/`, verify >25MB).
- **Sanitization policy (locked):** for a fixed-width string field with no NUL in-width, keep the longest prefix of printable ASCII (0x20–0x7E), capped at width−1; if the first byte is already non-printable, the field becomes the empty string. Numeric fields pass through unchanged when in plausible range; an `exploit_record` whose `type` is outside the known enum range is dropped (unrecoverable record), counted in the report.
- Recovered-file convention: salvaged JSON at the normal converted path (`<name>.exploits.json` / `<name>.objs.json`); original renamed to `<original>.salvaged-from` (distinct from `.migrated` so lossy salvage is forever distinguishable from lossless conversion).
- Empty (0-byte) files: nothing to salvage — report-only, untouched.
- Batteries: 32-bit (runner 571/7/0 of 578 + ctest 578 + boot-golden) after code lands and after the live run; macOS ctest as the libc++ check.
- Never commit lib/ or log/ content; `.claude/.no-autoformat` before C++ edits; imperative subjects ≤72.

---

### Task 1: Recovery tool + live salvage run

**Files:**
- Modify: `src/convert_exploits.cpp/.h` (add recovery mode), `src/convert_plrobjs.cpp/.h` (add recovery mode), the imp-command wiring (a `recover` argument to the existing commands, or a sibling command — follow the existing wiring, IMPL-gated)
- Create/extend tests: `src/tests/convert_exploits_tests.cpp`, `src/tests/convert_plrobjs_tests.cpp` (recovery fixtures)
- Possibly: small helpers in `exploits_json.cpp`/`objects_json.cpp` for lenient decode (flagged: keep the STRICT decoders unchanged — recovery uses its own lenient wrappers so normal conversion/verify semantics are untouched)

**Recovery semantics per family:**

*Exploits (899 files — size-valid, garbage string fields):* decode all records with the explicit-offset reader (works today); per record: sanitize `chtime`/`chVictimName` per the locked policy; drop records with out-of-range `type` (report count); serialize the sanitized set → re-parse verify → write `<name>.exploits.json` → rename original to `<name>.exploits.salvaged-from`.

*Rent files (273 — empty/truncated/garbage):* empty → report-only. Otherwise parse structurally from the front: rent header, then complete `obj_file_elem` records while they validate, stopping at the first truncated/invalid record; alias/board/follower sections included only if fully intact (sanitize alias strings per policy); salvage requires at least a valid rent header. Write salvaged `ObjectSaveData` → verify → `<name>.objs.json` → rename `.obj` to `.obj.salvaged-from`. Unsalvageable (no valid header) → untouched, reported.

- [ ] **Step 1 (TDD):** Fixtures: exploits file with (a) garbage-after-NUL record, (b) no-NUL garbage field, (c) out-of-range type record, (d) fully-valid record — assert exact salvaged output per policy. Rent: (a) truncated mid-record (salvage header + leading records), (b) empty (untouched), (c) garbage header (untouched, reported), (d) valid file (recovery mode must REFUSE it — recovery only runs on files strict conversion rejects, so lossless conversion never degrades to lossy salvage). Red → implement → green.
- [ ] **Step 2:** Full 32-bit battery + macOS ctest.
- [ ] **Step 3:** BACKUP, then live recovery run over lib/exploits/ and lib/plrobjs/ (recovery mode processes ONLY files strict conversion rejects). Expected: most of the 899 exploits salvage (garbage is in string fields; records decode); rent salvage varies (truncation-dependent). Full per-file statistics; ANY file whose salvage verify fails stays untouched and is listed.
- [ ] **Step 4:** Post-run verification: boot golden; spot-check in-game — `show exploits` on 2 recovered characters (sanitized fields render cleanly), rent-load one recovered character if any player-facing rent file was salvaged; 0 remaining strict-conversion rejects except the reported unsalvageable set; second boot steady-state.
- [ ] **Step 5:** Commit (code+tests only), final report with the salvage ledger table (converted / salvaged / dropped-records / untouched-unsalvageable counts per family).

---

## Plan Self-Review Notes

- Recovery never runs on files the strict converter accepts (Step 1 fixture d pins this) — lossless conversion can never be displaced by lossy salvage.
- The strict decoders/codecs stay byte-identical (lenient logic lives in recovery-only wrappers) — the 899/273 skip behavior of the NORMAL paths is unchanged for anything recovery doesn't touch, and the frozen fixtures/goldens are unaffected.
- Originals preserved under `.salvaged-from` + backups satisfy both the user's retention constraint and reversibility.
