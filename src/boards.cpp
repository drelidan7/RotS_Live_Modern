/* ************************************************************************
 *   File: boards.c                                      Part of CircleMUD *
 *  Usage: handling of multiple bulletin boards                            *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ************************************************************************ */

/* FEATURES & INSTALLATION INSTRUCTIONS ***********************************

Written by Jeremy "Ras" Elson (jelson@server.cs.jhu.edu)

This board code has many improvements over the infamously buggy standard
Diku board code.  Features include:

- Arbitrary number of boards handled by one set of generalized routines.
  Adding a new board is as easy as adding another entry to an array.
- Bug-free operation -- no more mixed messages!
- Safe removal of messages while other messages are being written.
- Does not allow messages to be removed by someone of a level less than
  the poster's level.

To install:

0.  Edit your makefile so that boards.c is compiled and linked into the server.

1.  In spec_assign.c, declare the specproc "gen_board".  Give ALL boards
    the gen_board specproc.

2.  In boards.h, change the constants CMD_READ, CMD_WRITE, CMD_REMOVE, and
    CMD_LOOK to the correct command numbers for your mud's interpreter.

3.  In boards.h, change NUM_OF_BOARDS to reflect how many different types
    of boards you have.  Change MAX_BOARD_MESSAGES to the maximum number
    of messages postable before people start to get a 'board is full'
    message.

4.  Follow the instructions for adding a new board (below) to correctly
    define the board_info array (also below).  Make sure you define an
    entry in this array for each object that you gave the gen_board specproc
    in step 1.

Send comments, bug reports, help requests, etc. to Jeremy Elson
(jelson@server.cs.jhu.edu).  Enjoy!

************************************************************************/

/* TO ADD A NEW BOARD, simply follow our easy 3-step program:

1 - Create a new board object in the object files

2 - Increase the NUM_OF_BOARDS constant in board.h

3 - Add a new line to the board_info array below.  The fields, in order, are:

        Board's virtual number.
        Min level one must be to look at this board or read messages on it.
        Min level one must be to post a message to the board.
        Min level one must be to remove other people's messages from this
                board (but you can always remove your own message).
        Filename of this board, in quotes.
        Last field must always be 0.
*/

#include "platdef.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform_compat.h"
#include "boards.h"
#include "comm.h"
#include "db.h"
#include "handler.h"
#include "interpre.h"
#include "json_utils.h"
#include "structs.h"
#include "utils.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <utility>

extern struct room_data world;
extern struct obj_data* obj_proto;
extern struct descriptor_data* descriptor_list;
extern struct player_index_element* player_table;
int find_name(char* name);
int has_mail(char* recipient);
int _parse_name(char* arg, char* name);

/*
 * format: lnum, vnum, read lvl, write lvl, remove lvl, filename, 0 at end
 * Be sure to also change NUM_OF_BOARDS in board.h
 */

byte board_info_type::num_of_boards = 0;
struct board_info_type* board_info[NUM_OF_BOARDS];
struct mail_info_type* mail_board;
int news_board_number = 1104;
struct board_info_type* news_board;

char* msg_storage[INDEX_SIZE];
int msg_storage_taken[INDEX_SIZE];

int find_slot(void)
{
    int i;

    for (i = 0; i < INDEX_SIZE; i++)
        if (!msg_storage_taken[i]) {
            msg_storage_taken[i] = 1;
            return i;
        }

    return -1;
}

/* search the room ch is standING(in to find which board he's looking at */
board_info_type* find_board(struct char_data* ch)
{
    struct obj_data* obj;
    int i;
    for (obj = world[ch->in_room].contents; obj; obj = obj->next_content)
        for (i = 0; i < NUM_OF_BOARDS; i++) {
            //       printf("boards are, real:%d, virt:%d\n",G_RNUM(board_info[i]),
            //	      G_VNUM(board_info[i]));
            if (G_RNUM(board_info[i]) == obj->item_number)
                return board_info[i];
        }

    return 0;
}

/* vnum, read, write, remove */
void init_boards(void)
{
    int i, fatal_error = 0, tmp_news;

    for (i = 0; i < INDEX_SIZE; i++) {
        msg_storage[i] = 0;
        msg_storage_taken[i] = 0;
    }
    board_info[0] = new board_info_type(1112, 0, 0, LEVEL_GOD + 1, MAX_BOARD_MESSAGES, "boa12",
        "Mobile board");
    board_info[1] = new board_info_type(1111, 0, 0, LEVEL_GOD + 1, MAX_BOARD_MESSAGES, "boa11",
        "Object board");
    board_info[2] = new board_info_type(1110, 0, 0, LEVEL_GRGOD + 1, MAX_BOARD_MESSAGES, "objects",
        "Zone assignment Board");
    board_info[3] = new board_info_type(1109, 0, 0, LEVEL_GRGOD, MAX_BOARD_MESSAGES, "zones",
        "Whitie mortal Board");
    board_info[4] = new board_info_type(1108, 0, 0, LEVEL_GOD + 1, MAX_BOARD_MESSAGES, "mob",
        "Zone planning Board");
    board_info[5] = new board_info_type(1107, 0, 0, LEVEL_GOD + 1, MAX_BOARD_MESSAGES, "shaping",
        "Creepy Bug Board");
    board_info[6] = new board_info_type(1106, 0, 0, LEVEL_GOD + 1, MAX_BIG_BOARD_MESSAGES, "immort",
        "Immortal Board");
    board_info[7] = new board_info_type(1105, 0, 0, LEVEL_GRGOD, MAX_BOARD_MESSAGES, "dim",
        "Dim's Board");
    board_info[8] = new board_info_type(1104, 0, LEVEL_GOD + 1, LEVEL_GRGOD + 1, MAX_BIG_BOARD_MESSAGES, "general",
        "News Board");
    board_info[9] = new board_info_type(1119, 0, 0, LEVEL_GOD + 1, MAX_BOARD_MESSAGES, "boa13",
        "Uruk Board");
    board_info[10] = new board_info_type(1118, LEVEL_GRGOD, LEVEL_GRGOD, LEVEL_GRGOD + 1, MAX_BOARD_MESSAGES, "boa14",
        "Flame Board");
    board_info[11] = new board_info_type(1117, 0, 0, LEVEL_GOD + 1, MAX_BOARD_MESSAGES, "boa15",
        "New World board");
    board_info[12] = new board_info_type(1116, 0, 0, LEVEL_GOD + 1, MAX_BOARD_MESSAGES, "boa16",
        "East board");
    board_info[13] = new board_info_type(1114, 0, 0, LEVEL_GOD + 1, MAX_BOARD_MESSAGES, "boa17",
        "Bright ideas Board");
    board_info[14] = new board_info_type(1113, 0, 0, LEVEL_GRGOD, MAX_BOARD_MESSAGES, "boa18",
        "Prami Help Board");
    board_info[15] = new board_info_type(1127, 0, 0, LEVEL_GOD + 1, MAX_BOARD_MESSAGES, "lightic",
        "Light Races IC Board");
    board_info[16] = new board_info_type(1128, 0, 0, LEVEL_GOD + 1, MAX_BOARD_MESSAGES, "darkic",
        "Evil Races IC Board");
    board_info[17] = new board_info_type(1129, 0, 0, LEVEL_GRGOD, MAX_BOARD_MESSAGES, "seether",
        "Seether's Board");
    board_info[18] = new board_info_type(1130, 0, 0, LEVEL_GOD + 1, MAX_BOARD_MESSAGES, "orcboard",
        "Orcish bulletin board");
    board_info[19] = new board_info_type(1131, LEVEL_AREAGOD, LEVEL_AREAGOD, LEVEL_GRGOD + 1, MAX_BOARD_MESSAGES, "cheaters",
        "Cheater board");
    board_info[20] = new board_info_type(1132, 0, 0, LEVEL_GOD + 1, MAX_BOARD_MESSAGES, "magiboard",
        "Magus bulletin board");
    board_info[21] = new board_info_type(1133, 0, 0, LEVEL_GOD + 1, MAX_BOARD_MESSAGES, "magiic",
        "Magus IC bulletin board");
    board_info[22] = new board_info_type(1134, LEVEL_GOD, LEVEL_GOD, LEVEL_GRGOD, MAX_BOARD_MESSAGES, "coders",
        "Loman/Fingolfin Code Board");
    board_info[23] = new board_info_type(1135, LEVEL_GOD, LEVEL_GOD, LEVEL_GRGOD, MAX_BOARD_MESSAGES, "Alkar",
        "Alkar's board");
    mail_board = new mail_info_type(0, 0, 0, 0, MAX_MAIL_MESSAGES, "mail", "Mail Board");
    //   board_info[8] = mail_board;
    //   printf("passed mail_board creation\n");
    tmp_news = news_board_number;
    news_board_number = -1;
    for (i = 0; i < NUM_OF_BOARDS; i++) {
        if (G_VNUM(board_info[i]) == tmp_news)
            news_board_number = i;
    }
    news_board = board_info[news_board_number];
    for (i = 0; i < NUM_OF_BOARDS; i++)
        board_info[i]->is_changed = 0;
    mail_board->is_changed = 0;
    if (fatal_error)
        exit(0);
    // printf("end of ini_boards\n");
}

