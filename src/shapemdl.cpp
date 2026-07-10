/*
 * src/shapemdl.cc
 *
 * Provide the in-game interface to ASIMA mudlle shaping
 */

#include "platdef.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "comm.h"
#include "db.h"
#include "handler.h"
#include "interpre.h"
#include "protos.h"
#include "structs.h"
#include "utils.h"
#include <filesystem>
#include <format>
#include <system_error>

extern struct room_data world;
extern char** mobile_program;
extern int* mobile_program_zone;
extern int num_of_programs;

char* mudlle_converter(char* source);

int load_mudlle(char_data* ch, char* arg)
{
    FILE* fp;
    int number, num, isnew;
    char str[MAX_STRING_LENGTH];
    char* tmpstr;

    if (!SHAPE_MUDLLE(ch)) {
        send_to_char("You are not shaping anything.\n\r", ch);
        return -1;
    }
    if (SHAPE_MUDLLE(ch)->prog_num > 0) {
        send_to_char("You are already shaping a program.\n\r", ch);
        return -1;
    }

    while (*arg && *arg <= ' ')
        arg++;

    number = 0;
    number = atoi(arg);
    if (!number) {
        send_to_char("Usage: /load [number].\n\r", ch);
        return -1;
    }
    // fname is only ever used as the %s/{} substitution below (the numeric
    // zone-directory component of the .mdl path) -- a std::string avoids the
    // fixed-size scratch buffer the original char fname[100] needed for that
    // single purpose. SHAPE_MDL_DIR/SHAPE_MDL_BACKDIR's %s templates are
    // inlined here (rather than reusing the macros, which stay %-style for
    // any other shape*.cpp file not yet converted in this pass) with the
    // literal path text unchanged.
    const std::string fname = std::format("{}", number / 100);
    strcpy(str, std::format("world/mdl/{}.mdl", fname).c_str());

    if (!(fp = fopen(str, "rb"))) {
        send_to_char("Could not open programs file.\n\r", ch);
        return -1;
    }
    strcpy(SHAPE_MUDLLE(ch)->f_from, str);
    strcpy(SHAPE_MUDLLE(ch)->f_old, std::format("world/mdl/oldmdls/{}.mdl", fname).c_str());

    num = 0;
    do {
        if (!fgets(str, 1000, fp)) {
            send_to_char("Wrong program file format.\n\r", ch);
            return -1;
        }
        tmpstr = str;
        while (*tmpstr && (*tmpstr < ' '))
            tmpstr++;
        if (*tmpstr == '#') {
            num = atoi(str + 1);
            if ((num == number) || (num == 99999))
                break;
        }
    } while (1);

    if (num == 99999) {
        CREATE(SHAPE_MUDLLE(ch)->txt, char, 1);
        send_to_char("A new program.\n\r", ch);
        SHAPE_MUDLLE(ch)
            ->real_num
            = 0;
        isnew = 1;
    } else {
        isnew = 0;
        memset(str, 0, MAX_STRING_LENGTH);
        do {
            tmpstr = str + strlen(str);
            fgets(tmpstr, MAX_STRING_LENGTH - strlen(str), fp);
            while (*tmpstr && (*tmpstr < ' '))
                tmpstr++;
        } while (*tmpstr != '#');
        *tmpstr = 0;

        CREATE(SHAPE_MUDLLE(ch)->txt, char, strlen(str) + 1);
        strcpy(SHAPE_MUDLLE(ch)->txt, str);

        SHAPE_MUDLLE(ch)
            ->real_num
            = real_program(number);

        if (!isnew) {
            send_to_char(std::format("You uploaded the special program #{}.\n\r", number).c_str(), ch);
        } else {
            send_to_char(std::format("Could not find a program #{}, created it.\n\r", number).c_str(), ch);
        }
    }

    SHAPE_MUDLLE(ch)
        ->permission
        = get_permission(number / 100, ch);
    if (!SHAPE_MUDLLE(ch)->permission)
        send_to_char("You have only limited permission for this zone.\n\r", ch);

    SHAPE_MUDLLE(ch)
        ->prog_num
        = num;
    ch->specials.prompt_value = num;

    return num;
}

