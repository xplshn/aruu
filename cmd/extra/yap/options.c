/* Copyright (c) 1985 Ceriel J.H. Jacobs */

#include "in_all.h"
#include "options.h"
#include "output.h"
#include "display.h"
#include <ctype.h>

int cflag;
int uflag;
int nflag;
int qflag;
char *startcomm;

static int parsopt(char *s);

/*
 * Read the options. Return the argv pointer following them if there were
 * no errors, otherwise return 0.
 */

char **
readoptions(char **argv)
{

	char **av = argv + 1;
	char *p;

	if ((p = getenv("YAP")) != 0) {
		(void)parsopt(p);
	}
	while (*av && **av == '-') {
		if (parsopt(*av)) {
			/*
			 * Error in option
			 */
			putline(*av);
			putline(": illegal option\n");
			return (char **)0;
		}
		av++;
	}
	if (*av && **av == '+') {
		/*
		 * Command in command line
		 */
		startcomm = *av + 1;
		av++;
	}
	return av;
}

static int
parsopt(char *s)
{
	int i;

	if (*s == '-')
		s++;
	if (isdigit(*s)) {
		/*
		 * pagesize option
		 */
		i = 0;
		do {
			i = i * 10 + *s++ - '0';
		} while (isdigit(*s));
		if (i < MINPAGESIZE)
			i = MINPAGESIZE;
		pagesize = i;
	}
	while (*s) {
		switch (*s++) {
		case 'c':
			cflag++;
			break;
		case 'n':
			nflag++;
			break;
		case 'u':
			uflag++;
			break;
		case 'q':
			qflag++;
			break;
		default:
			return 1;
		}
	}
	return 0;
}
