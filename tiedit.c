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
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

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

struct STerminfo {
    struct STerminfoHeader	h;
    char*		name;
    bool*		abool;
    int16_t*		anum;
    int16_t*		astro;
    char*		strings;
};

static struct STerminfo _info = {{0,0,0,0,0,0},NULL,NULL,NULL,NULL,NULL};

static void ReadBytes (int fd, void* buf, size_t bufsz)
{
    if (bufsz != (size_t) read (fd, buf, bufsz)) {
	perror ("read");
	exit (EXIT_FAILURE);
    }
}

static void* Realloc (void* op, size_t nsz)
{
    void* p = realloc (op, nsz);
    if (!p) {
	puts ("Error: out of memory");
	exit (EXIT_FAILURE);
    }
    return (p);
}

static void LoadTerminfo (const char* tifile)
{
    int fd = open (tifile, O_RDONLY);
    if (fd < 0) {
	perror ("open");
	exit (EXIT_FAILURE);
    }
    ReadBytes (fd, &_info.h, sizeof(_info.h));
    if (_info.h.magic != TERMINFO_MAGIC) {
	printf ("Error: %s is not a terminfo file\n", tifile);
	exit (EXIT_FAILURE);
    }
    _info.name = (char*) Realloc (_info.name, _info.h.namesSize);
    ReadBytes (fd, _info.name, _info.h.namesSize);

    close(fd);
}

//}}}-------------------------------------------------------------------
//{{{ UI

static void Draw (void)
{
    erase();
    mvprintw (10, 10, "%hu names: %s\n%hu bools\n%hu numbers\n%hu strings\n%hu strtable\n", _info.h.namesSize, _info.name, _info.h.nBooleans, _info.h.nNumbers, _info.h.nStrings, _info.h.strtableSize);
}

//}}}-------------------------------------------------------------------
//{{{ Process housekeeping

static void CleanupUI (void)
{
    endwin();
    if (_info.name)
	free (_info.name);
    if (_info.abool)
	free (_info.abool);
    if (_info.astro)
	free (_info.astro);
    if (_info.astro)
	free (_info.strings);
    memset (&_info, 0, sizeof(_info));
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
    LoadTerminfo ("/usr/share/terminfo/x/xterm");
    if (!initscr()) {
	puts ("Error: unable to initialize UI");
	return (EXIT_FAILURE);
    }
    Draw();
    getch();
    return (EXIT_SUCCESS);
}

//}}}-------------------------------------------------------------------
