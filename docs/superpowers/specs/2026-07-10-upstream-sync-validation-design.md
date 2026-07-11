# Upstream Sync & Validation — account-management merge (design)

**Date:** 2026-07-10 · **Branch under validation:** `merge/upstream-2026-07-10` (merge commit
`10536a9`, off master `47ce9c9`) · **Not a modernization phase** — a standalone upstream-sync
track (recurs as upstream evolves; own spec→plan→execute→merge lifecycle each time).

## What this is

The 5 new `upstream/account-management` commits were merged down and compile-checked on i386
(see `.superpowers/sdd/upstream-merge-2026-07-10-report.md`), but the impact evaluation was
deferred. This effort runs that evaluation: validate the merged work against our Phase 0–4
modernization, resolve the interaction surfaces, make the one deliberate golden change the
merge implies, fix any real defects, and merge to master.

**The 5 commits (inbound feature/fix work, not modernization):**
- `27ca9d8` prac command #271 — `spec_pro.cpp` guild SPECIAL rewritten for batched
  `"N <skill>"` / `"all <skill>"` via `std::regex`; adds `<regex>`/`<iostream>`, drops
  `platform_compat.h`/`safe_template.h` from that TU.
- `73734ee` MSDP JSON-sanitize #275 — `MSDPSanitizeValue()` escapes `"`,`\`,control chars in
  outbound MSDP strings; persistent `IacInput` subnegotiation-reassembly buffer added to
  `protocol_t`; NULL-`pProtocol` guards; `act_move.cpp`/`weather.cpp` route text through the
  sanitizer / `strip_trailing_line_break`.
- `77958a9` specialization-save — `specialization` field added to the character JSON schema
  (all three writers + reader + validator, range `PLRSPEC_NONE .. PS_Count-1`);
  `write_account_character_file` gains validate-before-write.
- `5a1a35e` release-frodo internal merge (no independent surface).
- `a695658` MSDP unit tests + a level-100 roster test.

## Decisions binding this effort (user, 2026-07-10)

1. Standalone "Upstream Sync & Validation" effort, separate from the modernization phase
   numbering.
2. Execute the runnable parts NOW via containers + the four-platform CI. Only the **local
   macOS-native boot-golden** confirmation defers to post-reboot (the CI macOS-arm64 job and
   both container boot-goldens are the primary gate and run now).
3. The `specialization` field's JSON golden change is the ONE sanctioned golden change of this
   effort — done deliberately via `UPDATE_GOLDENS=1` with owner sign-off (same discipline as
   the Wave-2 zone recapture).

## Tasks

### 1. JSON golden update for `specialization` (the sanctioned golden change)
- Confirm the field is emitted UNCONDITIONALLY by `serialize_character_to_json` / `_v2a` /
  `_v2b` (if conditional, the golden may not change — verify first).
- Regenerate the affected `CharacterizationJson` golden(s) (`character_seed_fixture.json` and
  any sibling) with `UPDATE_GOLDENS=1`; **verify the byte-diff is EXACTLY the added
  `specialization` field** (nothing else moved) and get owner sign-off before committing the
  regenerated golden. Its own commit, message states the deliberate change + sign-off date.
- Confirm `write_account_character_file`'s new validate-before-write path rejects NO
  currently-valid live character (a stored `profs.specialization` outside the new validated
  range would fail-closed — census the live `lib/` character files if any exist locally, else
  reason from the range).
- Cross-ABI: `profs.specialization` field offset identical between the i386 legacy binary and
  the 64-bit (rots64/macOS) JSON path — the frozen 32-bit fixtures re-verify layout for free.

### 2. MSDP surface (#275) vs our rots_net shim + MSSP
- Run the new MSDP/protocol tests (`protocol_tests.cpp` additions) on i386 + rots64 + CI
  (incl. windows-msvc + macOS-arm64).
- Verify a split `IAC SB … SE` subnegotiation reassembles correctly through our `rots_net`
  hand-rolled shim's read-chunking (the buffer assumes a chunking behavior our shim may
  differ on); confirm the `PendingInput` fragment path + the new single-byte IAC-tail carry
  interact correctly.
- Confirm `MSDPSanitizeValue` doesn't conflict with or double-escape against any MSSP /
  telnet-subneg emitter we added (grep for a parallel sanitizer; eyeball a control-char
  room/weather name through the escape).
- Confirm the new NULL-`pProtocol` guards don't mask a descriptor-lifecycle bug our socket
  layer relies on.

### 3. prac command (#271) — header transitivity + safety
- **Primary gate = windows-msvc + macos-arm64 CI presets** (it dropped
  `platform_compat.h`/`safe_template.h`, added `<regex>`/`<iostream>` — compiled on i386,
  MSVC/AppleClang header transitivity differs). A CI compile failure here is a real defect →
  restore the needed include(s) minimally.
- Check `memcpy(str2, item.c_str(), 255)` — fixed-255 copy from a possibly-shorter
  `std::string` is an over-read; guard to `std::min(item.size()+1, 255)` or equivalent if so.
- Verify `handle_pracs` decrements `spells_to_learn` and clamps `knowledge` correctly per
  batched iteration (a focused test if reachable).

### 4. Command-table alignment + pre-existing qemu segfault
- Static: verify merged `command[]` strings and `assign_command_pointers()` COMMANDO slots
  are index-aligned for entries 245–252 (our master: `convertplrobjs`@249,
  `convertexploits`@250, `savebench`@251 — the merge kept ours; confirm no off-by-one).
- Confirm the new `…LevelOneHundred` roster test (ported to `ScopedTestWorld`) passes
  off-qemu (rots64 + CI).
- Confirm `AccountManagement.FormatsCharacterPromptWithLinkedCharacterList` (pre-existing on
  master, segfaults under qemu-i386) is a qemu emulation artifact — passes on rots64 +
  CI-macOS + the CI native-i386 runner. If it fails off-qemu too, it's a real bug predating
  this merge — record and escalate (do not let it block the upstream validation, but flag it).

### 5. Battery + merge
- i386 container (ctest + runner + boot-golden verify) + rots64 (ctest + boot-golden --service).
- `make smoke-account` (env-blocked — no cargo/Python≥3.10; use the manual nanny()-drive
  substitute of record, since the merge touches interpre.cpp's account menu).
- Push → all four required CI jobs green (this is the finalization/pre-merge gate; the CI
  macOS-arm64 job covers macOS despite the local wedge).
- **Deferred to post-reboot:** local macOS-native `boot-golden.sh --native` confirmation only
  (redundant with the container boot-goldens + CI macOS ctest).
- Then the merge-to-master decision to the owner.

## Verification model & constraints
- Standing depot rules apply: no third-party libs in the game; legacy→JSON + frozen 32-bit
  fixtures intact; combat goldens byte-identical; RNG owned. The ONLY sanctioned golden change
  this effort is the `specialization` JSON golden (Task 1, signed off). Any OTHER golden diff
  (boot, combat) is a STOP.
- New-test ASan gate: the merge's new MSDP tests get one macOS ASan run (per the 2026-07-10
  directive) — catches the fixture-memory-error class before finalization.
- SDD review gate per task; any interaction defect (Tasks 2–4) fixed on the branch before
  merge; final whole-branch review before the merge decision.

## Explicitly out of scope
The upstream *features themselves* are accepted as merged (we chose to pull them); this effort
validates their interaction with our modernization and their portability, not their product
design. Redesigning the prac command / MSDP behavior is not in scope.
