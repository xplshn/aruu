/* see license file for copyright and license details */


#include <sys/stat.h>
#include <sys/types.h>
#ifndef major
#include <sys/sysmacros.h>
#endif

#include <dirent.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "utf.h"
#include "util.h"

struct entry {
	char   *name;
	mode_t  mode, tmode;
	nlink_t nlink;
	uid_t   uid;
	gid_t   gid;
	off_t   size;
	struct timespec t;
	dev_t   dev;
	dev_t   rdev;
	ino_t   ino, tino;
};

static struct {
	dev_t dev;
	ino_t ino;
} *tree;

static int ret   = 0;
static int Aflag = 0;
static int aflag = 0;
static int cflag = 0;
static int dflag = 0;
static int Fflag = 0;
static int fflag = 0;
static int Hflag = 0;
static int hflag = 0;
static int iflag = 0;
static int Lflag = 0;
static int lflag = 0;
static int nflag = 0;
static int pflag = 0;
static int qflag = 0;
static int Rflag = 0;
static int rflag = 0;
static int Uflag = 0;
static int uflag = 0;
static int first = 1;
static char sort = 0;
static int showdirs;

static int gflag = 0;
static int oflag = 0;

static int Cflag   = 0;
static int one_flag = 0;
static int termwidth = 80;

#if FEATURE_LS_COLOR
#define COLOR_DIR	"\033[1;34m"
#define COLOR_LNK	"\033[1;36m"
#define COLOR_FIFO	"\033[33m"
#define COLOR_SOCK	"\033[1;35m"
#define COLOR_DEV	"\033[1;33m"
#define COLOR_EXE	"\033[1;32m"
#define COLOR_RST	"\033[0m"

enum { COLOR_NEVER, COLOR_ALWAYS, COLOR_AUTO };
static int color_mode = COLOR_NEVER;
#endif

static void ls(const char *, const struct entry *, int);
static void printname_colored(const char *, mode_t);
static void printcols(const struct entry *, size_t);
static void output(const struct entry *);

static void
mkent(struct entry *ent, char *path, int dostat, int follow)
{
	struct stat st;

	ent->name = path;
	if (!dostat)
		return;
	if ((follow ? stat : lstat)(path, &st) < 0)
		eprintf("%s %s:", follow ? "stat" : "lstat", path);
	ent->mode  = st.st_mode;
	ent->nlink = st.st_nlink;
	ent->uid   = st.st_uid;
	ent->gid   = st.st_gid;
	ent->size  = st.st_size;
	if (cflag)
		ent->t = st.st_ctim;
	else if (uflag)
		ent->t = st.st_atim;
	else
		ent->t = st.st_mtim;
	ent->dev   = st.st_dev;
	ent->rdev  = st.st_rdev;
	ent->ino   = st.st_ino;
	if (S_ISLNK(ent->mode)) {
		if (stat(path, &st) == 0) {
			ent->tmode = st.st_mode;
			ent->dev   = st.st_dev;
			ent->tino  = st.st_ino;
		} else {
			ent->tmode = ent->tino = 0;
		}
	}
}

static char *
indicator(mode_t mode)
{
	if (pflag || Fflag)
		if (S_ISDIR(mode))
			return "/";

	if (Fflag) {
		if (S_ISLNK(mode))
			return "@";
		else if (S_ISFIFO(mode))
			return "|";
		else if (S_ISSOCK(mode))
			return "=";
		else if (mode & S_IXUSR || mode & S_IXGRP || mode & S_IXOTH)
			return "*";
	}

	return "";
}

static void
printname(const char *name)
{
	const char *c;
	Rune r;
	size_t l;

	for (c = name; *c; c += l) {
		l = chartorune(&r, c);
		if (!qflag || isprintrune(r))
			fwrite(c, 1, l, stdout);
		else
			putchar('?');
	}
}

static int
should_color(void)
{
#if FEATURE_LS_COLOR
	if (color_mode == COLOR_ALWAYS)
		return 1;
	if (color_mode == COLOR_AUTO)
		return isatty(STDOUT_FILENO);
#endif
	return 0;
}

