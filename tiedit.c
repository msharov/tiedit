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

static inline unsigned min (unsigned a, unsigned b) CONST;
static void* Realloc (void* op, size_t nsz);
static void ReadBytes (int fd, void* buf, size_t bufsz);

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

enum {
    FirstBoolean,
    NBooleans	= 44,
    FirstNumber = FirstBoolean + NBooleans,
    NNumbers	= 39,
    FirstString = FirstNumber + NNumbers,
    NStrings	= 414,
    NValues	= FirstString + NStrings
};

static const char* GetStrtableEntry (unsigned idx, unsigned maxstr, const char* strs, unsigned strssize) PURE;
static const char* GetBooleanName (unsigned i) PURE;
static const char* GetNumberName (unsigned i) PURE;
static const char* GetStringName (unsigned i) PURE;

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

static const char c_BooleanNames[];
static const char c_NumberNames[];
static const char c_StringNames[];

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

static void* Realloc (void* op, size_t nsz)
{
    void* p = realloc (op, nsz);
    if (!p) {
	puts ("Error: out of memory");
	exit (EXIT_FAILURE);
    }
    return (p);
}

static void ReadBytes (int fd, void* buf, size_t bufsz)
{
    if (bufsz != (size_t) read (fd, buf, bufsz)) {
	perror ("read");
	exit (EXIT_FAILURE);
    }
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
    if (_info.h.magic != TERMINFO_MAGIC
	|| _info.h.nBooleans > NBooleans
	|| _info.h.nNumbers > NNumbers
	|| _info.h.nStrings > NStrings) {
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
    move (l, 1);
    const unsigned dl = _topline+l;
    if (dl < FirstNumber) {
	const unsigned di = dl - FirstBoolean;
	const char* v = "false";
	if (di < _info.h.nBooleans && _info.abool[di])
	    v = "true";
	printw ("%-26s: %s", GetBooleanName(di), v);
    } else if (dl < FirstString) {
	const unsigned di = dl - FirstNumber;
	int16_t v = -1;
	if (di < _info.h.nNumbers)
	    v = _info.anum[di];
	printw ("%-26s: %hd", GetNumberName(di), v);
    } else if (dl < NValues) {
	const unsigned di = dl - FirstString;
	printw ("%-26s: ", GetStringName(di));
	const char* s = "";
	unsigned slen = 0;
	if (di < _info.h.nStrings && _info.astro[di] < _info.h.strtableSize) {
	    s = _info.strings+_info.astro[di];
	    slen = strnlen (s, _info.h.strtableSize - _info.astro[di]);
	}
	for (unsigned i = 0; i < slen; ++i) {
	    unsigned char c = s[i];
	    if (c < ' ' || c > '~') {
		attron (A_BOLD);
		if (c < ' ') {
		    addch ('^');
		    addch ('A'-1+c);
		} else
		    printw ("\\%o", c);
		attroff (A_BOLD);
	    } else
		addch (c);
	}
    } else
	addstr ("???");
}

static void Draw (void)
{
    erase();
    const unsigned nVisible = min (NValues, LINES-1), visselection = _selection-_topline;
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
    const unsigned pageSize = LINES-1;
    if (key == KEY_ESCAPE || key == 'q')
	_quitting = true;
    else if (key == KEY_HOME || key == '0')
	_selection = 0;
    else if (key == KEY_END || key == 'G')
	_selection = NValues-1;
    else if (key == 'H')
	_selection = _topline;
    else if (key == 'M')
	_selection = _topline+(pageSize-1)/2;
    else if (key == 'L')
	_selection = _topline+(pageSize-1);
    else if ((key == KEY_UP || key == 'k') && _selection > 0)
	--_selection;
    else if ((key == KEY_DOWN || key == 'j') && _selection < NValues-1)
	++_selection;
    else if (key == KEY_PPAGE || key == 'b') {
	if (_selection > pageSize)
	    _selection -= pageSize;
	else
	    _selection = 0;
    } else if (key == KEY_NPAGE || key == ' ') {
	if (_selection + pageSize < NValues-1)
	    _selection += pageSize;
	else
	    _selection = NValues-1;
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
//{{{ Value name tables

#define _(s) "\0" s
static const char c_BooleanNames[] =
    _("auto_left_margin")
    _("auto_right_margin")
    _("no_esc_ctlc")
    _("ceol_standout_glitch")
    _("eat_newline_glitch")
    _("erase_overstrike")
    _("generic_type")
    _("hard_copy")
    _("has_meta_key")
    _("has_status_line")
    _("insert_null_glitch")
    _("memory_above")
    _("memory_below")
    _("move_insert_mode")
    _("move_standout_mode")
    _("over_strike")
    _("status_line_esc_ok")
    _("dest_tabs_magic_smso")
    _("tilde_glitch")
    _("transparent_underline")
    _("xon_xoff")
    _("needs_xon_xoff")
    _("prtr_silent")
    _("hard_cursor")
    _("non_rev_rmcup")
    _("no_pad_char")
    _("non_dest_scroll_region")
    _("can_change")
    _("back_color_erase")
    _("hue_lightness_saturation")
    _("col_addr_glitch")
    _("cr_cancels_micro_mode")
    _("has_print_wheel")
    _("row_addr_glitch")
    _("semi_auto_right_margin")
    _("cpi_changes_res")
    _("lpi_changes_res")
    _("backspaces_with_bs")
    _("crt_no_scrolling")
    _("no_correctly_working_cr")
    _("gnu_has_meta_key")
    _("linefeed_is_newline")
    _("has_hardware_tabs")
    _("return_does_clr_eol")
;
static const char c_NumberNames[] =
    _("columns")
    _("init_tabs")
    _("lines")
    _("lines_of_memory")
    _("magic_cookie_glitch")
    _("padding_baud_rate")
    _("virtual_terminal")
    _("width_status_line")
    _("num_labels")
    _("label_height")
    _("label_width")
    _("max_attributes")
    _("maximum_windows")
    _("max_colors")
    _("max_pairs")
    _("no_color_video")
    _("buffer_capacity")
    _("dot_vert_spacing")
    _("dot_horz_spacing")
    _("max_micro_address")
    _("max_micro_jump")
    _("micro_col_size")
    _("micro_line_size")
    _("number_of_pins")
    _("output_res_char")
    _("output_res_line")
    _("output_res_horz_inch")
    _("output_res_vert_inch")
    _("print_rate")
    _("wide_char_size")
    _("buttons")
    _("bit_image_entwining")
    _("bit_image_type")
    _("magic_cookie_glitch_ul")
    _("carriage_return_delay")
    _("new_line_delay")
    _("backspace_delay")
    _("horizontal_tab_delay")
    _("number_of_function_keys")
;
static const char c_StringNames[] =
    _("back_tab")
    _("bell")
    _("carriage_return")
    _("change_scroll_region")
    _("clear_all_tabs")
    _("clear_screen")
    _("clr_eol")
    _("clr_eos")
    _("column_address")
    _("command_character")
    _("cursor_address")
    _("cursor_down")
    _("cursor_home")
    _("cursor_invisible")
    _("cursor_left")
    _("cursor_mem_address")
    _("cursor_normal")
    _("cursor_right")
    _("cursor_to_ll")
    _("cursor_up")
    _("cursor_visible")
    _("delete_character")
    _("delete_line")
    _("dis_status_line")
    _("down_half_line")
    _("enter_alt_charset_mode")
    _("enter_blink_mode")
    _("enter_bold_mode")
    _("enter_ca_mode")
    _("enter_delete_mode")
    _("enter_dim_mode")
    _("enter_insert_mode")
    _("enter_secure_mode")
    _("enter_protected_mode")
    _("enter_reverse_mode")
    _("enter_standout_mode")
    _("enter_underline_mode")
    _("erase_chars")
    _("exit_alt_charset_mode")
    _("exit_attribute_mode")
    _("exit_ca_mode")
    _("exit_delete_mode")
    _("exit_insert_mode")
    _("exit_standout_mode")
    _("exit_underline_mode")
    _("flash_screen")
    _("form_feed")
    _("from_status_line")
    _("init_1string")
    _("init_2string")
    _("init_3string")
    _("init_file")
    _("insert_character")
    _("insert_line")
    _("insert_padding")
    _("key_backspace")
    _("key_catab")
    _("key_clear")
    _("key_ctab")
    _("key_dc")
    _("key_dl")
    _("key_down")
    _("key_eic")
    _("key_eol")
    _("key_eos")
    _("key_f0")
    _("key_f1")
    _("key_f10")
    _("key_f2")
    _("key_f3")
    _("key_f4")
    _("key_f5")
    _("key_f6")
    _("key_f7")
    _("key_f8")
    _("key_f9")
    _("key_home")
    _("key_ic")
    _("key_il")
    _("key_left")
    _("key_ll")
    _("key_npage")
    _("key_ppage")
    _("key_right")
    _("key_sf")
    _("key_sr")
    _("key_stab")
    _("key_up")
    _("keypad_local")
    _("keypad_xmit")
    _("lab_f0")
    _("lab_f1")
    _("lab_f10")
    _("lab_f2")
    _("lab_f3")
    _("lab_f4")
    _("lab_f5")
    _("lab_f6")
    _("lab_f7")
    _("lab_f8")
    _("lab_f9")
    _("meta_off")
    _("meta_on")
    _("newline")
    _("pad_char")
    _("parm_dch")
    _("parm_delete_line")
    _("parm_down_cursor")
    _("parm_ich")
    _("parm_index")
    _("parm_insert_line")
    _("parm_left_cursor")
    _("parm_right_cursor")
    _("parm_rindex")
    _("parm_up_cursor")
    _("pkey_key")
    _("pkey_local")
    _("pkey_xmit")
    _("print_screen")
    _("prtr_off")
    _("prtr_on")
    _("repeat_char")
    _("reset_1string")
    _("reset_2string")
    _("reset_3string")
    _("reset_file")
    _("restore_cursor")
    _("row_address")
    _("save_cursor")
    _("scroll_forward")
    _("scroll_reverse")
    _("set_attributes")
    _("set_tab")
    _("set_window")
    _("tab")
    _("to_status_line")
    _("underline_char")
    _("up_half_line")
    _("init_prog")
    _("key_a1")
    _("key_a3")
    _("key_b2")
    _("key_c1")
    _("key_c3")
    _("prtr_non")
    _("char_padding")
    _("acs_chars")
    _("plab_norm")
    _("key_btab")
    _("enter_xon_mode")
    _("exit_xon_mode")
    _("enter_am_mode")
    _("exit_am_mode")
    _("xon_character")
    _("xoff_character")
    _("ena_acs")
    _("label_on")
    _("label_off")
    _("key_beg")
    _("key_cancel")
    _("key_close")
    _("key_command")
    _("key_copy")
    _("key_create")
    _("key_end")
    _("key_enter")
    _("key_exit")
    _("key_find")
    _("key_help")
    _("key_mark")
    _("key_message")
    _("key_move")
    _("key_next")
    _("key_open")
    _("key_options")
    _("key_previous")
    _("key_print")
    _("key_redo")
    _("key_reference")
    _("key_refresh")
    _("key_replace")
    _("key_restart")
    _("key_resume")
    _("key_save")
    _("key_suspend")
    _("key_undo")
    _("key_sbeg")
    _("key_scancel")
    _("key_scommand")
    _("key_scopy")
    _("key_screate")
    _("key_sdc")
    _("key_sdl")
    _("key_select")
    _("key_send")
    _("key_seol")
    _("key_sexit")
    _("key_sfind")
    _("key_shelp")
    _("key_shome")
    _("key_sic")
    _("key_sleft")
    _("key_smessage")
    _("key_smove")
    _("key_snext")
    _("key_soptions")
    _("key_sprevious")
    _("key_sprint")
    _("key_sredo")
    _("key_sreplace")
    _("key_sright")
    _("key_srsume")
    _("key_ssave")
    _("key_ssuspend")
    _("key_sundo")
    _("req_for_input")
    _("key_f11")
    _("key_f12")
    _("key_f13")
    _("key_f14")
    _("key_f15")
    _("key_f16")
    _("key_f17")
    _("key_f18")
    _("key_f19")
    _("key_f20")
    _("key_f21")
    _("key_f22")
    _("key_f23")
    _("key_f24")
    _("key_f25")
    _("key_f26")
    _("key_f27")
    _("key_f28")
    _("key_f29")
    _("key_f30")
    _("key_f31")
    _("key_f32")
    _("key_f33")
    _("key_f34")
    _("key_f35")
    _("key_f36")
    _("key_f37")
    _("key_f38")
    _("key_f39")
    _("key_f40")
    _("key_f41")
    _("key_f42")
    _("key_f43")
    _("key_f44")
    _("key_f45")
    _("key_f46")
    _("key_f47")
    _("key_f48")
    _("key_f49")
    _("key_f50")
    _("key_f51")
    _("key_f52")
    _("key_f53")
    _("key_f54")
    _("key_f55")
    _("key_f56")
    _("key_f57")
    _("key_f58")
    _("key_f59")
    _("key_f60")
    _("key_f61")
    _("key_f62")
    _("key_f63")
    _("clr_bol")
    _("clear_margins")
    _("set_left_margin")
    _("set_right_margin")
    _("label_format")
    _("set_clock")
    _("display_clock")
    _("remove_clock")
    _("create_window")
    _("goto_window")
    _("hangup")
    _("dial_phone")
    _("quick_dial")
    _("tone")
    _("pulse")
    _("flash_hook")
    _("fixed_pause")
    _("wait_tone")
    _("user0")
    _("user1")
    _("user2")
    _("user3")
    _("user4")
    _("user5")
    _("user6")
    _("user7")
    _("user8")
    _("user9")
    _("orig_pair")
    _("orig_colors")
    _("initialize_color")
    _("initialize_pair")
    _("set_color_pair")
    _("set_foreground")
    _("set_background")
    _("change_char_pitch")
    _("change_line_pitch")
    _("change_res_horz")
    _("change_res_vert")
    _("define_char")
    _("enter_doublewide_mode")
    _("enter_draft_quality")
    _("enter_italics_mode")
    _("enter_leftward_mode")
    _("enter_micro_mode")
    _("enter_near_letter_quality")
    _("enter_normal_quality")
    _("enter_shadow_mode")
    _("enter_subscript_mode")
    _("enter_superscript_mode")
    _("enter_upward_mode")
    _("exit_doublewide_mode")
    _("exit_italics_mode")
    _("exit_leftward_mode")
    _("exit_micro_mode")
    _("exit_shadow_mode")
    _("exit_subscript_mode")
    _("exit_superscript_mode")
    _("exit_upward_mode")
    _("micro_column_address")
    _("micro_down")
    _("micro_left")
    _("micro_right")
    _("micro_row_address")
    _("micro_up")
    _("order_of_pins")
    _("parm_down_micro")
    _("parm_left_micro")
    _("parm_right_micro")
    _("parm_up_micro")
    _("select_char_set")
    _("set_bottom_margin")
    _("set_bottom_margin_parm")
    _("set_left_margin_parm")
    _("set_right_margin_parm")
    _("set_top_margin")
    _("set_top_margin_parm")
    _("start_bit_image")
    _("start_char_set_def")
    _("stop_bit_image")
    _("stop_char_set_def")
    _("subscript_characters")
    _("superscript_characters")
    _("these_cause_cr")
    _("zero_motion")
    _("char_set_names")
    _("key_mouse")
    _("mouse_info")
    _("req_mouse_pos")
    _("get_mouse")
    _("set_a_foreground")
    _("set_a_background")
    _("pkey_plab")
    _("device_type")
    _("code_set_init")
    _("set0_des_seq")
    _("set1_des_seq")
    _("set2_des_seq")
    _("set3_des_seq")
    _("set_lr_margin")
    _("set_tb_margin")
    _("bit_image_repeat")
    _("bit_image_newline")
    _("bit_image_carriage_return")
    _("color_names")
    _("define_bit_image_region")
    _("end_bit_image_region")
    _("set_color_band")
    _("set_page_length")
    _("display_pc_char")
    _("enter_pc_charset_mode")
    _("exit_pc_charset_mode")
    _("enter_scancode_mode")
    _("exit_scancode_mode")
    _("pc_term_options")
    _("scancode_escape")
    _("alt_scancode_esc")
    _("enter_horizontal_hl_mode")
    _("enter_left_hl_mode")
    _("enter_low_hl_mode")
    _("enter_right_hl_mode")
    _("enter_top_hl_mode")
    _("enter_vertical_hl_mode")
    _("set_a_attributes")
    _("set_pglen_inch")
    _("termcap_init2")
    _("termcap_reset")
    _("linefeed_if_not_lf")
    _("backspace_if_not_bs")
    _("other_non_function_keys")
    _("arrow_key_map")
    _("acs_ulcorner")
    _("acs_llcorner")
    _("acs_urcorner")
    _("acs_lrcorner")
    _("acs_ltee")
    _("acs_rtee")
    _("acs_btee")
    _("acs_ttee")
    _("acs_hline")
    _("acs_vline")
    _("acs_plus")
    _("memory_lock")
    _("memory_unlock")
    _("box_chars_1")
;
#undef _

static const char* GetStrtableEntry (unsigned idx, unsigned maxstr, const char* strs, unsigned strssize)
{
#if __i386__ || __x86_64__
    unsigned i = min(idx,maxstr)+1;
    do {
	__asm__("repnz\tscasb":"+D"(strs),"+c"(strssize):"a"('\0'));
    } while (--i);
#else
    const char* strsend = strs+strssize;
    for (unsigned i = min(idx,maxstr)+1; --i && strs < strsend;)
	strs += strlen(strs)+1;
#endif
    return (strs);
}

static const char* GetBooleanName (unsigned i)
    { return (GetStrtableEntry (i, NBooleans, c_BooleanNames, sizeof(c_BooleanNames))); }
static const char* GetNumberName (unsigned i)
    { return (GetStrtableEntry (i, NNumbers, c_NumberNames, sizeof(c_NumberNames))); }
static const char* GetStringName (unsigned i)
    { return (GetStrtableEntry (i, NStrings, c_StringNames, sizeof(c_StringNames))); }

//}}}-------------------------------------------------------------------
