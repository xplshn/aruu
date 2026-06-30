/* See LICENSE file for copyright and license details. */
#include "util.h"

#include <sys/types.h>
#include <sys/wait.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct Range {
	int from;
	int to;
};

struct Diff {
#define DIFF_TYPE1 1
#define DIFF_TYPE2 2
#define DIFF_TYPE3 3
	int type;

	struct Range old;
	struct Range new;
};

#define EFLAG_NONE     0
#define EFLAG_OVERLAP  1
#define EFLAG_NOOVERLAP 2
#define EFLAG_UNMERGED 3

static size_t szchanges;

static struct Diff *d13;
static struct Diff *d23;
static struct Diff *de;
static char *overlap;
static int overlapcnt;
static FILE *fp[3];
static int cline[3];
static int sigpipe[2];
static int last[4];
static int Aflag, eflag, iflag, mflag, Tflag;
static int oflag;
static int strip_cr;
static char *f1mark, *f2mark, *f3mark;
static const char *oldmark = "<<<<<<<";
static const char *orgmark = "|||||||";
static const char *newmark = ">>>>>>>";
static const char *divider = "=======";

static int duplicate(struct Range *, struct Range *);
static int edit(struct Diff *, int, int, int);
static char *getchange(FILE *);
static char *get_line(FILE *, size_t *);
static int readin(int, struct Diff **);
static int skip(int, int, const char *);
static void change(int, struct Range *, int);
static void keep(int, struct Range *);
static void merge(int, int);
static void prange(struct Range *, int);
static void repos(int);
static void separate(const char *);
static void edscript(int);
static void Ascript(int);
static void mergescript(int);
static void increase(void);
static void usage(void);
static void printrange(FILE *, struct Range *);
static void diffexec(const char *, char **, int *);
static int strtoi(char *, char **);
static void handle_sig(int);
static void xasprintf(char **, const char *, ...);

static const char diff3_version[] = "FreeBSD diff3 20240925";

enum {
	DIFFPROG_OPT,
	STRIPCR_OPT,
	HELP_OPT,
	VERSION_OPT
};

#define DIFF_PATH "/usr/bin/diff"

#define OPTIONS "3aAeEiL:mTxX"
static struct option longopts[] = {
	{ "ed",                 no_argument,            NULL,   'e' },
	{ "show-overlap",       no_argument,            NULL,   'E' },
	{ "overlap-only",       no_argument,            NULL,   'x' },
	{ "initial-tab",        no_argument,            NULL,   'T' },
	{ "text",               no_argument,            NULL,   'a' },
	{ "strip-trailing-cr",  no_argument,            NULL,   STRIPCR_OPT },
	{ "show-all",           no_argument,            NULL,   'A' },
	{ "easy-only",          no_argument,            NULL,   '3' },
	{ "merge",              no_argument,            NULL,   'm' },
	{ "label",              required_argument,      NULL,   'L' },
	{ "diff-program",       required_argument,      NULL,   DIFFPROG_OPT },
	{ "help",               no_argument,            NULL,   HELP_OPT},
	{ "version",            no_argument,            NULL,   VERSION_OPT}
};

static void
usage(void)
{
	fprintf(stderr, "usage: diff3 [-3aAeEimTxX] [-L label1] [-L label2] "
	    "[-L label3] file1 file2 file3\n");
}

static int
strtoi(char *str, char **end)
{
	intmax_t num;

	errno = 0;
	num = strtoimax(str, end, 10);
	if ((end != NULL && *end == str) ||
	    num < 0 || num > INT_MAX ||
	    errno == EINVAL || errno == ERANGE)
		err(1, "error in diff output");
	return (int)num;
}