static void
printname_colored(const char *name, mode_t mode)
{
#if FEATURE_LS_COLOR
	int need_reset = 0;

	if (should_color()) {
		if (S_ISDIR(mode)) {
			fputs(COLOR_DIR, stdout);
			need_reset = 1;
		} else if (S_ISLNK(mode)) {
			fputs(COLOR_LNK, stdout);
			need_reset = 1;
		} else if (S_ISFIFO(mode)) {
			fputs(COLOR_FIFO, stdout);
			need_reset = 1;
		} else if (S_ISSOCK(mode)) {
			fputs(COLOR_SOCK, stdout);
			need_reset = 1;
		} else if (S_ISBLK(mode) || S_ISCHR(mode)) {
			fputs(COLOR_DEV, stdout);
			need_reset = 1;
		} else if (S_ISREG(mode) && (mode & (S_IXUSR | S_IXGRP | S_IXOTH))) {
			fputs(COLOR_EXE, stdout);
			need_reset = 1;
		}
	}
	printname(name);
	if (need_reset)
		fputs(COLOR_RST, stdout);
#else
	(void)mode;
	printname(name);
#endif
}

#include <sys/ioctl.h>

static void
gettermwidth(void)
{
	struct winsize ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
		termwidth = ws.ws_col;
	else
		termwidth = 80;
}

static size_t
entrywidth(const struct entry *ent)
{
	size_t w;
	char buf[32];

	w = utflen(ent->name);
	if (iflag) {
		snprintf(buf, sizeof(buf), "%lu ", (unsigned long)ent->ino);
		w += strlen(buf);
	}
	w += strlen(indicator(ent->mode));
	return w;
}

static void
printcols(const struct entry *ents, size_t n)
{
	int i, r, c, ncols, nrows, total_width;
	int *colwidths;
	int maxcols;

	if (n == 0)
		return;

	gettermwidth();

	colwidths = ecalloc(n, sizeof(*colwidths));

	maxcols = termwidth / 2;
	if (maxcols > (int)n)
		maxcols = n;

	for (ncols = maxcols; ncols > 1; ncols--) {
		nrows = (n + ncols - 1) / ncols;
		total_width = 0;

		for (c = 0; c < ncols; c++) {
			int maxw = 0;
			for (r = 0; r < nrows; r++) {
				int idx = c * nrows + r;
				if (idx < (int)n) {
					int w = entrywidth(&ents[idx]);
					if (w > maxw)
						maxw = w;
				}
			}
			colwidths[c] = maxw;
			total_width += maxw;
		}
		total_width += 2 * (ncols - 1);

		if (total_width < termwidth)
			break;
	}

	if (ncols <= 1) {
		for (i = 0; i < (int)n; i++) {
			output(&ents[i]);
		}
		free(colwidths);
		return;
	}

	nrows = (n + ncols - 1) / ncols;
	for (r = 0; r < nrows; r++) {
		for (c = 0; c < ncols; c++) {
			int idx = c * nrows + r;
			if (idx < (int)n) {
				int w = entrywidth(&ents[idx]);
				if (iflag)
					printf("%lu ", (unsigned long)ents[idx].ino);
				printname_colored(ents[idx].name, ents[idx].mode);
				fputs(indicator(ents[idx].mode), stdout);

				if (c < ncols - 1 && (c + 1) * nrows + r < (int)n) {
					int pad = colwidths[c] - w + 2;
					while (pad-- > 0)
						putchar(' ');
				}
			}
		}
		putchar('\n');
	}

	free(colwidths);
}

