/* color_convert.cc */

// Leaf TU for the pure color-conversion helpers that both `ageland` and
// `rots_convert` must execute identically: nearest_ansi_color(),
// convert_old_colormask(), sync_color_slot_foreground_from_ansi(), plus the
// color_color[]/num_of_colors table pair they read. Carved out of color.cpp
// (persist-split wave, PS Task 1) so the two binaries link ONE real
// definition of each instead of color.cpp's copy plus a synchronized
// verbatim duplicate in convert_stubs.cpp -- see convert_stubs.cpp's git
// history (pre-PS-Task-1) for the deleted duplicate and its per-symbol
// reachability analysis. These functions have no comm/game dependency (pure
// struct-field/table logic), so they belong in their own leaf TU both
// binaries can compile without pulling in send_to_char()/vsend_to_char()/
// half_chop()/str_cmp_nullable() the way linking color.cpp wholesale into
// rots_convert would (see convert_stubs.cpp's now-deleted color section for
// that constraint).
//
// Bodies are byte-identical to color.cpp's prior versions (see color.cpp's
// git history, pre-PS-Task-1, for the removed originals) with ONE named
// deviation: sync_color_slot_foreground_from_ansi() loses the anonymous-
// namespace linkage it had in color.cpp -- color.cpp's set_colornum() still
// calls it cross-TU after this move, so it needs external linkage. Declared
// in color.h next to nearest_ansi_color().

#include <limits>

#include "color.h"
#include "rots/persist/file_formats.h"

// Promoted out of color.cpp's anonymous namespace (this helper was
// file-local there). color.cpp's set_colornum() still calls it cross-TU
// after this move, so it now has external linkage; declared in color.h.
void sync_color_slot_foreground_from_ansi(struct char_prof_data* profs, int col)
{
    if (profs == nullptr || col < 0 || col >= MAX_COLOR_FIELDS)
        return;

    profs->color_settings[col].foreground.mode = COLOR_VALUE_ANSI16;
    profs->color_settings[col].foreground.ansi = static_cast<unsigned char>(profs->colors[col]);
}

const std::string_view color_color[] = {
    "normal",
    "red",
    "green",
    "yellow",
    "blue",
    "magenta",
    "cyan",
    "white",
    "bright red",
    "bright green",
    "bright yellow",
    "bright blue",
    "bright magenta",
    "bright cyan",
    "bright white",
    "\n"
};

int num_of_colors = sizeof(color_color) / sizeof(color_color[0]);

void convert_old_colormask(struct char_file_u* ch)
{
    int i;

    if (!ch->profs.color_mask)
        i = 0;
    else
        for (i = 0; i < 10; ++i)
            ch->profs.colors[i] = ch->profs.color_mask >> (i * 3) & 7;

    for (i = 0; i < MAX_COLOR_FIELDS; ++i) {
        if (ch->profs.color_settings[i].foreground.mode == COLOR_VALUE_DEFAULT)
            sync_color_slot_foreground_from_ansi(&ch->profs, i);
    }
}

int nearest_ansi_color(int red, int green, int blue)
{
    struct AnsiColor {
        int red;
        int green;
        int blue;
    };

    static const AnsiColor ansi_palette[] = {
        { 0, 0, 0 },
        { 170, 0, 0 },
        { 0, 170, 0 },
        { 170, 85, 0 },
        { 0, 0, 170 },
        { 170, 0, 170 },
        { 0, 170, 170 },
        { 170, 170, 170 },
        { 255, 85, 85 },
        { 85, 255, 85 },
        { 255, 255, 85 },
        { 85, 85, 255 },
        { 255, 85, 255 },
        { 85, 255, 255 },
        { 255, 255, 255 },
    };

    int best_index = CNRM;
    long best_distance = std::numeric_limits<long>::max();
    for (int index = 0; index < num_of_colors - 1; ++index) {
        const long red_distance = red - ansi_palette[index].red;
        const long green_distance = green - ansi_palette[index].green;
        const long blue_distance = blue - ansi_palette[index].blue;
        const long distance = red_distance * red_distance + green_distance * green_distance + blue_distance * blue_distance;
        if (distance < best_distance) {
            best_distance = distance;
            best_index = index;
        }
    }

    return best_index;
}
