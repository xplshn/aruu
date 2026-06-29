/*	$OpenBSD: diff.c,v 1.67 2019/06/28 13:35:00 deraadt Exp $	*/

/*
 * Copyright (c) 2003 Todd C. Miller <Todd.Miller@courtesan.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F39502-99-1-0512.
 */

#include <sys/cdefs.h>
#include <sys/stat.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"

#include "diff.h"
#include "xmalloc.h"

#ifndef __dead2
#define __dead2 __attribute__((__noreturn__))
#endif

static const char diff_version[] = "FreeBSD diff 20240307";
bool	 lflag, Nflag, Pflag, rflag, sflag, Tflag, cflag;
bool	 ignore_file_case, suppress_common, color, noderef;
static bool help = false;
int	 diff_format, diff_context, diff_algorithm, status;
bool	 diff_algorithm_set;
int	 tabsize = 8, width = 130;
static int	colorflag = COLORFLAG_NEVER;
char	*start, *ifdefname, *diffargs, *label[2];
char	*ignore_pats, *most_recent_pat;
char	*group_format = NULL;
const char	*add_code, *del_code;
struct stat stb1, stb2;
struct excludes *excludes_list;
regex_t	 ignore_re, most_recent_re;

static struct algorithm {
	const char *name;
	int id;
} algorithms[] = {
	{"stone", D_DIFFSTONE},
	{"myers", D_DIFFMYERS},
	{"patience", D_DIFFPATIENCE},
	{NULL, D_DIFFNONE}
};

#define	OPTIONS	"0123456789A:aBbC:cdD:efF:HhI:iL:lnNPpqrS:sTtU:uwW:X:x:y"
enum {
	OPT_TSIZE = CHAR_MAX + 1,
	OPT_STRIPCR,
	OPT_IGN_FN_CASE,
	OPT_NO_IGN_FN_CASE,
	OPT_NORMAL,
	OPT_HELP,
	OPT_HORIZON_LINES,
	OPT_CHANGED_GROUP_FORMAT,
	OPT_SUPPRESS_COMMON,
	OPT_COLOR,
	OPT_NO_DEREFERENCE,
	OPT_VERSION,
};

static struct option longopts[] = {
	{ "algorithm",			required_argument,	0,	'A' },
	{ "text",			no_argument,		0,	'a' },
	{ "ignore-space-change",	no_argument,		0,	'b' },
	{ "context",			optional_argument,	0,	'C' },
	{ "ifdef",			required_argument,	0,	'D' },
	{ "minimal",			no_argument,		0,	'd' },
	{ "ed",				no_argument,		0,	'e' },
	{ "forward-ed",			no_argument,		0,	'f' },
	{ "show-function-line",		required_argument,	0,	'F' },
	{ "speed-large-files",		no_argument,		NULL,	'H' },
	{ "ignore-blank-lines",		no_argument,		0,	'B' },
	{ "ignore-matching-lines",	required_argument,	0,	'I' },
	{ "ignore-case",		no_argument,		0,	'i' },
	{ "paginate",			no_argument,		NULL,	'l' },
	{ "label",			required_argument,	0,	'L' },
	{ "new-file",			no_argument,		0,	'N' },
	{ "rcs",			no_argument,		0,	'n' },
	{ "unidirectional-new-file",	no_argument,		0,	'P' },
	{ "show-c-function",		no_argument,		0,	'p' },
	{ "brief",			no_argument,		0,	'q' },
	{ "recursive",			no_argument,		0,	'r' },
	{ "report-identical-files",	no_argument,		0,	's' },
	{ "starting-file",		required_argument,	0,	'S' },
	{ "expand-tabs",		no_argument,		0,	't' },
	{ "initial-tab",		no_argument,		0,	'T' },
	{ "unified",			optional_argument,	0,	'U' },
	{ "ignore-all-space",		no_argument,		0,	'w' },
	{ "width",			required_argument,	0,	'W' },
	{ "exclude",			required_argument,	0,	'x' },
	{ "exclude-from",		required_argument,	0,	'X' },
	{ "side-by-side",		no_argument,		NULL,	'y' },
	{ "ignore-file-name-case",	no_argument,		NULL,	OPT_IGN_FN_CASE },
	{ "help",			no_argument,		NULL,	OPT_HELP},
	{ "horizon-lines",		required_argument,	NULL,	OPT_HORIZON_LINES },
	{ "no-dereference",		no_argument,		NULL,	OPT_NO_DEREFERENCE},
	{ "no-ignore-file-name-case",	no_argument,		NULL,	OPT_NO_IGN_FN_CASE },
	{ "normal",			no_argument,		NULL,	OPT_NORMAL },
	{ "strip-trailing-cr",		no_argument,		NULL,	OPT_STRIPCR },
	{ "tabsize",			required_argument,	NULL,	OPT_TSIZE },
	{ "changed-group-format",	required_argument,	NULL,	OPT_CHANGED_GROUP_FORMAT},
	{ "suppress-common-lines",	no_argument,		NULL,	OPT_SUPPRESS_COMMON },
	{ "color",			optional_argument,	NULL,	OPT_COLOR },
	{ "version",			no_argument,		NULL,	OPT_VERSION},
	{ NULL,				0,			0,	'\0'}
};

