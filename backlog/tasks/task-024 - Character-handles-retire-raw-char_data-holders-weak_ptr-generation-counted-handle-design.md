---
id: TASK-024
title: >-
  Character handles: retire raw char_data* holders (weak_ptr /
  generation-counted handle design)
status: To Do
assignee: []
created_date: '2026-08-22 20:50'
labels: []
milestone: m-3
dependencies: []
priority: low
ordinal: 24000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Design-level future work, recorded from the owner discussion during TASK-021 (2026-08-22). Four defects in a row -- TASK-018 (spell_fireball reads a freed caster), TASK-019 (spell_earthquake's fall loop), TASK-020 (affect_update walks a freed affected_list node), and the ABA exposure found in TASK-021's caster snapshot -- are one defect class: a raw `char_data*` held across an operation that can free or re-register the character. Every holder in the tree is raw: `character_list`, `combat_list`/`combat_next_dude`, `waiting_list`, `specials.fighting`, `master`/`followers`, `mount_data`, the room occupant chains, `descriptor_data::character`, the `universal_list` (ptr, abs_number) nodes, and now the `caster_snapshot::identity_ptr` / `char_special_data::poisoned_by` pairs. `char_data` is CREATE()'d and free_char()'d, and `abs_number` slots are recycled by `register_npc_char`, so "pointer + number" identities are only safe through a registry lookup.

What TASK-021 did instead, and why: a fixed `char_data* characters_by_abs_number[MAX_CHARACTERS]` table (entity_lifecycle.cpp, ~64 KB once, one writer in register_npc_char, one clearer in remove_char_exists) so that `caster_snapshot::resolve()` and `resolve_poisoner()` resolve by id through the registry and never dereference a stored pointer. The owner asked whether `std::shared_ptr<char_data>` could have avoided that table. Rationale recorded:
- `shared_ptr` alone is the wrong tool: a room affect owning a `shared_ptr` to its caster keeps the STORAGE alive after extract_char -- a zombie that is unlinked from every list, has no descriptor, and still answers `IS_NPC`/stat reads -- so every consumer would still need the "is this a live character?" question (`char_exists`), and the owner's requirement that cast-time stats be frozen means the snapshot stays regardless.
- `weak_ptr<char_data>::lock()` IS the right shape ("resolve or null, never dangling, no ABA" -- the control block is per allocation, so recycled abs_numbers and reused addresses cannot alias). It requires `char_data` to be owned by `shared_ptr` somewhere: realistically a registry (`vector<shared_ptr<char_data>>` keyed like the table), i.e. the table again holding owners instead of raw pointers. So `weak_ptr` does not remove the registry; it changes its shape and adds refcount traffic on every pointer copy.
- The real payoff is only reached by converting the OTHER raw holders listed above to `weak_ptr` (or to a generation-counted handle `{abs_number, generation}` resolved through the registry), which retires the whole defect class at once -- a codebase-wide ownership campaign over intrusive C-style lists, comparable in scale to the LocationSystem program (LS-1..LS-3b), not something to fold into a feature task.
- The cheapest no-`shared_ptr` variant is the generation-counted handle: stamp a monotonically increasing generation at registration, store (abs_number, generation) in every holder, resolve through the existing table and compare generations. It still needs the id -> pointer table (the floor for any by-id resolve that does not walk character_list), but it removes the pointer-identity compare and makes stale handles detectable even across address reuse.

## Why
Source: owner request 2026-08-22 ("document the ownership change as a low-priority future task with the rationale"). It is the structural fix behind TASK-018/019/020/021; the per-site fixes those tasks landed are correct but local. Prerequisites before designing: an inventory of every raw char_data* holder and its lifetime (a census in the RR/LS tradition), a ruling on weak_ptr vs generation handle (perf: refcount traffic in the pulse loops vs a table lookup), and a plan that converts holders tier by tier under the standing acyclicity/golden/ASan gates.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Census of every raw char_data* holder tree-wide with its lifetime and whether a free/re-register can occur across its hold (the TASK-018/019/020/021 sites as the seed set)
- [ ] #2 Owner ruling: weak_ptr-owned characters vs a generation-counted handle resolved through the abs_number registry, with measured pulse-loop cost for each
- [ ] #3 Design doc with a tier-by-tier conversion plan under the standing gates (acyclicity linkchecks, boot/seed42 goldens, ASan, i386 battery) and the explicit retirement of characters_by_abs_number's pointer-identity compare
<!-- AC:END -->
