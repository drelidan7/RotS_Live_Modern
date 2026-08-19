# Arc: LocationSystem

## Intent
Make character location a real abstraction instead of a raw `in_room` int scattered across ~300
files: reads behind a Placement API, mutations behind `set_location`/`char_to_room`, then a
private-handle representation swap — each stage enforced by a fail-closed census gate so the old
spellings can never creep back. The char-side program is COMPLETE; the arc stays open for the
optional object-side symmetric campaign (LS-4).

## Stories
- As a maintainer, I want every location read/write to go through one API, so that location
  semantics (light, occupant chains, zone power) live in one place instead of 1200 call sites.
- As the game's owner, I want the location-correctness defects the conversion surfaced (rnum/vnum
  channel mixing, load-room riders, follower placement) fixed with named tests, so that the
  refactor pays down real bugs, not just style.
- As a future implementer, I want `location_read_census.py` to fail the build on any raw-token
  regression, so that the invariant outlives the wave that established it.

## Specs & decisions
- docs/superpowers/specs/2026-07-23-locationsystem-program-design.md — program spec.
- docs/superpowers/specs/2026-07-30-ls3b-deferred-minors-design.md — the follow-up's spec.
- docs/superpowers/location-read-allowlist.md — allow-list ledger + location-state registry
  (Tables A/B, the closed world of every room-id store).
- tools/location_read_census.py — the gate (11 tokens, 9 reason prefixes, floor 300).

## Tasks
- TASK-007 — LS-4: obj_data::in_room object-side location swap (OPTIONAL, owner go/no-go first)

## How it unfolded
- 2026-07-24: LS-1 merged (PR #21, @`ee86f52d`) — seven libraries' location reads routed through
  the Stage-1 Placement API; the `LocationReadCensus` gate born.
- 2026-07-26: LS-2 merged (PR #22, @`fda8419f`) + follow-up PR #23 — app/header tier converted;
  the gate widened to all production `src/**`; finalization repairs caught by the i386 battery,
  CI, and dual adversarial reviews.
- 2026-07-28: LS-3a merged (PR #25, @`1c4f2e62` docs-final) + follow-ups #26/#27 — mutation
  conversion; the wave's flagged behavior-change rider set (rnum-persisting save sites, load-room
  fixes) landed with named tests; test-tier migration behind `test_placement.h`.
- 2026-07-29: LS-3b merged (PR #28, master @`0ee811ae`) — the representation swap: private
  handles (`ls_location_id_` et al.), the VNUM-channel storage split, SPELL_BEACON guard;
  dual adversarial reviews discharged; 1859 tests.
- 2026-07-30: deferred-MINORs follow-up merged (PR #29, @`e68e7849`) — location-state registry,
  gate hardening to 11 tokens, the `calc_load_room` bugged-arm clamp; the program closed at
  1860 tests. Successor campaign for the resolver side spun off as the Room-Resolve Retirement
  arc; the object side (LS-4) remains this arc's only open item.
