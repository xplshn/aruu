/* Copyright (c) 1985 Ceriel J.H. Jacobs */

#include "in_all.h"
#include "process.h"
#include "commands.h"
#include "display.h"
#include "prompt.h"
#include "getline.h"
#include "getcomm.h"
#include "main.h"
#include "options.h"
#include "output.h"

jmp_buf SetJmpBuf;
int DoneSetJmp;
int stdf;
int filecount;
char **filenames;
char *currentfile;
long maxpos;

static int nfiles; /* Number of filenames on command line */

/*
 * Visit a file, file name is "fn".
 */

void
visitfile(char *fn)
{
	struct stat statbuf;

	if (stdf > 0) {
		/*
		 * Close old input file
		 */
		(void)close(stdf);
	}
	currentfile = fn;
	if ((stdf = open(fn, O_RDONLY, 0)) < 0) {
		error(": could not open");
		maxpos = 0;
	} else { /* Get size for percentage in prompt */
		(void)fstat(stdf, &statbuf);
		maxpos = statbuf.st_size;
	}
	do_clean();
	d_clean();
}

/*
 * process the input files, one by one.
 * If there is none, input is from a pipe.
 */

void
processfiles(int n, char **argv)
{

	static char *dummies[3];
	long arg;

	if (!(nfiles = n)) {
		/*
		 * Input from pipe
		 */
		currentfile = "standard-input";
		/*
		 * Take care that *(filenames - 1) and *(filenames + 1) are 0
		 */
		filenames = &dummies[1];
		d_clean();
		do_clean();
	} else {
		filenames = argv;
		(void)nextfile(0);
	}
	*--argv = 0;
	if (startcomm) {
		n = getcomm(&arg);
		if (commands[n].c_flags & NEEDS_SCREEN) {
			redraw(0);
		}
		do_comm(n, arg);
		startcomm = 0;
	}
	redraw(1);
	if (setjmp(SetJmpBuf)) {
		nflush();
		redraw(1);
	}
	DoneSetJmp = 1;
	for (;;) {
		interrupt = 0;
		n = getcomm(&arg);
		do_comm(n, arg);
	}
}

/*
 * Get the next file the user asks for.
 */

int
nextfile(int n)
{
	int i;

	if ((i = filecount + n) >= nfiles || i < 0) {
		return 1;
	}
	filecount = i;
	visitfile(filenames[i]);
	return 0;
}
