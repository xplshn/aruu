/* Copyright (c) 1985 Ceriel J.H. Jacobs */

#include "in_all.h"
#include "commands.h"
#include "output.h"
#include "process.h"
#include "help.h"
#include "term.h"
#include "prompt.h"
#include "getline.h"
#include "getcomm.h"
#include "pattern.h"
#include "display.h"
#include "options.h"
#include "machine.h"
#include "keys.h"
#include "main.h"
#include "assert.h"

static long lastcount; /* Save last count for '.' command */
static int lastcomm;   /* Save last command for '.' command */
static int searchdir;  /* Direction of last search */

static int do_nocomm(long cnt);
static void do_search(char *str, long cnt, int dir);
static int do_fsearch(long cnt);
static int do_bsearch(long cnt);
static int n_or_rn_search(long cnt, int dir);
static int do_nsearch(long cnt);
static int do_rnsearch(long cnt);
static int shell_command(int esc_ch, long cnt);
static int do_shell(long cnt);
static int do_pipe(long cnt);
static int do_writefile(long cnt);
static int do_absolute(long cnt);
static int do_visit(long cnt);
static int do_error(long cnt);
static int prev_screen(int sz, int really);
static int next_screen(int sz, int really);
static int do_redraw(long cnt);
static int page_size(unsigned cnt);
static int do_forward(long cnt);
static int do_backward(long cnt);
static int do_firstline(long cnt);
static int do_lline(long cnt);
static int do_lf(long cnt);
static int do_upline(long cnt);
static int do_skiplines(long cnt);
static int do_bskiplines(long cnt);
static int do_fscreens(long cnt);
static int do_bscreens(long cnt);
static int scro_size(unsigned cnt);
static int do_f_scroll(long cnt);
static int do_b_scroll(long cnt);
static int do_previousfile(long cnt);
static int do_nextfile(long cnt);
static int do_lcomm(long cnt);
static int do_quit(long cnt);

static int
do_nocomm(long cnt)
{ /* Do nothing */
	(void)cnt;
	return 0;
}

int
do_chkm(long cnt)
{ /* Change key map */
	struct keymap *p;
	(void)cnt;

	if (!(p = othermap)) {
		error("No other keymap");
		return 0;
	}
	othermap = currmap;
	currmap = p;
	return 0;
}

/*
 * Perform searches
 */

static void
do_search(char *str, long cnt, int dir)
{
	char *p;
	long lineno;

	if (str) {
		/*
		 * We have to get a pattern, which we have to prompt for
		 * with the string "str".
		 */
		if ((p = readline(str)) == 0) {
			/*
			 * User cancelled command
			 */
			return;
		}
		if ((p = re_comp(p))) {
			/*
			 * There was an error in the pattern
			 */
			error(p);
			return;
		}
		searchdir = dir;
	}
	if (dir < 0)
		lineno = scr_info.firstline;
	else
		lineno = scr_info.lastline;
	for (;;) {
		p = 0;
		if ((lineno += dir) > 0)
			p = getline(lineno, 0);
		if (interrupt)
			return;
		if (!p) { /* End of file reached */
			error("pattern not found");
			return;
		}
		if (re_exec(p) && --cnt <= 0) {
			/*
			 * We found the pattern, and we found it often enough.
			 * Pity that we still don't know where the match is.
			 * We only know the linenumber. So, we just hope the
			 * following will at least bring it on the screen ...
			 */
			(void)display(lineno, 0, pagesize, 0);
			(void)scrollb(2, 0);
			redraw(0);
			return;
		}
	}
	/* NOTREACHED */
}

static int
do_fsearch(long cnt)
{ /* Forward search */
	do_search("/", cnt, 1);
	return 0;
}

static int
do_bsearch(long cnt)
{ /* Backward search */
	do_search("?", cnt, -1);
	return 0;
}

/*
 * Repeat last search in direction "dir"
 */