static void
output(const struct entry *ent)
{
	struct group *gr;
	struct passwd *pw;
	struct tm *tm;
	ssize_t len;
	char *fmt, buf[BUFSIZ], pwname[_SC_LOGIN_NAME_MAX],
	     grname[_SC_LOGIN_NAME_MAX], mode[] = "----------";

	if (iflag)
		printf("%lu ", (unsigned long)ent->ino);
	if (!lflag) {
		printname_colored(ent->name, ent->mode);
		puts(indicator(ent->mode));
		return;
	}
	if (S_ISREG(ent->mode))
		mode[0] = '-';
	else if (S_ISBLK(ent->mode))
		mode[0] = 'b';
	else if (S_ISCHR(ent->mode))
		mode[0] = 'c';
	else if (S_ISDIR(ent->mode))
		mode[0] = 'd';
	else if (S_ISFIFO(ent->mode))
		mode[0] = 'p';
	else if (S_ISLNK(ent->mode))
		mode[0] = 'l';
	else if (S_ISSOCK(ent->mode))
		mode[0] = 's';
	else
		mode[0] = '?';

	if (ent->mode & S_IRUSR) mode[1] = 'r';
	if (ent->mode & S_IWUSR) mode[2] = 'w';
	if (ent->mode & S_IXUSR) mode[3] = 'x';
	if (ent->mode & S_IRGRP) mode[4] = 'r';
	if (ent->mode & S_IWGRP) mode[5] = 'w';
	if (ent->mode & S_IXGRP) mode[6] = 'x';
	if (ent->mode & S_IROTH) mode[7] = 'r';
	if (ent->mode & S_IWOTH) mode[8] = 'w';
	if (ent->mode & S_IXOTH) mode[9] = 'x';

	if (ent->mode & S_ISUID) mode[3] = (mode[3] == 'x') ? 's' : 'S';
	if (ent->mode & S_ISGID) mode[6] = (mode[6] == 'x') ? 's' : 'S';
	if (ent->mode & S_ISVTX) mode[9] = (mode[9] == 'x') ? 't' : 'T';

	if (!nflag && (pw = getpwuid(ent->uid)))
		snprintf(pwname, sizeof(pwname), "%s", pw->pw_name);
	else
		snprintf(pwname, sizeof(pwname), "%d", ent->uid);

	if (!nflag && (gr = getgrgid(ent->gid)))
		snprintf(grname, sizeof(grname), "%s", gr->gr_name);
	else
		snprintf(grname, sizeof(grname), "%d", ent->gid);

	if (time(NULL) > ent->t.tv_sec + (180 * 24 * 60 * 60)) /* 6 months ago? */
		fmt = "%b %d  %Y";
	else
		fmt = "%b %d %H:%M";

	if ((tm = localtime(&ent->t.tv_sec)))
		strftime(buf, sizeof(buf), fmt, tm);
	else
		snprintf(buf, sizeof(buf), "%lld", (long long)(ent->t.tv_sec));
	printf("%s %4ld ", mode, (long)ent->nlink);
	if (!gflag)
		printf("%-8.8s ", pwname);
	if (!oflag)
		printf("%-8.8s ", grname);

	if (S_ISBLK(ent->mode) || S_ISCHR(ent->mode))
		printf("%4u, %4u ", major(ent->rdev), minor(ent->rdev));
	else if (hflag)
		printf("%10s ", humansize(ent->size));
	else
		printf("%10lu ", (unsigned long)ent->size);
	printf("%s ", buf);
	printname_colored(ent->name, ent->mode);
	fputs(indicator(ent->mode), stdout);
	if (S_ISLNK(ent->mode)) {
		if ((len = readlink(ent->name, buf, sizeof(buf) - 1)) < 0)
			eprintf("readlink %s:", ent->name);
		buf[len] = '\0';
		printf(" -> ");
		printname_colored(buf, ent->tmode);
		fputs(indicator(ent->tmode), stdout);
	}
	putchar('\n');
}

static int
entcmp(const void *va, const void *vb)
{
	int cmp = 0;
	const struct entry *a = va, *b = vb;

	switch (sort) {
	// ?man -S: sort by file size
	case 'S':
		cmp = b->size - a->size;
		break;
	// ?man -t: sort by modification time
	case 't':
		if (!(cmp = b->t.tv_sec - a->t.tv_sec))
			cmp = b->t.tv_nsec - a->t.tv_nsec;
		break;
	}

	if (!cmp)
		cmp = strcmp(a->name, b->name);

	return rflag ? 0 - cmp : cmp;
}

static void
lsdir(const char *path, const struct entry *dir)
{
	DIR *dp;
	struct entry *ent, *ents = NULL;
	struct dirent *d;
	size_t i, n = 0;
	char prefix[PATH_MAX];

	if (!(dp = opendir(dir->name))) {
		ret = 1;
		weprintf("opendir %s%s:", path, dir->name);
		return;
	}
	if (chdir(dir->name) < 0)
		eprintf("chdir %s:", dir->name);

	while ((d = readdir(dp))) {
		if (d->d_name[0] == '.' && !aflag && !Aflag)
			continue;
		else if (Aflag)
			if (strcmp(d->d_name, ".") == 0 ||
			    strcmp(d->d_name, "..") == 0)
				continue;

		ents = ereallocarray(ents, ++n, sizeof(*ents));
		mkent(&ents[n - 1], estrdup(d->d_name), Fflag || iflag ||
		    lflag || pflag || Rflag || sort || should_color(), Lflag);
	}

	closedir(dp);

	if (!Uflag)
		qsort(ents, n, sizeof(*ents), entcmp);

	if (path[0] || showdirs) {
		fputs(path, stdout);
		printname(dir->name);
		puts(":");
	}
	if (!lflag && Cflag) {
		printcols(ents, n);
	} else {
		for (i = 0; i < n; i++)
			output(&ents[i]);
	}

	if (Rflag) {
		if (snprintf(prefix, PATH_MAX, "%s%s/", path, dir->name) >=
		    PATH_MAX)
			eprintf("path too long: %s%s\n", path, dir->name);

		for (i = 0; i < n; i++) {
			ent = &ents[i];
			if (strcmp(ent->name, ".") == 0 ||
			    strcmp(ent->name, "..") == 0)
				continue;
			if (S_ISLNK(ent->mode) && S_ISDIR(ent->tmode) && !Lflag)
				continue;

			ls(prefix, ent, 1);
		}
	}

	for (i = 0; i < n; ++i)
		free(ents[i].name);
	free(ents);
}

