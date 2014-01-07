// This file is part of the tiedit project
//
// Copyright (c) 2014 by Mike Sharov <msharov@users.sourceforge.net>
// This file is free software, distributed under the MIT License.

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <curses.h>
#include <signal.h>
#include <stdbool.h>

//{{{ Prototypes -------------------------------------------------------

static void Draw (void);

static void CleanupUI (void);
static void OnQuitSignal (int sig);
static void OnMsgSignal (int sig);
static void InstallCleanupHandlers (void);

//}}}-------------------------------------------------------------------
//{{{ Terminfo data loading and editing

enum { TERMINFO_MAGIC = 0432 };

/// The header of the terminfo file
struct STerminfoHeader {
    uint16_t	magic;		///< Equal to TERMINFO_MAGIC constant above.
    uint16_t	namesSize;
    uint16_t	nBooleans;
    uint16_t	nNumbers;
    uint16_t	nStrings;
    uint16_t	strtableSize;
};

//}}}-------------------------------------------------------------------
//{{{ UI

static void Draw (void)
{
    erase();
    mvaddstr (10, 10, "Hello world!");
}

//}}}-------------------------------------------------------------------
//{{{ Process housekeeping

static void CleanupUI (void)
{
    endwin();
}

static void OnQuitSignal (int sig)
{
    static bool s_FirstTime = true;
    if (!s_FirstTime) {
	psignal (sig, "[S] Double error");
	_Exit (EXIT_FAILURE);
    }
    s_FirstTime = false;
    CleanupUI();
    psignal (sig, "[S] Error");
    exit (EXIT_FAILURE);
}

static void OnMsgSignal (int sig UNUSED)
{
    Draw();
}

static void InstallCleanupHandlers (void)
{
//{{{2 Signal sets for OnQuitSignal and OnMsgSignal
#define S(sn) (1u<<(sn))
    enum {
	sigset_Quit = S(SIGINT)|S(SIGQUIT)|S(SIGTERM)|S(SIGPWR)|S(SIGILL)|S(SIGABRT)
			|S(SIGBUS)|S(SIGFPE)|S(SIGSYS)|S(SIGSEGV)|S(SIGALRM)|S(SIGXCPU),
	sigset_Msg = S(SIGHUP)|S(SIGCHLD)|S(SIGWINCH)|S(SIGURG)|S(SIGXFSZ)|S(SIGUSR1)|S(SIGUSR2)|S(SIGPIPE)
    };
#undef S
//}}}2
    atexit (CleanupUI);
    for (unsigned b = 0; b < NSIG; ++b) {
	if (sigset_Quit & (1u << b))
	    signal (b, OnQuitSignal);
	else if (sigset_Msg & (1u << b))
	    signal (b, OnMsgSignal);
    }
}

int main (void)
{
    InstallCleanupHandlers();
    if (!initscr()) {
	puts ("Error: unable to initialize UI");
	return (EXIT_FAILURE);
    }
    Draw();
    getch();
    return (EXIT_SUCCESS);
}

//}}}-------------------------------------------------------------------