static int
readin(int fd, struct Diff **dd)
{
	int a, b, c, d;
	int i;
	char kind, *p;
	FILE *f;

	f = fdopen(fd, "r");
	if (f == NULL)
		err(2, "fdopen");
	for (i = 0; (p = getchange(f)) != NULL; i++) {
		if ((size_t)i >= szchanges - 1)
			increase();

		a = b = strtoi(p, &p);
		if (*p == ',')
			b = strtoi(p + 1, &p);
		kind = *p++;
		c = d = strtoi(p, &p);
		if (*p == ',')
			d = strtoi(p + 1, &p);
		if (*p != '\n')
			errx(1, "error in diff output");
		if (kind == 'a')
			a++;
		else if (kind == 'c')
			/* nothing */ ;
		else if (kind == 'd')
			c++;
		else
			errx(1, "error in diff output");
		b++;
		d++;
		if (b < a || d < c)
			errx(1, "error in diff output");
		(*dd)[i].old.from = a;
		(*dd)[i].old.to = b;
		(*dd)[i].new.from = c;
		(*dd)[i].new.to = d;
		if (i > 0) {
			if ((*dd)[i].old.from < (*dd)[i - 1].old.to ||
			    (*dd)[i].new.from < (*dd)[i - 1].new.to)
				errx(1, "diff output out of order");
		}
	}
	if (i > 0) {
		(*dd)[i].old.from = (*dd)[i].old.to = (*dd)[i - 1].old.to;
		(*dd)[i].new.from = (*dd)[i].new.to = (*dd)[i - 1].new.to;
	}
	fclose(f);
	return i;
}

static void
diffexec(const char *diffprog, char **diffargv, int fd[])
{
	switch (fork()) {
	case 0:
		close(fd[0]);
		if (dup2(fd[1], STDOUT_FILENO) == -1)
			err(2, "child could not duplicate descriptor");
		close(fd[1]);
		execvp(diffprog, diffargv);
		err(2, "could not execute diff: %s", diffprog);
		break;
	case -1:
		err(2, "could not fork");
		break;
	}
	close(fd[1]);
}

static char *
getchange(FILE *b)
{
	char *line;

	while ((line = get_line(b, NULL)) != NULL) {
		if (isdigit((unsigned char)line[0]))
			return line;
	}
	return NULL;
}

static char *
get_line(FILE *b, size_t *n)
{
	ssize_t len;
	static char *buf = NULL;
	static size_t bufsize = 0;

	if ((len = getline(&buf, &bufsize, b)) < 0)
		return NULL;

	if (strip_cr && len >= 2 && strcmp("\r\n", &(buf[len - 2])) == 0) {
		buf[len - 2] = '\n';
		buf[len - 1] = '\0';
		len--;
	}

	if (n != NULL)
		*n = len;

	return buf;
}

static void
merge(int m1, int m2)
{
	struct Diff *d1, *d2, *d3;
	int j, t1, t2;
	int dup = 0;

	d1 = d13;
	d2 = d23;
	j = 0;

	for (;;) {
		t1 = (d1 < d13 + m1);
		t2 = (d2 < d23 + m2);
		if (!t1 && !t2)
			break;

		if (!t2 || (t1 && d1->new.to < d2->new.from)) {
			if (eflag == EFLAG_NONE) {
				separate("1");
				change(1, &d1->old, 0);
				keep(2, &d1->new);
				change(3, &d1->new, 0);
			} else if (eflag == EFLAG_OVERLAP) {
				j = edit(d2, dup, j, DIFF_TYPE1);
			}
			d1++;
			continue;
		}
		if (!t1 || (t2 && d2->new.to < d1->new.from)) {
			if (eflag == EFLAG_NONE) {
				separate("2");
				keep(1, &d2->new);
				change(3, &d2->new, 0);
				change(2, &d2->old, 0);
			} else if (Aflag || mflag) {
				if (eflag == EFLAG_UNMERGED)
					j = edit(d2, dup, j, DIFF_TYPE2);
			}
			d2++;
			continue;
		}
		if (d1 + 1 < d13 + m1 && d1->new.to >= d1[1].new.from) {
			d1[1].old.from = d1->old.from;
			d1[1].new.from = d1->new.from;
			d1++;
			continue;
		}
		if (d2 + 1 < d23 + m2 && d2->new.to >= d2[1].new.from) {
			d2[1].old.from = d2->old.from;
			d2[1].new.from = d2->new.from;
			d2++;
			continue;
		}
		if (d1->new.from == d2->new.from && d1->new.to == d2->new.to) {
			dup = duplicate(&d1->old, &d2->old);
			if (eflag == EFLAG_NONE) {
				separate(dup ? "3" : "");
				change(1, &d1->old, dup);
				change(2, &d2->old, 0);
				d3 = d1->old.to > d1->old.from ? d1 : d2;
				change(3, &d3->new, 0);
			} else {
				j = edit(d1, dup, j, DIFF_TYPE3);
			}
			dup = 0;
			d1++;
			d2++;
			continue;
		}
		if (d1->new.from < d2->new.from) {
			d2->old.from -= d2->new.from - d1->new.from;
			d2->new.from = d1->new.from;
		} else if (d2->new.from < d1->new.from) {
			d1->old.from -= d1->new.from - d2->new.from;
			d1->new.from = d2->new.from;
		}
		if (d1->new.to > d2->new.to) {
			d2->old.to += d1->new.to - d2->new.to;
			d2->new.to = d1->new.to;
		} else if (d2->new.to > d1->new.to) {
			d1->old.to += d2->new.to - d1->new.to;
			d1->new.to = d2->new.to;
		}
	}

	if (mflag)
		mergescript(j);
	else if (Aflag)
		Ascript(j);
	else if (eflag)
		edscript(j);
}