static int
visit(const struct entry *ent)
{
	dev_t dev;
	ino_t ino;
	int i;

	dev = ent->dev;
	ino = S_ISLNK(ent->mode) ? ent->tino : ent->ino;

	for (i = 0; i < PATH_MAX && tree[i].ino; ++i) {
		if (ino == tree[i].ino && dev == tree[i].dev)
			return -1;
	}

	tree[i].ino = ino;
	tree[i].dev = dev;

	return i;
}

static void
ls(const char *path, const struct entry *ent, int listdir)
{
	int treeind;
	char cwd[PATH_MAX];

	if (!listdir) {
		output(ent);
	} else if (S_ISDIR(ent->mode) ||
	    (S_ISLNK(ent->mode) && S_ISDIR(ent->tmode))) {
		if ((treeind = visit(ent)) < 0) {
			ret = 1;
			weprintf("%s%s: Already visited\n", path, ent->name);
			return;
		}

		if (!getcwd(cwd, PATH_MAX))
			eprintf("getcwd:");

		if (first)
			first = 0;
		else
			putchar('\n');

		lsdir(path, ent);
		tree[treeind].ino = 0;

		if (chdir(cwd) < 0)
			eprintf("chdir %s:", cwd);
	}
}

static void
usage(void)
{
	eprintf("usage: %s [-1ACacdFfGghiLlnopqRrtUu] [--color[=always|never|auto]] [file ...]\n", argv0);
}

