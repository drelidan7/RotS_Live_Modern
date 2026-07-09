#include "convert_plrobjs.h"

#include "char_utils.h"
#include "comm.h"
#include "db.h"
#include "handler.h"
#include "objects_json.h"
#include "structs.h"
#include "utils.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <system_error>
#include <vector>

namespace {

// True iff `value` ends with exactly `suffix` (used to find legacy `.obj`
// files without also matching `.obj.migrated`/`.objs.json`, and to find
// `.obj.migrated` files for the delete_after pass).
bool ends_with(const std::string& value, const std::string& suffix)
{
    if (suffix.size() > value.size())
        return false;
    return std::equal(suffix.rbegin(), suffix.rend(), value.rbegin());
}

// Recursively collects every regular file under `dir_path` whose name ends
// with `suffix` into `*out_paths`. Ported from the opendir/readdir/stat
// walking style previously shared with account_management.cpp/db.cpp onto
// std::filesystem::directory_iterator (Phase 3 filesystem migration); the
// result is still sorted by every caller immediately after collection, so
// this recursive walk's enumeration order was never load-bearing.
// plrobjs/ is shallow in practice (bucket subdirectories one level down, plus
// a few stray top-level files -- see lib/plrobjs/*.obj) but this recurses to
// any depth defensively rather than assuming a fixed layout.
void collect_files_with_suffix(const std::string& dir_path, const std::string& suffix, std::vector<std::string>* out_paths)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::directory_iterator it(dir_path, ec);
    if (ec)
        return;

    const fs::directory_iterator end;
    while (it != end) {
        const std::string entry_path = it->path().string();

        std::error_code status_ec;
        const fs::file_status entry_status = it->status(status_ec);
        if (!status_ec) {
            if (fs::is_directory(entry_status)) {
                collect_files_with_suffix(entry_path, suffix, out_paths);
            } else if (fs::is_regular_file(entry_status) && ends_with(entry_path, suffix)) {
                out_paths->push_back(entry_path);
            }
        }

        it.increment(ec);
        if (ec)
            return;
    }
}

// Reads the whole file at `path` into `*bytes`. A local copy of the same
// small helper objsave.cpp keeps to itself (anonymous namespace there, not
// exported) -- this codebase's established pattern is for each translation
// unit needing this to keep its own copy rather than add cross-TU coupling
// for a few lines of file I/O (see db.cpp's own separate
// read_binary_file_contents for another example of the same convention).
bool read_binary_file_contents(const char* path, std::string* bytes)
{
    FILE* file = std::fopen(path, "rb");
    if (file == nullptr)
        return false;

    std::string loaded_bytes;
    char buffer[4096];
    bool read_ok = true;
    while (true) {
        const size_t bytes_read = std::fread(buffer, sizeof(char), sizeof(buffer), file);
        if (bytes_read > 0)
            loaded_bytes.append(buffer, bytes_read);

        if (bytes_read < sizeof(buffer)) {
            if (std::ferror(file))
                read_ok = false;
            break;
        }
    }
    std::fclose(file);

    if (!read_ok)
        return false;

    *bytes = std::move(loaded_bytes);
    return true;
}

// Writes `contents` to `path` via temp-file + rename (crash-safe: a reader
// never observes a partial write), matching write_player_objects_json's
// pattern in objsave.cpp for the same on-disk convention.
bool write_file_contents_atomically(const std::string& path, const std::string& contents, std::string* error)
{
    const std::string temp_path = path + ".tmp";

    FILE* temp_file = std::fopen(temp_path.c_str(), "wb");
    if (temp_file == nullptr) {
        if (error)
            *error = std::string("failed to open temp file '") + temp_path + "': " + std::strerror(errno);
        return false;
    }

    const size_t bytes_written = contents.empty() ? 0 : std::fwrite(contents.data(), sizeof(char), contents.size(), temp_file);
    const int flush_result = std::fflush(temp_file);
    const int close_result = std::fclose(temp_file);

    if (bytes_written != contents.size() || flush_result != 0 || close_result != 0) {
        std::remove(temp_path.c_str());
        if (error)
            *error = "failed to write temp file '" + temp_path + "'";
        return false;
    }

    if (std::rename(temp_path.c_str(), path.c_str()) != 0) {
        const std::string rename_error = std::strerror(errno);
        std::remove(temp_path.c_str());
        if (error)
            *error = "failed to move temp file into place: " + rename_error;
        return false;
    }

    return true;
}