SPECIAL(gen_board)
{
    board_info_type* board;
    //  static int	loaded = 0;
    int tmp;
    char argb[MAX_INPUT_LENGTH];
    char* arg1;

    if (!ch->desc)
        return 0;

    if (!wtl || (callflag != SPECIAL_COMMAND))
        return 0;

    if ((wtl->cmd != CMD_WRITE) && (wtl->cmd != CMD_LOOK) && (wtl->cmd != CMD_READ) && (wtl->cmd != CMD_EXAMINE) && (wtl->cmd != CMD_REMOVE) && (wtl->cmd != CMD_NEXT) && (wtl->cmd != CMD_SEND) && (wtl->subcmd != SCMD_NEWS) && (wtl->subcmd != SCMD_MAIL))
        return 0;

    cmd = wtl->cmd;
    if (wtl->targ1.type == TARGET_TEXT)
        arg1 = wtl->targ1.ptr.text->text;
    else
        arg1 = arg;

    //  if (!loaded) {
    //    init_boards();
    //    loaded = 1;
    //  }
    while (*arg && (*arg <= ' '))
        arg++;

    for (tmp = 0; tmp < 29 && arg[tmp] > ' '; tmp++)
        argb[tmp] = arg[tmp];
    argb[tmp] = 0;
    arg1 = arg + tmp;

    if (wtl->subcmd == SCMD_MAIL) {
        board = mail_board;
        if (!*arg) {
            cmd = CMD_LOOK;
            if (strlen(arg) > 20)
                arg[20] = 0;
            sprintf(argb, "board %s\n", arg);
            arg1 = argb;
        } else if (isdigit(*arg)) {
            cmd = CMD_READ;
            arg1 = arg;
        } else if (!strncmp("all", argb, strlen(argb))) {
            cmd = CMD_LOOK;
            if (strlen(arg) > 20)
                arg[20] = 0;
            sprintf(argb, "board %s\n", arg);
            arg1 = argb;
        } else if (!strncmp("read", argb, strlen(argb))) {
            cmd = CMD_READ;
        } else if (!strncmp("write", argb, strlen(argb))) {
            cmd = CMD_WRITE;
        } else if (!strncmp("remove", argb, strlen(argb))) {
            cmd = CMD_REMOVE;
        }
    } else if ((wtl->subcmd == SCMD_NEWS) && (news_board_number >= 0)) {
        arg1 = arg;
        board = board_info[news_board_number];
        //    printf("news call, argb=%s.\n",argb);
        if (*argb && !strcmp(argb, "all")) {
            cmd = CMD_EXAMINE;
            sprintf(argb, "board");
            arg1 = argb;
        } else if (*argb)
            cmd = CMD_READ;
        else {
            cmd = CMD_LOOK;
            //      printf("news: argument is:%s.\n",arg);
            if (strlen(argb) > 20)
                argb[20] = 0;
            sprintf(argb, "board %s\n", arg);
            arg1 = argb;
        }
    } else if (cmd == CMD_SEND) {
        board = mail_board;
        cmd = CMD_WRITE;
        arg1 = arg;
    } else {
        arg1 = arg;
        //    cmd = CMD_LOOK;
        if ((board = find_board(ch)) == 0) {
            log("SYSERR:  degenerate board!  (what the hell..)");
            return 0;
        }
    }
    // printf("arg:%s, news_number:%d\n",arg, news_board_number);
    switch (cmd) {
    case CMD_WRITE:
        board->is_changed = 1;
        board->write_message(ch, arg1);
        return 1;
        break;
    case CMD_LOOK:
        if (board->is_changed) {
            board->flush_board();
            board->save_board();
            //      board->is_changed = 0;
        }
        return (board->show_board(ch, arg1, 0));
        break;
    case CMD_EXAMINE:
        if (board->is_changed)
            board->save_board();
        //    board->is_changed = 0;
        return (board->show_board(ch, arg1, 1));
        break;
    case CMD_NEXT:
        return (board->display_msg(ch, arg1, 1));
        break;
    case CMD_READ:
        return (board->display_msg(ch, arg1, 0));
        break;
    case CMD_REMOVE:
        tmp = board->remove_msg(ch, arg1);
        board->save_board();
        //    board->is_changed = 0;
        return tmp;
        break;
    default:
        send_to_char("Unrecognized option.\n\r", ch);
        return 0;
        break;
    }
}