static int
n_or_rn_search(long cnt, int dir)
{
	char *p;

	if (dir == 1) {
		p = "/\r";
	} else if (dir == -1) {
		p = "?\r";
	} else {
		error("No previous pattern");
		return 0;
	}
	if (!stupid)
		clrbline();
	putline(p);
	flush();
	do_search((char *)0, cnt, dir);
	return 0;
}

static int
do_nsearch(long cnt)
{ /* Repeat search in same direction */
	n_or_rn_search(cnt, searchdir);
	return 0;
}

static int
do_rnsearch(long cnt)
{ /* Repeat search in opposite direction */
	n_or_rn_search(cnt, -searchdir);
	return 0;
}

static int
shell_command(int esc_ch, long cnt)
{
	char *p;
	static char buf[2];

	buf[0] = esc_ch;
	buf[1] = '\0';
	if ((p = readline(buf)) != 0) {
		shellescape(p, esc_ch);
		if (cnt >= 0 && !hardcopy) {
			p = startcomm;
			startcomm = 0;
			ret_to_continue();
			putline(TI);
			if (!p) {
				/*
				 * Avoid double redraw.
				 * After a "startcomm", a redraw will
				 * take place anyway.
				 */
				redraw(1);
			}
		}
	}
	return 0;
}

static int
do_shell(long cnt)
{ /* Execute a shell escape */
	return shell_command('!', cnt);
}

static int
do_pipe(long cnt)
{ /* Execute a shell escape */
	return shell_command('|', cnt);
}

static int
do_writefile(long cnt)
{ /* Write input to a file */
	char *p;
	int fd;
	(void)cnt;

	if ((p = readline("Filename: ")) == 0 || !*p) {
		/*
		 * No file name given
		 */
		return 0;
	}
	if ((fd = open(p, O_CREAT | O_EXCL | O_WRONLY, 0644)) < 0) {
		if (errno == EEXIST) {
			error("File exists");
			return 0;
		}
		error("Could not open file");
		return 0;
	}
	wrt_fd(fd);
	(void)close(fd);
	return 0;
}

void
wrt_fd(int fd)
{
	long l = 1;
	char *p = getline(l, 0), *pbuf;
	char buf[1024];

	while (p) {
		pbuf = buf;
		while (p && pbuf < &buf[1024]) {
			if (!*p) {
				*pbuf++ = '\n';
				p = getline(++l, 0);
			} else
				*pbuf++ = *p++;
		}
		if (write(fd, buf, pbuf - buf) < 0) {
			error("Write failed");
			break;
		}
	}
}

static int
do_absolute(long cnt)
{ /* Go to linenumber "cnt" */

	if (!getline(cnt, 0)) { /* Not there or interrupt */
		if (!interrupt) {
			/*
			 * User did'nt give an interrupt, so the line number
			 * was too high. Go to the last line.
			 */
			do_lline(cnt);
		}
		return 0;
	}
	(void)display(cnt, 0, pagesize, 1);
	return 0;
}

static int
do_visit(long cnt)
{ /* Visit a file */
	char *p;
	static char fn[128]; /* Keep file name */
	(void)cnt;

	if ((p = readline("Filename: ")) == 0) {
		return 0;
	}
	if (*p) {
		(void)strcpy(fn, p);
		visitfile(fn);
	} else {
		/*
		 * User typed a return. Visit the current file
		 */
		if (!(p = filenames[filecount])) {
			error("No current file");
			return 0;
		}
		visitfile(p);
	}
	(void)display(1L, 0, pagesize, 1);
	return 0;
}

static int
do_error(long cnt)
{ /* Called when user types wrong key sequence */
	(void)cnt;
	error(currmap->k_help);
	return 0;
}

/*
 * Interface routine for displaying previous screen,
 * depending on cflag.
 */

