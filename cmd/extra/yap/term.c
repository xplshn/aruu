/* Copyright (c) 1985 Ceriel J.H. Jacobs */

/*
 * Terminal handling routines, mostly initializing.
 */

#include "in_all.h"
#include "term.h"
#include "machine.h"
#include "output.h"
#include "display.h"
#include "options.h"
#include "getline.h"
#include "keys.h"
#include "main.h"
#define getline yap_getline
#define getch yap_getch
#include <term.h>
#undef getch
#undef getline
#undef insert_line

int expandtabs;
int stupid;
int hardcopy;
char *CE, *CL, *SO, *SE, *US, *UE, *UC, *MD, *ME, *TI, *TE, *CM, *TA, *SR, *AL;
char *UP, *HO, *BO;
int LINES, COLS, AM, XN, DB;
int erasech, killch;
struct state *sppat;
char *BC;

#ifdef TIOCGWINSZ
static struct winsize w;
#endif

#ifdef TIOCSPGRP
static int proc_id, saved_pgrpid;
#endif

static char *ll;

struct linelist _X[100]; /* 100 is enough ? */

static struct termios _tty, _svtty;

static void
handle(cc_t *c)
{ /* if character *c is used, set it to undefined */

	if (isused(*c))
		*c = 0xff;
}

/*
 * Set terminal in cbreak mode.
 * Also check if tabs need expanding.
 */

void
inittty()
{
	struct termios *p = &_tty;

	tcgetattr(0, p);
	_svtty = *p;
	p->c_oflag &= ~OPOST;
	p->c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL | ICANON);
	if (isused('S' & 0x1f) || isused('Q' & 0x1f))
		p->c_iflag &= ~IXON;
	handle(&p->c_cc[VINTR]);
	handle(&p->c_cc[VQUIT]);
	erasech = p->c_cc[VERASE];
	killch = p->c_cc[VKILL];
	p->c_cc[VMIN] = 1; /* Just wait for one character */
	p->c_cc[VTIME] = 0;
	tcsetattr(0, TCSANOW, p);
}

void
backspace()
{
	putline(BC);
}

void
clrscreen()
{
	tputs(CL, LINES, fputch);
}

void
clrtoeol()
{
	tputs(CE, 1, fputch);
}

void
scrollreverse()
{
	tputs(SR, LINES, fputch);
}

void
standout()
{
	tputs(SO, 1, fputch);
}

void
standend()
{
	tputs(SE, 1, fputch);
}

void
underline()
{
	tputs(US, 1, fputch);
}

void
end_underline()
{
	tputs(UE, 1, fputch);
}

void
bold()
{
	tputs(MD, 1, fputch);
}

void
end_bold()
{
	tputs(ME, 1, fputch);
}

void
underchar()
{
	tputs(UC, 1, fputch);
}

void
givetab()
{
	tputs(TA, 1, fputch);
}

/*
 * Reset the terminal to its original state
 */

void
resettty()
{
	tcsetattr(0, TCSANOW, &_svtty);
	putline(TE);
	flush();
}

/*
 * Get string terminal capability "cap".
 * If not present, return an empty string.
 */

static char *
getcap(char *cap)
{
	char *s;

	s = tigetstr(cap);
	if (!s || s == (char *)-1)
		return "";
	return s;
}

static int
getflag(char *cap)
{
	int value;

	value = tigetflag(cap);
	return value < 0 ? 0 : value;
}

static int
getnumcap(char *cap)
{
	int value;

	value = tigetnum(cap);
	return value < 0 ? -1 : value;
}

/*
 * Initialize some terminal-dependent stuff.
 */