static void checked_regcomp(char const *, regex_t *);
static void usage(void) __dead2;
static void conflicting_format(void) __dead2;
static void push_excludes(char *);
static void push_ignore_pats(char *);
static void read_excludes_file(char *file);
static void set_argstr(char **, char **);
static char *splice(char *, char *);
static bool do_color(void);

/* ?man diff: differential file and directory comparator
arguments: file1 file2
synopsis: [-aBbdipTtw] [-c|-e|-f|-n|-q|-u|-y] [-A algo] [--brief] [--color=when] [--changed-group-format GFMT] [--ed] [--expand-tabs] [--forward-ed] [--ignore-all-space] [--ignore-case] [--ignore-space-change] [--initial-tab] [--minimal] [--no-dereference] [--no-ignore-file-name-case] [--normal] [--rcs] [--show-c-function] [--starting-file] [--speed-large-files] [--strip-trailing-cr] [--tabsize number] [--text] [-I pattern] [-F pattern] [-L label] file1 file2
synopsis: [-aBbdilpTtw] [-A algo] [-I pattern] [-F pattern] [-L label] [--brief] [--color=when] [--changed-group-format GFMT] [--ed] [--expand-tabs] [--forward-ed] [--ignore-all-space] [--ignore-case] [--ignore-space-change] [--initial-tab] [--minimal] [--no-dereference] [--no-ignore-file-name-case] [--normal] [--paginate] [--rcs] [--show-c-function] [--speed-large-files] [--starting-file] [--strip-trailing-cr] [--tabsize number] [--text] -C number file1 file2
synopsis: [-aBbdiltw] [-A algo] [-I pattern] [--brief] [--color=when] [--changed-group-format GFMT] [--ed] [--expand-tabs] [--forward-ed] [--ignore-all-space] [--ignore-case] [--ignore-space-change] [--initial-tab] [--minimal] [--no-dereference] [--no-ignore-file-name-case] [--normal] [--paginate] [--rcs] [--show-c-function] [--speed-large-files] [--starting-file] [--strip-trailing-cr] [--tabsize number] [--text] -D string file1 file2
synopsis: [-aBbdilpTtw] [-A algo] [-I pattern] [-F pattern] [-L label] [--brief] [--color=when] [--changed-group-format GFMT] [--ed] [--expand-tabs] [--forward-ed] [--ignore-all-space] [--ignore-case] [--ignore-space-change] [--initial-tab] [--minimal] [--no-dereference] [--no-ignore-file-name-case] [--normal] [--paginate] [--rcs] [--show-c-function] [--speed-large-files] [--starting-file] [--strip-trailing-cr] [--tabsize number] [--text] -U number file1 file2
synopsis: [-aBbdilNPprsTtw] [-c|-e|-f|-n|-q|-u] [-A algo] [--brief] [--color=when] [--changed-group-format GFMT] [--context] [--ed] [--expand-tabs] [--forward-ed] [--ignore-all-space] [--ignore-case] [--ignore-space-change] [--initial-tab] [--minimal] [--new-file] [--no-dereference] [--no-ignore-file-name-case] [--normal] [--paginate] [--rcs] [--recursive] [--report-identical-files] [--show-c-function] [--speed-large-files] [--strip-trailing-cr] [--tabsize number] [--text] [--unidirectional-new-file] [--unified] [-I pattern] [-F pattern] [-L label] [-S name] [-X file] [-x pattern] dir1 dir2
synopsis: [-aBbditwW] [--color=when] [--expand-tabs] [--ignore-all-space] [--ignore-blank-lines] [--ignore-case] [--minimal] [--no-dereference] [--no-ignore-file-name-case] [--strip-trailing-cr] [--suppress-common-lines] [--tabsize number] [--text] -y file1 file2
synopsis: diff [--help]
synopsis: diff [--version]
The diff utility compares the contents of file1 and file2 and writes to the standard output the list of changes necessary to convert one file into the other. No output is produced if the files are identical.
## OUTPUT OPTIONS
### -C number, --context number
Like c but produces a diff with number lines of context.
### -c
Produces a diff with 3 lines of context.

With c the output format is modified slightly: the output begins with identification of the files involved and their creation dates and then each change is separated by a line with fifteen *'s. The lines removed from file1 are marked with '- '; those added to file2 are marked '+ '. Lines which are changed from one file to the other are marked in both files with '! '. Changes which lie within 3 lines of each other are grouped together on output.
### -D string, --ifdef string
Creates a merged version of file1 and file2 on the standard output, with C preprocessor controls included so that a compilation of the result without defining string is equivalent to compiling file1, while defining string will yield file2.
### -e, --ed
Produces output in a form suitable as input for the editor utility, ed(1), which can then be used to convert file1 into file2.

Extra commands are added to the output when comparing directories with e, so that the result is a sh(1) script for converting text files which are common to the two directories from their state in dir1 to their state in dir2. Note that when comparing directories with e, the resulting file may no longer be interpreted as an ed(1) script. Output is added to indicate which file each set of ed(1) commands applies to. These hunks can be manually extracted to produce an ed(1) script, which can also be applied with patch(1).
### -f, --forward-ed
Identical output to that of the e flag, but in reverse order. It cannot be digested by ed(1).
### --help
This option prints a summary to stdout and exits with status 0.
### -n
Produces a script similar to that of e, but in the opposite order and with a count of changed lines on each insert or delete command. This is the form used by rcsdiff.
### -q, --brief
Just print a line when the files differ. Does not output a list of changes.
### -U number, --unified number
Like u but produces a diff with number lines of context.
### -u
Produces a unified diff with 3 lines of context.

A unified diff is similar to the context diff produced by the c option. However, unlike with c, all lines to be changed (added and/or removed) are present in a single section.
### --version
This option prints a version string to stdout and exits with status 0.
### -y, --side-by-side
Output in two columns with a marker between them.

The marker can be one of the following:

### space
Corresponding lines are identical.
### '|'
Corresponding lines are different.
### '<'
Files differ and only the first file contains the line.
### '>'
Files differ and only the second file contains the line.
## COMPARISON OPTIONS
### -A algo, --algorithm algo
Configure the algorithm used when comparing files. diff supports 3 algorithms:

### myers
The Myers diff algorithm finds the shortest edit which transforms one input into the other. It generally runs in O(N+D²) time, requiring O(N) space, where N is the sum of the lengths of the inputs and D is the length of the difference between them, with a theoretical O(N·D) worst case. If it encounters worst-case input, the implementation used by diff falls back to a less optimal but faster algorithm.
### patience
The Patience variant of the Myers algorithm attempts to create more aesthetically pleasing diff output by logically grouping lines.
### stone
The Stone algorithm (commonly known as Hunt-McIlroy or Hunt-Szymanski) looks for the longest common subsequence between compared files. Stone encounters worst case performance when there are long common subsequences. In large files this can lead to a significant performance impact. The Stone algorithm is maintained for compatibility.

The diff utility defaults to the Myers algorithm, but will fall back to the Stone algorithm if the input or output options are not supported by the Myers implementation.
### -a, --text
Treat all files as ASCII text.

Normally diff will simply print "Binary files ... differ" if files contain binary characters. Use of this option forces diff to produce a diff.
### -B, --ignore-blank-lines
Causes chunks that include only blank lines to be ignored.
### -b, --ignore-space-change
Causes trailing blanks (spaces and tabs) to be ignored, and other strings of blanks to compare equal.
### --color when
Color the additions green, and removals red, or the value in the DIFFCOLORS environment variable.

The possible values of when are "never", "always" and "auto". auto will use color if the output is a tty and the COLORTERM environment variable is set to a non-empty string.
### -d, --minimal
Try very hard to produce a diff as small as possible. This may consume a lot of processing power and memory when processing large files with many changes.
### -F pattern, --show-function-line pattern
Like p, but display the last line that matches provided pattern.
### -I pattern, --ignore-matching-lines pattern
Ignores changes, insertions, and deletions whose lines match the extended regular expression pattern. Multiple I patterns may be specified. All lines in the change must match some pattern for the change to be ignored. See re_format(7) for more information on regular expression patterns.
### -i, --ignore-case
Ignores the case of letters. E.g., "A" will compare equal to "a".
### -l, --paginate
Pass the output through pr(1) to paginate it.
### -L label, --label label
Print label instead of the first (and second, if this option is specified twice) file name and time in the context or unified diff header.
### -p, --show-c-function
With unified and context diffs, show with each change the first 40 characters of the last line before the context beginning with a letter, an underscore or a dollar sign. For C and Objective-C source code following standard layout conventions, this will show the prototype of the function the change applies to.
### -T, --initial-tab
Print a tab rather than a space before the rest of the line for the normal, context or unified output formats. This makes the alignment of tabs in the line consistent.
### -t, --expand-tabs
Will expand tabs in output lines.

Normal or c output adds character(s) to the front of each line which may screw up the indentation of the original source lines and make the output listing difficult to interpret. This option will preserve the original source's indentation.
### -w, --ignore-all-space
Is similar to b --ignore-space-change but causes whitespace (blanks and tabs) to be totally ignored. E.g., "if (  a == b  )" will compare equal to "if(a==b)".
### -W number, --width number
Output at most number columns when using side by side format. The default value is 130. Note that unless t was specified, diff will always align the second column to a tab stop, so values of --width smaller than approximately five times the value of --tabsize may yield surprising results.
### --changed-group-format GFMT
Format input groups in the provided.

the format is a string with special keywords:

### %<
lines from FILE1
### %>
lines from FILE2
### --ignore-file-name-case
ignore case when comparing file names
### --no-dereference
do not follow symbolic links
### --no-ignore-file-name-case
do not ignore case when comparing file names (default)
### --normal
default diff output
### --speed-large-files
stub option for compatibility with GNU diff
### --strip-trailing-cr
strip carriage return on input files
### --suppress-common-lines
Do not output common lines when using the side by side format
### --tabsize number
Number of spaces representing a tab (default 8)
## DIRECTORY COMPARISON OPTIONS
### -N, --new-file
If a file is found in only one directory, act as if it was found in the other directory too but was of zero size.
### -P, --unidirectional-new-file
If a file is found only in dir2, act as if it was found in dir1 too but was of zero size.
### -r, --recursive
Causes application of diff recursively to common subdirectories encountered.
### -S name, --starting-file name
Re-starts a directory diff in the middle, beginning with file name.
### -s, --report-identical-files
Causes diff to report files which are the same, which are otherwise not mentioned.
### -X file, --exclude-from file
Exclude files and subdirectories from comparison whose basenames match lines in file. Multiple X options may be specified.
### -x pattern, --exclude pattern
Exclude files and subdirectories from comparison whose basenames match pattern. Patterns are matched using shell-style globbing via fnmatch(3). Multiple x options may be specified.

If both arguments are directories, diff sorts the contents of the directories by name, and then runs the regular file diff algorithm, producing a change list, on text files which are different. Binary files which differ, common subdirectories, and files which appear in only one directory are described as such. In directory mode only regular files and directories are compared. If a non-regular file such as a device special file or FIFO is encountered, a diagnostic message is printed.

If only one of file1 and file2 is a directory, diff is applied to the non-directory file and the file contained in the directory file with a filename that is the same as the last component of the non-directory file.

If either file1 or file2 is -, the standard input is used in its place.
## OUTPUT STYLE
The default (without e, c, or n -rcs options) output contains lines of these forms, where XX, YY, ZZ, QQ are line numbers respective of file order.

### XXaYY
At (the end of) line XX of file1, append the contents of line YY of file2 to make them equal.
### XXaYY,ZZ
Same as above, but append the range of lines, YY through ZZ of file2 to line XX of file1.
### XXdYY
At line XX delete the line. The value YY tells to which line the change would bring file1 in line with file2.
### XX,YYdZZ
Delete the range of lines XX through YY in file1.
### XXcYY
Change the line XX in file1 to the line YY in file2.
### XX,YYcZZ
Replace the range of specified lines with the line ZZ.
### XX,YYcZZ,QQ
Replace the range XX,YY from file1 with the range ZZ,QQ from file2.

These lines resemble ed(1) subcommands to convert file1 into file2. The line numbers before the action letters pertain to file1; those after pertain to file2. Thus, by exchanging a for d and reading the line in reverse order, one can also determine how to convert file2 into file1. As in ed(1), identical pairs (where num1 = num2) are abbreviated as a single number.
## ENVIRONMENT
### DIFFCOLORS
The value of this variable is the form add:rm, where add is the ASCII escape sequence for additions and rm is the ASCII escape sequence for deletions. If this is unset, diff uses green for additions and red for removals.
## FILES
### /tmp/diff.XXXXXXXX
Temporary file used when comparing a device or the standard input. Note that the temporary file is unlinked as soon as it is created so it will not show up in a directory listing.
## EXIT STATUS
### 0
No differences were found.
### 1
Differences were found.
### >1
An error occurred.

The --help and --version options exit with a status of 0.
## EXAMPLES
### Compare old_dir and new_dir recursively generating an unified diff and treating files found only in one of those directories as new files
$ diff -ruN /path/to/old_dir /path/to/new_dir
### Same as above but excluding files matching the expressions "*.h" and "*.c"
$ diff -ruN -x '*.h' -x '*.c' /path/to/old_dir /path/to/new_dir
### Show a single line indicating if the files differ
$ diff -q /boot/loader.conf /boot/defaults/loader.conf
Files /boot/loader.conf and /boot/defaults/loader.conf differ
### Assuming a file named example.txt with the following contents
FreeBSD is an operating system
Linux is a kernel
OpenBSD is an operating system
### Compare stdin with example.txt excluding from the comparison those lines containing either "Linux" or "Open"
$ echo "FreeBSD is an operating system" | diff -q -I 'Linux|Open' example.txt -
## SEE ALSO
cmp(1), comm(1), diff3(1), ed(1), patch(1), pr(1), sdiff(1)
## STANDARDS
The diff utility is compliant with the POSIX.1-2008 specification.

The flags A a D d I i L l N n P p q S s T t w X x y are extensions to that specification.
## HISTORY
A diff command appeared in AT&T Version 6 UNIX.

The diff implementation used in FreeBSD was GNU diff until FreeBSD 11.4. This was replaced in FreeBSD 12.0 by a BSD-licensed implementation written by Todd Miller. Some GNUisms were lost in the process.

libdiff was imported from the Game of Trees version control system and default algorithm was changed to Myers for FreeBSD 15 .
*/
int
main(int argc, char **argv)
{
	const char *errstr = NULL;
	char *ep, **oargv;
	long  l;
	int   ch, dflags, lastch, gotstdin, prevoptind, newarg;

	oargv = argv;
	gotstdin = 0;
	dflags = 0;
	lastch = '\0';
	prevoptind = 1;
	newarg = 1;
	diff_context = 3;
	diff_format = D_UNSET;
	diff_algorithm = D_DIFFMYERS;
	diff_algorithm_set = false;
#define	FORMAT_MISMATCHED(type)	\
	(diff_format != D_UNSET && diff_format != (type))
	while ((ch = getopt_long(argc, argv, OPTIONS, longopts, NULL)) != -1) {
		switch (ch) {
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			if (newarg)
				usage();	/* disallow -[0-9]+ */
			else if (lastch == 'c' || lastch == 'u')
				diff_context = 0;
			else if (!isdigit(lastch) || diff_context > INT_MAX / 10)
				usage();
			diff_context = (diff_context * 10) + (ch - '0');
			break;
		case 'A':
			diff_algorithm = D_DIFFNONE;
			for (struct algorithm *a = algorithms; a->name;a++) {
				if(strcasecmp(optarg, a->name) == 0) {
					diff_algorithm = a->id;
					diff_algorithm_set = true;
					break;
				}
			}

			if (diff_algorithm == D_DIFFNONE) {
				printf("unknown algorithm: %s\n", optarg);
				usage();
			}
			break;
		case 'a':
			dflags |= D_FORCEASCII;
			break;
		case 'b':
			dflags |= D_FOLDBLANKS;
			break;
		case 'C':
		case 'c':
			if (FORMAT_MISMATCHED(D_CONTEXT))
				conflicting_format();
			cflag = true;
			diff_format = D_CONTEXT;
			if (optarg != NULL) {
				l = strtol(optarg, &ep, 10);
				if (*ep != '\0' || l < 0 || l >= INT_MAX)
					usage();
				diff_context = (int)l;
			}
			break;
		case 'd':
			dflags |= D_MINIMAL;
			break;
		case 'D':
			if (FORMAT_MISMATCHED(D_IFDEF))
				conflicting_format();
			diff_format = D_IFDEF;
			ifdefname = optarg;
			break;
		case 'e':
			if (FORMAT_MISMATCHED(D_EDIT))
				conflicting_format();
			diff_format = D_EDIT;
			break;
		case 'f':
			if (FORMAT_MISMATCHED(D_REVERSE))
				conflicting_format();
			diff_format = D_REVERSE;
			break;
		case 'H':
			/* ignore but needed for compatibility with GNU diff */
			break;
		case 'h':
			/* silently ignore for backwards compatibility */
			break;
		case 'B':
			dflags |= D_SKIPBLANKLINES;
			break;
		case 'F':
			if (dflags & D_PROTOTYPE)
				conflicting_format();
			dflags |= D_MATCHLAST;
			most_recent_pat = xstrdup(optarg);
			break;
		case 'I':
			push_ignore_pats(optarg);
			break;
		case 'i':
			dflags |= D_IGNORECASE;
			break;
		case 'L':
			if (label[0] == NULL)
				label[0] = optarg;
			else if (label[1] == NULL)
				label[1] = optarg;
			else
				usage();
			break;
		case 'l':
			lflag = true;
			break;
		case 'N':
			Nflag = true;
			break;
		case 'n':
			if (FORMAT_MISMATCHED(D_NREVERSE))
				conflicting_format();
			diff_format = D_NREVERSE;
			break;
		case 'p':
			if (dflags & D_MATCHLAST)
				conflicting_format();
			dflags |= D_PROTOTYPE;
			break;
		case 'P':
			Pflag = true;
			break;
		case 'r':
			rflag = true;
			break;
		case 'q':
			if (FORMAT_MISMATCHED(D_BRIEF))
				conflicting_format();
			diff_format = D_BRIEF;
			break;
		case 'S':
			start = optarg;
			break;
		case 's':
			sflag = true;
			break;
		case 'T':
			Tflag = true;
			break;
		case 't':
			dflags |= D_EXPANDTABS;
			break;
		case 'U':
		case 'u':
			if (FORMAT_MISMATCHED(D_UNIFIED))
				conflicting_format();
			diff_format = D_UNIFIED;
			if (optarg != NULL) {
				l = strtol(optarg, &ep, 10);
				if (*ep != '\0' || l < 0 || l >= INT_MAX)
					usage();
				diff_context = (int)l;
			}
			break;
		case 'w':
			dflags |= D_IGNOREBLANKS;
			break;
		case 'W':
			width = (int) strtonum(optarg, 1, INT_MAX, &errstr);
			if (errstr) {
				warnx("Invalid argument for width");
				usage();
			}
			break;
		case 'X':
			read_excludes_file(optarg);
			break;
		case 'x':
			push_excludes(optarg);
			break;
		case 'y':
			if (FORMAT_MISMATCHED(D_SIDEBYSIDE))
				conflicting_format();
			diff_format = D_SIDEBYSIDE;
			break;
		case OPT_CHANGED_GROUP_FORMAT:
			if (FORMAT_MISMATCHED(D_GFORMAT))
				conflicting_format();
			diff_format = D_GFORMAT;
			group_format = optarg;
			break;
		case OPT_HELP:
			help = true;
			usage();
			break;
		case OPT_HORIZON_LINES:
			break; /* XXX TODO for compatibility with GNU diff3 */
		case OPT_IGN_FN_CASE:
			ignore_file_case = true;
			break;
		case OPT_NO_IGN_FN_CASE:
			ignore_file_case = false;
			break;
		case OPT_NORMAL:
			if (FORMAT_MISMATCHED(D_NORMAL))
				conflicting_format();
			diff_format = D_NORMAL;
			break;
		case OPT_TSIZE:
			tabsize = (int) strtonum(optarg, 1, INT_MAX, &errstr);
			if (errstr) {
				warnx("Invalid argument for tabsize");
				usage();
			}
			break;
		case OPT_STRIPCR:
			dflags |= D_STRIPCR;
			break;
		case OPT_SUPPRESS_COMMON:
			suppress_common = 1;
			break;
		case OPT_COLOR:
			if (optarg == NULL || strncmp(optarg, "auto", 4) == 0)
				colorflag = COLORFLAG_AUTO;
			else if (strncmp(optarg, "always", 6) == 0)
				colorflag = COLORFLAG_ALWAYS;
			else if (strncmp(optarg, "never", 5) == 0)
				colorflag = COLORFLAG_NEVER;
			else
				errx(2, "unsupported --color value '%s' (must be always, auto, or never)",
					optarg);
			break;
		case OPT_NO_DEREFERENCE:
			noderef = true;
			break;
		case OPT_VERSION:
			printf("%s\n", diff_version);
			exit(0);
		default:
			usage();
			break;
		}
		lastch = ch;
		newarg = optind != prevoptind;
		prevoptind = optind;
	}
	if (diff_format == D_UNSET && (dflags & D_PROTOTYPE) != 0)
		diff_format = D_CONTEXT;
	if (diff_format == D_UNSET)
		diff_format = D_NORMAL;
	argc -= optind;
	argv += optind;

	if (do_color()) {
		char *p;
		const char *env;

		color = true;
		add_code = "32";
		del_code = "31";
		env = getenv("DIFFCOLORS");
		if (env != NULL && *env != '\0' && (p = strdup(env))) {
			add_code = p;
			strsep(&p, ":");
			if (p != NULL)
				del_code = p;
		}
	}

#ifdef __OpenBSD__
	if (pledge("stdio rpath tmppath", NULL) == -1)
		err(2, "pledge");
#endif

	/*
	 * Do sanity checks, fill in stb1 and stb2 and call the appropriate
	 * driver routine.  Both drivers use the contents of stb1 and stb2.
	 */
	if (argc != 2)
		usage();
	checked_regcomp(ignore_pats, &ignore_re);
	checked_regcomp(most_recent_pat, &most_recent_re);
	if (strcmp(argv[0], "-") == 0) {
		fstat(STDIN_FILENO, &stb1);
		gotstdin = 1;
	} else if (stat(argv[0], &stb1) != 0) {
		if (!Nflag || errno != ENOENT)
			err(2, "%s", argv[0]);
		dflags |= D_EMPTY1;
		memset(&stb1, 0, sizeof(struct stat));
	}

	if (strcmp(argv[1], "-") == 0) {
		fstat(STDIN_FILENO, &stb2);
		gotstdin = 1;
	} else if (stat(argv[1], &stb2) != 0) {
		if (!Nflag || errno != ENOENT)
			err(2, "%s", argv[1]);
		dflags |= D_EMPTY2;
		memset(&stb2, 0, sizeof(stb2));
		stb2.st_mode = stb1.st_mode;
	}

	if (dflags & D_EMPTY1 && dflags & D_EMPTY2){
		warn("%s", argv[0]);
		warn("%s", argv[1]);
		exit(2);
	}

	if (stb1.st_mode == 0)
		stb1.st_mode = stb2.st_mode;

	if (gotstdin && (S_ISDIR(stb1.st_mode) || S_ISDIR(stb2.st_mode)))
		errx(2, "can't compare - to a directory");
	set_argstr(oargv, argv);
	if (S_ISDIR(stb1.st_mode) && S_ISDIR(stb2.st_mode)) {
		if (diff_format == D_IFDEF)
			errx(2, "-D option not supported with directories");
		diffdir(argv[0], argv[1], dflags);
	} else {
		if (S_ISDIR(stb1.st_mode)) {
			argv[0] = splice(argv[0], argv[1]);
			if (stat(argv[0], &stb1) == -1)
				err(2, "%s", argv[0]);
		}
		if (S_ISDIR(stb2.st_mode)) {
			argv[1] = splice(argv[1], argv[0]);
			if (stat(argv[1], &stb2) == -1)
				err(2, "%s", argv[1]);
		}
		print_status(diffreg(argv[0], argv[1], dflags, 1), argv[0],
		    argv[1], "");
	}
	if (fflush(stdout) != 0)
		err(2, "stdout");
	exit(status);
}