static int
prev_screen(int sz, int really)
{
	int retval;

	retval = scrollb(sz - 1, really && cflag);
	if (really && !cflag) {
		/*
		 * The previous call did not display anything, but at least we
		 * know where to start
		 */
		return display(scr_info.firstline, scr_info.nf, sz, 1);
	}
	return retval;
}

/*
 * Interface routine for displaying the next screen,
 * dependent on cflag.
 */

static int
next_screen(int sz, int really)
{
	int t;
	struct scr_info *p = &scr_info;

	if (cflag) {
		return scrollf(sz - 1, really);
	}
	t = p->tail->cnt - 1;
	if (p->lastline == p->firstline) {
		t += p->nf;
	}
	return display(p->lastline, t, sz, really);
}

static int
do_redraw(long cnt)
{
	(void)cnt;
	redraw(1);
	return 0;
}

static int
page_size(unsigned cnt)
{

	if (cnt) {
		if (cnt > (unsigned)maxpagesize)
			return maxpagesize;
		if (cnt < MINPAGESIZE)
			return MINPAGESIZE;
		return (int)cnt;
	}
	return pagesize;
}

static int
do_forward(long cnt)
{ /* Display next page */
	int i;

	i = page_size((unsigned)cnt);
	if (status & EOFILE) {
		/*
		 * May seem strange, but actually a visit to the next file
		 * has already been done here
		 */
		(void)display(1L, 0, i, 1);
		return 0;
	}
	(void)next_screen(i, 1);
	return 0;
}

static int
do_backward(long cnt)
{
	int i, temp;

	i = page_size((unsigned)cnt);
	if (!(status & START)) {
		(void)prev_screen(i, 1);
		return 0;
	}
	if (stdf < 0) {
		(void)display(1L, 0, i, 1);
		return 0;
	}
	/*
	 * The next part is a bit clumsy.
	 * We want to display the last page of the previous file (for which
	 * a visit has already been done), but the pagesize may temporarily
	 * be different because the command had a count
	 */
	temp = pagesize;
	pagesize = i;
	do_lline(cnt);
	pagesize = temp;
	return 0;
}

static int
do_firstline(long cnt)
{ /* Go to start of input */
	(void)cnt;
	do_absolute(1L);
	return 0;
}

static int
do_lline(long cnt)
{ /* Go to end of input */
	int i = 0;
	int j = pagesize - 1;

	if ((cnt = to_lastline()) < 0) {
		/*
		 * Interrupted by the user
		 */
		return 0;
	}
	/*
	 * Display the page such that only the last line of the page is
	 * a "~", independant of the pagesize
	 */
	while (!(display(cnt, i, j, 0) & EOFILE)) {
		/*
		 * The last line could of course be very long ...
		 */
		i += j;
	}
	(void)scrollb(j - scr_info.tail->cnt, 0);
	redraw(0);
	return 0;
}

static int
do_lf(long cnt)
{ /* Display next line, or go to line */

	if (cnt) { /* Go to line */
		do_absolute(cnt);
		return 0;
	}
	(void)scrollf(1, 1);
	return 0;
}

static int
do_upline(long cnt)
{ /* Display previous line, or go to line */

	if (cnt) { /* Go to line */
		do_absolute(cnt);
		return 0;
	}
	(void)scrollb(1, 1);
	return 0;
}

static int
do_skiplines(long cnt)
{ /* Skip lines forwards */

	/* Should be interruptable ... */
	(void)scrollf((int)(cnt + maxpagesize - 1), 0);
	redraw(0);
	return 0;
}

static int
do_bskiplines(long cnt)
{ /* Skip lines backwards */

	/* Should be interruptable ... */
	(void)scrollb((int)(cnt + pagesize - 1), 0);
	redraw(0);
	return 0;
}

static int
do_fscreens(long cnt)
{ /* Skip screens forwards */

	do {
		if ((next_screen(pagesize, 0) & EOFILE) || interrupt)
			break;
	} while (--cnt >= 0);
	redraw(0);
	return 0;
}

