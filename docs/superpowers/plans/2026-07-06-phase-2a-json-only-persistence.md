# Phase 2a: JSON-Only Persistence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Retire every struct-layout-dependent file format (player rent objects, boards, mail, pkill, crime records, non-account exploits) in favor of JSON, leaving binary decoding only inside portable, explicit-offset migration readers — so a 64-bit build can never corrupt live data.

**Architecture:** First half of the spec's Phase 2 ("Retire binary player I/O: the JSON path becomes the only save format"), split out because 64-bit runtime work (Phase 2b) is unsafe until this lands: `obj_file_elem` embeds a `long`, `board_msginfo` embeds a raw `char*`, and mail's block math shifts under LP64 — a 64-bit boot against live data would corrupt it. All work happens and is verified on the 32-bit container build; the tested `objects_json`/`json_utils` codecs from the account-management merge are the foundation. Authority inversion is the core move: today the binary file is written first and account JSON is a mirror; after this phase the JSON file IS the data.

**Tech Stack:** C++17 (32-bit container), the in-repo `json_utils` reader/writer, `objects_json` codec, GoogleTest, existing characterization-golden conventions (`UPDATE_GOLDENS=1`).

## Global Constraints

- Everything builds/tests/boots in the 32-bit container; batteries per task: monolithic runner (`/rots/bin/tests`, currently 496 pass / 7 skip / 0 fail), ctest via root `make test` (503, 100%), `scripts/boot-golden.sh verify`.
- Gameplay behavior must not change; persistence format changes must be semantically lossless (decoded `ObjectSaveData`/message content identical before and after). Characterization tests pin each format BEFORE its swap.
- **Live-data safety:** before ANY task that mutates files under `lib/` (Task 3's sweep, Task 7's boot-conversions), create a dated backup: `tar czf ../rots-lib-backup-$(date +%Y-%m-%d-%H%M).tar.gz lib/` (repo parent dir, never committed). Never commit anything under `lib/players/`, `lib/plrobjs/`, `lib/exploits/`, `lib/accounts/`, `lib/world/`, `lib/boards/`, `lib/misc/`, or `log/`.
- Binary decoders that survive do so ONLY as migration readers and must be **explicit-offset** (parse the documented 32-bit layout byte-by-byte), never `memcpy` into a native struct — that is what makes them ABI-portable for Phase 2b.
- The 32-bit on-disk layouts are documented in `docs/data-formats/object-rent-files.md` (rent/obj records) — consult it before writing any decoder; where this plan states offsets, verify against that doc and the current 32-bit `sizeof` at implementation time (flagged adaptation points).
- New JSON formats use the in-repo `json_utils` (`JsonReader`, `escape_json_string`) exactly like `objects_json.cpp` does — no new dependencies.
- Repo conventions: role comments on file/class-scope variables; commit subjects imperative ≤72 chars; `.claude/.no-autoformat` must exist before editing C++ (check first; recreate if missing).

**Key research anchors (verified 2026-07-06, post-olog-hai-fix master):**
- Rent writers: `objsave.cpp` `Crash_crashsave:1283`, `Crash_idlesave:~1330`, `Crash_rentsave:1377`, object encode `Crash_obj2store:863`, follower save `:909-943`, alias/board-marker block `Crash_alias_save:1161-1189`. Reader: `Crash_load:575` (physical file first; account-staged bytes fallback via `open_account_backed_object_stream:145`); mirror-back: `refresh_account_backed_object_file:204`.
- Codec (exists, tested): `objects_json.h`: `ObjectSaveData`, `object_save_data_from_binary`, `legacy_object_save_data_from_binary`, `object_save_data_to_binary`, `serialize_objects_to_json`, `deserialize_objects_from_json`. Its binary side currently uses whole-struct `read_pod` memcpy (`objects_json.cpp:315-473`) — the thing Task 1 replaces.
- Boards: `boards.cpp` `save_board:774-794` / `load_board:849-858`, struct `board_msginfo` (`boards.h:28-36`, includes raw `char* heading` written to disk). Mail: `mail.cpp` `write_to_file:173`/`read_from_file:195`/`scan_file:253`, struct `header_block_type_d` (`mail.h`, three `long`s; `BLOCK_SIZE`=100 write vs `sizeof(struct)` read mismatch). Pkill: `pkill.cpp:406,415,464` (`PKILL`, int-only). Crime: `db.cpp:3711-3820` (`crime_record_type`, int/sh_int). Exploits (non-account-linked only): `db.cpp:4153-4278` via `exploits_json::exploit_records_to_binary/from_binary`.
- NOT in scope (verified text or filename-based): `char_file_u` (never binary — legacy fallback is the text writer `save_player:2806`), player index, fight/socials/shop/zone messages.

