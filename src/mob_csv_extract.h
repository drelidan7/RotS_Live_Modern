#pragma once

#include "interpre.h"
#include "structs.h"

#include <string_view>

/// Exports the current mobile prototypes to a comma-separated value file.
class mob_csv_extract {
public:
    // Stores the destination selected by the administrative export command.
    std::string file_location = "../mobs/mob.csv";
    // Owns the active C stream between create_file and close_file; null when no export is open.
    FILE* file = nullptr;

    /// Generates one CSV row for the supplied mobile prototype.
    static std::string generate_npc_stat(char_data* ch);
    /// Opens the configured export destination and reports failures to the invoking character.
    void create_file(char_data* ch);
    /// Writes the fixed CSV column header to the active export stream.
    void create_header(char_data* ch) const;
    /// Closes and clears the active export stream.
    void close_file(char_data* ch);
    /// Writes the first-null-terminated prefix of bounded CSV row text.
    void write_to_file(char_data* ch, std::string_view mob_data) const;
};

ACMD(do_mob_csv_extract);