static void
checked_regcomp(char const *pattern, regex_t *comp)
{
	char buf[BUFSIZ];
	int error;

	if (pattern == NULL)
		return;

	error = regcomp(comp, pattern, REG_NEWLINE | REG_EXTENDED);
	if (error != 0) {
		regerror(error, comp, buf, sizeof(buf));
		if (*pattern != '\0')
			errx(2, "%s: %s", pattern, buf);
		else
			errx(2, "%s", buf);
	}
}

static void
set_argstr(char **av, char **ave)
{
	size_t argsize;
	char **ap;

	argsize = strlen("diff") + 1;
	for (ap = av + 1; ap < ave; ap++) {
		if (strcmp(*ap, "--") != 0)
			argsize += 1 + strlen(*ap);
	}
	diffargs = xmalloc(argsize);
	strlcpy(diffargs, "diff", argsize);
	for (ap = av + 1; ap < ave; ap++) {
		if (strcmp(*ap, "--") != 0) {
			strlcat(diffargs, " ", argsize);
			strlcat(diffargs, *ap, argsize);
		}
	}
}

/*
 * Read in an excludes file and push each line.
 */
static void
read_excludes_file(char *file)
{
	FILE *fp;
	char *pattern = NULL;
	size_t blen = 0;
	ssize_t len;

	if (strcmp(file, "-") == 0)
		fp = stdin;
	else if ((fp = fopen(file, "r")) == NULL)
		err(2, "%s", file);
	while ((len = getline(&pattern, &blen, fp)) >= 0) {
		if ((len > 0) && (pattern[len - 1] == '\n'))
			pattern[len - 1] = '\0';
		push_excludes(pattern);
		/* we allocate a new string per line */
		pattern = NULL;
		blen = 0;
	}
	free(pattern);
	if (strcmp(file, "-") != 0)
		fclose(fp);
}