static void
separate(const char *s)
{
	printf("====%s\n", s);
}

static void
change(int i, struct Range *rold, int dup)
{
	printf("%d:", i);
	last[i] = rold->to;
	prange(rold, 0);
	if (dup)
		return;
	i--;
	skip(i, rold->from, NULL);
	skip(i, rold->to, "  ");
}

static void
prange(struct Range *rold, int delete)
{
	if (rold->to <= rold->from)
		printf("%da\n", rold->from - 1);
	else {
		printf("%d", rold->from);
		if (rold->to > rold->from + 1)
			printf(",%d", rold->to - 1);
		if (delete)
			printf("d\n");
		else
			printf("c\n");
	}
}

static void
keep(int i, struct Range *rnew)
{
	int delta;
	struct Range trange;

	delta = last[3] - last[i];
	trange.from = rnew->from - delta;
	trange.to = rnew->to - delta;
	change(i, &trange, 1);
}

static int
skip(int i, int from, const char *pr)
{
	size_t j, n;
	char *line;

	for (n = 0; cline[i] < from - 1; n += j) {
		if ((line = get_line(fp[i], &j)) == NULL)
			errx(EXIT_FAILURE, "logic error");
		if (pr != NULL)
			printf("%s%s", Tflag == 1 ? "\t" : pr, line);
		cline[i]++;
	}
	return (int)n;
}

static int
duplicate(struct Range *r1, struct Range *r2)
{
	int c, d;
	int nchar;
	int nline;

	if (r1->to-r1->from != r2->to-r2->from)
		return 0;
	skip(0, r1->from, NULL);
	skip(1, r2->from, NULL);
	nchar = 0;
	for (nline = 0; nline < r1->to - r1->from; nline++) {
		do {
			c = getc(fp[0]);
			d = getc(fp[1]);
			if (c == -1 && d == -1)
				break;
			if (c == -1 || d == -1)
				errx(EXIT_FAILURE, "logic error");
			nchar++;
			if (c != d) {
				repos(nchar);
				return 0;
			}
		} while (c != '\n');
	}
	repos(nchar);
	return 1;
}

static void
repos(int nchar)
{
	int i;

	for (i = 0; i < 2; i++)
		(void)fseek(fp[i], (long)-nchar, SEEK_CUR);
}

static int
edit(struct Diff *diff, int dup, int j, int difftype)
{
	if (!(eflag == EFLAG_UNMERGED ||
	    (!dup && eflag == EFLAG_OVERLAP) ||
	    (dup && eflag == EFLAG_NOOVERLAP))) {
		return j;
	}
	j++;
	overlap[j] = !dup;
	if (!dup)
		overlapcnt++;

	de[j].type = difftype;
	de[j].old.from = diff->old.from;
	de[j].old.to = diff->old.to;
	de[j].new.from = diff->new.from;
	de[j].new.to = diff->new.to;
	return j;
}

