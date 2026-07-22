/* color.cc */

#include <ctype.h>
#include <algorithm>
#include <charconv>
#include <cstring>
#include <cstdio>
#include <format>
#include <iterator>
#include <stdlib.h>
#include <string>
#include <string.h>

#include "color.h"
#include "comm.h"
#include "db.h"
#include "interpre.h"
#include "text_view.h"
#include "utils.h"
#include "rots/persist/file_formats.h"
#include "rots/core/character.h"
#include "rots/core/types.h"

namespace {

// kColorRenderBufferCount/kColorRenderBufferSize/ansi_background_sequence()/
// append_escape()/append_truecolor_escape()/has_non_default_background()
// relocated verbatim to visibility.cpp (spell-family closure wave Task 1;
// sf-census.md section 4.1), alongside color_sequence[]/get_color_sequence()
// below -- their sole caller. Library-reader scan (CC_USE/CC_NORM/CC_FIX,
// color.h:65-70) found ZERO existing library readers of these macros (every
// current expansion site -- act_comm.cpp/act_info.cpp/act_wiz.cpp/color.cpp/
// comm.cpp/interpre.cpp/spell_pa.cpp/utility.cpp -- is still app-tier), so
// the controller's rots_core-floor amendment does not fire; rots_combat is
// this move's legal destination. This file's remaining helpers
// (parse_integer_token()/parse_rgb_triplet()/parse_hex_triplet()/
// describe_color_value()/etc. below) are unrelated to get_color_sequence()
// and stay here.

    bool parse_integer_token(std::string_view token, int* value)
    {
        token = rots::text::truncate_at_null(token);
        if (value == nullptr || token.empty())
            return false;

        int parsed = 0;
        const auto parse_result = std::from_chars(token.data(), token.data() + token.size(), parsed);
        if (parse_result.ec != std::errc() || parse_result.ptr != token.data() + token.size())
            return false;
        *value = parsed;
        return true;
    }

    bool parse_rgb_triplet(char* arguments, int* red, int* green, int* blue)
    {
        if (arguments == nullptr || red == nullptr || green == nullptr || blue == nullptr)
            return false;

        char first[MAX_INPUT_LENGTH];
        char remainder[MAX_INPUT_LENGTH];
        char second[MAX_INPUT_LENGTH];
        char final_token[MAX_INPUT_LENGTH];
        half_chop(arguments, first, remainder);
        half_chop(remainder, second, final_token);
        if (!parse_integer_token(first, red) || !parse_integer_token(second, green) || !parse_integer_token(final_token, blue))
            return false;
        return true;
    }

    bool is_valid_rgb_channel(int value)
    {
        return value >= 0 && value <= 255;
    }

    int parse_hex_channel(char high, char low)
    {
        auto decode = [](char value) -> int {
            if (value >= '0' && value <= '9')
                return value - '0';
            value = static_cast<char>(tolower(value));
            if (value >= 'a' && value <= 'f')
                return 10 + (value - 'a');
            return -1;
        };

        const int high_value = decode(high);
        const int low_value = decode(low);
        if (high_value < 0 || low_value < 0)
            return -1;
        return (high_value << 4) | low_value;
    }

    bool parse_hex_triplet(std::string_view token, int* red, int* green, int* blue)
    {
        token = rots::text::truncate_at_null(token);
        if (red == nullptr || green == nullptr || blue == nullptr)
            return false;

        if (token.starts_with('#'))
            token.remove_prefix(1);
        if (token.size() != 6)
            return false;

        const int parsed_red = parse_hex_channel(token[0], token[1]);
        const int parsed_green = parse_hex_channel(token[2], token[3]);
        const int parsed_blue = parse_hex_channel(token[4], token[5]);
        if (parsed_red < 0 || parsed_green < 0 || parsed_blue < 0)
            return false;

        *red = parsed_red;
        *green = parsed_green;
        *blue = parsed_blue;
        return true;
    }

    void describe_color_value(const color_value_data& value, int fallback_ansi, char* buffer, size_t buffer_size)
    {
        if (buffer == nullptr || buffer_size == 0)
            return;

        if (value.mode == COLOR_VALUE_TRUECOLOR) {
            snprintf(buffer, buffer_size, "#%02X%02X%02X",
                static_cast<unsigned int>(value.red),
                static_cast<unsigned int>(value.green),
                static_cast<unsigned int>(value.blue));
            return;
        }

        if (value.mode == COLOR_VALUE_ANSI16) {
            snprintf(buffer, buffer_size, "ansi %s", color_color[value.ansi].data());
            return;
        }

        if (fallback_ansi != CNRM)
            snprintf(buffer, buffer_size, "ansi %s", color_color[fallback_ansi].data());
        else
            snprintf(buffer, buffer_size, "default");
    }