/*
 * Push a pattern onto the excludes list.
 */
static void
push_excludes(char *pattern)
{
	struct excludes *entry;

	entry = xmalloc(sizeof(*entry));
	entry->pattern = pattern;
	entry->next = excludes_list;
	excludes_list = entry;
}

static void
push_ignore_pats(char *pattern)
{
	size_t len;

	if (ignore_pats == NULL)
		ignore_pats = xstrdup(pattern);
	else {
		/* old + "|" + new + NUL */
		len = strlen(ignore_pats) + strlen(pattern) + 2;
		ignore_pats = xreallocarray(ignore_pats, 1, len);
		strlcat(ignore_pats, "|", len);
		strlcat(ignore_pats, pattern, len);
	}
}

void
print_status(int val, char *path1, char *path2, const char *entry)
{
	if (label[0] != NULL)
		path1 = label[0];
	if (label[1] != NULL)
		path2 = label[1];

	switch (val) {
	case D_BINARY:
		printf("Binary files %s%s and %s%s differ\n",
		    path1, entry, path2, entry);
		break;
	case D_DIFFER:
		if (diff_format == D_BRIEF)
			printf("Files %s%s and %s%s differ\n",
			    path1, entry, path2, entry);
		break;
	case D_SAME:
		if (sflag)
			printf("Files %s%s and %s%s are identical\n",
			    path1, entry, path2, entry);
		break;
	case D_MISMATCH1:
		printf("File %s%s is a directory while file %s%s is a regular file\n",
		    path1, entry, path2, entry);
		break;
	case D_MISMATCH2:
		printf("File %s%s is a regular file while file %s%s is a directory\n",
		    path1, entry, path2, entry);
		break;
	case D_SKIPPED1:
		printf("File %s%s is not a regular file or directory and was skipped\n",
		    path1, entry);
		break;
	case D_SKIPPED2:
		printf("File %s%s is not a regular file or directory and was skipped\n",
		    path2, entry);
		break;
	case D_ERROR:
		break;
	}
}