void
ini_terminal()
{

	char *s;
	struct linelist *lp, *lp1;
	int i;
	int UG, SG;
	char tempbuf[20];
	char *mb, *mh, *mr; /* attributes */
	int err;

	initkeys();
#ifdef TIOCSPGRP
	proc_id = getpid();
	ioctl(0, TIOCGPGRP, (char *)&saved_pgrpid);
#endif
	inittty();
	stupid = 1;
	BC = "\b";
	TA = "\t";
	if (!(s = getenv("TERM")))
		s = "dumb";
	if (setupterm(s, 1, &err) != 0) {
		panic("No terminfo entry");
	}
	stupid = 0;
	hardcopy = getflag("hc"); /* Hard copy terminal?*/
	if (*(s = getcap("cub1"))) {
		/*
		 * Backspace if not ^H
		 */
		BC = s;
	}
	UP = getcap("cuu1");   /* move up a line */
	CE = getcap("el");     /* clear to end of line */
	CL = getcap("clear");  /* clear screen */
	if (!*CL)
		cflag = 1;
	TI = getcap("smcup");  /* Initialization for CM */
	TE = getcap("rmcup");  /* end for CM */
	CM = getcap("cup");    /* cursor addressing */
	SR = getcap("ri");     /* scroll reverse */
	AL = getcap("il");     /* Insert line */
	SO = getcap("smso");   /* standout */
	SE = getcap("rmso");   /* standend */
	SG = getnumcap("xmc"); /* blanks left by attributes */
	if (SG < 0)
		SG = 0;
	US = getcap("smul"); /* underline */
	UE = getcap("rmul"); /* end underline */
	UG = getnumcap("xmc"); /* blanks left by attributes */
	if (UG < 0)
		UG = 0;
	UC = getcap("uc");   /* underline a character */
	mb = getcap("blink"); /* blinking attribute */
	MD = getcap("bold");  /* bold attribute */
	ME = getcap("sgr0");  /* turn off attributes */
	mh = getcap("dim");   /* half bright attribute */
	mr = getcap("rev");   /* reversed video attribute */
	if (!nflag) {
		/*
		 * Recognize special strings
		 */
		(void)addstring(SO, SG, &sppat);
		(void)addstring(SE, SG, &sppat);
		(void)addstring(US, UG, &sppat);
		(void)addstring(UE, UG, &sppat);
		(void)addstring(mb, 0, &sppat);
		(void)addstring(MD, 0, &sppat);
		(void)addstring(ME, 0, &sppat);
		(void)addstring(mh, 0, &sppat);
		(void)addstring(mr, 0, &sppat);
		if (*UC) {
			(void)strcpy(tempbuf, BC);
			(void)strcat(tempbuf, UC);
			(void)addstring(tempbuf, 0, &sppat);
		}
	}
	if (UG > 0 || uflag) {
		US = "";
		UE = "";
	}
	if (*US || uflag)
		UC = "";
	COLS = getnumcap("cols"); /* columns on page */
	i = getnumcap("lines");   /* Lines on page */
	AM = getflag("am");       /* terminal wraps automatically? */
	XN = getflag("xenl");     /* and then ignores next newline? */
	DB = getflag("db");       /* terminal retains lines below */
	HO = getcap("home");
	if (!*HO && *CM) {
		HO = tiparm(CM, 0, 0); /* Another way of getting home */
	}
	if ((!*CE && !*AL) || !*HO || hardcopy) {
		cflag = stupid = 1;
	}
	if (*(s = getcap("ht"))) {
		/*
		 * Tab (other than ^I or padding)
		 */
		TA = s;
	}
	if (!*(ll = getcap("ll")) && *CM && i > 0) {
		/*
		 * Lower left hand corner
		 */
		BO = tiparm(CM, i - 1, 0);
	} else
		BO = ll;
	if (COLS <= 0 || COLS > 256) {
		if ((unsigned)COLS >= 65409) {
			/* SUN bug */
			COLS &= 0xffff;
			COLS -= (65409 - 128);
		}
		if (COLS <= 0 || COLS > 256)
			COLS = 80;
	}
	if (i <= 0) {
		i = 24;
		cflag = stupid = 1;
	}
	LINES = i;
	maxpagesize = i - 1;
	scrollsize = maxpagesize / 2;
	if (scrollsize <= 0)
		scrollsize = 1;
	if (!pagesize || pagesize >= i) {
		pagesize = maxpagesize;
	}

	/*
	 * The next part does not really belong here, but there it is ...
	 * Initialize a circular list for the screenlines.
	 */

	scr_info.tail = lp = _X;
	lp1 = lp + (100 - 1);
	for (; lp <= lp1; lp++) {
		/*
		 * Circular doubly linked list
		 */
		lp->next = lp + 1;
		lp->prev = lp - 1;
	}
	lp1->next = scr_info.tail;
	lp1->next->prev = lp1;
	if (stupid) {
		BO = "\r\n";
	}
	putline(TI);
	window();
}

/*
 * Place cursor at start of line n.
 */

void
mgoto(int n)
{

	if (n == 0)
		home();
	else if (n == maxpagesize && *BO)
		bottom();
	else if (*CM) {
		/*
		 * Cursor addressing
		 */
		tputs(tiparm(CM, n, 0), 1, fputch);
	} else if (*BO && *UP && n >= (maxpagesize >> 1)) {
		/*
		 * Bottom and then up
		 */
		bottom();
		while (n++ < maxpagesize)
			putline(UP);
	} else { /* Home, and then down */
		home();
		while (n--)
			putline("\r\n");
	}
}

/*
 * Clear bottom line
 */

void
clrbline()
{

	if (stupid) {
		putline("\r\n");
		return;
	}
	bottom();
	if (*CE) {
		/*
		 * We can clear to end of line
		 */
		clrtoeol();
		return;
	}
	ins_line(maxpagesize);
}

void
ins_line(int l)
{
	tputs(tiparm(AL, l), maxpagesize - l, fputch);
}

void
home()
{

	tputs(HO, 1, fputch);
}

void
bottom()
{

	tputs(BO, 1, fputch);
	if (!*BO)
		mgoto(maxpagesize);
}

int
window()
{
#ifdef TIOCGWINSZ
	if (ioctl(1, TIOCGWINSZ, &w) < 0)
		return 0;

	if (w.ws_col == 0)
		w.ws_col = COLS;
	if (w.ws_row == 0)
		w.ws_row = LINES;
	if (w.ws_col != COLS || w.ws_row != LINES) {
		COLS = w.ws_col;
		LINES = w.ws_row;
		maxpagesize = LINES - 1;
		pagesize = maxpagesize;
		if (!*ll)
			BO = tiparm(CM, maxpagesize, 0);
		scr_info.currentpos = 0;
		scrollsize = maxpagesize / 2;
		if (scrollsize <= 0)
			scrollsize = 1;
		return 1;
	}
#endif
	return 0;
}