---

### Task 1: Portable explicit-offset legacy decoders in objects_json

Replace the `read_pod` whole-struct memcpy decode of `rent_info` / `obj_file_elem` / `follower_file_elem` with explicit-offset readers of the documented 32-bit layout, so legacy `.obj` bytes decode identically on ANY ABI. Encoding to binary (`object_save_data_to_binary`) stays as-is for now — it dies in Task 2 except for round-trip tests.

**Files:**
- Modify: `src/objects_json.cpp` (the `*_from_binary_impl` readers, ~lines 315-473)
- Create: `src/tests/objects_json_layout_tests.cpp`
- Modify: `src/tests/Makefile`, `src/CMakeLists.txt` (`ROTS_TEST_SOURCES`) — add the new TU to both

**Interfaces:**
- Consumes: `docs/data-formats/object-rent-files.md` (authoritative 32-bit field offsets — read it first).
- Produces: `object_save_data_from_binary` / `legacy_object_save_data_from_binary` with unchanged signatures and unchanged results for all existing inputs — later tasks and existing tests rely on exact behavioral equivalence.

- [ ] **Step 1: Capture the current decoder's behavior as fixtures (the safety net)**

Write `src/tests/objects_json_layout_tests.cpp`. It builds a byte-buffer the way the CURRENT 32-bit build lays out the structs, decodes it with the public API, and asserts the decoded fields — so when Step 3 rewrites the internals, these tests prove equivalence:

```cpp
#include "../objects_json.h"
#include <gtest/gtest.h>

#include <cstring>
#include <string>

namespace {

// Serializes the CURRENT (32-bit container) in-memory layout of the legacy
// structs into a byte string, exactly as the historical fwrite() did. These
// helpers intentionally use the native structs: on the 32-bit build they
// reproduce the historical on-disk bytes, which is the format the portable
// decoder must read forever. (This test file is only meaningful on the
// 32-bit build until the fixtures below are frozen as goldens in Step 4.)
std::string bytes_of_rent_info()
{
    rent_info rent{};
    rent.time = 1234567890;
    rent.rentcode = 3;          // RENT_RENTED-style code; exact enum per structs.h
    rent.net_cost_per_diem = 250;
    rent.gold_left = 10000;
    rent.account_cost = 42;
    rent.nitems = 2;
    std::string bytes(reinterpret_cast<const char*>(&rent), sizeof(rent));
    return bytes;
}

std::string bytes_of_obj_file_elem(int item_number)
{
    obj_file_elem elem{};
    elem.item_number = item_number;
    elem.value[0] = 11; elem.value[1] = 22; elem.value[2] = 33; elem.value[3] = 44;
    elem.extra_flags = 0x00F0;
    elem.weight = 7;
    elem.timer = 99;
    elem.bitvector = 0x12345678L;   // the `long` that breaks on LP64
    elem.wear_pos = 5;
    elem.loaded_by = 777;
    std::string bytes(reinterpret_cast<const char*>(&elem), sizeof(elem));
    return bytes;
}

} // namespace

TEST(ObjectsJsonLayout, DecodesRentAndObjectRecordsFieldForField)
{
    std::string bytes = bytes_of_rent_info();
    bytes += bytes_of_obj_file_elem(3001);
    bytes += bytes_of_obj_file_elem(3002);

    objects_json::ObjectSaveData data;
    std::string error;
    ASSERT_TRUE(objects_json::legacy_object_save_data_from_binary(bytes, &data, nullptr, &error)) << error;

    EXPECT_EQ(1234567890, data.rent.time);
    EXPECT_EQ(3, data.rent.rentcode);
    EXPECT_EQ(250, data.rent.net_cost_per_diem);
    EXPECT_EQ(10000, data.rent.gold_left);
    EXPECT_EQ(42, data.rent.account_cost);
    EXPECT_EQ(2, data.rent.nitems);

    ASSERT_EQ(2u, data.objects.size());
    EXPECT_EQ(3001, data.objects[0].item_number);
    EXPECT_EQ(22, data.objects[0].values[1]);
    EXPECT_EQ(0x12345678L, data.objects[0].bitvector);
    EXPECT_EQ(5, data.objects[0].wear_pos);
    EXPECT_EQ(3002, data.objects[1].item_number);
}
```

