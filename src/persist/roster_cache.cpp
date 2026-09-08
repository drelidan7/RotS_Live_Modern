#include "roster_cache.h"

#include "account_management.h"
#include "rots/persist/file_formats.h"
#include "text_view.h"

#include <algorithm>
#include <string>
#include <unordered_map>

namespace roster_cache {

namespace {

    std::unordered_map<std::string, RosterSummary> g_summaries;

    std::string compose_key(std::string_view root_directory, std::string_view character_name)
    {
        root_directory = rots::text::truncate_at_null(root_directory);
        const std::string normalized_name = account::normalize_account_name(character_name);
        // A length prefix keeps roots containing any separator byte distinct.
        std::string key = std::to_string(root_directory.size());
        key.push_back(':');
        key.append(root_directory);
        key.append(normalized_name);
        return key;
    }

    bool g_enabled = false;

    bool default_reader(std::string_view root_directory, std::string_view account_name,
        std::string_view character_name, char_file_u* out_stored_character, std::string* out_error_message)
    {
        return account::read_account_character_file(
            root_directory, account_name, character_name, out_stored_character, out_error_message);
    }

    ReaderFn g_reader = &default_reader;

    short clamp_coefficient(int value)
    {
        // Domain of square_root[] is 0..170 (consts.cpp). Clamp so a corrupt file cannot produce a
        // wild sort key or, later, an out-of-bounds index.
        if (value < 0) {
            return 0;
        }
        if (value > 170) {
            return 170;
        }
        return static_cast<short>(value);
    }

    RosterSummary summarize(const char_file_u& stored_character)
    {
        RosterSummary summary;
        summary.readable = true;
        summary.level = static_cast<unsigned char>(stored_character.level);
        summary.race = static_cast<unsigned char>(stored_character.race);
        for (int profession = 0; profession <= MAX_PROFS; ++profession) {
            summary.prof_coof[profession] = clamp_coefficient(stored_character.profs.prof_coof[profession]);
        }
        return summary;
    }

} // namespace

bool get(std::string_view root_directory, std::string_view account_name,
    std::string_view character_name, RosterSummary* out_summary)
{
    if (out_summary == nullptr) {
        return false;
    }

    root_directory = rots::text::truncate_at_null(root_directory);
    account_name = rots::text::truncate_at_null(account_name);
    const std::string normalized_name = account::normalize_account_name(character_name);
    std::string key = compose_key(root_directory, normalized_name);
    if (g_enabled) {
        const auto cached_entry = g_summaries.find(key);
        if (cached_entry != g_summaries.end()) {
            *out_summary = cached_entry->second;
            return true;
        }
    }

    char_file_u stored_character { };
    std::string read_error;
    RosterSummary loaded;
    if (g_reader(root_directory, account_name, normalized_name, &stored_character, &read_error)) {
        loaded = summarize(stored_character);
    }
    // else: loaded keeps its defaults, readable == false -- the "[ ?? ???]" row.

    if (g_enabled) {
        g_summaries.emplace(std::move(key), loaded);
    }

    *out_summary = loaded;
    return true;
}

void invalidate_character(std::string_view root_directory, std::string_view character_name)
{
    g_summaries.erase(compose_key(root_directory, character_name));
}

void clear()
{
    g_summaries.clear();
}

void set_enabled(bool enabled)
{
    g_enabled = enabled;
}

bool is_enabled()
{
    return g_enabled;
}

void set_backing_reader_for_testing(ReaderFn reader)
{
    g_reader = &default_reader;
    if (reader != nullptr) {
        g_reader = reader;
    }
    clear();
}

} // namespace roster_cache