int save_mudlle(struct char_data* ch)
{
    FILE *fp, *ofp;
    int tmp, num, saved;
    char str[MAX_STRING_LENGTH];

    if (!SHAPE_MUDLLE(ch)) {
        send_to_char("You are not shaping anything.\n\r", ch);
        return -1;
    }
    if (SHAPE_MUDLLE(ch)->prog_num < 0) {
        send_to_char("You have nothing to save.\n\r", ch);
        return -1;
    }
    if (!SHAPE_MUDLLE(ch)->permission) {
        send_to_char("You have no access to this zone, may not save.\n\r", ch);
        return -1;
    }
    // f_from/f_old are char[80] struct members read here as %s/{} sources --
    // decay to const char* so libc++'s std::format doesn't format the raw
    // fixed-size array (which would emit all 80 bytes, embedded NULs and all,
    // instead of stopping at the string's terminator).
    const std::string copy_command = std::format("{} {} {}", COPY_COMMAND,
        static_cast<const char*>(SHAPE_MUDLLE(ch)->f_from), static_cast<const char*>(SHAPE_MUDLLE(ch)->f_old));
    // copy_command is a locally-built command line, not a format string -- pass it
    // through fputs rather than as fprintf's format argument (a non-literal-format-
    // string bug: any '%' bytes it happens to contain would previously be
    // interpreted as conversion specifiers).
    fputs(copy_command.c_str(), stderr);
    // Was system(str) (a shell "cp <f_from> <f_old>"); the return value was
    // never checked, so a failed copy silently left f_old stale -- preserve
    // that by ignoring copy_ec here too.
    std::error_code copy_ec;
    std::filesystem::copy_file(SHAPE_MUDLLE(ch)->f_from, SHAPE_MUDLLE(ch)->f_old,
        std::filesystem::copy_options::overwrite_existing, copy_ec);
    fp = fopen(SHAPE_MUDLLE(ch)->f_from, "wb+");
    ofp = fopen(SHAPE_MUDLLE(ch)->f_old, "rb");

    num = -1;
    saved = 0;
    while (!feof(ofp)) {
        fgets(str, MAX_STRING_LENGTH, ofp);
        // str is a raw line read from a mudlle program file, not a format string --
        // same non-literal-format-string fix as above.
        fputs(str, stderr);
        for (tmp = 0; (tmp < MAX_STRING_LENGTH) && (str[tmp] <= ' '); tmp++)
            ;

        if (str[tmp] == '#') {
            num = atoi(str + tmp + 1);
            if ((num >= SHAPE_MUDLLE(ch)->prog_num) && !saved) {
                clean_text(SHAPE_MUDLLE(ch)->txt);
                // "%-d" has no field width, so the '-' (left-justify) flag is a
                // no-op here -- equivalent to plain "%d"/"{}".
                strcpy(str, std::format("#{}\n", SHAPE_MUDLLE(ch)->prog_num).c_str());
                fwrite(str, 1, strlen(str), fp);
                fwrite(SHAPE_MUDLLE(ch)->txt, 1, strlen(SHAPE_MUDLLE(ch)->txt), fp);
                saved = 1;
            }
        }
        if (num != SHAPE_MUDLLE(ch)->prog_num)
            fwrite(str, 1, strlen(str), fp);
    }
    if (saved)
        send_to_char("Saved succesfully.\n\r", ch);
    else
        send_to_char("Could not save. Sorry.\n\r", ch);

    fclose(fp);
    fclose(ofp);
    return SHAPE_MUDLLE(ch)->prog_num;
}

void implement_mudlle(struct char_data* ch)
{
    if (!SHAPE_MUDLLE(ch)) {
        send_to_char("You are not shaping anything.\n\r", ch);
        return;
    }
    if (SHAPE_MUDLLE(ch)->prog_num < 0) {
        send_to_char("You have nothing to implement.\n\r", ch);
        return;
    }
    if (!SHAPE_MUDLLE(ch)->permission) {
        send_to_char("You have no access to this zone, may not implement.\n\r", ch);
        return;
    }
    if (SHAPE_MUDLLE(ch)->real_num < 0) {
        send_to_char("This is a new program; implementation requires a reboot.\n\r", ch);
        return;
    }
    RELEASE(mobile_program[SHAPE_MUDLLE(ch)->real_num]);
    mobile_program[SHAPE_MUDLLE(ch)->real_num] = mudlle_converter(SHAPE_MUDLLE(ch)->txt);

    send_to_char("Program implemented.\n\r", ch);
}

void show_mudlle(struct char_data* ch)
{
    if (!SHAPE_MUDLLE(ch))
        return;
    if ((!SHAPE_MUDLLE(ch)->txt) || (!SHAPE_MUDLLE(ch)->prog_num < 0)) {
        send_to_char("You have no program to shape.\n\r", ch);
        return;
    }

    send_to_char(std::format("Program #{} (real #{}).\n\rProgram text:\n\r",
        SHAPE_MUDLLE(ch)->prog_num, SHAPE_MUDLLE(ch)->real_num).c_str(), ch);
    send_to_char(SHAPE_MUDLLE(ch)->txt, ch);
    send_to_char("\n\r", ch);

    return;
}

void free_mudlle(struct char_data* ch)
{
    RELEASE(SHAPE_MUDLLE(ch)->txt);
    RELEASE(ch->temp);
    ch->specials.prompt_value = -1;
    ch->specials.prompt_number = 0;
    REMOVE_BIT(PRF_FLAGS(ch), PRF_DISPTEXT);
    return;
}

void edit_mudlle(struct char_data* ch)
{
    if (!ch->desc)
        return;
    string_add_init(ch->desc, &(SHAPE_MUDLLE(ch)->txt));
    return;
}

void shape_center_mudlle(struct char_data* ch, char* argument)
{
    char str[255], str2[255];
    int len;

    str[0] = str2[0] = 0;
    if (!sscanf(argument, "%s %s", str, str2))
        return;
    len = strlen(str);

    if (!strncmp(str, "load", len)) {
        load_mudlle(ch, str2);
        return;
    } else if (!strncmp(str, "show", len)) {
        show_mudlle(ch);
        return;
    } else if (!strncmp(str, "free", len)) {
        free_mudlle(ch);
        return;
    } else if (!strncmp(str, "save", len)) {
        save_mudlle(ch);
        return;
    } else if (!strncmp(str, "implement", len)) {
        implement_mudlle(ch);
        return;
    } else if (!strncmp(str, "edit", len)) {
        edit_mudlle(ch);
        return;
    } else if (!strncmp(str, "done", len)) {
        implement_mudlle(ch);
        save_mudlle(ch);
        free_mudlle(ch);
        return;
    }
}