Adaptation points (resolve while writing, against `structs.h` and `objects_json.h`): the exact member names of `rent_info` / `obj_file_elem` (e.g. `value` vs `values`, affect arrays, name/description fields if `obj_file_elem` carries them — mirror EVERY field the decoder populates, including the affect array and any char arrays, with distinct values) and the exact `ObjectRecord` member names. The test must cover every field `ObjectSaveData` carries, plus a follower section (`bytes_of_follower_file_elem` in the same style) and the `legacy_...` missing-follower-section tolerance flag.

- [ ] **Step 2: Wire the TU into both test builds and verify it passes against the CURRENT decoder**

Add `objects_json_layout_tests.cpp` to `src/tests/Makefile` `SRCS` (+ dep stanza on `../objects_json.h`) and to `ROTS_TEST_SOURCES` in `src/CMakeLists.txt`. Run:

```bash
docker compose run --rm rots bash -lc 'cd /rots/src/tests && make -j8 tests && /rots/bin/tests --gtest_filter=ObjectsJsonLayout.*'
```

Expected: PASS (this is a characterization of the current decoder — green before the rewrite is the point; the "red" phase of this task is Step 3's temporary breakage if offsets are wrong).

- [ ] **Step 3: Rewrite the binary readers as explicit-offset decoders**

In `src/objects_json.cpp`, replace the `read_pod<rent_info>` / `read_pod<obj_file_elem>` / `read_pod<follower_file_elem>` decode paths with a little-endian, fixed-offset reader of the documented 32-bit layout. Core helpers (place in the file's anonymous namespace, style-matched):

```cpp
// Explicit-offset little-endian readers for the legacy 32-bit on-disk layout.
// These deliberately do NOT memcpy into the native structs: on a 64-bit build
// sizeof(long) and struct padding change, but the bytes in lib/plrobjs were
// written by the 32-bit game and never change. Offsets per
// docs/data-formats/object-rent-files.md.
uint32_t read_u32le(const std::string& bytes, size_t offset)
{
    return static_cast<uint32_t>(static_cast<unsigned char>(bytes[offset]))
        | (static_cast<uint32_t>(static_cast<unsigned char>(bytes[offset + 1])) << 8)
        | (static_cast<uint32_t>(static_cast<unsigned char>(bytes[offset + 2])) << 16)
        | (static_cast<uint32_t>(static_cast<unsigned char>(bytes[offset + 3])) << 24);
}

int32_t read_s32le(const std::string& bytes, size_t offset)
{
    return static_cast<int32_t>(read_u32le(bytes, offset));
}

int16_t read_s16le(const std::string& bytes, size_t offset)
{
    return static_cast<int16_t>(static_cast<uint16_t>(static_cast<unsigned char>(bytes[offset]))
        | (static_cast<uint16_t>(static_cast<unsigned char>(bytes[offset + 1])) << 8));
}
```

Then rewrite each `*_from_binary_impl` to read fields at the layout's offsets (derive each offset from the data-format doc AND verify the total record size equals the 32-bit `sizeof` — add `static_assert(sizeof(obj_file_elem) == <documented size>, "layout doc drift");` guarded by `#if !defined(__LP64__) && !defined(_WIN64)` so the assert checks the doc on 32-bit builds and compiles away elsewhere). Bounds-check every read against `bytes.size()` and surface the existing error-message behavior unchanged.

- [ ] **Step 4: Verify equivalence and freeze the fixture as a golden**

```bash
docker compose run --rm rots bash -lc 'cd /rots/src/tests && make -j8 tests && /rots/bin/tests --gtest_filter=ObjectsJson*'
```

Expected: ALL objects_json tests pass — the pre-existing `objects_json_tests.cpp` suite (round-trips through the SAME public API) plus the new layout tests. Then freeze the byte fixture so it survives the 32-bit toolchain's eventual retirement: add a second test that writes `bytes_of_rent_info() + 2×bytes_of_obj_file_elem(...)` to `src/tests/goldens/legacy_rent_fixture.bin` under `UPDATE_GOLDENS=1` and, in compare mode, reads that file and asserts the decoder produces the same fields (same expected values as Step 1's test). Generate it:

```bash
docker compose run --rm rots bash -lc 'cd /rots/src/tests && UPDATE_GOLDENS=1 /rots/bin/tests --gtest_filter=ObjectsJsonLayout.*'
docker compose run --rm rots bash -lc 'cd /rots/src/tests && /rots/bin/tests --gtest_filter=ObjectsJsonLayout.*'
```

Expected: golden written (a few hundred bytes, binary, committed — it is the 32-bit layout's permanent reference), then compare-mode PASS.

- [ ] **Step 5: Full battery + commit**

```bash
docker compose run --rm rots bash -lc 'cd /rots && make test'
docker compose run --rm rots bash -lc 'cd /rots/src/tests && /rots/bin/tests'
scripts/boot-golden.sh verify
git add src/objects_json.cpp src/tests/objects_json_layout_tests.cpp src/tests/goldens/legacy_rent_fixture.bin src/tests/Makefile src/CMakeLists.txt
git commit -m "refactor: explicit-offset legacy rent decoders (ABI-portable)"
```

Expected: ctest 100%; runner 496+N pass / 7 skip / 0 fail; boot golden matches.

---

### Task 2: Plrobjs goes JSON-primary

The physical player-objects file becomes JSON; the binary `.obj` format becomes read-only legacy input. Writers write `<name>.objs.json`; the reader prefers it; the account mirror stores the identical JSON string.

**Files:**
- Modify: `src/objsave.cpp` (writers `Crash_crashsave:1283` / `Crash_idlesave:~1330` / `Crash_rentsave:1377` and their shared helpers `Crash_obj2store:863` / `Crash_follower_save:909` / `Crash_alias_save:1161`; reader `Crash_load:575`; account glue `open_account_backed_object_stream:145` / `refresh_account_backed_object_file:204`)
- Create: `src/tests/objsave_json_tests.cpp`
- Modify: `src/tests/Makefile`, `src/CMakeLists.txt`

**Interfaces:**
- Consumes: `objects_json::serialize_objects_to_json(const ObjectSaveData&)`, `deserialize_objects_from_json`, `legacy_object_save_data_from_binary` (Task 1's portable version), `object_save_data_from_binary`.
- Produces: on-disk convention consumed by Tasks 3 and 7 — for player `Name`, objects live at the existing bucket path with extension `.objs.json` (JSON, `serialize_objects_to_json` output); the old `<name>.obj` binary is read as fallback only and NEVER written.

Design constraints (binding):
1. **Single serialization point.** The three save entry points currently stream structs directly to `FILE*`. Restructure minimally: each builds an `objects_json::ObjectSaveData` in memory (rent header per save-kind, object records, followers, alias/board-marker block — the exact data the current code writes at the anchors above), then one helper writes it: `bool write_player_objects_json(const char* player_name, const objects_json::ObjectSaveData& data, std::string* error)` — serialize, write to a temp file in the same directory, `rename()` over the target (crash-safe), mirror the same string through the existing account-refresh path for linked characters.
2. **Reader order in `Crash_load`:** `<name>.objs.json` (deserialize; hard error → refuse load, log, do NOT fall through to stale binary) → else legacy `<name>.obj` via the Task 1 portable decoder → else account-staged bytes (existing fallback). After a successful legacy-binary load, do NOT delete the `.obj` (Task 3's sweep owns retirement).
3. **No behavior drift:** rentcodes, per-diem math, gold deduction, "you could not afford" paths, follower re-attachment, and the alias/board-marker semantics all stay byte-for-byte in game terms. The change is WHERE bytes go, not WHAT is decided.
4. The `object_save_data_to_binary` encoder becomes test-only: nothing in production writes binary after this task (verify with `grep -n 'object_save_data_to_binary' src/*.cpp` — expect hits only in `objects_json.cpp`'s definition and tests; the login-staging call at `interpre.cpp:3076` must be reworked to consume JSON/`ObjectSaveData` directly instead of round-tripping through binary — read that call site and its consumer first, then remove the binary hop).

- [ ] **Step 1: Write the failing round-trip test**

`src/tests/objsave_json_tests.cpp` — pin the NEW convention before implementing (model world/char fixtures on `damage_test_context.h`):

```cpp
#include "../objects_json.h"
#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <string>

// Declared in objsave.cpp (new in this task).
bool write_player_objects_json(const char* player_name, const objects_json::ObjectSaveData& data, std::string* error);
std::string player_objects_json_path(const char* player_name);

namespace {

objects_json::ObjectSaveData make_save_data()
{
    objects_json::ObjectSaveData data;
    data.rent.time = 1700000000;
    data.rent.rentcode = 1;
    data.rent.nitems = 1;
    objects_json::ObjectRecord record{};
    record.item_number = 3001;
    record.wear_pos = 5;
    record.bitvector = 0x1234;
    data.objects.push_back(record);
    return data;
}

} // namespace

TEST(ObjsaveJson, WritesJsonFileAndRoundTrips)
{
    objects_json::ObjectSaveData data = make_save_data();
    std::string error;
    ASSERT_TRUE(write_player_objects_json("jsontestchar", data, &error)) << error;

    std::string path = player_objects_json_path("jsontestchar");
    std::ifstream in(path.c_str());
    ASSERT_TRUE(in.good()) << "expected JSON file at " << path;
    std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    objects_json::ObjectSaveData parsed;
    ASSERT_TRUE(objects_json::deserialize_objects_from_json(contents, &parsed, &error)) << error;
    EXPECT_EQ(data.rent.time, parsed.rent.time);
    ASSERT_EQ(1u, parsed.objects.size());
    EXPECT_EQ(3001, parsed.objects[0].item_number);

    std::remove(path.c_str());
}
```

(Adapt member names to the real `ObjectSaveData`/`ObjectRecord` fields, as in Task 1. The path helper centralizes the bucket-directory logic the current code duplicates — extract it from the existing filename-building code in objsave.cpp rather than re-deriving it.)

- [ ] **Step 2: Run to verify it fails** — `--gtest_filter=ObjsaveJson.*`: FAIL to link (`write_player_objects_json` undefined).

- [ ] **Step 3: Implement per the design constraints** (helper + writer restructure + reader order + `interpre.cpp:3076` de-binary). Keep each save-kind's rent header values identical to what the binary writer put in `rent_info`.

- [ ] **Step 4: Green + regression battery**

```bash
docker compose run --rm rots bash -lc 'cd /rots/src/tests && make -j8 tests && /rots/bin/tests'
docker compose run --rm rots bash -lc 'cd /rots && make test'
```

Expected: new tests pass; ALL existing tests pass (especially `objects_json_tests`, `account_management_tests`, `interpre_account_menu_tests` — the account staging path changed).

- [ ] **Step 5: In-game smoke of the full rent cycle (the tests can't cover the FILE* plumbing end-to-end)**

Boot in-container (`scripts/rots-docker.sh boot`), log in a throwaway character, acquire an object, `rent` at the receptionist (or quit to trigger crashsave), verify `<name>.objs.json` appeared in `lib/plrobjs/<bucket>/` with plausible JSON and NO new `.obj` file was written; log back in, verify the object is present. Then `scripts/boot-golden.sh verify` (boot log must still match — this task adds no boot output). Stop the server.

- [ ] **Step 6: Commit**

```bash
git add src/objsave.cpp src/interpre.cpp src/tests/objsave_json_tests.cpp src/tests/Makefile src/CMakeLists.txt
git commit -m "feat: player objects persist as JSON; binary rent is read-only legacy"
```

---

### Task 3: Plrobjs conversion sweep + live-data verification

One-shot converter that walks `lib/plrobjs/`, converts every legacy `.obj` to `.objs.json` with a decode-equality proof, and retires the binaries — run against the real local data after a backup.

**Files:**
- Create: `src/convert_plrobjs.cpp` (a `do_` style wizard command OR a `-c` style boot flag — read how `mob_csv_extract.cpp` wires its extraction entry point and follow that existing pattern; wire into the same build lists)
- Create: `src/tests/convert_plrobjs_tests.cpp`
- Modify: `src/Makefile`, `src/CMakeLists.txt`, `src/tests/Makefile`

**Interfaces:**
- Consumes: Task 1's portable decoder, Task 2's `write_player_objects_json` + `player_objects_json_path`.
- Produces: `int convert_all_legacy_plrobjs(const char* plrobjs_root, bool delete_after, std::string* report)` — returns count converted, `report` gets a per-file summary; Task 7 runs it for real.

Conversion contract (per file): read `.obj` bytes → `legacy_object_save_data_from_binary` → `serialize_objects_to_json` → **verify**: `deserialize_objects_from_json` of the just-written string decodes to an `ObjectSaveData` that compares equal field-for-field to the original decode (write an `operator==`-style helper in the test/converter, not by re-serializing) → only then write `.objs.json`; move the `.obj` to `<name>.obj.migrated` (rename, don't delete — `delete_after` merely controls whether `.migrated` files are removed; default false). Any file that fails decode or verify is SKIPPED and named in the report — never destroyed.

- [ ] **Step 1: TDD the converter against synthesized fixtures** — test creates a temp dir, writes 2 legacy-format files (reuse Task 1's `bytes_of_*` helpers via a small shared test header `src/tests/legacy_rent_fixture.h` — extract them there in this step), one valid + one truncated/corrupt; asserts: valid file converts (JSON exists, `.obj.migrated` exists, decode-equality held), corrupt file is skipped and reported, count==1. Run red (converter doesn't exist), implement, run green.
- [ ] **Step 2: Full battery** (suite + ctest + boot-golden) — all green, converter is inert until invoked.
- [ ] **Step 3: BACKUP then convert the live data**

```bash
tar czf ../rots-lib-backup-$(date +%Y-%m-%d-%H%M).tar.gz lib/ && ls -la ../rots-lib-backup-*.tar.gz | tail -1
```

Then invoke the converter against `lib/plrobjs/` (in-container, via the wiring chosen in Step 1 — e.g. boot flag or wiz command from a booted server). Expected: report lists every player object file converted, zero skips (if there ARE skips, STOP — investigate each before proceeding; corrupt legacy files are a decision, not collateral).

- [ ] **Step 4: Live verification** — boot, load 2-3 real characters (the user's own imports) via telnet, verify inventories/equipment match expectations (compare `.objs.json` contents against the `.obj.migrated` decode with the converter's verify helper for the same files); `scripts/boot-golden.sh verify`; stop server; `git status --porcelain` clean (lib/ ignored).
- [ ] **Step 5: Commit** — `git commit -m "feat: plrobjs legacy-to-JSON conversion sweep"` (code + tests only; data stays uncommitted).

---

### Task 4: Boards go JSON

`board_msginfo` writes a raw pointer into files; replace board persistence with a JSON file per board plus a one-time boot converter for legacy files.

**Files:**
- Modify: `src/boards.cpp` (`save_board:774`, `load_board:849`, plus the boot path that calls them ~:940/:985)
- Create: `src/tests/boards_json_tests.cpp`
- Modify: `src/tests/Makefile`, `src/CMakeLists.txt`

**Interfaces:**
- Consumes: `json_utils` (follow `exploits_json.cpp` as the pattern for a small array-of-records codec).
- Produces: per-board `<boardfile>.json` (array of `{slot_num, msg_num, heading, level, post_time, message}`), written atomically (temp+rename). Message text lives IN the JSON (today heading and body are written adjacent to the struct records using `heading_len`/`message_len` — fold them in). Legacy converter: `bool convert_legacy_board_file(const char* path, std::string* error)` — explicit-offset decode of the 32-bit record layout (`board_msginfo` on 32-bit: 7×4 bytes = 28 bytes/record, the `char*` field read-and-discarded; verify against a hexdump of a real board file before trusting this — flagged adaptation point), then write JSON and rename legacy to `.migrated`.
- Boot behavior: `load_board` prefers `<file>.json`; if absent and legacy exists → convert once, log one line, load the JSON.

Steps: (1) TDD codec round-trip + converter against a synthesized 28-byte-record fixture (same style as Task 3's, fixture built from the real struct on the 32-bit build and frozen under `src/tests/goldens/legacy_board_fixture.bin` with the UPDATE_GOLDENS convention); red → implement → green. (2) Full battery. **The boot-golden WILL drift here** (one conversion log line per board on first boot; none on subsequent boots): boot once to trigger conversions of the real board files (AFTER a fresh backup per Global Constraints), boot again, `scripts/boot-golden.sh verify` — if the steady-state second boot still matches the old golden, done; if the boot phrasing changed permanently, recapture (`scripts/boot-golden.sh capture`) and commit the new golden WITH a commit-message note explaining exactly which lines changed and why. (3) In-game smoke: read an existing board with history, post a message, verify it lands in the JSON, reboot, verify it survived. (4) Commit: `"feat: board persistence as JSON with legacy conversion"`.

---

### Task 5: Mail goes JSON

The 100-byte block-chain format dies; mail becomes one JSON file, converted once from the legacy blob.

**Files:**
- Modify: `src/mail.cpp` (all of `write_to_file:173` / `read_from_file:195` / `scan_file:253` and the block-management around them), `src/mail.h` (the block structs become converter-local)
- Create: `src/tests/mail_json_tests.cpp`
- Modify: `src/tests/Makefile`, `src/CMakeLists.txt`

**Interfaces:**
- Produces: `lib/misc/mail.json` — `{"messages": [{"to": "...", "from": "...", "mail_time": N, "body": "..."}]}`; public mail API (`store_mail`, `has_mail`, `read_delete`, `scan_file`) keeps its exact signatures and game-visible behavior (including the mail-time formatting in `read_delete`'s rendered letter).
- Legacy converter: walks the old `lib/misc/mail` with **hardcoded 32-bit sizes** (`BLOCK_SIZE`=100, `long`=4 → header data size per the `HEADER_BLOCK_DATASIZE` formula computed with LONG_SIZE=4; block_type values `HEADER_BLOCK`=-1/`LAST_BLOCK`=-2/`DELETED_BLOCK`=-3), reassembles chained messages, emits them into `mail.json`, renames legacy file to `mail.migrated`. Runs at boot when `mail.json` is absent and legacy `mail` exists.

Steps: (1) TDD: fixture builder writes a legacy two-message mail file (one single-block, one chained across a data block) using the 32-bit layout constants — frozen as `src/tests/goldens/legacy_mail_fixture.bin` (UPDATE_GOLDENS convention, built from the real structs on the 32-bit build); converter test asserts both messages come out with exact from/to/time/body; JSON-store tests cover `store_mail`→`has_mail`→`read_delete`→gone, and `read_delete` on empty. Red → implement → green. (2) Full battery + boot-golden discipline identical to Task 4 (conversion may log once; steady-state must match or be deliberately recaptured with rationale). (3) In-game smoke: `mail` a letter between two throwaway characters, `receive` it at the post office, reboot in between to prove persistence. (4) Commit: `"feat: mail storage as JSON with legacy block-file conversion"`.

---

### Task 6: Low-risk POD trio — pkill, crime records, non-account exploits

Layout-stable on LP64 (int/sh_int only) but still struct-layout I/O; convert for the clean exit criterion: no struct-layout file I/O outside migration readers.

**Files:**
- Modify: `src/pkill.cpp` (`pkill_read_file:406`, `pkill_update_file:464`), `src/db.cpp` (crime records `:3711-3820`; non-linked exploits `:4153-4278`)
- Create: `src/tests/pod_persistence_json_tests.cpp`
- Modify: `src/tests/Makefile`, `src/CMakeLists.txt`

**Interfaces:**
- Same pattern three times (follow `exploits_json.cpp`): JSON array files replacing each binary file, converter-at-boot when JSON absent + legacy present (explicit-offset decode; trivial here — all fields are 4- or 2-byte, same both ABIs, but write the explicit reader anyway for uniformity), legacy renamed `.migrated`.
- Non-linked exploits: reuse `exploits_json`'s existing JSON serialization (already used for account-linked chars) — the change is only that the NON-linked path (db.cpp:4153-4278) also reads/writes the JSON file format at the legacy path location (`<name>.exploits.json`), with binary decode as one-time fallback per file. Account-linked behavior unchanged.

Steps: (1) One TDD test file covering all three: round-trip each JSON codec + each converter against synthesized legacy fixtures (pkill records, crime records, exploit records — native-struct-built on 32-bit, frozen as goldens like Tasks 4/5). Red → implement → green. (2) Full battery + boot-golden discipline (pkill + crime load at boot — `boot_pkills` at db.cpp:378 — conversion lines may appear once). (3) Commit: `"feat: pkill, crime, and exploit files persist as JSON"`.

---

### Task 7: Live conversion pass, exit battery, docs

**Files:**
- Modify: `docs/data-formats/object-rent-files.md`, `docs/data-formats/player-save.md` (status headers), `AGENTS.md` (one line), `CLAUDE.md` (gotcha update)
- No production code (fixes from failures go through review like any task)

**Interfaces:** consumes everything above.

- [ ] **Step 1: Fresh backup** (Global Constraints command) — verify the tarball exists and is non-trivial (`>50MB` expected with world+players).
- [ ] **Step 2: Full live conversion** — boot the server once (board/mail/pkill/crime/exploit converters fire for any remaining legacy files; Task 3's plrobjs sweep already ran); check the boot log's conversion lines; boot a second time (steady state: zero conversion lines).
- [ ] **Step 3: Exit battery**

```bash
docker compose run --rm rots bash -lc 'cd /rots && make test'
docker compose run --rm rots bash -lc 'cd /rots/src/tests && /rots/bin/tests'
scripts/boot-golden.sh verify
grep -rn 'fwrite' src/objsave.cpp src/boards.cpp src/mail.cpp src/pkill.cpp | grep -v '_test'
```

Expected: ctest 100%; runner all pass (7 skips minus none — report actual); boot golden matches (or was deliberately recaptured in Tasks 4-6 with documented rationale); the `fwrite` grep returns ZERO hits in those production files (the exit criterion made mechanical). Also `grep -rn 'read_pod' src/objects_json.cpp` → zero (Task 1 removed them).

- [ ] **Step 4: Account smoke** — full manual telnet pass as in Phase 0 Task 5: existing character logs in with inventory intact, mail and boards work, new character created, rent cycle round-trips.
- [ ] **Step 5: Docs** — data-format docs get a status banner: "LEGACY — retired 2026-07; live format is JSON (see src/objects_json.cpp / boards.cpp / mail.cpp); this document describes the frozen 32-bit layout that the migration decoders read." CLAUDE.md gotcha: player data now JSON end-to-end; binary decoders are migration-only and explicit-offset. AGENTS.md testing note: goldens now include `legacy_*_fixture.bin` byte fixtures — never regenerate those on a 64-bit build (they encode the historical 32-bit layout; UPDATE_GOLDENS on them is only valid in the 32-bit container).
- [ ] **Step 6: Commit docs; final ledger entry.**

```bash
git add docs/data-formats AGENTS.md CLAUDE.md
git commit -m "docs: JSON persistence is live; binary formats frozen as migration-only"
```

---

## Plan Self-Review Notes

- **Spec coverage (Phase 2 first half):** "Retire binary player I/O: the JSON path becomes the only save format" → Tasks 2-3 (plrobjs, the one live binary player format — research showed `char_file_u` was never binary); "migration tooling converts legacy" → Task 1's portable decoders + Tasks 3-6 converters; "binary kept only inside the migration module, then deleted when live data is confirmed migrated" → converters keep `.migrated` renames; actual deletion deferred to Phase 5 per spec ("retire the 32-bit legacy preset ... once live player data is confirmed fully migrated"). Boards/mail/pkill/crime were not named in the spec but are struct-layout I/O the spec's 64-bit hazard audit (item 4: "struct packing/sizeof assumptions") requires resolving before 64-bit runtime — documented here as in-scope discoveries. The second half of Phase 2 (64-bit runtime + macOS port + CI promotion) is the separate Phase 2b plan, written after this lands.
- **Placeholder scan:** the struct-member names in Task 1/2 test code are flagged adaptation points anchored to real headers, resolved while writing — consistent with prior plans' convention. No TBDs.
- **Type consistency:** `write_player_objects_json(const char*, const objects_json::ObjectSaveData&, std::string*)` and `player_objects_json_path(const char*)` identical in Tasks 2 and 3; `legacy_object_save_data_from_binary` signature unchanged from objects_json.h across Tasks 1-3; the UPDATE_GOLDENS + `.migrated` + temp-file+rename conventions repeat verbatim across Tasks 3-6.
- **Risk note:** Tasks 4-6 may legitimately change the boot log (one-time conversion lines + possibly steady-state phrasing). The plan's rule: steady-state boot must either match the existing golden or the golden is recaptured in the same commit with the drift explained. Reviewers should reject silent recaptures.
