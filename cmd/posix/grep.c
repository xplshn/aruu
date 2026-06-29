
#include "config.h"
#include "queue.h"
#include "util.h"

#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

enum { Match = 0, NoMatch = 1, Error = 2 };

static void addpattern(const char *);
static void addpatternfile(FILE *);
static int grep(FILE *, const char *);

static int Eflag;
static int Fflag;
static int Hflag;
static int eflag;
static int fflag;
static int hflag;
static int iflag;
static int sflag;
static int vflag;
static int wflag;
static int xflag;
static int many;
static int mode;
#if FEATURE_GREP_CONTEXT
static long Aflag = 0;
static long Bflag = 0;
#endif
#if FEATURE_GREP_MAX_COUNT
static long mval = -1;
#endif

struct pattern {
	regex_t preg;
	SLIST_ENTRY(pattern) entry;
	char pattern[];
};

static SLIST_HEAD(phead, pattern) phead;

static void
addpattern(const char *pattern)
{
	struct pattern *pnode;
	size_t patlen;

	patlen = strlen(pattern);

	pnode = enmalloc(Error, sizeof(*pnode) + patlen + 9);
	SLIST_INSERT_HEAD(&phead, pnode, entry);

	if (Fflag || (!xflag && !wflag)) {
		memcpy(pnode->pattern, pattern, patlen + 1);
	} else {
		sprintf(pnode->pattern, "%s%s%s%s%s",
			xflag ? "^" : "\\<",
			Eflag ? "(" : "\\(",
			pattern,
			Eflag ? ")" : "\\)",
			xflag ? "$" : "\\>");
	}
}

static void
addpatternfile(FILE *fp)
{
	static char *buf = NULL;
	static size_t size = 0;
	ssize_t len = 0;

	while ((len = getline(&buf, &size, fp)) > 0) {
		if (buf[len - 1] == '\n')
			buf[len - 1] = '\0';
		addpattern(buf);
	}
	if (ferror(fp))
		enprintf(Error, "read error:");
}

#if FEATURE_GREP_CONTEXT
static void
print_line(const char *str, const char *line, long line_no, char sep)
{
	if (!hflag && (many || Hflag))
		printf("%s%c", str, sep);
	if (mode == 'n')
		printf("%ld%c", line_no, sep);
	puts(line);
}
#endif