static void
printrange(FILE *p, struct Range *r)
{
	char *line = NULL;
	size_t len = 0;
	int i = 1;

	if (r->from == r->to)
		return;

	if (r->from > r->to)
		errx(EXIT_FAILURE, "invalid print range");

	fseek(p, 0L, SEEK_SET);
	while (getline(&line, &len, p) > 0) {
		if (i >= r->from)
			printf("%s", line);
		if (++i > r->to - 1)
			break;
	}
	free(line);
}

static void
edscript(int n)
{
	int delete;
	struct Range *new, *old;

	for (; n > 0; n--) {
		new = &de[n].new;
		old = &de[n].old;

		delete = (new->from == new->to);
		if (de[n].type == DIFF_TYPE1) {
			if (delete)
				printf("%dd\n", new->from - 1);
			else if (old->from == new->from && old->to == new->to) {
				printf("%dc\n", old->from);
				printrange(fp[2], old);
				printf(".\n");
			}
			continue;
		} else {
			if (!oflag || !overlap[n]) {
				prange(old, delete);
			} else {
				printf("%da\n", old->to - 1);
				printf("%s\n", divider);
			}
			printrange(fp[2], new);
			if (!oflag || !overlap[n]) {
				if (!delete)
					printf(".\n");
			} else {
				printf("%s %s\n.\n", newmark, f3mark);
				printf("%da\n%s %s\n.\n", old->from - 1, oldmark, f1mark);
			}
		}
	}
	if (iflag)
		printf("w\nq\n");

	exit(eflag == EFLAG_NONE ? overlapcnt : 0);
}

static void
Ascript(int n)
{
	int startmark;
	int deletenew;
	int deleteold;
	struct Range *new, *old;

	for (; n > 0; n--) {
		new = &de[n].new;
		old = &de[n].old;
		deletenew = (new->from == new->to);
		deleteold = (old->from == old->to);

		if (de[n].type == DIFF_TYPE2) {
			if (!oflag || !overlap[n]) {
				prange(old, deletenew);
				printrange(fp[2], new);
			} else {
				startmark = new->to - 1;
				printf("%da\n", startmark);
				printf("%s %s\n", newmark, f3mark);
				printf(".\n");
				printf("%da\n", startmark - (new->to - new->from));
				printf("%s %s\n", oldmark, f2mark);
				if (!deleteold)
					printrange(fp[1], old);
				printf("%s\n.\n", divider);
			}
		} else if (de[n].type == DIFF_TYPE3) {
			startmark = old->to - 1;
			if (!oflag || !overlap[n]) {
				prange(old, deletenew);
				printrange(fp[2], new);
			} else {
				printf("%da\n", startmark);
				printf("%s %s\n", orgmark, f2mark);
				if (deleteold) {
					struct Range r;
					r.from = old->from-1;
					r.to = new->to;
					printrange(fp[1], &r);
				} else
					printrange(fp[1], old);
				printf("%s\n", divider);
				printrange(fp[2], new);
			}
			if (!oflag || !overlap[n]) {
				if (!deletenew)
					printf(".\n");
			} else {
				printf("%s %s\n.\n", newmark, f3mark);
				printf("%da\n%s %s\n.\n", startmark - (old->to - old->from), oldmark, f1mark);
			}
		}
	}
	if (iflag)
		printf("w\nq\n");

	exit(overlapcnt > 0);
}