    void show_extended_color_usage(struct char_data* ch)
    {
        send_to_char("Usage:\n\r", ch);
        send_to_char("  color <slot> <ansi colour>\n\r", ch);
        send_to_char("  color <slot> fg ansi <ansi colour>\n\r", ch);
        send_to_char("  color <slot> fg rgb <red> <green> <blue>\n\r", ch);
        send_to_char("  color <slot> fg hex #RRGGBB\n\r", ch);
        send_to_char("  color <slot> fg default\n\r", ch);
        send_to_char("  color <slot> bg ansi <ansi colour>\n\r", ch);
        send_to_char("  color <slot> bg rgb <red> <green> <blue>\n\r", ch);
        send_to_char("  color <slot> bg hex #RRGGBB\n\r", ch);
        send_to_char("  color <slot> bg default\n\r", ch);
    }

    void set_ansi_background(struct char_data* ch, int col, int value)
    {
        if (!ch || !ch->profs || col < 0 || col >= MAX_COLOR_FIELDS)
            return;

        ch->profs->color_settings[col].background.mode = COLOR_VALUE_ANSI16;
        ch->profs->color_settings[col].background.ansi = static_cast<unsigned char>(value);
        ch->profs->color_settings[col].background.red = 0;
        ch->profs->color_settings[col].background.green = 0;
        ch->profs->color_settings[col].background.blue = 0;
    }

    // Builds " name1 name2 ... nameN" (a leading space before every entry, no
    // trailing separator) for the first `count` entries of `names`. Used when
    // `do_color` reports the valid field/colour choices back to the player
    // after a lookup fails. Composing into a local std::string (rather than
    // the historical `sprintf(buf, "%s %s", buf, names[tmp])` loop) avoids
    // passing the same buffer as both source and destination of sprintf --
    // aliasing that happened to produce the intended left-to-right
    // accumulation under glibc, but is undefined behavior in the general
    // case (see upstream/sprintf-replacement's incremental-snprintf fix for
    // the same pattern in an earlier version of this file).
    std::string join_with_leading_spaces(const std::string_view* names, int count)
    {
        std::string joined;
        for (int index = 0; index < count; ++index)
            std::format_to(std::back_inserter(joined), " {}", names[index]);
        return joined;
    }

} // namespace

const std::string_view color_fields[] = {
    "narrate",
    "chat",
    "yell",
    "tell",
    "say",
    "roomname",
    "hit",
    "damage",
    "character",
    "object",
    "enemy",
    "description",
    "group",
    "magic",
    "weather",
    "off",
    "on",
    "default",
    "\n",
};

int num_of_color_fields = sizeof(color_fields) / sizeof(color_fields[0]);
static constexpr int kNumConfigurableColorFields = 15;
static constexpr int kColorCommandOff = kNumConfigurableColorFields;
static constexpr int kColorCommandOn = kNumConfigurableColorFields + 1;
static constexpr int kColorCommandDefault = kNumConfigurableColorFields + 2;

static void show_color_slot_summary(struct char_data* ch, int slot)
{
    if (ch == nullptr || ch->profs == nullptr || slot < 0 || slot >= MAX_COLOR_FIELDS)
        return;

    char foreground[64];
    char background[64];
    describe_color_value(ch->profs->color_settings[slot].foreground, ch->profs->colors[slot], foreground, sizeof(foreground));
    describe_color_value(ch->profs->color_settings[slot].background, CNRM, background, sizeof(background));
    snprintf(buf, sizeof(buf), "%11s: fg %s bg %s\n\r", color_fields[slot].data(), foreground, background);
    send_to_char(buf, ch);
}

// color_sequence[] relocated verbatim to visibility.cpp (spell-family
// closure wave Task 1; sf-census.md section 4.1), alongside its sole
// reader get_color_sequence() below. Declared in color.h (unchanged);
// CC_NORM/CC_FIX (color.h) still expand to a direct color_sequence[...]
// read at every existing (still app-tier) call site, now resolving
// downward into rots_combat instead of a same-TU definition.

int find_color_field(std::string_view field_name)
{
    field_name = rots::text::truncate_at_null(field_name);
    if (field_name.empty()) {
        return -1;
    }

    for (int field_index = 0; field_index < num_of_color_fields - 1; ++field_index) {
        const std::string_view candidate(color_fields[field_index]);
        if (field_name.size() <= candidate.size()
            && candidate.substr(0, field_name.size()) == field_name) {
            return field_index;
        }
    }
    return -1;
}

int find_ansi_color(std::string_view color_name)
{
    color_name = rots::text::truncate_at_null(color_name);
    if (color_name.empty()) {
        return -1;
    }

    for (int color_index = 0; color_index < num_of_colors - 1; ++color_index) {
        if (color_name == color_color[color_index]) {
            return color_index;
        }
    }
    return -1;
}