static int
grep(FILE *fp, const char *str)
{
	static char *buf = NULL;
	static size_t size = 0;
	ssize_t len = 0;
	long c = 0, n;
	struct pattern *pnode;
	int match, result = NoMatch;
#if FEATURE_GREP_MAX_COUNT
	long matches = 0;
#endif
#if FEATURE_GREP_CONTEXT
	struct context_line {
		char *str;
		long line_no;
	} *before_buf = NULL;
	size_t before_head = 0, before_count = 0, i = 0, idx = 0;
	long after_left = 0;
	long last_printed_line = 0;

	if (Bflag > 0 && !(mode == 'c' || mode == 'l' || mode == 'q'))
		before_buf = ecalloc(Bflag, sizeof(*before_buf));
#endif

	for (n = 1; (len = getline(&buf, &size, fp)) > 0; n++) {
		/* remove the trailing newline if one is present */
		if (buf[len - 1] == '\n')
			buf[len - 1] = '\0';
		match = 0;
		SLIST_FOREACH(pnode, &phead, entry) {
			if (Fflag) {
				if (xflag) {
					if (!(iflag ? strcasecmp : strcmp)(buf, pnode->pattern)) {
						match = 1;
						break;
					}
				} else {
					if ((iflag ? strcasestr : strstr)(buf, pnode->pattern)) {
						match = 1;
						break;
					}
				}
			} else {
				if (regexec(&pnode->preg, buf, 0, NULL, 0) == 0) {
					match = 1;
					break;
				}
			}
		}
		if (match != vflag) {
			result = Match;
#if FEATURE_GREP_MAX_COUNT
			matches++;
#endif
			switch (mode) {
			case 'c':
				c++;
				break;
			case 'l':
				puts(str);
				goto end;
			case 'q':
				exit(Match);
			default:
#if FEATURE_GREP_CONTEXT
				if (Aflag > 0 || Bflag > 0) {
					if (last_printed_line > 0 && n > last_printed_line + 1)
						puts("--");
					for (i = 0; i < before_count; i++) {
						idx = (before_head - before_count + i + Bflag) % Bflag;
						print_line(str, before_buf[idx].str, before_buf[idx].line_no, '-');
						free(before_buf[idx].str);
						before_buf[idx].str = NULL;
					}
					before_count = 0;
					before_head = 0;
					print_line(str, buf, n, ':');
					after_left = Aflag;
					last_printed_line = n;
				} else {
#endif
					if (!hflag && (many || Hflag))
						printf("%s:", str);
					if (mode == 'n')
						printf("%ld:", n);
					puts(buf);
#if FEATURE_GREP_CONTEXT
				}
#endif
				break;
			}
#if FEATURE_GREP_MAX_COUNT
			if (mval >= 0 && matches >= mval)
				goto end;
#endif
		}
#if FEATURE_GREP_CONTEXT
		else if (Aflag > 0 || Bflag > 0) {
			if (mode != 'c' && mode != 'l' && mode != 'q') {
				if (after_left > 0) {
					print_line(str, buf, n, '-');
					after_left--;
					last_printed_line = n;
				}
				if (Bflag > 0) {
					if (before_count == (size_t)Bflag)
						free(before_buf[before_head].str);
					before_buf[before_head].str = estrdup(buf);
					before_buf[before_head].line_no = n;
					before_head = (before_head + 1) % Bflag;
					if (before_count < (size_t)Bflag)
						before_count++;
				}
			}
		}
#endif
	}
	if (mode == 'c')
		printf("%ld\n", c);
end:
#if FEATURE_GREP_CONTEXT
	if (before_buf) {
		for (i = 0; i < (size_t)Bflag; i++)
			free(before_buf[i].str);
		free(before_buf);
	}
#endif
	if (ferror(fp)) {
		weprintf("%s: read error:", str);
		result = Error;
	}
	return result;
}

static void
usage(void)
{
	enprintf(Error, "usage: %s [-EFHchilnqsvwx]"
#if FEATURE_GREP_CONTEXT
	         " [-A num] [-B num] [-C num]"
#endif
#if FEATURE_GREP_MAX_COUNT
	         " [-m num]"
#endif
	         " [-e pattern] [-f file] [pattern] [file ...]\n", argv0);
}