void board_info_type::write_message(struct char_data* ch, char* arg, int num)
{
    char* tmstr;
    int len;
    long ct;
    char buf[MAX_INPUT_LENGTH], buf2[MAX_INPUT_LENGTH];

    if (GET_LEVEL(ch) < WRITE_LVL) {
        send_to_char("You are not holy enough to write on this board.\n\r", ch);
        return;
    }
    if (num_of_msgs >= max_of_msgs) {
        send_to_char("The board is full.\n\r", ch);
        return;
    }

    if ((NEW_MSG_INDEX.slot_num = find_slot()) == -1) {
        send_to_char("The board is malfunctioning - sorry.\n\r", ch);
        log("SYSERR: Board: failed to find empty slot on write.");
        return;
    }

    /* skip blanks */
    for (; isspace(*arg); arg++)
        ;

    if (!*arg) {
        send_to_char("We must have a headline!\n\r", ch);
        return;
    }

    if ((strlen(arg) >= 3) && (!strncmp(arg, "IMM", 3)) && (GET_LEVEL(ch) < LEVEL_IMMORT)) {
        arg[1] = 'm';
        arg[2] = 'm';
    }
    ct = time(0);
    tmstr = (char*)asctime(localtime(&ct));
    *(tmstr + strlen(tmstr) - 1) = '\0';

    sprintf(buf2, "(%s)", GET_NAME(ch));
    sprintf(buf, "%6.10s %-12s :: %s", tmstr, buf2, arg);
    len = strlen(buf) + 1;
    CREATE((NEW_MSG_INDEX.heading), char, len);
    if (!(NEW_MSG_INDEX.heading)) {
        send_to_char("The board is malfunctioning - sorry.\n\r", ch);
        return;
    }
    strcpy(NEW_MSG_INDEX.heading, buf);
    NEW_MSG_INDEX.heading[len - 1] = '\0';
    NEW_MSG_INDEX.level = GET_LEVEL(ch);

    act("$n starts to write a message.", TRUE, ch, 0, 0, TO_ROOM);
    msg_storage[NEW_MSG_INDEX.slot_num] = 0;
    string_add_init(ch->desc, &(msg_storage[NEW_MSG_INDEX.slot_num]));

    if (num > 0)
        NEW_MSG_INDEX.msg_num = num;
    else {
        if (last_message) {
            last_message++;
            NEW_MSG_INDEX.msg_num = last_message;
        } else
            NEW_MSG_INDEX.msg_num = last_message = 1;
    }
    NEW_MSG_INDEX.post_time = time(0);
    num_of_msgs++;
}

int board_info_type::approve_msg(char_data* ch, board_msginfo* msg, int cur_num, int* num)
{
    int len;
    *num = msg->msg_num;
    len = strlen(msg->heading);
    if (tmp_allflag || (!tmp_allflag && (*num > cur_num))) {
        if ((len < 30) || (strncmp(msg->heading + 27, "IMM", 3)) || (GET_LEVEL(ch) >= LEVEL_IMMORT))
            return 1;
        else
            return 0;
    } else
        return 0;
}
int board_info_type::count_msg(char_data* ch, int cur_num)
{
    int tmp, tmp2, count;

    tmp2 = 0;
    for (tmp = 0, count = 0; tmp < num_of_msgs; tmp++) {
        if (approve_msg(ch, msg_index + tmp, cur_num, &tmp2))
            count++;
    }

    return count;
}

int board_info_type::show_board(struct char_data* ch,
    char* arg, int allflag)
{
    int i, cur, show_num, count, chng_mark;
    char tmp[MAX_STRING_LENGTH], buf[MAX_STRING_LENGTH];
    char* arg1;
    descriptor_data* d;

    // printf("going to show board\n");
    if (!ch->desc)
        return 0;
    arg1 = one_argument(arg, tmp);
    while (*arg1 && (*arg1 <= ' '))
        arg1++;

    if (!*tmp || !isname(tmp, "board bulletin"))
        return 0;
    if (*arg1 && !strcmp("all", arg1))
        allflag = 1;
    if (GET_LEVEL(ch) < READ_LVL) {
        send_to_char("You try but fail to understand the holy words.\n\r", ch);
        return 1;
    }
    tmp_allflag = allflag;
    if (allflag)
        cur = -1;
    else
        cur = MSG_CURMSG(ch);
    show_num = 0;
    count = count_msg(ch, cur);
    //   act("$n studies the board.", TRUE, ch, 0, 0, TO_ROOM);
    if ((RNUM > 0) && (obj_proto[RNUM].action_description))
        strcpy(buf, obj_proto[RNUM].action_description);
    else
        strcpy(buf, "");
    if (!num_of_msgs)
        strcat(buf, "The board is empty.\n\r");
    else if (!count)
        strcat(buf, "There is no unread messages.\n\r");
    else {
        sprintf(buf + strlen(buf), "There are %d %smessages.\n\r",
            count, (allflag) ? ("") : ("unread "));
        chng_mark = 0;
        for (i = 0; i < num_of_msgs; i++) {
            for (d = descriptor_list; d; d = d->next)
                chng_mark |= (!d->connected && d->str == &(msg_storage[MSG_SLOTNUM(i)]));

            if (approve_msg(ch, msg_index + i, cur, &show_num))
                if (MSG_HEADING(i))
                    sprintf(buf + strlen(buf), "%4d : %s\n\r", show_num, MSG_HEADING(i));
                else {
                    log("SYSERR: The board is fubar'd.");
                    send_to_char("Sorry, the board isn't working.\n\r", ch);
                    return 1;
                }
        }
        is_changed = chng_mark;
    }
    page_string(ch->desc, buf, 1);
    return 1;
}

int board_info_type::select_msg(int msg, int softflag)
{
    int tmp;
    if (msg <= 0)
        msg = msg_msgnum(0);
    for (tmp = 0; tmp < num_of_msgs; tmp++) {
        if (!softflag) {
            if (msg_msgnum(tmp) == msg)
                break;
        } else if (msg_msgnum(tmp) >= msg)
            break;
    }
    if (tmp != num_of_msgs)
        msg = tmp + 1;
    else
        return (-1);
    return msg;
}
int board_info_type::display_msg(struct char_data* ch,
    char* arg, int nextflag)
{
    char number[MAX_STRING_LENGTH], buffer[MAX_STRING_LENGTH];
    int msg, ind, tmp, show_num, looseflag;

    while (*arg && (*arg <= ' '))
        arg++;
    for (tmp = 0; *(arg + tmp) > ' '; tmp++)
        ;

    if (*arg && !strncmp("next", arg, tmp)) {
        nextflag = 1;
        while (*arg && (*arg > ' '))
            arg++;
    } else if (*arg && !strncmp("last", arg, tmp)) {
        nextflag = 2;
        while (*arg && (*arg > ' '))
            arg++;
    } else if ((!*arg && !nextflag) || (*arg && !isdigit(*arg))) {
        send_to_char("Read what?\n\r", ch);
        return 1;
    }
    //   printf("read board, arg is:%s.\n",arg);
    //   if(!strncmp("next",arg,strlen(arg))){
    //   nextflag = 1;
    if (nextflag) {
        if (!*arg) {
            msg = MSG_CURMSG(ch);
            if (nextflag == 2)
                msg--; //"read last", to get the same message again.
            looseflag = 1;
        } else {
            msg = atoi(arg);
            looseflag = 0;
        }
    } else {
        looseflag = 0;
        one_argument(arg, number);
        if (!*number || !isdigit(*number))
            return 0;
        if ((msg = atoi(number)) < 0)
            return 0;
    }
    if (GET_LEVEL(ch) < READ_LVL) {
        send_to_char("You try but fail to understand the holy words.\n\r", ch);
        return 1;
    }

    if (!num_of_msgs) {
        send_to_char("The board is empty!\n\r", ch);
        return (1);
    }

    //   msg = select_msg(msg,(nextflag)?1:0);
    for (tmp = 0, show_num = 0; tmp < num_of_msgs; tmp++)
        if (approve_msg(ch, msg_index + tmp, 0, &show_num))
            if ((!looseflag && show_num == msg) || (looseflag && show_num > msg))
                break;
    if (tmp == num_of_msgs)
        msg = -1;
    else
        msg = tmp;

    /*   if(nextflag){
   for(tmp++ ; tmp <num_of_msgs; tmp++)
     if(approve_msg(ch, msg_index + tmp, 0, &show_num)) break;
   if(tmp == num_of_msgs) msg = -1;
   else msg = tmp;
   }
   */
    if (msg < 0) {
        send_to_char("No such message.\n\r",
            ch);
        return (-1);
    }
    if (nextflag) {
        if (msg >= num_of_msgs) {
            send_to_char("No next message.\n\r", ch);
            return 1;
        } else {
            //       msg = msg + 1;
            //       if(msg >1)
            //	 MSG_CURMSG(ch, board_type) = MSG_MSGNUM(board_type, msg - 1);
        }
    }
    if (msg < 0 || msg >= num_of_msgs) {
        send_to_char("weird message number. Aborted.\n\r", ch);
        return 1;
    }
    //   if(!nextflag) MSG_CURMSG(ch, board_type) = msg;
    if (msg >= 0 && nextflag)
        MSG_CURMSG(ch) = show_num;
    ind = msg; // - 1;
    if (MSG_SLOTNUM(ind) < 0 || MSG_SLOTNUM(ind) >= INDEX_SIZE) {
        send_to_char("Sorry, the board is not working.\n\r", ch);
        log("SYSERR: Board is screwed up.");
        return 1;
    }

    if (!(MSG_HEADING(ind))) {
        send_to_char("That message appears to be screwed up.\n\r", ch);
        return 1;
    }

    if (!(msg_storage[MSG_SLOTNUM(ind)])) {
        send_to_char("That message seems to be empty.\n\r", ch);
        return 1;
    }

    sprintf(buffer, "Message %4d : %s\n\r", show_num,
        MSG_HEADING(ind));
    send_to_char(buffer, ch);
    page_string(ch->desc, msg_storage[MSG_SLOTNUM(ind)], 1);
    send_to_char("\n\r", ch);

    return 1;
}