// Converts one legacy `.obj` file at `legacy_path` (which must end in
// ".obj", not ".obj.migrated"). Appends exactly one line to `*report`
// describing the outcome. Returns true iff a file was actually converted
// (JSON written and legacy file renamed to .obj.migrated).
bool convert_one_legacy_plrobj_file(const std::string& legacy_path, std::ostringstream* report)
{
    std::string legacy_bytes;
    if (!read_binary_file_contents(legacy_path.c_str(), &legacy_bytes)) {
        *report << "SKIP " << legacy_path << ": failed to read file: " << std::strerror(errno) << "\n";
        return false;
    }

    objects_json::ObjectSaveData decoded;
    bool accepted_missing_follower_section = false;
    std::string decode_error;
    if (!objects_json::legacy_object_save_data_from_binary(legacy_bytes, &decoded, &accepted_missing_follower_section, &decode_error)) {
        *report << "SKIP " << legacy_path << ": decode failed: " << decode_error << "\n";
        return false;
    }

    const std::string json = objects_json::serialize_objects_to_json(decoded);

    // Verify (binding conversion contract): decode the just-written JSON back
    // and compare it, field-for-field, to the original decode -- NOT a
    // re-serialization/string comparison.
    objects_json::ObjectSaveData reparsed;
    std::string verify_decode_error;
    if (!objects_json::deserialize_objects_from_json(json, &reparsed, &verify_decode_error)) {
        *report << "SKIP " << legacy_path << ": verify-decode of freshly serialized JSON failed: " << verify_decode_error << "\n";
        return false;
    }

    if (!objects_json::object_save_data_equal(decoded, reparsed)) {
        *report << "SKIP " << legacy_path << ": verify mismatch -- re-decoded JSON does not equal the original legacy decode\n";
        return false;
    }

    // legacy_path ends in ".obj" (guaranteed by the caller's collection
    // filter), so stripping the last 4 characters gives the extensionless
    // base path -- the same convention player_objects_json_path (objsave.cpp)
    // uses for the live writer.
    const std::string base_path = legacy_path.substr(0, legacy_path.size() - 4);
    const std::string json_path = base_path + ".objs.json";

    std::string write_error;
    if (!write_file_contents_atomically(json_path, json, &write_error)) {
        *report << "SKIP " << legacy_path << ": " << write_error << "\n";
        return false;
    }

    const std::string migrated_path = legacy_path + ".migrated";
    if (std::rename(legacy_path.c_str(), migrated_path.c_str()) != 0) {
        // The JSON is written and verified at this point -- data is not at
        // risk -- but the legacy file could not be retired. Report it as a
        // partial success rather than a skip: the .obj is left in place
        // (never destroyed), and a future run will just rewrite the same
        // .objs.json (idempotent) and can retry the rename.
        *report << "CONVERTED (legacy rename failed) " << legacy_path << " -> " << json_path
                << " (legacy file left in place: " << std::strerror(errno) << ")\n";
        return true;
    }

    *report << "CONVERTED " << legacy_path << " -> " << json_path << " (legacy renamed to " << migrated_path << ")\n";
    return true;
}

// True iff STRICT conversion (legacy decode -> serialize -> verify-decode ->
// field-compare, without the file I/O) would accept `legacy_bytes` as-is.
// Shared by recovery's binding refuse-if-already-valid check so it can never
// diverge from what the strict sweep itself would decide.
bool legacy_plrobj_bytes_round_trip_losslessly(const std::string& legacy_bytes)
{
    objects_json::ObjectSaveData strict_decoded;
    bool accepted_missing_follower_section = false;
    std::string strict_decode_error;
    if (!objects_json::legacy_object_save_data_from_binary(legacy_bytes, &strict_decoded, &accepted_missing_follower_section, &strict_decode_error))
        return false;

    const std::string strict_json = objects_json::serialize_objects_to_json(strict_decoded);

    objects_json::ObjectSaveData strict_reparsed;
    std::string strict_verify_error;
    if (!objects_json::deserialize_objects_from_json(strict_json, &strict_reparsed, &strict_verify_error))
        return false;

    return objects_json::object_save_data_equal(strict_decoded, strict_reparsed);
}

