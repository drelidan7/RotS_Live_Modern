#include "player_file_finalize.h"
#include "text_view.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

bool finalize_player_file_legacy(std::string_view scratch_path, std::string_view base_path,
                                 std::string_view versioned_path) {
    const std::string scratch_path_owner(rots::text::truncate_at_null(scratch_path));
    const std::string base_path_owner(rots::text::truncate_at_null(base_path));
    const std::string versioned_path_owner(rots::text::truncate_at_null(versioned_path));
    char command[300];

    snprintf(command, sizeof(command), "rm %s.*", base_path_owner.c_str());
    int remove_result = system(command);
    snprintf(command, sizeof(command), "cp %s %s", scratch_path_owner.c_str(), versioned_path_owner.c_str());
    int copy_result = system(command);

    return (remove_result != -1) && (copy_result != -1);
}

bool finalize_player_file_rename(std::string_view scratch_path, std::string_view dir_path,
                                 std::string_view base_name, std::string_view versioned_path) {
    namespace fs = std::filesystem;
    std::error_code filesystem_error;
    const std::string scratch_path_owner(rots::text::truncate_at_null(scratch_path));
    const std::string directory_path_owner(rots::text::truncate_at_null(dir_path));
    const std::string base_name_owner(rots::text::truncate_at_null(base_name));
    const std::string versioned_path_owner(rots::text::truncate_at_null(versioned_path));

    // 1. Publish the new file first so a crash here cannot lose the save (atomic move).
    fs::rename(scratch_path_owner, versioned_path_owner, filesystem_error);
    if (filesystem_error) {
        return false;
    }

    // 2. Remove any OTHER stale "<base>." entries, leaving the file we just wrote.
    const size_t base_length = base_name_owner.size();
    const std::string_view versioned_view(versioned_path_owner);
    const size_t v_slash = versioned_view.find_last_of('/');
    const std::string_view keep_name =
        (v_slash == std::string_view::npos) ? versioned_view : versioned_view.substr(v_slash + 1);

    std::vector<fs::path> victims;
    fs::directory_iterator iterator(directory_path_owner, filesystem_error);
    if (filesystem_error) {
        return false;
    }
    const fs::directory_iterator end;
    // Check ec right AFTER each increment: on error libstdc++ resets the iterator to end,
    // so a top-of-loop check would be skipped and the error silently swallowed.
    while (iterator != end) {
        // path::filename() is the portable way to get just the last path component --
        // no manual '/'-splitting needed (which also assumed a POSIX-style separator).
        // path::string() (not native()) always yields std::string on every platform;
        // native() is std::wstring on Windows, which doesn't even compile against the
        // std::string_view usage below (found via the windows-msvc CI log, Phase 3
        // Task 5 bring-up round 2).
        const std::string name = iterator->path().filename().string();
        if (name.size() > base_length && name.compare(0, base_length, base_name_owner) == 0 &&
            name[base_length] == '.' && name != keep_name) {
            victims.push_back(iterator->path());
        }
        iterator.increment(filesystem_error);
        if (filesystem_error) {
            return false;
        }
    }
    for (const fs::path& victim : victims) {
        fs::remove(victim, filesystem_error);
        if (filesystem_error) {
            return false;
        }
    }
    return true;
}