static void
mergescript(int i)
{
	struct Range r, *new, *old;
	int n;
	int delete = 0;

	r.from = 1;
	r.to = 1;

	for (n = 1; n <= i; n++) {
		new = &de[n].new;
		old = &de[n].old;

		delete = (new->from == new->to);
		if (de[n].type == DIFF_TYPE1 && delete)
			r.to = new->from - 1;
		else if (de[n].type == DIFF_TYPE3 && (old->from == old->to)) {
			r.from = old->from - 1;
			r.to = new->from;
		} else
			r.to = old->from;

		printrange(fp[0], &r);
		switch (de[n].type) {
		case DIFF_TYPE1:
			if (!delete)
				printrange(fp[2], new);
			break;
		case DIFF_TYPE2:
			printf("%s %s\n", oldmark, f2mark);
			printrange(fp[1], old);
			printf("%s\n", divider);
			printrange(fp[2], new);
			printf("%s %s\n", newmark, f3mark);
			break;
		case DIFF_TYPE3:
			if (!oflag || !overlap[n]) {
				printrange(fp[2], new);
			} else {
				printf("%s %s\n", oldmark, f1mark);
				printrange(fp[0], old);
				if (eflag != EFLAG_OVERLAP) {
					printf("%s %s\n", orgmark, f2mark);
					if (old->from == old->to) {
						struct Range or;
						or.from = old->from - 1;
						or.to = new->to;
						printrange(fp[1], &or);
					} else {
						printrange(fp[1], old);
					}
				}
				printf("%s\n", divider);
				printrange(fp[2], new);
				printf("%s %s\n", newmark, f3mark);
			}
			break;
		default:
			printf("Error: Unhandled diff type - exiting\n");
			exit(EXIT_FAILURE);
		}

		if (old->from == old->to)
			r.from = new->to;
		else
			r.from = old->to;
	}

	new = &de[n-1].new;
	old = &de[n-1].old;

	if (old->from == new->from && old->to == new->to)
		r.from--;
	else if (new->from == new->to)
		r.from = old->from;

	r.to = INT_MAX;
	printrange(fp[2], &r);
	exit(overlapcnt > 0);
}

static void
increase(void)
{
	struct Diff *p;
	char *q;
	size_t newsz, incr;

	newsz = szchanges == 0 ? 64 : 2 * szchanges;
	incr = newsz - szchanges;

	p = ereallocarray(d13, newsz, sizeof(*p));
	memset(p + szchanges, 0, incr * sizeof(*p));
	d13 = p;

	p = ereallocarray(d23, newsz, sizeof(*p));
	memset(p + szchanges, 0, incr * sizeof(*p));
	d23 = p;

	p = ereallocarray(de, newsz, sizeof(*p));
	memset(p + szchanges, 0, incr * sizeof(*p));
	de = p;

	q = ereallocarray(overlap, newsz, 1);
	memset(q + szchanges, 0, incr * 1);
	overlap = q;
	szchanges = newsz;
}

static void
handle_sig(int signo)
{
	write(sigpipe[1], &signo, sizeof(signo));
}

static void
xasprintf(char **strp, const char *fmt, ...)
{
	va_list ap;
	int r;

	va_start(ap, fmt);
	r = vasprintf(strp, fmt, ap);
	va_end(ap);
	if (r == -1)
		err(2, "asprintf");
}

