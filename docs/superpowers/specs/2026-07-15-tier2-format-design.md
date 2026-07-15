# Tier 2 Format/Allocation Design — prompt builder, serializer switch, ostringstream retirement

**Date:** 2026-07-15
**Status:** Approved by owner (scope questions resolved in session)
**Predecessor:** Tier 1 (`perf/format-to-tier1`, merged at `4a60088`) — converted all 113
`+= std::format` sites to `std::format_to(std::back_inserter(...))` and reserved the big
listing builders.

## Goal

Remove the remaining allocation-heavy text composition from live paths — the per-render
prompt builder, the live character serializer, and production ostringstream users — with
byte-identical output throughout.

## Scope decisions (owner-approved)

1. **Character serializer:** switch production to the existing, equivalence-tested
   `serialize_character_to_json_v2b`. Do NOT rewrite v1 in place; v1 remains as the
   savebench comparison baseline.
2. **Prompt builder:** rewrite, tests-first — extract, pin with new characterization
   tests, then optimize.
3. **ostringstream:** convert production groups 1 (message/score text) and 2 (warm
   JSON/save writers). Group 3 (cold one-shot converters: convert_plrobjs,
   convert_exploits, mob_csv_extract, savebench pipeline internals) and all test files
   keep their ostringstreams.
4. **`send_to_char_fmt` helper:** dropped (YAGNI). Investigation showed fight.cpp flows
   through `act()` and the prompt path is handled separately; no hot
   `send_to_char(std::format)` cluster exists.

## Component 1 — Prompt builder rewrite (comm.cpp:1039-1113)

Current behavior: every prompt render composes via chained
`strcpy(prompt, std::format("{} ...", static_cast<const char*>(prompt)).c_str())` —
each fragment re-formats the entire prompt so far into a temporary `std::string` and
copies it back. This is the most frequently executed text path in the game.

Design, in three strictly ordered steps:

1. **Extraction (pure refactor):** move the composition logic verbatim into a named
   function declared in comm.h — `void build_prompt(descriptor_data* d, std::string& out)`
   (accumulator out-param so the test can inspect bytes and the caller controls storage).
   The call site in comm.cpp uses it to fill the buffer it previously built in place.
   Verified by the existing full suite + boot golden; no behavior change.
2. **Characterization tests (new file `src/tests/prompt_format_tests.cpp`):** pin
   `build_prompt` output bytes for representative states — normal prompt, invis level
   set, HP/mana/moves display variants, combat with tank and opponent names (PERS
   rendering), maul mode, and the `>` / `]` terminators. Tests are written against the
   extracted-but-unchanged logic and must be green before any optimization.
3. **Rewrite:** single-pass composition — the caller-supplied accumulator is
   `clear()`ed and `reserve(128)`d, fragments appended with `std::format_to`
   /`append`; every self-copy eliminated. The step-2 tests must pass unchanged.
   The reusable buffer lives at the call site as a function-local `static std::string`
   (the server is single-threaded); `build_prompt` itself stays stateless.

## Component 2 — Character serializer: production switch to v2b

- `src/account_management_assets.cpp:28`: `serialize_character_to_json` →
  `serialize_character_to_json_v2b`.
- Plan-time check: if the production load path calls a v1 deserializer where an
  equivalence-tested v2 variant exists (`deserialize_character_from_json_v2a/_v2b`),
  switch it identically; otherwise leave loads alone.
- v1 serializer and its ostringstream internals are intentionally untouched.

Acceptance: `json_perf_tests` equivalence suite, `CharacterizationJson` goldens, full
ctest, and `make smoke-account` (repo-mandated for account-path changes).

## Component 3 — ostringstream → std::string + format_to

**Group 1 — message/score text:**
- Interface change: `report_exposed_data(std::ostringstream&)` →
  `report_exposed_data(std::string&)` on the spec-data structs in `structs.h`
  (declaration at structs.h:1503 and siblings), with all implementations and callers in
  `char_utils.cpp` (~10 sites) converted from `<<` chains to `std::format_to` appends.
- Singles: `act_info.cpp:211` (inventory writer), `act_othe.cpp:1353`,
  `ranger.cpp:2824`, `mystic.cpp:637`.

**Group 2 — warm JSON/save writers:** `boards.cpp:1012`, `pkill.cpp:227`,
`db.cpp:4226` (crime), `mail.cpp:384`, `objects_json.cpp` (3 sites),
`exploits_json.cpp`, `account_management.cpp`/`_storage.cpp`/`_presentation.cpp`
(3 each) — replace ostringstream accumulation with `std::string` +
`format_to`/`append`.

Byte-identity is pinned by the existing JSON goldens
(`*.GoldenRoundTripsByteStable`, `CharacterizationJson`, mail/boards/pkill suites) and
the message-text characterization suites.

`<<` → append conversion rules: `<< "literal"` → `.append("literal")`;
`<< value` → `std::format_to(std::back_inserter(out), "{}", value)` or the enclosing
fragment folded into one format call; numeric formatting must preserve the stream's
exact rendering (beware any site using `std::setw`/`setprecision`/`hex` — replicate
with format specs and pin with tests before converting; none are known in scope, treat
discovery as a stop-and-verify moment).

## Constraints (identical regime to Tier 1)

- Branch `perf/format-tier2` off master (post-Tier 1). Merge is the owner's decision.
- No formatter runs on drifted files; source edits to existing `.cpp`/`.h` via Python
  byte-edits through Bash only (the Edit-tool clang-format hook would bury diffs).
  Brand-new files (the prompt test file) may be written normally.
- `-Wall -Wextra -Werror` clean; ~100-column convention on changed lines.
- Goldens never regenerated; a golden diff means the change is wrong.
- Output byte-identity is the acceptance bar for every component.

## Verification cadence

- Per task: macos-arm64 build + full ctest.
- Per component completion: rots64 build + ctest + both boot goldens.
- Component 1 adds: macOS AddressSanitizer preset run (new test file rule).
- Component 2 adds: `make smoke-account`.
- Finalization: qemu-i386 battery + six blocking CI jobs before merge (owner-triggered).

## Sequencing

Component 2 first (smallest, isolated, biggest save-path win), then Component 3
(group 1, then group 2), then Component 1 last (largest; needs new test scaffolding).