char get_colornum(struct char_data* ch, int col)
{
    if (!ch)
        return 0;

    if (!ch->profs)
        return 0;

    return ch->profs->colors[col];
}

void set_colornum(struct char_data* ch, int col, int value)
{
    if (!ch || !ch->profs)
        return;

    ch->profs->colors[col] = value;
    sync_color_slot_foreground_from_ansi(ch->profs, col);
}

void set_truecolor_foreground(struct char_data* ch, int col, int red, int green, int blue)
{
    if (!ch || !ch->profs || col < 0 || col >= MAX_COLOR_FIELDS)
        return;

    color_slot_data& slot = ch->profs->color_settings[col];
    slot.foreground.mode = COLOR_VALUE_TRUECOLOR;
    slot.foreground.red = static_cast<unsigned char>(red);
    slot.foreground.green = static_cast<unsigned char>(green);
    slot.foreground.blue = static_cast<unsigned char>(blue);
    slot.foreground.ansi = static_cast<unsigned char>(nearest_ansi_color(red, green, blue));
    ch->profs->colors[col] = static_cast<char>(slot.foreground.ansi);
}

void set_truecolor_background(struct char_data* ch, int col, int red, int green, int blue)
{
    if (!ch || !ch->profs || col < 0 || col >= MAX_COLOR_FIELDS)
        return;

    color_slot_data& slot = ch->profs->color_settings[col];
    slot.background.mode = COLOR_VALUE_TRUECOLOR;
    slot.background.red = static_cast<unsigned char>(red);
    slot.background.green = static_cast<unsigned char>(green);
    slot.background.blue = static_cast<unsigned char>(blue);
    slot.background.ansi = static_cast<unsigned char>(nearest_ansi_color(red, green, blue));
}

void clear_color_background(struct char_data* ch, int col)
{
    if (!ch || !ch->profs || col < 0 || col >= MAX_COLOR_FIELDS)
        return;

    ch->profs->color_settings[col].background = color_value_data {};
}

// get_color_sequence() relocated verbatim to visibility.cpp (spell-family
// closure wave Task 1; sf-census.md section 4.1: presentation over
// char_data's color_settings/profs fields, self-contained aside from the
// helpers/array relocated alongside it). Declared in color.h (unchanged);
// CC_USE (color.h) still expands to a direct get_color_sequence(ch, col)
// call at every existing (still app-tier) call site, now resolving
// downward into rots_combat instead of a same-TU call.

/*
 * Give 'ch' the set of RotS default colors.
 */
void set_colors_default(struct char_data* ch)
{
    SET_BIT(PRF_FLAGS(ch), PRF_COLOR);
    set_colornum(ch, COLOR_NARR, CYEL);
    set_colornum(ch, COLOR_CHAT, CMAG);
    set_colornum(ch, COLOR_YELL, CRED);
    set_colornum(ch, COLOR_TELL, CGRN);
    set_colornum(ch, COLOR_SAY, CCYN);
    set_colornum(ch, COLOR_ROOM, CYEL);
    set_colornum(ch, COLOR_HIT, CCYN);
    set_colornum(ch, COLOR_DAMG, CRED);
    set_colornum(ch, COLOR_CHAR, CGRN);
    set_colornum(ch, COLOR_OBJ, CCYN);
    set_colornum(ch, COLOR_ENMY, CBWHT);
    set_colornum(ch, COLOR_DESC, CGRN);
    set_colornum(ch, COLOR_GTELL, CGRN);
    set_colornum(ch, COLOR_MAGIC, CBMAG);
    set_colornum(ch, COLOR_WEATHER, CBCYN);
}

