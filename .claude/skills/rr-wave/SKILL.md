---
name: rr-wave
description: Use when starting or executing a room-resolve retirement (RR) classification wave — draining TODO rows in docs/superpowers/room-resolve-ledger.md for a source tier (e.g. src/combat/, src/script/, src/app/), or when touching tools/room_resolve_census.py's ceilings.
---

# Standing up a room-resolve (RR) classification wave

The RR program proves every `room_data::operator[]`-reaching site's input in-range before
dereference, one tier per wave. **The source of truth is
`docs/superpowers/room-resolve-playbook.md`** — read it in full before classifying anything; this
skill sequences the wave, it does not replace the playbook's recipes. Also read:

- `docs/superpowers/specs/2026-07-31-room-resolve-retirement-design.md` — the program spec
  (note: pending owner ruling RR-O-1 §2a blocks only the final flip wave, not classification).
- `docs/superpowers/room-resolve-ledger.md` — the classification ledger (six classes:
  `TODO`/`PROVEN`/`GUARDED`/`TEST-FIXTURE`/`RESOLVER-IMPL`/`DECL`).
- `tools/room_resolve_census.py` module + self-test docstrings — the gate's own account of its
  token surface, ratchet semantics, and pinned-key closures.
- The wave's backlog task (`backlog/`) — create/advance it per CLAUDE.md "Task tracking".

## Wave sequence (the R2 shape, which the playbook's cost table budgets)

1. **Task 0 — read-only mini-census.** Bucket every in-scope ledger row/site under
   `.superpowers/sdd/<wave>/` before touching code. Treat census advisories as *hypotheses*: R2
   measured a substantial advisory/premise overturn rate (5 overturns across the wave), so verify
   each advisory against the real code — read the macro definitions, count the callers yourself.
2. **Classify per tier** using the playbook's per-proof-kind recipes (`entry-guard`,
   `loop-bound`, `caller-contract`, `dominating-resolve`, `occupant-chain`). Hard rules:
   - **No medium-confidence proofs land.** A row you cannot prove to the playbook's standard
     stays `TODO` with an enumerated reason (see the stayed-TODO taxonomy).
   - `PROVEN`/`GUARDED` rows need genuinely non-empty Kind/Proof cells; proofs cite real call
     sites, and both halves (sentinel AND in-range) must be argued.
   - ACMD-argument dispatch-pattern sites are formally deferred to their own policy design
     (TASK-002) — do not improvise a proof for that class.
3. **`GUARDED` behavior changes are red-first tested** (playbook "The GUARDED procedure"):
   failing test first, then the guard, and the boot golden must be byte-identical before AND
   after. GUARDED is the only class allowed to change behavior; say so in the commit message.
4. **Update the ledger and the ceiling.** Recompute totals from `parse_ledger()`/`--check`
   output — **never hand-compute or trust a review's estimate** (the stale-arithmetic class this
   repo's history recurringly corrects). New `MAXIMUM_TODO_COUNT` = old − sites drained,
   confirmed by a green `python3 tools/room_resolve_census.py --check`. The ratchet is a TODO
   **site-sum**, not a row count.
5. **Gates per production-touching commit** (the standing cadence, AGENTS.local.md):
   macOS `cmake --build`/`ctest --preset macos-arm64` (which runs `RoomResolveCensus`/
   `RoomResolveCensusSelfTest`), ASan preset when a test file is new/rewritten, native boot
   golden, and `python3 tools/location_read_census.py --check` — an `LS1-ALLOW` annotation is
   NOT an RR proof; the two gates ask different questions and keep separate ledgers.
6. **Doc fold-ins at wave end:** docs/BUILD.md "Room-resolve retirement (RR program)" subsection,
   AGENTS.md's `tools/` entry (token counts, ceilings, ctest total) with *measured* numbers, the
   playbook's cost table (mark drained rows RESOLVED, correct any estimate the wave overturned),
   and the arc doc + dated `backlog/docs/journal.md` entry per the task-tracking conventions.
7. **Finalization:** i386 battery (use the `i386-battery` skill) and the six blocking CI jobs;
   adversarial branch review per the standing dual-review institution; merge is the owner's call.

## Known overturn classes to expect (playbook "Pitfalls")

Scanner `occupant-loop?` advisories mismatch real code; macro-parameter premises are wrong until
the macro body is read; caller counts drift (R2's "8" was really 10); line-range citations
drift off-by-one. Budget a fix round for these — every RR wave so far has needed one.