int board_info_type::remove_msg(struct char_data* ch, char* arg)
{
    int ind, msg, slot_num, tmp, show_num, remflag;
    char number[MAX_INPUT_LENGTH], buf[MAX_INPUT_LENGTH];
    char* tmpptr;
    struct descriptor_data* d;

    one_argument(arg, number);

    if (!*number || !isdigit(*number))
        return 0;
    if (!(msg = atoi(number)))
        return (0);

    if (!num_of_msgs) {
        send_to_char("The board is empty!\n\r", ch);
        return 1;
    }
    for (tmp = 0, show_num = 0; tmp < num_of_msgs; tmp++)
        if (approve_msg(ch, msg_index + tmp, 0, &show_num))
            if (show_num == msg)
                break;
    if (tmp == num_of_msgs)
        msg = -1;
    else
        msg = tmp;
    // printf("remove_msg, msg=%d\n",msg);
    if (msg < 0 || msg >= num_of_msgs) {
        send_to_char("That message exists only in your imagination..\n\r", ch);
        return 1;
    }
    ind = msg;
    if (!MSG_HEADING(ind)) {
        send_to_char("That message appears to be screwed up.\n\r", ch);
        return 1;
    }

    //   sprintf(buf, "(%s)", GET_NAME(ch));

    slot_num = MSG_SLOTNUM(ind);
    if (slot_num < 0 || slot_num >= INDEX_SIZE) {
        log("SYSERR: The board is seriously screwed up.");
        send_to_char("That message is majorly screwed up.\n\r", ch);
        return 1;
    }

    sprintf(buf, "(%s)", GET_NAME(ch));
    if (GET_LEVEL(ch) < LEVEL_AREAGOD && !(strstr(MSG_HEADING(ind), buf))) {
        send_to_char("You cannot remove that message.\n\r", ch);
        return 1;
    }

    for (d = descriptor_list; d; d = d->next)
        if (!d->connected && d->str == &(msg_storage[slot_num])) {
            send_to_char("At least wait until the author is finished before removING(it!\n\r", ch);
            return 1;
        }
    tmpptr = msg_storage[slot_num];
    RELEASE(msg_storage[slot_num]);
    msg_storage[MSG_SLOTNUM(ind)] = 0;
    msg_storage_taken[MSG_SLOTNUM(ind)] = 0;

    tmp = 0;
    for (ind = 0; ind < num_of_msgs; ind++) {
        remflag = 0;
        if (MSG_SLOTNUM(ind) == slot_num) {
            //     printf("removing msg, ind=%d, tmp=%d\n",ind,tmp);
            remflag = 1;
            RELEASE(MSG_HEADING(ind));
            tmp++;
            num_of_msgs--;
        }
        MSG_HEADING(ind) = MSG_HEADING(ind + tmp);
        MSG_SLOTNUM(ind) = MSG_SLOTNUM(ind + tmp);
        msg_msgnum(ind, msg_msgnum(ind + tmp));
        MSG_LEVEL(ind) = MSG_LEVEL(ind + tmp);

        if (remflag)
            ind--;
    }
    send_to_char("Message removed.\n\r", ch);
    //   sprintf(buf, "$n just removed some message.");
    //   act(buf, TRUE, ch, 0, 0, TO_ROOM);
    //   save_board();

    return 1;
}

void board_info_type::flush_board()
{
    char_data dummy;
    char str[100];
    int i, shn;

    dummy.desc = 0;
    dummy.player.name = "Auto";
    dummy.specials.invis_level = LEVEL_IMPL;
    dummy.specials2.idnum = -1;
    GET_LEVEL(&dummy) = LEVEL_IMPL;

    for (i = 0, shn = 0; i < num_of_msgs; i++) {
        approve_msg(&dummy, msg_index + i, 0, &shn);
        if (!(msg_storage[MSG_SLOTNUM(i)])) {
            sprintf(str, "%d", shn);
            //      printf("going to remove msg %s\n",str);
            remove_msg(&dummy, str);
        }
    }
}

// ---------------------------------------------------------------------------
// Phase 2a Task 4: board persistence as JSON, plus a one-time legacy
// converter. See boards.h for the schema/contract doc comments.
// ---------------------------------------------------------------------------
namespace boards_json {
namespace {

    void set_error(std::string* error_message, const std::string& message)
    {
        if (error_message)
            *error_message = message;
    }

    // Little-endian 4-byte int read at an explicit offset -- portable
    // regardless of the reading process's own endianness/ABI (this repo's
    // established convention, see objects_json.cpp's read_pod/read_u32le).
    bool read_i32(const std::string& bytes, size_t* offset, int* value, std::string* error_message, const char* label)
    {
        if (*offset + 4 > bytes.size()) {
            set_error(error_message, std::string("Truncated board file while reading ") + label + ".");
            return false;
        }

        const uint32_t raw = static_cast<uint32_t>(static_cast<unsigned char>(bytes[*offset]))
            | (static_cast<uint32_t>(static_cast<unsigned char>(bytes[*offset + 1])) << 8)
            | (static_cast<uint32_t>(static_cast<unsigned char>(bytes[*offset + 2])) << 16)
            | (static_cast<uint32_t>(static_cast<unsigned char>(bytes[*offset + 3])) << 24);
        *value = static_cast<int>(static_cast<int32_t>(raw));
        *offset += 4;
        return true;
    }

    bool skip_bytes(const std::string& bytes, size_t* offset, size_t length, std::string* error_message, const char* label)
    {
        if (*offset + length > bytes.size()) {
            set_error(error_message, std::string("Truncated board file while reading ") + label + ".");
            return false;
        }
        *offset += length;
        return true;
    }