static void
usage(void)
{
	(void)fprintf(help ? stdout : stderr,
	    "usage: diff [-aBbdilpTtw] [-c | -e | -f | -n | -q | -u] [--ignore-case]\n"
	    "            [--no-ignore-case] [--normal] [--strip-trailing-cr] [--tabsize]\n"
	    "            [-I pattern] [-F pattern] [-L label] file1 file2\n"
	    "       diff [-aBbdilpTtw] [-I pattern] [-L label] [--ignore-case]\n"
	    "            [--no-ignore-case] [--normal] [--strip-trailing-cr] [--tabsize]\n"
	    "            [-F pattern] -C number file1 file2\n"
	    "       diff [-aBbdiltw] [-I pattern] [--ignore-case] [--no-ignore-case]\n"
	    "            [--normal] [--strip-trailing-cr] [--tabsize] -D string file1 file2\n"
	    "       diff [-aBbdilpTtw] [-I pattern] [-L label] [--ignore-case]\n"
	    "            [--no-ignore-case] [--normal] [--tabsize] [--strip-trailing-cr]\n"
	    "            [-F pattern] -U number file1 file2\n"
	    "       diff [-aBbdilNPprsTtw] [-c | -e | -f | -n | -q | -u] [--ignore-case]\n"
	    "            [--no-ignore-case] [--normal] [--tabsize] [-I pattern] [-L label]\n"
	    "            [-F pattern] [-S name] [-X file] [-x pattern] dir1 dir2\n"
	    "       diff [-aBbditwW] [--expand-tabs] [--ignore-all-space]\n"
	    "            [--ignore-blank-lines] [--ignore-case] [--minimal]\n"
	    "            [--no-ignore-file-name-case] [--strip-trailing-cr]\n"
	    "            [--suppress-common-lines] [--tabsize] [--text] [--width]\n"
	    "            -y | --side-by-side file1 file2\n"
	    "       diff [--help] [--version]\n");

	if (help)
		exit(0);
	else
		exit(2);
}

static void
conflicting_format(void)
{

	fprintf(stderr, "error: conflicting output format options.\n");
	usage();
}

static bool
do_color(void)
{
	const char *p, *p2;

	switch (colorflag) {
	case COLORFLAG_AUTO:
		p = getenv("CLICOLOR");
		p2 = getenv("COLORTERM");
		if ((p != NULL && *p != '\0') || (p2 != NULL && *p2 != '\0'))
			return isatty(STDOUT_FILENO);
		break;
	case COLORFLAG_ALWAYS:
		return (true);
	case COLORFLAG_NEVER:
		return (false);
	}

	return (false);
}

static char *
splice(char *dir, char *path)
{
	char *tail, *buf;
	size_t dirlen;

	dirlen = strlen(dir);
	while (dirlen != 0 && dir[dirlen - 1] == '/')
	    dirlen--;
	if ((tail = strrchr(path, '/')) == NULL)
		tail = path;
	else
		tail++;
	xasprintf(&buf, "%.*s/%s", (int)dirlen, dir, tail);
	return (buf);
}
