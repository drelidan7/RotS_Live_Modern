#pragma once
// rots/core/descriptor.h — connection-state constants (CON_*), snoop_data, and
// the descriptor_data session struct. char_data is pointer-only (fwd.h).
// protocol.h still lives flat in src/ — relative include on purpose, see
// rots/core/types.h for the rationale.
#include "../../../../protocol.h" // protocol_t
#include "rots/core/fwd.h"
#include "rots/core/types.h"

/* ***********************************************************
 *  The following structures are related to descriptor_data   *
 *********************************************************** */

/* modes of connectedness */

#define CON_PLYNG 0
#define CON_NME 1
#define CON_NMECNF 2
#define CON_PWDNRM 3
#define CON_PWDGET 4
#define CON_PWDCNF 5
#define CON_QSEX 6
#define CON_RMOTD 7
#define CON_SLCT 8
#define CON_EXDSCR 9
#define CON_QPROF 10
#define CON_LDEAD 11
#define CON_PWDNQO 12
#define CON_PWDNEW 13
#define CON_PWDNCNF 14
#define CON_CLOSE 15
#define CON_DELCNF1 16
#define CON_DELCNF2 17
#define CON_QRACE 18
#define CON_QOWN 19
#define CON_QOWN2 20
#define CON_CREATE 21
#define CON_CREATE2 22
#define CON_LINKLS 23
#define CON_LATIN 24
#define CON_COLOR 25
#define CON_ACCTPWD 26
#define CON_ACCTSLCT 27
#define CON_ACCTLINKPWD 28
#define CON_ACCTNEWCNF 29
#define CON_ACCTNEWPWD 30
#define CON_ACCTNEWPWDCNF 31
#define CON_ACCTMENU 32
#define CON_ACCTLINKNAME 33
#define CON_ACCTRESETOLD 34
#define CON_ACCTRESETNEW 35
#define CON_ACCTRESETCNF 36
#define CON_ACCTNEWCHAR 37
#define CON_ACCTLEGPWD 38
#define CON_ACCTVERIFY 39
#define CON_ACCTDELCNF1 40

/* modes for flags */
#define DFLAG_IS_SPAMMING 1

struct snoop_data {
    struct char_data* snooping; /* Who is this char snooping		*/
    struct char_data* snoop_by; /* And who is snooping this char	*/
};

#define BLOCK_STR_LEN                     \
    512 /* how much to allocate initially \
           for string_add messages */

struct descriptor_data {
    SocketType descriptor; /* file descriptor for socket	*/
    char* name; /* ptr to name for mail system		*/
    char host[50]; /* hostname				*/
    uint32_t proxy_peer_address; /* pending proxy peer address */
    byte proxy_peer_bytes_read; /* bytes read for pending proxy header */
    bool waiting_for_proxy_header; /* descriptor is waiting for proxy header completion */
    char pwd[MAX_PWD_LENGTH + 1]; /* password			*/
    char account_name[MAX_INPUT_LENGTH]; /* authenticated account login */
    char account_email[MAX_INPUT_LENGTH]; /* authenticated account email */
    char account_password[MAX_ACCOUNT_PASSWORD_LENGTH + 1]; /* transient account password */
    char account_character_name[MAX_INPUT_LENGTH]; /* pending account character action */
    int bad_pws; /* number of bad pw attemps this login	*/
    int pos; /* position in player-file		*/
    int connected; /* mode of 'connectedness'		*/
    //   int	wait;			/* wait for how many loops    	*/
    int desc_num; /* unique num assigned to desc		*/
    time_t login_time; /* when the person connected; time_t (not long) so &login_time is a valid time_t* for localtime() on Windows LLP64 -- Phase 3 Task 6 */
    char* showstr_head; /* for paging through texts		*/
    char* showstr_point; /*		-			*/
    char** str; /* for the modify-str system		*/
    unsigned int max_str; /*  allocated length of *str		*/
    unsigned int len_str; /* present length of *str               */
    unsigned int cur_str; /* current pointer position in *str     */
    int prompt_mode; /* control of prompt-printing		*/
    char buf[MAX_STRING_LENGTH]; /* buffer for raw input			*/
    char last_input[MAX_INPUT_LENGTH]; /* the last input			*/
    char small_outbuf[SMALL_BUFSIZE]; /* standard output bufer		*/
    char* output; /* ptr to the current output buffer	*/
    int bufptr; /* ptr to end of current output		*/
    int bufspace; /* space left in the output buffer	*/
    unsigned char dflags; /* flags for this descriptor            */
    time_t last_input_time; /* time(0) of last_input               */
    struct txt_block* large_outbuf; /* ptr to large buffer, if we need it */
    struct txt_q input; /* q of unprocessed input		*/
    struct char_data* character; /* linked to char			*/
    struct char_data* original; /* original char if switched		*/
    struct snoop_data snoop; /* to snoop people			*/
    struct descriptor_data* next; /* link to next descriptor		*/
    protocol_t* pProtocol;
};