    // Reads `length_including_nul` bytes of text starting at `*offset` and
    // strips the trailing NUL byte the legacy writer always included (it
    // wrote strlen(text)+1 bytes -- see save_board's heading_len/message_len
    // computation below).
    bool read_text(const std::string& bytes, size_t* offset, size_t length_including_nul, std::string* out, std::string* error_message, const char* label)
    {
        if (*offset + length_including_nul > bytes.size()) {
            set_error(error_message, std::string("Truncated board file while reading ") + label + ".");
            return false;
        }
        if (length_including_nul == 0) {
            out->clear();
            return true;
        }
        out->assign(bytes, *offset, length_including_nul - 1);
        *offset += length_including_nul;
        return true;
    }

    // Decodes one 28-byte `board_msginfo` record (slot_num, msg_num, the
    // `char*` heading pointer -- read past and discarded, level, post_time,
    // heading_len, message_len) plus its adjacent heading/message text.
    bool read_legacy_record(const std::string& bytes, size_t* offset, BoardMessageData* message, std::string* error_message)
    {
        int slot_num = 0, msg_num = 0, level = 0, post_time = 0, heading_len = 0, message_len = 0;
        if (!read_i32(bytes, offset, &slot_num, error_message, "message slot_num"))
            return false;
        if (!read_i32(bytes, offset, &msg_num, error_message, "message msg_num"))
            return false;
        if (!skip_bytes(bytes, offset, 4, error_message, "message heading pointer"))
            return false;
        if (!read_i32(bytes, offset, &level, error_message, "message level"))
            return false;
        if (!read_i32(bytes, offset, &post_time, error_message, "message post_time"))
            return false;
        if (!read_i32(bytes, offset, &heading_len, error_message, "message heading_len"))
            return false;
        if (!read_i32(bytes, offset, &message_len, error_message, "message message_len"))
            return false;

        if (heading_len < 1) {
            set_error(error_message, "Board file corrupt: message heading_len must be >= 1.");
            return false;
        }
        if (message_len < 0) {
            set_error(error_message, "Board file corrupt: message message_len must be >= 0.");
            return false;
        }

        std::string heading;
        if (!read_text(bytes, offset, static_cast<size_t>(heading_len), &heading, error_message, "message heading"))
            return false;

        const bool has_message = message_len > 0;
        std::string text;
        if (has_message && !read_text(bytes, offset, static_cast<size_t>(message_len), &text, error_message, "message body"))
            return false;

        message->slot_num = slot_num;
        message->msg_num = msg_num;
        message->level = level;
        message->post_time = post_time;
        message->heading = std::move(heading);
        message->has_message = has_message;
        message->message = has_message ? std::move(text) : std::string();
        return true;
    }

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

    // Temp-file + rename atomic write, matching write_player_objects_json's
    // pattern in objsave.cpp.
    bool write_file_contents_atomically(const std::string& path, const std::string& contents, std::string* error_message)
    {
        const std::string temp_path = path + ".tmp";

        FILE* temp_file = std::fopen(temp_path.c_str(), "wb");
        if (temp_file == nullptr) {
            set_error(error_message, std::string("Unable to open temporary board file '") + temp_path + "': " + std::strerror(errno));
            return false;
        }

        const size_t bytes_written = contents.empty() ? 0 : std::fwrite(contents.data(), sizeof(char), contents.size(), temp_file);
        const int flush_result = std::fflush(temp_file);
        const int close_result = std::fclose(temp_file);

        if (bytes_written != contents.size() || flush_result != 0 || close_result != 0) {
            std::remove(temp_path.c_str());
            set_error(error_message, std::string("Failed to write temporary board file '") + temp_path + "'.");
            return false;
        }

        if (rots_rename_replace(temp_path.c_str(), path.c_str()) != 0) {
            const std::string rename_error = std::strerror(errno);
            std::remove(temp_path.c_str());
            set_error(error_message, "Failed to move temporary board file into place: " + rename_error);
            return false;
        }

        return true;
    }