// ?man grep: search files for a pattern
// ?man arguments: pattern [file ...]
// ?man grep searches the named input files for lines matching the given pattern
// ?man if no files are named, or a file is -, standard input is searched
// ?man by default, matching lines are written to standard output
int
main(int argc, char *argv[])
{
	struct pattern *pnode;
	int m, flags = REG_NOSUB, match = NoMatch;
	FILE *fp;
	char *arg;

	SLIST_INIT(&phead);

	ARGBEGIN {
#if FEATURE_GREP_CONTEXT
	// ?man -A:num: specify A option
	case 'A':
		// ?man -A num: print num lines of trailing context after each match
		Aflag = estrtonum(EARGF(usage()), 0, LONG_MAX);
		break;
	// ?man -B:num: specify B option
	case 'B':
		// ?man -B num: print num lines of leading context before each match
		Bflag = estrtonum(EARGF(usage()), 0, LONG_MAX);
		break;
	// ?man -C:num: specify C option
	case 'C':
		// ?man -C num: print num lines of context before and after each match; equivalent to -A num -B num
		Aflag = Bflag = estrtonum(EARGF(usage()), 0, LONG_MAX);
		break;
	// ?man ARGNUM: specify RGNUM option
	ARGNUM:
		Aflag = Bflag = ARGNUMF();
		break;
#endif
#if FEATURE_GREP_MAX_COUNT
	// ?man -m:num: specify m option
	case 'm':
		// ?man -m num: stop reading a file after num matching lines
		mval = estrtonum(EARGF(usage()), 0, LONG_MAX);
		break;
#endif
	// ?man -E: specify E option
	case 'E':
		// ?man -E: interpret pattern as an extended regular expression
		Eflag = 1;
		Fflag = 0;
		flags |= REG_EXTENDED;
		break;
	// ?man -F: specify F option
	case 'F':
		// ?man -F: interpret pattern as a list of fixed strings separated by newlines
		Fflag = 1;
		Eflag = 0;
		flags &= ~REG_EXTENDED;
		break;
	// ?man -H: specify H option
	case 'H':
		// ?man -H: always print the file name with matching lines
		Hflag = 1;
		hflag = 0;
		break;
	// ?man -e:file: specify e option
	case 'e':
		// ?man -e pattern: specify a pattern to match; may be given multiple times
		arg = EARGF(usage());
		if (!(fp = fmemopen(arg, strlen(arg) + 1, "r")))
			eprintf("fmemopen:");
		addpatternfile(fp);
		efshut(fp, arg);
		eflag = 1;
		break;
	// ?man -f:file: specify f option
	case 'f':
		// ?man -f file: read patterns from file, one per line
		arg = EARGF(usage());
		fp = fopen(arg, "r");
		if (!fp)
			enprintf(Error, "fopen %s:", arg);
		addpatternfile(fp);
		efshut(fp, arg);
		fflag = 1;
		break;
	// ?man -h: specify h option
	case 'h':
		// ?man -h: never print file names with matching lines
		hflag = 1;
		Hflag = 0;
		break;
	// ?man -c: specify c option
	case 'c':
		// ?man -c: print only a count of matching lines per file
		/* FALLTHROUGH */
	// ?man -l: specify l option
	case 'l':
		// ?man -l: print only the names of files with at least one matching line
		/* FALLTHROUGH */
	// ?man -n: specify n option
	case 'n':
		// ?man -n: prefix each matching line with its line number within its file
		/* FALLTHROUGH */
	// ?man -q: specify q option
	case 'q':
		// ?man -q: quiet mode; exit immediately with status 0 on first match and write nothing
		mode = ARGC();
		break;
	// ?man -i: specify i option
	case 'i':
		// ?man -i: perform case-insensitive matching
		flags |= REG_ICASE;
		iflag = 1;
		break;
	// ?man -s: specify s option
	case 's':
		// ?man -s: suppress error messages about nonexistent or unreadable files
		sflag = 1;
		break;
	// ?man -v: specify v option
	case 'v':
		// ?man -v: invert the sense of matching to select non-matching lines
		vflag = 1;
		break;
	// ?man -w: specify w option
	case 'w':
		// ?man -w: match only whole words
		wflag = 1;
		break;
	// ?man -x: specify x option
	case 'x':
		// ?man -x: match only whole lines
		xflag = 1;
		break;
	default:
		usage();
	} ARGEND

	if (argc == 0 && !eflag && !fflag)
		usage(); /* no pattern */

	/* just add literal pattern to list */
	if (!eflag && !fflag) {
		if (!(fp = fmemopen(argv[0], strlen(argv[0]) + 1, "r")))
			eprintf("fmemopen:");
		addpatternfile(fp);
		efshut(fp, argv[0]);
		argc--;
		argv++;
	}

	if (!Fflag)
		/* compile regex for all search patterns */
		SLIST_FOREACH(pnode, &phead, entry)
			enregcomp(Error, &pnode->preg, pnode->pattern, flags);
	many = (argc > 1);
	if (argc == 0) {
		match = grep(stdin, "<stdin>");
	} else {
		for (; *argv; argc--, argv++) {
			if (!strcmp(*argv, "-")) {
				*argv = "<stdin>";
				fp = stdin;
			} else if (!(fp = fopen(*argv, "r"))) {
				if (!sflag)
					weprintf("fopen %s:", *argv);
				match = Error;
				continue;
			}
			m = grep(fp, *argv);
			if (m == Error || (match != Error && m == Match))
				match = m;
			if (fp != stdin && fshut(fp, *argv))
				match = Error;
		}
	}

	if (fshut(stdin, "<stdin>") | fshut(stdout, "<stdout>"))
		match = Error;

	// ?man
	// ?man ## Exit status
	// ?man
	// ?man 0
	// ?man : one or more lines matched in at least one file
	// ?man
	// ?man 1
	// ?man : no lines matched in any file
	// ?man
	// ?man 2
	// ?man : an error occurred
	// ?man
	// ?man ## See also
	// ?man
	// ?man sed(1), awk(1)
	// ?man

	return match;
}
