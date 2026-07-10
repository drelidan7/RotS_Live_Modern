# Phase 4 Wave 2 — Zone sentinel fix, template hardening, format population (design)

**Date:** 2026-07-10 · **Parent spec:** `2026-07-06-cpp-modernization-design.md` (Phase 4) ·
**Predecessor:** Wave 1 (`2026-07-09-phase-4-wave-1-foundations-design.md`, merged 53a60b3) ·
**Branch:** `modernization/phase-4-wave-2` off master.

## User decisions binding this wave (2026-07-10)

1. **Zone sentinel fix approved** — fix the `%hd`-into-`int` truncation that has made
   world-builder `-1` zone-command arguments load as `65535` for decades, WITH a
   world-data census first (blast radius visible in the report before the fix lands)
   and an **intentional, documented boot-golden recapture**. This is the wave's ONLY
   sanctioned golden change; combat goldens stay byte-identical.
2. **Format slice: shape\* tools + act_othe/act_soci/act_offe** (~370 sites) — the
   giants (act_info, act_wiz) wait for Wave 3; fight/db/comm stay last per the parent
   spec.
3. Standing constraints carry: no third-party libraries in the game binary
   (std::format is the formatting target); legacy→JSON path + frozen 32-bit fixtures
   intact; i386 container is the legacy guard.

## Tasks

### 1. Zone sentinel fix (census → fix → deliberate recapture)

- **Census:** parse `lib/world` zone files for every `-1` in the arg positions read by
  the three `fscanf` sites (`src/zone.cpp` ~:116-140; the parked patch names them);
  produce a table: zone number/name, command, arg position, sentinel meaning at that
  position, count. The census ships in the task report so the owner sees exactly which
  zones change meaning.
- **Fix:** apply/refresh the parked patch (`.superpowers/sdd/zone-hd-sentinel-fix.parked.patch`,
  3 fscanf call sites, 9 `%hd`→`%d` conversions — destination fields are `int`; kills
  both the varargs UB and the misread). Unit-test the parse (a fixture zone snippet
  with `-1` args parsed to `-1`, not `65535`).
- **Recapture:** `scripts/boot-golden.sh capture` (and meta) as its own commit whose
  message states the intentional behavior change, with the before/after boot-log diff
  quoted in the report. Combat/JSON goldens must remain byte-identical (verify — a
  diff there means the fix leaked wider than parsing).
- Live-data safety: fresh backup tarball before any boot; census is read-only over
  `lib/world` (never committed).

### 2. World-data format-template hardening

- Target class (from Wave 1's closing census): non-literal printf-family format
  strings **with** conversion arguments, where world/builder data supplies the
  template. Top billing: `spec_pro.cpp:3561` (`death_cry2`, expects exactly 2 `%s`)
  and `shop.cpp`'s `no_such_item1`-family shop message templates (expanded with
  `GET_NAME(ch)` etc.). Enumerate the full class from the Wave 1 Task 4 report's
  58-hit table; harden every site where the template crosses a data boundary
  (world files, shop files, script text). Compile-time-literal tables (exit_mark,
  prompt_text, SHAPE_*_DIR macros) are OUT of scope — they are not attacker/builder
  data.
- Design: a small validating expansion helper (single header/cpp pair, e.g.
  `src/safe_template.{h,cpp}`): given a template + the expected conversion signature
  (e.g. "exactly two %s"), verify the template's conversions match; on match, expand
  exactly as today; on mismatch, emit a safe fallback (the raw template via "%s" or a
  neutral default message) + one syslog line. Behavior for WELL-FORMED templates is
  byte-identical (pinned first); only malformed templates (today: UB/garbage-stack
  reads) change behavior — documented as the sanctioned-and-safe delta, not
  golden-covered (verify).
- TDD the helper (signature-match matrix: correct, too many, too few, wrong type,
  %n, %%).

### 3. std::format population — shape\* tools (~260 sites, admin-only)

Wave 1's binding pattern VERBATIM (characterization pins first and passing pre-
conversion; translation table; sink rules; bounded copies into fixed-layout buffers
stay snprintf; skip-with-note sanctioned), plus the Wave 1 lesson as an explicit
checklist item: **every `char[N]` struct member decays via `static_cast<const char*>`
before `std::format`**. Files: `shapemdl.cpp`, `shapemob.cpp`, `shapeobj.cpp`,
`shaperom.cpp`, `shapezon.cpp`, `shapescript.cpp`. Per-file commits; battery after
each file (minimum macOS + boot-golden), full ×4 at task end.

### 4. std::format population — act_othe / act_soci / act_offe (~110 sites, player-facing)

Same binding pattern. These files feed social/emote/combat-adjacent messaging —
characterization pins and goldens carry the gate. Per-file commits, same battery
cadence as Task 3.

### 5. Issue #2 small cleanups

- Delete `object_utils.cpp`'s orphaned `get_weapon_damage(const obj_data&)` twin
  (caller-grep proof discipline from Wave 1 Task 2; its only caller died with
  combat_manager).
- Extend `ScopedTestWorld` with the multi-room contract mage_tests needs (explicit
  design: room-count parameter or a sibling fixture — smallest correct shape) and
  migrate mage_tests' fifth clone onto it; monolithic repeat/shuffle gates re-run.
- `DamageTranscriptSeed42` corpse leak (~880B/repeat): free the corpse objects in the
  test's teardown path (test-hygiene only; the production death path is untouched);
  `leaks --atExit` proof.
- Update issue #2 checkboxes as items land.

### 6. Exit

Docs touched where reality changed (BUILD.md formatting section gains the template-
hardening note; AGENTS/CLAUDE counts), issue updates, exit battery ×4 with recorded
actuals + CI URL, wave exit section in the plan doc, final whole-branch review
(Fable), merge decision to the owner.

## Verification model

Battery ×4 gates every task (baselines at branch start: 664/0 with skips 71/7/73;
Windows 660/0/20; adjusted only by explicitly-accounted new tests). Boot golden
changes EXACTLY ONCE (Task 1, deliberate, its own commit); combat/JSON goldens
byte-identical throughout. Frozen `legacy_*_fixture.bin` never regenerated. iCloud
FileProvider workaround (Wave 1 Task 3 report) remains standard for local macOS legs
until the host reboots. SDD per-task review gates; fresh backup tarball before each
task's first lib/-touching boot.

## Deferred (recorded, not dropped)

act_info/act_wiz (Wave 3), fight/db/comm (last), RAII lifecycle audit wave,
remaining 7 utility.cpp sites (need proto fixtures), Y2038 narrowings (format
version bump), C++23 (MSVC stabilization), monolithic-runner further hardening.