// ?man diff3: 3-way differential file comparison
// ?man arguments: file1 file2 file3
// ?man synopsis: [-3aAeEiL:mTxX] [--diff-program program] [--strip-trailing-cr] [-L label1] [-L label2] [-L label3] file1 file2 file3
int
main(int argc, char **argv)
{
	int ch, nblabels, status, m, n, npe, nleft;
	char *labels[] = { NULL, NULL, NULL };
	const char *diffprog = DIFF_PATH;
	char *file1, *file2, *file3;
	char *diffargv[7];
	int diffargc = 0;
	int fd13[2], fd23[2], signo;
	pid_t wpid;
	struct pollfd pfd;

	nblabels = 0;
	eflag = EFLAG_NONE;
	oflag = 0;
	diffargv[diffargc++] = (char *)diffprog;
	while ((ch = getopt_long(argc, argv, OPTIONS, longopts, NULL)) != -1) {
		// ?man -3: output changes specific only to file3
		switch (ch) {
		case '3':
			eflag = EFLAG_NOOVERLAP;
			break;
		// ?man -a: treat files as text
		case 'a':
			diffargv[diffargc++] = "-a";
			break;
		// ?man -A: output all changes bracketing conflicts
		case 'A':
			Aflag = 1;
			break;
		// ?man -e: output ed script
		case 'e':
			eflag = EFLAG_UNMERGED;
			break;
		// ?man -E: show overlapping changes
		case 'E':
			eflag = EFLAG_OVERLAP;
			oflag = 1;
			break;
		// ?man -i: append w and q ed commands
		case 'i':
			iflag = 1;
			break;
		// ?man -L:label: define label
		case 'L':
			oflag = 1;
			if (nblabels >= 3)
				errx(2, "too many file label options");
			labels[nblabels++] = optarg;
			break;
		// ?man -m: merge output
		case 'm':
			Aflag = 1;
			oflag = 1;
			mflag = 1;
			break;
		// ?man -T: use tab prefix in normal listing
		case 'T':
			Tflag = 1;
			break;
		// ?man -x: output script with changes specific to all three
		case 'x':
			eflag = EFLAG_OVERLAP;
			break;
		// ?man -X: show overlapping changes
		case 'X':
			oflag = 1;
			eflag = EFLAG_OVERLAP;
			break;
		case DIFFPROG_OPT:
			diffprog = optarg;
			break;
		case STRIPCR_OPT:
			strip_cr = 1;
			diffargv[diffargc++] = "--strip-trailing-cr";
			break;
		case HELP_OPT:
			usage();
			exit(0);
		case VERSION_OPT:
			printf("%s\n", diff3_version);
			exit(0);
		}
	}
	argc -= optind;
	argv += optind;

	if (Aflag) {
		if (eflag == EFLAG_NONE)
			eflag = EFLAG_UNMERGED;
		oflag = 1;
	}

	if (argc != 3) {
		usage();
		exit(2);
	}

	file1 = argv[0];
	file2 = argv[1];
	file3 = argv[2];

	if (oflag) {
		xasprintf(&f1mark, "%s", labels[0] != NULL ? labels[0] : file1);
		xasprintf(&f2mark, "%s", labels[1] != NULL ? labels[1] : file2);
		xasprintf(&f3mark, "%s", labels[2] != NULL ? labels[2] : file3);
	}
	fp[0] = fopen(file1, "r");
	if (fp[0] == NULL)
		err(2, "Can't open %s", file1);

	fp[1] = fopen(file2, "r");
	if (fp[1] == NULL)
		err(2, "Can't open %s", file2);

	fp[2] = fopen(file3, "r");
	if (fp[2] == NULL)
		err(2, "Can't open %s", file3);

	if (pipe(fd13))
		err(2, "pipe");
	if (pipe(fd23))
		err(2, "pipe");
	if (pipe(sigpipe))
		err(2, "pipe");
	if (fcntl(sigpipe[0], F_SETFD, FD_CLOEXEC))
		err(2, "fcntl");
	if (fcntl(sigpipe[1], F_SETFD, FD_CLOEXEC))
		err(2, "fcntl");

	pfd.fd = sigpipe[0];
	pfd.events = POLLIN;
	pfd.revents = 0;

	if (signal(SIGCHLD, handle_sig) == SIG_ERR)
		err(2, "signal");

	diffargv[diffargc] = file1;
	diffargv[diffargc + 1] = file3;
	diffargv[diffargc + 2] = NULL;

	nleft = 0;
	diffexec(diffprog, diffargv, fd13);
	nleft++;

	diffargv[diffargc] = file2;
	diffexec(diffprog, diffargv, fd23);
	nleft++;

	increase();
	m = readin(fd13[0], &d13);
	n = readin(fd23[0], &d23);

	while (nleft > 0) {
		npe = poll(&pfd, 1, -1);
		if (npe == -1) {
			if (errno == EINTR)
				continue;
			err(2, "poll");
		}
		if (pfd.revents != POLLIN)
			continue;
		if (read(pfd.fd, &signo, sizeof(signo)) < 0)
			err(2, "read");
		while ((wpid = waitpid(-1, &status, WNOHANG)) > 0) {
			if (WIFEXITED(status) && WEXITSTATUS(status) >= 2)
				errx(2, "diff exited abnormally");
			else if (WIFSIGNALED(status))
				errx(2, "diff killed by signal %d", WTERMSIG(status));
			nleft--;
		}
	}
	merge(m, n);

	return EXIT_SUCCESS;
}
