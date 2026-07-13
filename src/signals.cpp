/* ************************************************************************
 *   File: signals.c                                     Part of CircleMUD *
 *  Usage: Signal trapping and signal handlers                             *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ************************************************************************ */

#include "platdef.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "rots_net.h"
#include "structs.h"
#include "utils.h"

extern struct descriptor_data* descriptor_list;
extern SocketType mother_desc;

void checkpointing(int);
void logsig(int);
void diesig(int);
void hupsig(int);
void badcrash(int);
void unrestrict_game(int);
void reread_wizlists(int);
void Emergency_save(void);

int graceful_tried = 0;

#if defined PREDEF_PLATFORM_WINDOWS
// Windows console-close/logoff/shutdown notifications (Phase 3 Task 5: MSVC bring-up).
// The CRT signal() mechanism only delivers SIGINT/SIGTERM on Windows -- there is no
// process-wide POSIX signal for "the console window is closing" or "the system is
// shutting down"; SetConsoleCtrlHandler is the native mechanism for those, so it is
// registered alongside signal() in signal_setup() below and routed to the exact same
// hupsig() graceful-shutdown path (Emergency_save + clean exit) that SIGHUP/SIGINT/
// SIGTERM use on POSIX. The handler runs on its own dedicated thread per the Win32
// contract, but hupsig() already terminates the process (exit(0)) before returning, so
// there is no meaningful concurrent-execution window with the game's main thread.
BOOL WINAPI rots_console_ctrl_handler(DWORD ctrl_type)
{
    switch (ctrl_type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        hupsig(0);
        return TRUE; // unreachable: hupsig() calls exit(0) and never returns
    default:
        return FALSE;
    }
}
#endif

void signal_setup(void)
{
#if defined PREDEF_PLATFORM_LINUX
    // struct itimerval itime;
    // struct timeval interval;

    //  return;
    signal(SIGUSR1, reread_wizlists);
    signal(SIGUSR2, unrestrict_game);

    /* just to be on the safe side: */

    signal(SIGHUP, hupsig);
    signal(SIGILL, diesig);
    //   signal(SIGSEGV, diesig); Also should be included once the bugs are found :-)
    signal(SIGFPE, diesig);
    /*   signal(SIGTRAP, diesig); */
    signal(SIGFPE, diesig);
    signal(SIGBUS, diesig);
    /*   signal(SIGIOT, diesig);  */
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, hupsig);
    signal(SIGALRM, logsig);
    signal(SIGTERM, hupsig);
    //   signal(SIGSEGV, badcrash);
    //   signal(SIGBUS, badcrash);   This line and above commented out 11 Jan 00.  Put back in one day :-)

    /* set up the deadlock-protection */

    //   interval.tv_sec = 900;    /* 15 minutes */
    //   interval.tv_usec = 0;
    // itime.it_interval = interval;
    // itime.it_value = interval;
    // setitimer(ITIMER_VIRTUAL, &itime, 0);
    signal(SIGVTALRM, checkpointing);
#elif defined PREDEF_PLATFORM_WINDOWS
    // Windows' CRT <signal.h> has no SIGUSR1/SIGUSR2/SIGHUP/SIGBUS/SIGPIPE/SIGALRM/
    // SIGVTALRM -- those don't exist as a concept on this platform, so wizlist-reload
    // (SIGUSR1 -> reread_wizlists), unrestrict-game (SIGUSR2 -> unrestrict_game), the
    // periodic deadlock-checkpoint alarm (SIGVTALRM -> checkpointing), and the
    // ignore-broken-pipe/log-and-ignore-alarm handlers simply have nothing to bind to
    // here. This is NOT an operational gap for the first two: `reload wizlist`/
    // `reload all` (db.cpp's do_reload) and `wizlock 0` (act_wiz.cpp's do_wizlock) are
    // in-game admin commands that already do the same work the signals used to trigger
    // remotely. checkpointing()'s deadlock watchdog has no in-game replacement and is
    // simply unavailable on Windows in this phase (Phase 5 candidate: a
    // std::chrono-based watchdog thread instead of SIGVTALRM/setitimer).
    //
    // SIGINT/SIGTERM are ISO C signals MSVC does define; SetConsoleCtrlHandler
    // additionally catches console-close/logoff/shutdown (which CRT signal() never
    // delivers on Windows) and maps every one of them to the same hupsig() graceful-
    // shutdown path.
    signal(SIGINT, hupsig);
    signal(SIGTERM, hupsig);
    signal(SIGILL, diesig);
    signal(SIGFPE, diesig);
    SetConsoleCtrlHandler(rots_console_ctrl_handler, TRUE);
#endif
}

void checkpointing(int)
{
    extern int tics;

    if (!tics) {
        log("CHECKPOINT shutdown: tics not updated");
        abort();
    } else
        tics = 0;
}

void reread_wizlists(int)
{
    void reboot_wizlists(void);

    // SIGUSR1 doesn't exist on Windows (see signal_setup()); this handler is simply
    // never registered there, so the re-arm below only needs to compile on POSIX.
#if defined PREDEF_PLATFORM_LINUX
    signal(SIGUSR1, reread_wizlists);
#endif
    mudlog("Rereading wizlists.", CMP, LEVEL_IMMORT, FALSE);
    reboot_wizlists();
}

void unrestrict_game(int)
{
    extern int restrict;
    extern int num_invalid;

    // SIGUSR2 doesn't exist on Windows (see signal_setup()); this handler is simply
    // never registered there, so the re-arm below only needs to compile on POSIX.
#if defined PREDEF_PLATFORM_LINUX
    signal(SIGUSR2, unrestrict_game);
#endif
    mudlog("Received SIGUSR2 - unrestricting game (emergent)",
        BRF, LEVEL_IMMORT, TRUE);
    restrict = 0;
    num_invalid = 0;
}

/* kick out players etc */

void close_sockets(SocketType s);

void hupsig(int)
{
    extern SocketType mother_desc;
    log("Received SIGHUP, SIGINT, or SIGTERM.  Shutting down...");
    Emergency_save();
    close_sockets(mother_desc);
    exit(0); /* something more elegant should perhaps be substituted */
}

void badcrash(int)
{
    void close_socket(struct descriptor_data * d);
    struct descriptor_data* desc;

    log("SIGSEGV or SIGBUS received.  Trying to shut down gracefully.");
    Emergency_save();
    if (!graceful_tried) {
        /* Routed through the rots_net shim like every other socket close
           (close vs closesocket per platform). We are inside a SIGSEGV/SIGBUS
           handler: the shim is a thin non-allocating wrapper around the raw
           close call, so it is exactly as async-signal-safe as the close()
           it replaces. */
        rots_net::close_socket(mother_desc);
        log("Trying to close all sockets.");
        graceful_tried = 1;
        for (desc = descriptor_list; desc; desc = desc->next)
            rots_net::close_socket(desc->descriptor);
    }
    abort();
}

void logsig(int)
{
    log("Signal received.  Ignoring.");
}
void diesig(int)
{
    // Try to save everyone.
    Emergency_save();
    exit(0);
}