static int
do_bscreens(long cnt)
{ /* Skip screens backwards */

	do {
		if ((prev_screen(pagesize, 0) & START) || interrupt)
			break;
	} while (--cnt >= 0);
	redraw(0);
	return 0;
}

static int
scro_size(unsigned cnt)
{

	if (cnt >= (unsigned)maxpagesize)
		return maxpagesize;
	if (cnt)
		return (int)cnt;
	return scrollsize;
}

static int
do_f_scroll(long cnt)
{ /* Scroll forwards */

	(void)scrollf(scro_size((unsigned)cnt), 1);
	return 0;
}

static int
do_b_scroll(long cnt)
{ /* Scroll backwards */

	(void)scrollb(scro_size((unsigned)cnt), 1);
	return 0;
}

static int
do_previousfile(long cnt)
{ /* Visit previous file */

	if (nextfile(-(int)cnt)) {
		error("No (Nth) previous file");
		return 0;
	}
	redraw(0);
	return 0;
}

static int
do_nextfile(long cnt)
{ /* Visit next file */

	if (nextfile((int)cnt)) {
		error("No (Nth) next file");
		return 0;
	}
	redraw(0);
	return 0;
}

static int
do_quit(long cnt)
{
	(void)cnt;
	return quit();
}

/*
 * The next array is initialized, sorted on the first element of the structs,
 * so that we can perform binary search
 */
struct commands commands[] = {
    {"", 0, do_error, ""},
    {"", 0, do_nocomm, ""},
    {"bf", STICKY | NEEDS_COUNT,
     do_previousfile, "Visit previous file"},
    {"bl", NEEDS_SCREEN | STICKY,
     do_upline, "Scroll one line up, or go to line"},
    {"bot", STICKY,
     do_lline, "Go to last line of the input"},
    {"bp", BACK | NEEDS_SCREEN | TOPREVFILE | STICKY,
     do_backward, "display previous page"},
    {"bps", SCREENSIZE_ADAPT | BACK | NEEDS_SCREEN | TOPREVFILE | STICKY,
     do_backward, "Display previous page, set pagesize"},
    {"bs", BACK | NEEDS_SCREEN | STICKY,
     do_b_scroll, "Scroll backwards"},
    {"bse", 0, do_bsearch, "Search backwards for pattern"},
    {"bsl", BACK | NEEDS_SCREEN | STICKY | NEEDS_COUNT,
     do_bskiplines, "Skip lines backwards"},
    {"bsp", BACK | NEEDS_SCREEN | STICKY | NEEDS_COUNT,
     do_bscreens, "Skip screens backwards"},
    {"bss", SCROLLSIZE_ADAPT | BACK | NEEDS_SCREEN | STICKY,
     do_b_scroll, "Scroll backwards, set scrollsize"},
    {"chm", 0, do_chkm, "Switch to other keymap"},
    {"exg", STICKY, exgmark, "Exchange current page with mark"},
    {"ff", STICKY | NEEDS_COUNT,
     do_nextfile, "Visit next file"},
    {"fl", NEEDS_SCREEN | STICKY,
     do_lf, "Scroll one line down, or go to line"},
    {"fp", TONEXTFILE | AHEAD | STICKY,
     do_forward, "Display next page"},
    {"fps", SCREENSIZE_ADAPT | TONEXTFILE | AHEAD | STICKY,
     do_forward, "Display next page, set pagesize"},
    {"fs", AHEAD | NEEDS_SCREEN | STICKY,
     do_f_scroll, "Scroll forwards"},
    {"fse", 0, do_fsearch, "Search forwards for pattern"},
    {"fsl", AHEAD | NEEDS_SCREEN | STICKY | NEEDS_COUNT,
     do_skiplines, "Skip lines forwards"},
    {"fsp", AHEAD | NEEDS_SCREEN | STICKY | NEEDS_COUNT,
     do_fscreens, "Skip screens forwards"},
    {"fss", SCROLLSIZE_ADAPT | AHEAD | NEEDS_SCREEN | STICKY,
     do_f_scroll, "Scroll forwards, set scrollsize"},
    {"hlp", 0, do_help, "Give description of all commands"},
    {"mar", 0, setmark, "Set a mark on the current page"},
    {"nse", STICKY, do_nsearch, "Repeat the last search"},
    {"nsr", STICKY, do_rnsearch, "Repeat last search in other direction"},
    {"pip", ESC, do_pipe, "pipe input into shell command"},
    {"qui", 0, do_quit, "Exit from yap"},
    {"red", 0, do_redraw, "Redraw screen"},
    {"rep", 0, do_lcomm, "Repeat last command"},
    {"shl", ESC, do_shell, "Execute a shell escape"},
    {"tom", 0, tomark, "Go to mark"},
    {"top", STICKY, do_firstline, "Go to the first line of the input"},
    {"vis", 0, do_visit, "Visit a file"},
    {"wrf", 0, do_writefile, "Write input to a file"},
};