    // Defensive consistency check for deserialized JSON: `has_message` and
    // `message` must agree (a hand-edited or corrupted JSON file could set
    // them inconsistently, which apply_board_save_data below has no sane way
    // to interpret).
    bool message_len_invariant_holds(const BoardSaveData& data)
    {
        for (const BoardMessageData& message : data.messages) {
            if (!message.has_message && !message.message.empty())
                return false;
        }
        return true;
    }

} // namespace

bool legacy_board_file_from_binary(const std::string& bytes, BoardSaveData* data, std::string* error_message)
{
    if (data == nullptr) {
        set_error(error_message, "Board data output parameter must not be null.");
        return false;
    }

    size_t offset = 0;
    int num_of_msgs = 0, last_message = 0;
    if (!read_i32(bytes, &offset, &num_of_msgs, error_message, "num_of_msgs"))
        return false;
    if (!read_i32(bytes, &offset, &last_message, error_message, "last_message"))
        return false;

    if (num_of_msgs < 1) {
        set_error(error_message, "Board file corrupt: num_of_msgs must be >= 1.");
        return false;
    }

    BoardSaveData parsed;
    parsed.last_message = last_message;
    parsed.messages.reserve(static_cast<size_t>(num_of_msgs));
    for (int i = 0; i < num_of_msgs; ++i) {
        BoardMessageData message;
        if (!read_legacy_record(bytes, &offset, &message, error_message))
            return false;
        parsed.messages.push_back(std::move(message));
    }

    // Deliberate post-legacy hardening (Phase 2a Task 4 review finding): the
    // original loader trusted num_of_msgs and never checked for leftover
    // bytes after the last record, so a truncated/appended-to legacy file
    // could silently under- or over-read. This decoder is stricter and
    // rejects any trailing bytes outright. Verified harmless against all 25
    // real legacy .boa files on disk at the time this check was added (none
    // had trailing bytes) -- this is a one-time migration-path guard, not a
    // behavior change to the live JSON format.
    if (offset != bytes.size()) {
        set_error(error_message, "Board file corrupt: trailing bytes after the last message record.");
        return false;
    }

    *data = std::move(parsed);
    set_error(error_message, "");
    return true;
}

std::string serialize_board_to_json(const BoardSaveData& data)
{
    std::ostringstream output;
    output << "{\n";
    output << "  \"version\": " << data.version << ",\n";
    output << "  \"last_message\": " << data.last_message << ",\n";
    output << "  \"messages\": [\n";
    for (size_t index = 0; index < data.messages.size(); ++index) {
        const BoardMessageData& message = data.messages[index];
        output << "    {\n";
        output << "      \"slot_num\": " << message.slot_num << ",\n";
        output << "      \"msg_num\": " << message.msg_num << ",\n";
        output << "      \"heading\": \"" << json_utils::escape_json_string(message.heading) << "\",\n";
        output << "      \"level\": " << message.level << ",\n";
        output << "      \"post_time\": " << message.post_time << ",\n";
        output << "      \"has_message\": " << (message.has_message ? "true" : "false") << ",\n";
        output << "      \"message\": \"" << json_utils::escape_json_string(message.message) << "\"\n";
        output << "    }";
        if (index + 1 < data.messages.size())
            output << ",";
        output << "\n";
    }
    output << "  ]\n";
    output << "}\n";
    return output.str();
}

bool deserialize_board_from_json(const std::string& json, BoardSaveData* data, std::string* error_message)
{
    if (data == nullptr) {
        set_error(error_message, "Board data output parameter must not be null.");
        return false;
    }

    BoardSaveData parsed;
    const bool ok = json_utils::JsonReader(json).parse_root_object(
        [&](const std::string& key, json_utils::JsonReader* reader, std::string* nested_error) {
            if (key == "version")
                return reader->parse_integer(&parsed.version, nested_error);
            if (key == "last_message")
                return reader->parse_integer(&parsed.last_message, nested_error);
            if (key == "messages") {
                return reader->parse_array(
                    [&](json_utils::JsonReader* message_reader, std::string* message_error) {
                        BoardMessageData message;
                        const bool message_ok = message_reader->parse_object(
                            [&](const std::string& message_key, json_utils::JsonReader* nested_reader, std::string* nested_message_error) {
                                if (message_key == "slot_num")
                                    return nested_reader->parse_integer(&message.slot_num, nested_message_error);
                                if (message_key == "msg_num")
                                    return nested_reader->parse_integer(&message.msg_num, nested_message_error);
                                if (message_key == "heading")
                                    return nested_reader->parse_string(&message.heading, nested_message_error);
                                if (message_key == "level")
                                    return nested_reader->parse_integer(&message.level, nested_message_error);
                                if (message_key == "post_time")
                                    return nested_reader->parse_integer(&message.post_time, nested_message_error);
                                if (message_key == "has_message")
                                    return nested_reader->parse_bool(&message.has_message, nested_message_error);
                                if (message_key == "message")
                                    return nested_reader->parse_string(&message.message, nested_message_error);
                                return nested_reader->skip_value(nested_message_error);
                            },
                            message_error);
                        if (!message_ok)
                            return false;
                        parsed.messages.push_back(std::move(message));
                        return true;
                    },
                    nested_error);
            }
            return reader->skip_value(nested_error);
        },
        error_message);

    if (!ok)
        return false;

    if (!message_len_invariant_holds(parsed)) {
        set_error(error_message, "Board JSON message is inconsistent: has_message=false but message is non-empty.");
        return false;
    }

    *data = std::move(parsed);
    set_error(error_message, "");
    return true;
}

bool board_save_data_equal(const BoardSaveData& a, const BoardSaveData& b)
{
    if (a.version != b.version || a.last_message != b.last_message)
        return false;
    if (a.messages.size() != b.messages.size())
        return false;

    for (size_t index = 0; index < a.messages.size(); ++index) {
        const BoardMessageData& m1 = a.messages[index];
        const BoardMessageData& m2 = b.messages[index];
        if (m1.slot_num != m2.slot_num || m1.msg_num != m2.msg_num || m1.level != m2.level
            || m1.post_time != m2.post_time || m1.heading != m2.heading
            || m1.has_message != m2.has_message || m1.message != m2.message)
            return false;
    }

    return true;
}

std::string board_json_path(const std::string& legacy_path)
{
    return legacy_path + ".json";
}

bool convert_legacy_board_file(const char* legacy_path, std::string* error_message)
{
    if (legacy_path == nullptr || !*legacy_path) {
        set_error(error_message, "Legacy board path must not be empty.");
        return false;
    }

    std::string legacy_bytes;
    if (!read_binary_file_contents(legacy_path, &legacy_bytes)) {
        set_error(error_message, std::string("Failed to read legacy board file '") + legacy_path + "': " + std::strerror(errno));
        return false;
    }

    BoardSaveData decoded;
    std::string decode_error;
    if (!legacy_board_file_from_binary(legacy_bytes, &decoded, &decode_error)) {
        set_error(error_message, "Decode failed: " + decode_error);
        return false;
    }

    const std::string json = serialize_board_to_json(decoded);

    // Verify (binding conversion contract): re-decode the freshly serialized
    // JSON and compare it field-for-field to the original decode -- not a
    // re-serialization/string comparison.
    BoardSaveData reparsed;
    std::string verify_error;
    if (!deserialize_board_from_json(json, &reparsed, &verify_error)) {
        set_error(error_message, "Verify-decode of freshly serialized JSON failed: " + verify_error);
        return false;
    }

    if (!board_save_data_equal(decoded, reparsed)) {
        set_error(error_message, "Verify mismatch: re-decoded JSON does not equal the original legacy decode.");
        return false;
    }

    const std::string json_path = board_json_path(legacy_path);
    std::string write_error;
    if (!write_file_contents_atomically(json_path, json, &write_error)) {
        set_error(error_message, write_error);
        return false;
    }

    const std::string migrated_path = std::string(legacy_path) + ".migrated";
    if (rots_rename_replace(legacy_path, migrated_path.c_str()) != 0) {
        // The JSON is written and verified at this point -- data is not at
        // risk -- but the legacy file could not be retired. Matches
        // convert_plrobjs.cpp's "partial success" contract: report it (via
        // a non-empty, non-fatal error_message) but still return true, since
        // the thing that matters for subsequent loads (the JSON file) is
        // safely in place; the legacy file is simply left behind for a
        // future retry.
        set_error(error_message,
            std::string("Board converted but legacy rename to '") + migrated_path + "' failed: " + std::strerror(errno));
        return true;
    }

    set_error(error_message, "");
    return true;
}

} // namespace boards_json

static char html_message_line[MAX_STRING_LENGTH + 200];
void board_info_type::save_board()
{
    FILE *ht_fl, *ind_fl;
    int i, j1, j2, no_html;
    char *tmp1 = 0, *tmp2 = 0;
    char ht_name[100];

    const std::string legacy_path = FILENAME;
    const std::string json_path = boards_json::board_json_path(legacy_path);

    if (!num_of_msgs) {
        unlink(legacy_path.c_str());
        unlink(json_path.c_str());
        return;
    }

    no_html = 0;

    sprintf(ht_name, "%s/%s.index", BOARD_HTML_DIR, short_name);

    if (!(ind_fl = fopen(ht_name, "w+")))
        no_html = 1;

    sprintf(ht_name, "%s/%s.html", BOARD_HTML_DIR, short_name);
    if (!(ht_fl = fopen(ht_name, "w+"))) {
        no_html = 1;
        fclose(ind_fl);
    }

    if (!no_html) {
        fprintf(ind_fl, "<HTML>\n\r<BODY>\n\r<head>\n\r<title>%s</title></head><br>\n\r",
            title);
        fprintf(ht_fl, "<HTML>\n\r<BODY>\n\r<head>\n\r<title>%s</title></head><br>\n\r<dt>%s</dt><br><hr><br>",
            title, title);
    }

    boards_json::BoardSaveData data;
    data.last_message = last_message;
    data.messages.reserve(static_cast<size_t>(num_of_msgs));

    for (i = 0; i < num_of_msgs; i++) {
        if ((tmp1 = MSG_HEADING(i)))
            msg_index[i].heading_len = strlen(tmp1) + 1;
        else
            msg_index[i].heading_len = 0;

        if (MSG_SLOTNUM(i) < 0 || MSG_SLOTNUM(i) >= INDEX_SIZE || (!(tmp2 = msg_storage[MSG_SLOTNUM(i)])))
            msg_index[i].message_len = 0;
        else
            msg_index[i].message_len = strlen(tmp2) + 1;

        boards_json::BoardMessageData message;
        message.slot_num = msg_index[i].slot_num;
        message.msg_num = msg_index[i].msg_num;
        message.heading = tmp1 ? std::string(tmp1) : std::string();
        message.level = msg_index[i].level;
        message.post_time = msg_index[i].post_time;
        message.has_message = (tmp2 != 0);
        message.message = tmp2 ? std::string(tmp2) : std::string();
        data.messages.push_back(std::move(message));

        if (!no_html) {

            j1 = j2 = 0;
            if (tmp2) {
                for (j1 = 0, j2 = 0; j1 < msg_index[i].message_len; j1++) {
                    if (tmp2[j1] == '\r')
                        continue;
                    if (tmp2[j1] == '\n') {
                        html_message_line[j2++] = '<';
                        html_message_line[j2++] = 'b';
                        html_message_line[j2++] = 'r';
                        html_message_line[j2++] = '>';
                    } else
                        html_message_line[j2++] = tmp2[j1];
                }
            }
            html_message_line[j2] = 0;

            fprintf(ind_fl, "<A HREF=\"%s.html#Message%d\">Message %3d, %s</a><br>\n\r",
                short_name, msg_index[i].msg_num, msg_index[i].msg_num,
                (tmp1) ? tmp1 : "No title");
            fprintf(ht_fl, "<b><u><A NAME=\"Message%d\">Message %3d, %s</A></u></b><BR>", msg_index[i].msg_num, msg_index[i].msg_num,
                (tmp1) ? tmp1 : "No title");

            if (j2)
                fwrite(html_message_line, sizeof(char), j2, ht_fl);
            fprintf(ht_fl, "<br><br>");
        }
    }

    if (!no_html) {
        fclose(ind_fl);
        fclose(ht_fl);
    }

    const std::string json = boards_json::serialize_board_to_json(data);
    std::string write_error;
    if (!boards_json::write_file_contents_atomically(json_path, json, &write_error)) {
        char errbuf[512];
        snprintf(errbuf, sizeof(errbuf), "SYSERR: failed to write board JSON file '%s': %s", json_path.c_str(), write_error.c_str());
        log(errbuf);
    }

    // The legacy binary format is never written again -- JSON is the sole
    // write format going forward. A stale legacy file (e.g. one that hasn't
    // been converted yet because this board was never loaded this boot) is
    // left untouched here; load_board's boot-time converter is what retires
    // it.
}

