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

#define KEY_ESCAPE	27

static void FillRect (unsigned x, unsigned y, unsigned w, unsigned h);
static void DrawLine (unsigned l);
static void Draw (void);
static void OnKey (unsigned key);

static void EventLoop (void);
static void InitUI (void);
static void CleanupUI (void);
static void OnQuitSignal (int sig);
static void OnMsgSignal (int sig);
static void InstallCleanupHandlers (void);

enum { TERMINFO_MAGIC = 0432 };

/// The header of the terminfo file
struct STerminfoHeader {
    uint16_t	magic;		///< Equal to TERMINFO_MAGIC constant above.
    uint16_t	nameSize;
    uint16_t	nBooleans;
    uint16_t	nNumbers;
    uint16_t	nStrings;
    uint16_t	strtableSize;
};

struct STerminfo {
    struct STerminfoHeader h;
    char*		name;
    bool*		abool;
    int16_t*		anum;
    uint16_t*		astro;
    char*		strings;
};

//}}}-------------------------------------------------------------------
//{{{ Globals

static struct STerminfo _info = {{0,0,0,0,0,0},NULL,NULL,NULL,NULL,NULL};
static bool _quitting = false;
static unsigned _topline = 0;
static unsigned _selection = 0;

//}}}-------------------------------------------------------------------
//{{{ Utility functions

static inline unsigned min (unsigned a, unsigned b)
{
    return (a < b ? a : b);
}

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

//}}}-------------------------------------------------------------------
//{{{ Terminfo data loading and editing

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
    _info.name = (char*) Realloc (_info.name, _info.h.nameSize * sizeof(char));
    _info.abool = (bool*) Realloc (_info.abool, _info.h.nBooleans * sizeof(bool));
    _info.anum = (int16_t*) Realloc (_info.anum, _info.h.nNumbers * sizeof(int16_t));
    _info.astro = (uint16_t*) Realloc (_info.astro, _info.h.nStrings * sizeof(uint16_t));
    _info.strings = (char*) Realloc (_info.strings, _info.h.strtableSize * sizeof(char));
    ReadBytes (fd, _info.name, _info.h.nameSize * sizeof(char));
    ReadBytes (fd, _info.abool, _info.h.nBooleans * sizeof(bool));
    ReadBytes (fd, _info.anum, _info.h.nNumbers * sizeof(int16_t));
    ReadBytes (fd, _info.astro, _info.h.nStrings * sizeof(uint16_t));
    ReadBytes (fd, _info.strings, _info.h.strtableSize * sizeof(char));
    close(fd);
}

//}}}-------------------------------------------------------------------
//{{{ UI

static void FillRect (unsigned x, unsigned y, unsigned w, unsigned h)
{
    for (unsigned j = 0; j < h; ++j) {
	move (y+j, x);
	for (unsigned i = 0; i < w; ++i)
	    addch (' ');
    }
}

static void DrawLine (unsigned l)
{
    mvprintw (l, 1, "Line %u", _topline+l);
}

static void Draw (void)
{
    erase();
    const unsigned nLines = _info.h.nBooleans + _info.h.nNumbers + _info.h.nStrings;
    const unsigned nVisible = min (nLines, LINES-1), visselection = _selection-_topline;
    for (unsigned l = 0; l < nVisible; ++l) {
	if (visselection == l)
	    attron (A_REVERSE);
	FillRect (0, l, COLS, 1);
	DrawLine (l);
	if (visselection == l)
	    attroff (A_REVERSE);
    }
    attron (A_REVERSE);
    FillRect (0, LINES-1, COLS, 1);
    mvaddstr (LINES-1, 1, _info.name);
    attroff (A_REVERSE);
}

static void OnKey (unsigned key)
{
    const unsigned nLines = _info.h.nBooleans + _info.h.nNumbers + _info.h.nStrings;
    const unsigned pageSize = LINES-1;
    if (key == KEY_ESCAPE || key == 'q')
	_quitting = true;
    else if (key == KEY_HOME || key == '0')
	_selection = 0;
    else if (key == KEY_END || key == 'G')
	_selection = nLines-1;
    else if (key == 'H')
	_selection = _topline;
    else if (key == 'M')
	_selection = _topline+(pageSize-1)/2;
    else if (key == 'L')
	_selection = _topline+(pageSize-1);
    else if ((key == KEY_UP || key == 'k') && _selection > 0)
	--_selection;
    else if ((key == KEY_DOWN || key == 'j') && _selection < nLines-1)
	++_selection;
    else if (key == KEY_PPAGE || key == 'b') {
	if (_selection > pageSize)
	    _selection -= pageSize;
	else
	    _selection = 0;
    } else if (key == KEY_NPAGE || key == ' ') {
	if (_selection + pageSize < nLines-1)
	    _selection += pageSize;
	else
	    _selection = nLines-1;
    }
    if (_topline > _selection)
	_topline = _selection;
    if (_topline + pageSize-1 < _selection)
	_topline = _selection - (pageSize-1);
}

//}}}-------------------------------------------------------------------
//{{{ Process housekeeping

static void InitUI (void)
{
    if (!initscr()) {
	puts ("Error: unable to initialize UI");
	exit (EXIT_FAILURE);
    }
    cbreak();
    noecho();
    keypad (stdscr, true);
    curs_set (false);
}

static void EventLoop (void)
{
    while (!_quitting) {
	Draw();
	int key = getch();
	if (key > 0)
	    OnKey (key);
    }
}

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
    refresh();
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
    InitUI();
    EventLoop();
    return (EXIT_SUCCESS);
}

//}}}-------------------------------------------------------------------