// Recovery counterpart to convert_one_legacy_plrobj_file: attempts a lossy
// structural salvage of one legacy `.obj` file that STRICT conversion
// rejects. See convert_plrobjs.h for the full contract. Appends exactly one
// line to `*report`. Returns true iff a file was actually salvaged (JSON
// written and legacy file renamed to `.obj.salvaged-from`).
bool recover_one_legacy_plrobj_file(const std::string& legacy_path, std::ostringstream* report)
{
    std::string legacy_bytes;
    if (!read_binary_file_contents(legacy_path.c_str(), &legacy_bytes)) {
        *report << "SKIP (recovery) " << legacy_path << ": failed to read file: " << std::strerror(errno) << "\n";
        return false;
    }

    if (legacy_bytes.empty()) {
        *report << "UNSALVAGEABLE (recovery) " << legacy_path << ": empty file, nothing to salvage\n";
        return false;
    }

    // Binding invariant: recovery never runs on a file STRICT conversion
    // would already accept.
    if (legacy_plrobj_bytes_round_trip_losslessly(legacy_bytes)) {
        *report << "REFUSED (recovery) " << legacy_path
                << ": file already round-trips losslessly under strict conversion; run convertplrobjs (no recover argument) instead\n";
        return false;
    }

    objects_json::ObjectSaveData salvaged;
    int dropped_partial_record_count = 0;
    std::string salvage_error;
    if (!objects_json::recover_object_save_data_from_binary(legacy_bytes, &salvaged, &dropped_partial_record_count, &salvage_error)) {
        *report << "UNSALVAGEABLE (recovery) " << legacy_path << ": " << salvage_error << "\n";
        return false;
    }

    const std::string json = objects_json::serialize_objects_to_json(salvaged);

    objects_json::ObjectSaveData reparsed;
    std::string verify_decode_error;
    if (!objects_json::deserialize_objects_from_json(json, &reparsed, &verify_decode_error)) {
        *report << "SKIP (recovery) " << legacy_path << ": verify-decode of freshly serialized salvage JSON failed: " << verify_decode_error << "\n";
        return false;
    }

    if (!objects_json::object_save_data_equal(salvaged, reparsed)) {
        *report << "SKIP (recovery) " << legacy_path << ": verify mismatch -- re-decoded salvage JSON does not equal the salvaged decode\n";
        return false;
    }

    // legacy_path ends in ".obj" (guaranteed by the caller's collection
    // filter), same JSON-path convention as the strict sweep.
    const std::string base_path = legacy_path.substr(0, legacy_path.size() - 4);
    const std::string json_path = base_path + ".objs.json";

    std::string write_error;
    if (!write_file_contents_atomically(json_path, json, &write_error)) {
        *report << "SKIP (recovery) " << legacy_path << ": " << write_error << "\n";
        return false;
    }

    const std::string salvaged_from_path = legacy_path + ".salvaged-from";
    if (std::rename(legacy_path.c_str(), salvaged_from_path.c_str()) != 0) {
        // Same partial-success handling as the strict path: the JSON is
        // written and verified -- data is safe -- but the legacy file
        // couldn't be retired, so it's left in place rather than treated as
        // a failure.
        *report << "SALVAGED (legacy rename failed) " << legacy_path << " -> " << json_path << " (" << salvaged.objects.size()
                << " top-level object(s), " << salvaged.aliases.size() << " alias(es), " << salvaged.followers.size()
                << " follower(s) recovered; " << dropped_partial_record_count
                << " partial trailing object record(s) dropped; legacy file left in place: " << std::strerror(errno) << ")\n";
        return true;
    }

    *report << "SALVAGED " << legacy_path << " -> " << json_path << " (" << salvaged.objects.size() << " top-level object(s), "
            << salvaged.aliases.size() << " alias(es), " << salvaged.followers.size() << " follower(s) recovered; "
            << dropped_partial_record_count << " partial trailing object record(s) dropped; legacy renamed to " << salvaged_from_path << ")\n";
    return true;
}

} // namespace