/*
 * Lookup string "s" in the commands array, and return index.
 * return 0 if not found.
 */

int
lookup(char *s)
{
	struct commands *l, *u, *m;

	l = &commands[2];
	u = &commands[sizeof(commands) / sizeof(*u) - 1];
	do {
		/*
		 * Perform binary search
		 */
		m = l + (u - l) / 2;
		if (strcmp(s, m->c_cmd) > 0)
			l = m + 1;
		else
			u = m;
	} while (l < u);
	if (!strcmp(s, u->c_cmd))
		return u - commands;
	return 0;
}

/*ARGSUSED*/
static int
do_lcomm(long cnt)
{ /* Repeat last command */
	(void)cnt;

	if (!lastcomm) {
		error("No previous command");
		return 0;
	}
	do_comm(lastcomm, lastcount);
	return 0;
}

/*
 * Execute a command, with optional count "count".
 */

void
do_comm(int comm, long count)
{
	struct commands *pcomm;
	int temp;
	int flags;

	pcomm = &commands[comm];
	flags = pcomm->c_flags;

	/*
	 * Check the command.
	 * If the last line of the file is displayed and the command goes
	 * forwards and does'nt have the ability to go to the next file, it
	 * is an error.
	 * If the first line of the file is displayed and the command goes
	 * backwards and does'nt have the ability to go to the previous file,
	 * it is an error.
	 * Also check wether we need the next or previous file. If so, get it.
	 */
	if ((status & EOFILE) && (flags & AHEAD)) {
		if (qflag || !(flags & TONEXTFILE))
			return;
		if (nextfile(1))
			quit();
	}
	if ((status & START) && (flags & BACK)) {
		if (qflag || !(flags & TOPREVFILE))
			return;
		if (nextfile(-1))
			quit();
	}
	/*
	 * Does the command stick around for LASTCOMM?
	 */
	if (flags & STICKY) {
		lastcomm = comm;
		lastcount = count;
	}
	if (!count) {
		if (flags & NEEDS_COUNT)
			count = 1;
	} else {
		/*
		 * Does the command adapt the screensize?
		 */
		if (flags & SCREENSIZE_ADAPT) {
			temp = maxpagesize;
			if ((unsigned)count < (unsigned)temp) {
				temp = (int)count;
			}
			if (temp < MINPAGESIZE) {
				temp = MINPAGESIZE;
			}
			count = 0;
			pagesize = temp;
		}
		/*
		 * Does the command adapt the scrollsize?
		 */
		if (flags & SCROLLSIZE_ADAPT) {
			temp = maxpagesize - 1;
			if ((unsigned)count < (unsigned)temp) {
				temp = (int)count;
			}
			scrollsize = temp;
			count = 0;
		}
	}
	/*
	 * Now execute the command.
	 */
	(*(pcomm->c_func))(count);
}