// ?man ls: list directory contents
// ?man arguments: [file ...]
// ?man list information about files and directories
int
main(int argc, char *argv[])
{
	struct entry ent, *dents, *fents;
	size_t i, ds, fs;
#if FEATURE_LS_COLOR
	char *val;
#endif

	if (isatty(STDOUT_FILENO))
		Cflag = 1;
	else
		one_flag = 1;

#if FEATURE_LS_COLOR
	if ((val = getenv("CLICOLOR_FORCE"))) {
		if (*val && strcmp(val, "0") != 0)
			color_mode = COLOR_ALWAYS;
		else
			color_mode = COLOR_NEVER;
	} else if ((val = getenv("CLICOLOR"))) {
		if (*val && strcmp(val, "0") != 0)
			color_mode = COLOR_AUTO;
		else
			color_mode = COLOR_NEVER;
	}
#endif

	tree = ereallocarray(NULL, PATH_MAX, sizeof(*tree));

	ARGBEGIN {
	// ?man -1: list one file per line
	case '1':
		one_flag = 1;
		Cflag = 0;
		lflag = 0;
		break;
	// ?man -A: list all entries except dot and dot dot
	case 'A':
		Aflag = 1;
		break;
	// ?man -a: list all entries including those starting with a dot
	case 'a':
		aflag = 1;
		break;
	// ?man -c: sort by ctime or use ctime for long listing
	case 'c':
		cflag = 1;
		uflag = 0;
		break;
	// ?man -C: list entries in columns sorted vertically
	case 'C':
		Cflag = 1;
		one_flag = 0;
		lflag = 0;
		break;
	// ?man -d: list directory entries instead of their contents
	case 'd':
		dflag = 1;
		break;
	// ?man -f: do not sort and enable a and U
	case 'f':
		aflag = 1;
		fflag = 1;
		Uflag = 1;
		break;
	// ?man -F: append type indicators
	case 'F':
		Fflag = 1;
		break;
#if FEATURE_LS_COLOR
	// ?man -G: enable colored output
	case 'G':
		color_mode = COLOR_AUTO;
		break;
#endif
	// ?man -g: list in long format without owner name
	case 'g':
		gflag = 1;
		lflag = 1;
		Cflag = 0;
		one_flag = 0;
		break;
	// ?man -H: follow symlinks on the command line
	case 'H':
		Hflag = 1;
		break;
	// ?man -h: print human readable sizes
	case 'h':
		hflag = 1;
		break;
	// ?man -i: print inode number of each file
	case 'i':
		iflag = 1;
		break;
	// ?man -L: follow all symlinks
	case 'L':
		Lflag = 1;
		break;
	// ?man -l: use a long listing format
	case 'l':
		lflag = 1;
		Cflag = 0;
		one_flag = 0;
		break;
	// ?man -n: list numeric uids and gids
	case 'n':
		lflag = 1;
		nflag = 1;
		Cflag = 0;
		one_flag = 0;
		break;
	// ?man -o: list in long format without group name
	case 'o':
		oflag = 1;
		lflag = 1;
		Cflag = 0;
		one_flag = 0;
		break;
	// ?man -p: append slash indicator to directories
	case 'p':
		pflag = 1;
		break;
	// ?man -q: print non printable characters as question marks
	case 'q':
		qflag = 1;
		break;
	// ?man -R: list subdirectories recursively
	case 'R':
		Rflag = 1;
		break;
	// ?man -r: reverse sort order
	case 'r':
		rflag = 1;
		break;
	// ?man -S: sort by file size
	case 'S':
		sort = 'S';
		break;
	// ?man -t: sort by modification time
	case 't':
		sort = 't';
		break;
	// ?man -U: do not sort
	case 'U':
		Uflag = 1;
		break;
	// ?man -u: sort by atime or use atime for long listing
	case 'u':
		uflag = 1;
		cflag = 0;
		break;
	// ?man --: specify - option
	case '-':
#if FEATURE_LS_COLOR
		// ?man --color [when]: control coloring
		if (strncmp(argv[0], "-color", 6) == 0) {
			char *val = NULL;
			if (argv[0][6] == '=') {
				val = &argv[0][7];
			} else if (argv[0][6] == '\0') {
				val = "always";
			}
			if (val) {
				if (strcmp(val, "always") == 0)
					color_mode = COLOR_ALWAYS;
				else if (strcmp(val, "never") == 0)
					color_mode = COLOR_NEVER;
				else if (strcmp(val, "auto") == 0)
					color_mode = COLOR_AUTO;
				else {
					fprintf(stderr, "ls: invalid --color value: %s\n", val);
					usage();
				}
			}
			brk_ = 1;
		} else {
			usage();
		}
#else
		usage();
#endif
		break;
	default:
		usage();
	} ARGEND

	switch (argc) {
	case 0:
		*--argv = ".", ++argc;
		/* fallthrough */
	case 1:
		mkent(&ent, argv[0], 1, Hflag || Lflag);
		ls("", &ent, (!dflag && S_ISDIR(ent.mode)) ||
		    (S_ISLNK(ent.mode) && S_ISDIR(ent.tmode) &&
		     !(dflag || Fflag || lflag)));

		break;
	default:
		for (i = ds = fs = 0, fents = dents = NULL; i < (size_t)argc; ++i) {
			mkent(&ent, argv[i], 1, Hflag || Lflag);

			if ((!dflag && S_ISDIR(ent.mode)) ||
			    (S_ISLNK(ent.mode) && S_ISDIR(ent.tmode) &&
			     !(dflag || Fflag || lflag))) {
				dents = ereallocarray(dents, ++ds, sizeof(*dents));
				memcpy(&dents[ds - 1], &ent, sizeof(ent));
			} else {
				fents = ereallocarray(fents, ++fs, sizeof(*fents));
				memcpy(&fents[fs - 1], &ent, sizeof(ent));
			}
		}

		showdirs = ds > 1 || (ds && fs);

		qsort(fents, fs, sizeof(ent), entcmp);
		qsort(dents, ds, sizeof(ent), entcmp);

		if (!lflag && Cflag && fs > 0) {
			printcols(fents, fs);
		} else {
			for (i = 0; i < fs; ++i)
				ls("", &fents[i], 0);
		}
		free(fents);
		if (fs && ds)
			putchar('\n');
		for (i = 0; i < ds; ++i)
			ls("", &dents[i], 1);
		free(dents);
	}

	return (fshut(stdout, "<stdout>") | ret);
}
