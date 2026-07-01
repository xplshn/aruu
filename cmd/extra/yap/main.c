/* Copyright (c) 1985 Ceriel J.H. Jacobs */

#include "in_all.h"
#include "main.h"
#include "term.h"
#include "options.h"
#include "output.h"
#include "process.h"
#include "commands.h"
#include "display.h"
#include "prompt.h"

int nopipe;
char *progname;
int interrupt;
int no_tty;

static int initialize(int x);
static void sigquit(int signo);
#ifdef SIGTSTP
static void suspsig(int signo);
#endif

int
main(int argc, char **argv)
{
	char **av;

	(void)setlocale(LC_CTYPE, "");
	if (!isatty(1)) {
		no_tty = 1;
	}
	argv[argc] = 0;
	progname = argv[0];
	if ((av = readoptions(argv)) == (char **)0 ||
	    initialize(*av ? 1 : 0)) {
		if (no_tty) {
			close(1);
			(void)dup(2);
		}
		putline("Usage: ");
		putline(argv[0]);
		putline(
		    " [-c] [-u] [-n] [-q] [-number] [+command] [file ... ]\n");
		flush();
		exit(1);
	}
	if (no_tty) {
		*--av = "cat";
		execve("/bin/cat", av, (char *const[]){NULL});
	} else
		processfiles(argc - (av - argv), av);
	(void)quit();
	/* NOTREACHED */
}

/*
 * Open temporary file for reading and writing.
 * Panic if it fails
 */

/*
 * Collect initializing stuff here.
 */

static int
initialize(int x)
{

	if (!(nopipe = x)) {
		/*
		 * Reading from pipe
		 */
		if (isatty(0)) {
			return 1;
		}
		stdf = dup(0); /* Duplicate file descriptor of input */
		if (no_tty)
			return 0;
		/*
		 * Make sure standard input is from the terminal.
		 */
		(void)close(0);
		if (open("/dev/tty", O_RDONLY, 0) != 0) {
			putline("Couldn't open terminal\n");
			flush();
			exit(1);
		}
	}
	if (no_tty)
		return 0;
	/*
	 * Handle signals.
	 * Catch QUIT, DELETE and ^Z
	 */
	(void)signal(SIGQUIT, SIG_IGN);
	(void)signal(SIGINT, catchdel);
	ini_terminal();
#ifdef SIGTSTP
	if (signal(SIGTSTP, SIG_IGN) == SIG_DFL) {
		(void)signal(SIGTSTP, suspsig);
	}
#endif
	(void)signal(SIGQUIT, sigquit);
	return 0;
}

void
catchdel(int signo)
{
	(void)signo;
	(void)signal(SIGINT, catchdel);
	interrupt = 1;
}

static void
sigquit(int signo)
{
	(void)signo;
	quit();
}

#ifdef SIGTSTP

/*
 * We had a SIGTSTP signal.
 * Suspend, by a call to this routine.
 */

void
suspend()
{

	nflush();
	resettty();
	(void)signal(SIGTSTP, SIG_DFL);
	(void)kill(0, SIGTSTP);
	/*
	 * We are not here anymore ...
	 *

	 *
	 * But we arive here ...
	 */
	inittty();
	putline(TI);
	flush();
	(void)signal(SIGTSTP, suspsig);
}

/*
 * SIGTSTP signal handler.
 * Just indicate that we had one, ignore further ones and return.
 */

static void
suspsig(int signo)
{
	(void)signo;

	suspend();
	if (DoneSetJmp)
		longjmp(SetJmpBuf, 1);
}
#endif

/*
 * quit : called on exit.
 * I bet you guessed that much.
 */

int
quit()
{

	clrbline();
	resettty();
	flush();
	exit(0);
}

/*
 * Exit, but nonvoluntarily.
 * At least tell the user why.
 */

void
panic(char *s)
{

	putline("\a\a\a\r\n");
	putline(s);
	putline("\r\n");
	quit();
}