int convert_all_legacy_plrobjs(const char* plrobjs_root, bool delete_after, std::string* report)
{
    std::ostringstream report_stream;

    if (plrobjs_root == nullptr || !*plrobjs_root) {
        if (report)
            *report = "SKIP: plrobjs_root must not be empty.\n";
        return 0;
    }

    std::vector<std::string> legacy_paths;
    collect_files_with_suffix(plrobjs_root, ".obj", &legacy_paths);
    std::sort(legacy_paths.begin(), legacy_paths.end());

    int converted_count = 0;
    for (const std::string& legacy_path : legacy_paths) {
        if (convert_one_legacy_plrobj_file(legacy_path, &report_stream))
            ++converted_count;
    }

    if (delete_after) {
        std::vector<std::string> migrated_paths;
        collect_files_with_suffix(plrobjs_root, ".obj.migrated", &migrated_paths);
        std::sort(migrated_paths.begin(), migrated_paths.end());
        for (const std::string& migrated_path : migrated_paths) {
            if (std::remove(migrated_path.c_str()) == 0) {
                report_stream << "DELETED " << migrated_path << "\n";
            } else {
                report_stream << "WARN failed to delete " << migrated_path << ": " << std::strerror(errno) << "\n";
            }
        }
    }

    report_stream << converted_count << " file(s) converted out of " << legacy_paths.size() << " legacy .obj file(s) found.\n";

    if (report)
        *report = report_stream.str();
    return converted_count;
}

int recover_all_legacy_plrobjs(const char* plrobjs_root, std::string* report)
{
    std::ostringstream report_stream;

    if (plrobjs_root == nullptr || !*plrobjs_root) {
        if (report)
            *report = "SKIP: plrobjs_root must not be empty.\n";
        return 0;
    }

    std::vector<std::string> legacy_paths;
    collect_files_with_suffix(plrobjs_root, ".obj", &legacy_paths);
    std::sort(legacy_paths.begin(), legacy_paths.end());

    int salvaged_count = 0;
    for (const std::string& legacy_path : legacy_paths) {
        if (recover_one_legacy_plrobj_file(legacy_path, &report_stream))
            ++salvaged_count;
    }

    report_stream << salvaged_count << " file(s) salvaged out of " << legacy_paths.size() << " legacy .obj file(s) examined.\n";

    if (report)
        *report = report_stream.str();
    return salvaged_count;
}

ACMD(do_convert_plrobjs)
{
    if (GET_LEVEL(ch) < LEVEL_IMPL) {
        send_to_char("You can't do that.\r\n", ch);
        return;
    }

    std::string argument_lower;
    if (argument != nullptr) {
        argument_lower = argument;
        std::transform(argument_lower.begin(), argument_lower.end(), argument_lower.begin(),
            [](unsigned char character) { return static_cast<char>(tolower(character)); });
    }

    if (argument_lower.find("recover") != std::string::npos) {
        std::string report;
        const int salvaged = recover_all_legacy_plrobjs("plrobjs", &report);

        // Distinct filename from the strict sweep's own report -- a
        // recovery run must never clobber convert_report.txt.
        const char* report_path = "plrobjs/recovery_report.txt";
        std::string write_error;
        write_file_contents_atomically(report_path, report, &write_error);

        char buf[MAX_STRING_LENGTH];
        snprintf(buf, sizeof(buf),
            "Plrobjs recovery sweep complete: %d file(s) salvaged. Full report: %s\r\n", salvaged, report_path);
        send_to_char(buf, ch);
        return;
    }

    const bool delete_after = argument_lower.find("delete") != std::string::npos;

    std::string report;
    const int converted = convert_all_legacy_plrobjs("plrobjs", delete_after, &report);

    // Lives under plrobjs/ itself (not the data-directory root) so it falls
    // under the same .gitignore coverage as everything else this sweep
    // touches -- a stray file at the lib/ root would show as untracked in
    // git status after every real admin run.
    const char* report_path = "plrobjs/convert_report.txt";
    std::string write_error;
    write_file_contents_atomically(report_path, report, &write_error);

    char buf[MAX_STRING_LENGTH];
    snprintf(buf, sizeof(buf),
        "Plrobjs conversion sweep complete: %d file(s) converted (delete_after=%s). Full report: %s\r\n", converted,
        delete_after ? "true" : "false", report_path);
    send_to_char(buf, ch);
}