ACMD(do_color)
{
    int tmp, num, col;
    char option[MAX_INPUT_LENGTH];
    char remainder[MAX_INPUT_LENGTH];

    half_chop(argument, buf, arg);

    if (!*buf) {
        /* so we report the colors currently set */
        if (!PRF_FLAGGED(ch, PRF_COLOR)) {
            send_to_char("Your colours are turned off.\n\r", ch);
            return;
        }

        send_to_char("Your colours are:\n\r", ch);
        for (tmp = 0; tmp < kNumConfigurableColorFields; tmp++)
            show_color_slot_summary(ch, tmp);
        return;
    }

    num = find_color_field(buf);

    if (num < 0) {
        send_to_char("Possible arguments are:\n\r", ch);
        std::string message = join_with_leading_spaces(color_fields, num_of_color_fields - 1);
        message += "\n\r";
        send_to_char(message, ch);
        show_extended_color_usage(ch);
        return;
    }

    if (num == kColorCommandDefault) {
        set_colors_default(ch);
        send_to_char("Ok, you'll use the default colour set.\r\n", ch);
        return;
    } else if (num == kColorCommandOn) {
        SET_BIT(PRF_FLAGS(ch), PRF_COLOR);
        send_to_char("Colours turned on.\n\r", ch);
        return;
    } else if (num == kColorCommandOff) {
        REMOVE_BIT(PRF_FLAGS(ch), PRF_COLOR);
        send_to_char("Colours turned off.\n\r", ch);
        return;
    }

    if (!*arg) {
        show_extended_color_usage(ch);
        return;
    }

    half_chop(arg, option, remainder);
    if (!str_cmp_nullable(option, "fg") || !str_cmp_nullable(option, "bg")) {
        const bool foreground = !str_cmp_nullable(option, "fg");
        char mode[MAX_INPUT_LENGTH];
        char value_arguments[MAX_INPUT_LENGTH];
        half_chop(remainder, mode, value_arguments);

        if (!*mode) {
            show_extended_color_usage(ch);
            return;
        }

        if (!str_cmp_nullable(mode, "default")) {
            if (foreground) {
                ch->profs->color_settings[num].foreground = color_value_data {};
                ch->profs->colors[num] = CNRM;
                vsend_to_char(
                    ch, "You set %s foreground to default.\n\r", color_fields[num].data());
            } else {
                clear_color_background(ch, num);
                vsend_to_char(
                    ch, "You set %s background to default.\n\r", color_fields[num].data());
            }
            return;
        }

        if (!str_cmp_nullable(mode, "ansi")) {
            col = find_ansi_color(value_arguments);
            if (col < 0) {
                send_to_char("Possible colours are:", ch);
                std::string message = join_with_leading_spaces(color_color, 8);
                message += ".\r\n";
                message += "Additionally, you may prefix any of the above colours with 'bright'.\r\n";
                send_to_char(message, ch);
                return;
            }

            if (foreground) {
                set_colornum(ch, num, col);
                vsend_to_char(ch, "You set %s foreground to %s%s%s.\n\r",
                    color_fields[num].data(), CC_USE(ch, num), color_color[col].data(),
                    CC_NORM(ch).data());
            } else {
                set_ansi_background(ch, num, col);
                vsend_to_char(ch, "You set %s background to %s.\n\r", color_fields[num].data(),
                    color_color[col].data());
            }
            return;
        }

        if (!str_cmp_nullable(mode, "rgb")) {
            int red, green, blue;
            if (!parse_rgb_triplet(value_arguments, &red, &green, &blue)) {
                send_to_char("RGB colours must be provided as three integers.\n\r", ch);
                return;
            }
            if (!is_valid_rgb_channel(red) || !is_valid_rgb_channel(green) || !is_valid_rgb_channel(blue)) {
                send_to_char("RGB values must be between 0 and 255.\n\r", ch);
                return;
            }

            if (foreground) {
                set_truecolor_foreground(ch, num, red, green, blue);
                vsend_to_char(ch, "You set %s foreground to #%02X%02X%02X.\n\r",
                    color_fields[num].data(), red, green, blue);
            } else {
                set_truecolor_background(ch, num, red, green, blue);
                vsend_to_char(ch, "You set %s background to #%02X%02X%02X.\n\r",
                    color_fields[num].data(), red, green, blue);
            }
            return;
        }

        if (!str_cmp_nullable(mode, "hex")) {
            int red, green, blue;
            if (!parse_hex_triplet(value_arguments, &red, &green, &blue)) {
                send_to_char("Hex colours must look like #RRGGBB.\n\r", ch);
                return;
            }

            if (foreground) {
                set_truecolor_foreground(ch, num, red, green, blue);
                vsend_to_char(ch, "You set %s foreground to #%02X%02X%02X.\n\r",
                    color_fields[num].data(), red, green, blue);
            } else {
                set_truecolor_background(ch, num, red, green, blue);
                vsend_to_char(ch, "You set %s background to #%02X%02X%02X.\n\r",
                    color_fields[num].data(), red, green, blue);
            }
            return;
        }

        show_extended_color_usage(ch);
        return;
    }

    col = find_ansi_color(arg);

    if (col < 0) {
        send_to_char("Possible colours are:", ch);
        std::string message = join_with_leading_spaces(color_color, 8);
        message += ".\r\n";
        message += "Additionally, you may prefix any of the above "
                   "colours with 'bright'.\r\n";
        send_to_char(message, ch);
        return;
    }

    set_colornum(ch, num, col);

    vsend_to_char(ch, "You colour %s %s%s%s.\n\r",
        color_fields[num].data(),
        CC_USE(ch, num),
        color_color[col].data(),
        CC_NORM(ch).data());
}