namespace {

    bool read_whole_file(const char* path, std::string* out)
    {
        FILE* file = fopen(path, "rb");
        if (!file)
            return false;

        std::string bytes;
        char buffer[4096];
        size_t bytes_read;
        while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0)
            bytes.append(buffer, bytes_read);
        fclose(file);
        *out = std::move(bytes);
        return true;
    }

    // Applies decoded/deserialized board data into `board`'s runtime state
    // (the msg_index array plus the shared msg_storage/find_slot() pool),
    // mirroring the legacy load loop's semantics exactly -- including the
    // historical quirk where a message with no body (has_message == false)
    // keeps whatever slot_num was on disk instead of getting a freshly
    // allocated slot (the legacy loader's own `if ((len2 =
    // msg_index[i].message_len))` gate only assigns via find_slot() when a
    // body is present).
    bool apply_board_save_data(board_info_type* board, const boards_json::BoardSaveData& data)
    {
        const int num_of_msgs = static_cast<int>(data.messages.size());
        if (num_of_msgs < 1 || num_of_msgs > board->max_of_msgs) {
            log("SYSERR: Board file corrupt (load).  Resetting.");
            board->reset_board();
            return false;
        }

        board->last_message = data.last_message;

        for (int i = 0; i < num_of_msgs; ++i) {
            const boards_json::BoardMessageData& message = data.messages[i];

            board->msg_index[i].slot_num = message.slot_num;
            board->msg_index[i].msg_num = message.msg_num;
            board->msg_index[i].level = message.level;
            board->msg_index[i].post_time = message.post_time;

            char* heading = 0;
            CREATE(heading, char, message.heading.size() + 1);
            memcpy(heading, message.heading.c_str(), message.heading.size() + 1);
            board->msg_index[i].heading = heading;
            board->msg_index[i].heading_len = static_cast<int>(message.heading.size()) + 1;

            if (message.has_message) {
                const int slot = find_slot();
                if (slot == -1) {
                    log("SYSERR: Out of slots booting board!  Resetting..");
                    board->reset_board();
                    return false;
                }
                board->msg_index[i].slot_num = slot;
                board->msg_index[i].message_len = static_cast<int>(message.message.size()) + 1;

                char* text = 0;
                CREATE(text, char, message.message.size() + 1);
                memcpy(text, message.message.c_str(), message.message.size() + 1);
                msg_storage[slot] = text;
            } else {
                board->msg_index[i].message_len = 0;
            }
        }

        board->num_of_msgs = num_of_msgs;
        return true;
    }

} // namespace

void board_info_type::load_board()
{
    const std::string legacy_path = FILENAME;
    const std::string json_path = boards_json::board_json_path(legacy_path);

    std::string json_bytes;
    if (read_whole_file(json_path.c_str(), &json_bytes)) {
        boards_json::BoardSaveData data;
        std::string error;
        if (!boards_json::deserialize_board_from_json(json_bytes, &data, &error)) {
            char errbuf[512];
            snprintf(errbuf, sizeof(errbuf), "SYSERR: Board JSON file '%s' corrupt (load): %s  Resetting.", json_path.c_str(), error.c_str());
            log(errbuf);
            reset_board();
            return;
        }
        apply_board_save_data(this, data);
        return;
    }

    // No JSON file yet -- if a legacy binary file exists, convert it once.
    FILE* legacy_probe = fopen(legacy_path.c_str(), "rb");
    if (legacy_probe) {
        fclose(legacy_probe);

        std::string convert_error;
        if (!boards_json::convert_legacy_board_file(legacy_path.c_str(), &convert_error)) {
            char errbuf[512];
            snprintf(errbuf, sizeof(errbuf), "SYSERR: Failed converting legacy board file '%s' to JSON: %s", legacy_path.c_str(), convert_error.c_str());
            log(errbuf);
            reset_board();
            return;
        }

        char logbuf[512];
        if (!convert_error.empty())
            snprintf(logbuf, sizeof(logbuf), "Converted legacy board file '%s' to JSON (warning: %s).", legacy_path.c_str(), convert_error.c_str());
        else
            snprintf(logbuf, sizeof(logbuf), "Converted legacy board file '%s' to JSON.", legacy_path.c_str());
        log(logbuf);

        if (!read_whole_file(json_path.c_str(), &json_bytes)) {
            log("SYSERR: Board JSON file missing immediately after conversion.  Resetting.");
            reset_board();
            return;
        }

        boards_json::BoardSaveData data;
        std::string error;
        if (!boards_json::deserialize_board_from_json(json_bytes, &data, &error)) {
            char errbuf[512];
            snprintf(errbuf, sizeof(errbuf), "SYSERR: Board JSON file '%s' corrupt immediately after conversion: %s  Resetting.", json_path.c_str(), error.c_str());
            log(errbuf);
            reset_board();
            return;
        }

        apply_board_save_data(this, data);
        return;
    }

    // Neither a JSON file nor a legacy file exists -- a fresh board with no
    // messages posted yet. Matches the legacy loader's own silent-no-op here.
    perror("Error reading board");
}

