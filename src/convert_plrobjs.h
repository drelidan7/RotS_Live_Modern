#pragma once

#include "interpre.h"

#include <string>

// One-shot converter (Phase 2a Task 3): walks `plrobjs_root` recursively,
// converts every legacy `<name>.obj` rent file it finds to `<name>.objs.json`
// (Task 2's on-disk convention), and retires the binary by renaming it to
// `<name>.obj.migrated` -- never deleting it.
//
// Conversion contract per file: decode the legacy bytes
// (objects_json::legacy_object_save_data_from_binary), serialize to JSON
// (objects_json::serialize_objects_to_json), then VERIFY by decoding that
// JSON back (objects_json::deserialize_objects_from_json) and comparing it
// field-for-field to the original decode (objects_json::object_save_data_equal
// -- a real structural comparison, not a re-serialization/string compare).
// Only once that verification passes is the `.objs.json` written (temp file +
// rename) and the legacy `.obj` renamed to `.obj.migrated`.
//
// Any file that fails to read, fails to decode, or fails the verify step is
// SKIPPED -- left exactly as found on disk -- and named in `*report`. Files
// are never deleted or overwritten by this function.
//
// `delete_after`: if true, after the conversion pass this also walks
// `plrobjs_root` a second time and removes every `<name>.obj.migrated` file
// found (not just ones this call converted) -- i.e. it controls cleanup of
// already-retired binaries, nothing else. Defaults to false at all call
// sites; a converted `.obj.migrated` is never deleted unless the caller asks.
//
// Returns the count of files successfully converted. `report`, if non-null,
// is replaced (not appended to) with one line per file processed
// (converted/skipped/deleted), plus one line per file deleted if
// `delete_after` was set.
int convert_all_legacy_plrobjs(const char* plrobjs_root, bool delete_after, std::string* report);

// Wizard command wiring (mirrors mob_csv_extract.cpp's do_mob_csv_extract):
// LEVEL_IMPL only. `argument`, if it contains the word "delete", sets
// delete_after=true for this run (see above). Runs convert_all_legacy_plrobjs
// against "plrobjs" (relative to the server's data directory, same as every
// other plrobjs path in objsave.cpp) and writes the full report to
// "plrobjs/convert_report.txt" (inside plrobjs/ itself, so it falls under the
// same .gitignore coverage as the rest of the sweep's output), sending only a
// one-line summary + the report path to the invoking character.
ACMD(do_convert_plrobjs);