void board_info_type::reset_board()
{
    int i;
    // printf("Entering board reset.\n");
    for (i = 0; i < max_of_msgs; i++) {
        if (MSG_HEADING(i)) {
            printf("Trying to remove message #%d.\n", i);
            RELEASE(MSG_HEADING(i));
        }
        if (msg_storage[MSG_SLOTNUM(i)])
            RELEASE(msg_storage[MSG_SLOTNUM(i)]);
        msg_storage_taken[MSG_SLOTNUM(i)] = 0;
        memset(&(msg_index[i]), '\0', sizeof(struct board_msginfo));
        msg_index[i].slot_num = -1;
    }
    num_of_msgs = 0;
    unlink(FILENAME);
    unlink(boards_json::board_json_path(FILENAME).c_str());
}
board_info_type::board_info_type(int objnum, int l_read, int l_write, int l_rem,
    int max_msg, char* file, char* titlename)
{
    /** This stuff is copied lower to the mail_info_type constructor -
        be careful and considerate. **/

    vnum = objnum;
    if (vnum > 0) {
        rnum = real_object(vnum);
        if (rnum < 0) {
            log("Board does not exist.");
            rnum = -1;
        }
    } else
        rnum = -1;

    lnum = num_of_boards;
    num_of_boards++;
    read_lvl = l_read;
    write_lvl = l_write;
    remove_lvl = l_rem;
    num_of_msgs = 0;
    max_of_msgs = max_msg;
    last_message = 0;
    //  msg_index = (struct board_msginfo *)
    //    calloc(max_msg,sizeof(struct board_msginfo));
    CREATE(msg_index, board_msginfo, max_msg);
    strcpy(short_name, file);
    sprintf(filename, "%s/%s.boa", BOARD_DIR, file);
    strcpy(title, titlename);
    load_board();
}
board_info_type::board_info_type()
{
    vnum = 1;
    rnum = 0;
    lnum = num_of_boards++;
    read_lvl = write_lvl = remove_lvl = 0;
    max_of_msgs = 1;
    num_of_msgs = 0;
    //  msg_index = (struct board_msginfo *)
    //    calloc(1,sizeof(struct board_msginfo));
    CREATE1(msg_index, board_msginfo);
    filename[0] = 0;
}
mail_info_type::mail_info_type(int objnum, int l_read, int l_write, int l_rem,
    int max_msg, char* file, char* titlename) /*:
  /  board_info_type::board_info_type/(objnum, l_read, l_write, l_rem, max_msg, file)*/
{
    vnum = objnum;
    if (vnum > 0) {
        rnum = real_object(vnum);
        if (rnum < 0) {
            log("Board does not exist.");
            rnum = -1;
        }
    } else
        rnum = -1;

    lnum = num_of_boards;
    num_of_boards++;
    read_lvl = l_read;
    write_lvl = l_write;
    remove_lvl = l_rem;
    num_of_msgs = 0;
    max_of_msgs = max_msg;
    last_message = 0;
    //  msg_index = (struct board_msginfo *)
    //    calloc(max_msg,sizeof(struct board_msginfo));
    CREATE(msg_index, board_msginfo, max_msg);

    strcpy(short_name, file);
    sprintf(filename, "%s/%s.boa", BOARD_DIR, file);
    strcpy(title, titlename);

    load_board();
    //  printf("mail_info_type created\n");
}

void mail_info_type::write_message(struct char_data* ch, char* arg, int num)
{
    char name[100];
    int i, numb, len, slotnum, oldmsgnum;
    time_t ct;
    char* tmstr;

    while (*arg && isspace(*arg))
        arg++;

    if (!*arg) {
        send_to_char("Send the letter to whom?\n\r", ch);
        return;
    }
    if ((slotnum = find_slot()) == -1) {
        send_to_char("The mailboard is malfunctioning - sorry.\n\r", ch);
        log("SYSERR: Mail: failed to find empty slot on write.");
        return;
    }

    oldmsgnum = num_of_msgs;

    ct = time(0);
    tmstr = (char*)asctime(localtime(&ct));
    *(tmstr + strlen(tmstr) - 1) = '\0';

    for (i = 0; i < 99 && arg[i] > 0 && arg[i] != ':'; i++)
        sprintf(buf2, "(%s)", GET_NAME(ch));
    sprintf(buf, "%6.10s %-12s :%s", tmstr, buf2, arg + i);
    len = strlen(buf) + 1;

    do {
        for (i = 0; i < 99 && arg[i] > ' ' && arg[i] != ':'; i++)
            name[i] = arg[i];
        name[i] = 0;
        arg += i;

        numb = find_name(name);

        if (numb < 0) {
            send_to_char("No such person.\n\r", ch);
            break;
        }

        /* skip blanks */
        for (; *arg && isspace(*arg); arg++)
            ;
        /*    if (!*arg) {
          send_to_char("We must have a headline!\n\r", ch);
          return;
          }
          */
        CREATE(NEW_MSG_INDEX.heading, char, len);
        if (!NEW_MSG_INDEX.heading) {
            send_to_char("The board is malfunctioning - sorry.\n\r", ch);
            return;
        }
        strcpy(NEW_MSG_INDEX.heading, buf);
        NEW_MSG_INDEX.heading[len - 1] = '\0';
        NEW_MSG_INDEX.level = GET_LEVEL(ch);
        NEW_MSG_INDEX.slot_num = slotnum;
        NEW_MSG_INDEX.msg_num = player_table[numb].idnum; // here was numb + 1;
        num_of_msgs++;
        if (num_of_msgs > max_of_msgs) {
            send_to_char("The mail system is full.\n\r", ch);
            log("ALERT: Mail system overflowed.");
            numb = -1;
            break;
        }
    } while (*arg && *arg != ':');

    if (numb < 0) {
        num_of_msgs = oldmsgnum;
        return;
    }
    arg++;

    act("$n starts to write a message.", TRUE, ch, 0, 0, TO_ROOM);
    string_add_init(ch->desc, &(msg_storage[slotnum]));
}

int mail_info_type::approve_msg(char_data* ch, board_msginfo* msg, int cur_num, int* num)
{
    if ((msg->msg_num == ch->specials2.idnum) || (ch->specials2.idnum < 0)) {
        *num = *num + 1;
        return 1;
    }
    return 0;
}

void report_news(struct char_data* ch)
{
    int count;
    char message[100];

    news_board->tmp_allflag = 0;
    count = news_board->count_msg(ch, ch->specials.board_point[news_board->lnum]);

    switch (count) {
    case 0:
        send_to_char("There is no unread news.\n\r", ch);
        break;
    case 1:
        send_to_char("There is 1 unread news.\n\r", ch);
        break;
    default:
        sprintf(message, "There are %d unread news.\n\r", count);
        send_to_char(message, ch);
        break;
    }
}

/* 6-12-01 Errent
 *  The old report_mail function has been replaced with has_mail from the new mail system
 *  Rather than rewrite all instances of report_mail, i just made report_mail use has_mail
 */
void report_mail(struct char_data* ch)
{
    char recipient[100];

    _parse_name(GET_NAME(ch), recipient);

    if (has_mail(recipient))
        send_to_char("You have new mail waiting.\n\r", ch);
    else
        send_to_char("There is no mail for you.\n\r", ch);

    /* old stuff . . . .
  int count;
  char message[100];

  count=mail_board->count_msg(ch, 0);

  switch(count){
  case 0:
    send_to_char("There is no mail for you.\n\r",ch);
    break;
  case 1:
    send_to_char("There is 1 letter for you.\n\r",ch);
    break;
  default:
    sprintf(message,"There are %d letters for you.\n\r",count);
    send_to_char(message,ch);
    break;
  }
*/
}

ACMD(do_board)
{
    gen_board(0, ch, cmd, argument, SPECIAL_COMMAND, wtl);
}
