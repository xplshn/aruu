

#include "config.h"
#include "fs.h"
#include "utf.h"
#include "util.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <grp.h>
#include <libgen.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#if FEATURE_TAR_TTY_SAFE
static void
safe_puts(const char *s)
{
	Rune r;
	int n;

	if (isatty(1)) {
		while (*s) {
			n = chartorune(&r, s);
			if (r == Runeerror) {
				putchar('?');
				s++;
			} else if (isprintrune(r)) {
				fputrune(&r, stdout);
				s += n;
			} else {
				putchar('?');
				s += n;
			}
		}
		putchar('\n');
	} else {
		puts(s);
	}
}
#else
#define safe_puts(s) puts(s)
#endif

#define BLKSIZ (sizeof (struct header)) /* must equal 512 bytes */

enum Type {
	REG       = '0',
	AREG      = '\0',
	HARDLINK  = '1',
	SYMLINK   = '2',
	CHARDEV   = '3',
	BLOCKDEV  = '4',
	DIRECTORY = '5',
	FIFO      = '6',
	RESERVED  = '7'
};

struct header {
	char name[100];
	char mode[8];
	char uid[8];
	char gid[8];
	char size[12];
	char mtime[12];
	char chksum[8];
	char type;
	char linkname[100];
	char magic[6];
	char version[2];
	char uname[32];
	char gname[32];
	char major[8];
	char minor[8];
	char prefix[155];
	char padding[12];
};

static struct dirtime {
	char *name;
	time_t mtime;
} *dirtimes;

static size_t dirtimeslen;

static int tarfd;
static ino_t tarinode;
static dev_t tardev;

static int mflag, vflag;
static int filtermode;

#if FEATURE_TAR_EXCLUDE
static char **excludes = NULL;
static size_t excludes_cnt = 0;

static void
add_exclude(const char *pattern)
{
	excludes = ereallocarray(excludes, excludes_cnt + 1, sizeof(*excludes));
	excludes[excludes_cnt++] = estrdup(pattern);
}

static int
is_excluded(const char *path)
{
	size_t i;
	const char *base = strrchr(path, '/');
	base = base ? base + 1 : path;

	for (i = 0; i < excludes_cnt; i++) {
		if (fnmatch(excludes[i], path, 0) == 0 ||
		    fnmatch(excludes[i], base, 0) == 0)
			return 1;
	}
	return 0;
}
#endif
static const char *filtertool;

static const char *filtertools[] = {
	['J'] = "xz",
	['Z'] = "compress",
	['a'] = "lzma",
	['j'] = "bzip2",
	['z'] = "gzip",
};

#if FEATURE_TAR_TO_STDOUT
static int Oflag_stdout = 0;
#else
#define Oflag_stdout 0
#endif

#if FEATURE_TAR_KEEP_OLD
static int kflag_keep = 0;
#else
#define kflag_keep 0
#endif

#if FEATURE_TAR_STRIP_COMPONENTS
static int strip_components_count = 0;

static char *
strip_components(char *path, int count)
{
	char *p = path;
	int i;

	for (i = 0; i < count; i++) {
		p = strchr(p, '/');
		if (!p)
			return NULL;
		while (*p == '/')
			p++;
	}
	return p;
}
#else
#define strip_components_count 0
#endif

#if FEATURE_TAR_FILES_FROM
static char **files_from = NULL;
static size_t files_from_cnt = 0;

static void
add_files_from(const char *path)
{
	files_from = ereallocarray(files_from, files_from_cnt + 1, sizeof(*files_from));
	files_from[files_from_cnt++] = estrdup(path);
}

static void
load_files_from_file(const char *path)
{
	FILE *fp = fopen(path, "r");
	char line[PATH_MAX];

	if (!fp)
		eprintf("open %s:", path);
	while (fgets(line, sizeof(line), fp)) {
		size_t len = strlen(line);
		while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
			len--;
		line[len] = '\0';
		if (len > 0)
			add_files_from(line);
	}
	fclose(fp);
}
#else
#define files_from_cnt 0
#endif

#if FEATURE_TAR_EXCLUDE_FROM
static void
load_excludes_from_file(const char *path)
{
	FILE *fp = fopen(path, "r");
	char line[PATH_MAX];

	if (!fp)
		eprintf("open %s:", path);
	while (fgets(line, sizeof(line), fp)) {
		size_t len = strlen(line);
		while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
			len--;
		line[len] = '\0';
		if (len > 0) {
#if FEATURE_TAR_EXCLUDE
			add_exclude(line);
#else
			(void)line;
#endif
		}
	}
	fclose(fp);
}
#endif

static void
pushdirtime(char *name, time_t mtime)
{
	dirtimes = ereallocarray(dirtimes, dirtimeslen + 1, sizeof(*dirtimes));
	dirtimes[dirtimeslen].name = estrdup(name);
	dirtimes[dirtimeslen].mtime = mtime;
	dirtimeslen++;
}

static struct dirtime *
popdirtime(void)
{
	if (dirtimeslen) {
		dirtimeslen--;
		return &dirtimes[dirtimeslen];
	}
	return NULL;
}

static int
comp(int fd, const char *tool, const char *flags)
{
	int fds[2];

	if (pipe(fds) < 0)
		eprintf("pipe:");

	switch (fork()) {
	case -1:
		eprintf("fork:");
		/* fallthrough */
	case 0:
		dup2(fd, 1);
		dup2(fds[0], 0);
		close(fds[0]);
		close(fds[1]);

		execlp(tool, tool, flags, NULL);
		weprintf("execlp %s:", tool);
		_exit(1);
	}
	close(fds[0]);
	return fds[1];
}

static int
decomp(int fd, const char *tool, const char *flags)
{
	int fds[2];

	if (pipe(fds) < 0)
		eprintf("pipe:");

	switch (fork()) {
	case -1:
		eprintf("fork:");
		/* fallthrough */
	case 0:
		dup2(fd, 0);
		dup2(fds[1], 1);
		close(fds[0]);
		close(fds[1]);

		execlp(tool, tool, flags, NULL);
		weprintf("execlp %s:", tool);
		_exit(1);
	}
	close(fds[1]);
	return fds[0];
}

static ssize_t
eread(int fd, void *buf, size_t n)
{
	ssize_t r;

again:
	r = read(fd, buf, n);
	if (r < 0) {
		if (errno == EINTR)
			goto again;
		eprintf("read:");
	}
	return r;
}

static ssize_t
ewrite(int fd, const void *buf, size_t n)
{
	ssize_t r;

	if ((r = write(fd, buf, n)) < 0 || (size_t)r != n)
		eprintf("write:");
	return r;
}

static unsigned
chksum(struct header *h)
{
	unsigned sum, i;

	memset(h->chksum, ' ', sizeof(h->chksum));
	for (i = 0, sum = 0, assert(BLKSIZ == 512); i < BLKSIZ; i++)
		sum += *((unsigned char *)h + i);
	return sum;
}

#if FEATURE_TAR_CREATE
static void
putoctal(char *dst, unsigned num, int size)
{
	if (snprintf(dst, size, "%.*o", size - 1, num) >= size)
		eprintf("putoctal: input number '%o' too large\n", num);
}

static int
archive(const char *path)
{
	static const struct header blank = {
		.name = "././@LongLink",
		.mode = "0000600",
		.uid = "0000000",
		.gid = "0000000",
		.size = "00000000000",
		.mtime = "00000000000",
		.chksum = "       ",
		.type = AREG,
		.linkname = "",
		.magic = "ustar",
		.version = { '0', '0' }
	};
	char   b[BLKSIZ + BLKSIZ], *p;
	struct header *h = (struct header *)b;
	struct group  *gr;
	struct passwd *pw;
	struct stat st;
	ssize_t l, n, r;
	int fd = -1;

	if (lstat(path, &st) < 0) {
		weprintf("lstat %s:", path);
		return 0;
	} else if (st.st_ino == tarinode && st.st_dev == tardev) {
		weprintf("ignoring %s\n", path);
		return 0;
	}
	pw = getpwuid(st.st_uid);
	gr = getgrgid(st.st_gid);

	*h = blank;
	n  = strlcpy(h->name, path, sizeof(h->name));
	if ((size_t)n >= sizeof(h->name)) {
		*++h = blank;
		h->type = 'L';
		putoctal(h->size,   n,         sizeof(h->size));
		putoctal(h->chksum, chksum(h), sizeof(h->chksum));
		ewrite(tarfd, (char *)h, BLKSIZ);

		for (p = (char *)path; n > 0; n -= BLKSIZ, p += BLKSIZ) {
			if ((size_t)n < BLKSIZ) {
				p = memcpy(h--, p, n);
				memset(p + n, 0, BLKSIZ - (size_t)n);
			}
			ewrite(tarfd, p, BLKSIZ);
		}
	}

	putoctal(h->mode,    (unsigned)st.st_mode & 0777, sizeof(h->mode));
	putoctal(h->uid,     (unsigned)st.st_uid,         sizeof(h->uid));
	putoctal(h->gid,     (unsigned)st.st_gid,         sizeof(h->gid));
	putoctal(h->mtime,   (unsigned)st.st_mtime,       sizeof(h->mtime));
	estrlcpy(h->uname,   pw ? pw->pw_name : "",       sizeof(h->uname));
	estrlcpy(h->gname,   gr ? gr->gr_name : "",       sizeof(h->gname));

	if (S_ISREG(st.st_mode)) {
		h->type = REG;
		putoctal(h->size, st.st_size,  sizeof(h->size));
		fd = open(path, O_RDONLY);
		if (fd < 0)
			eprintf("open %s:", path);
	} else if (S_ISDIR(st.st_mode)) {
		h->type = DIRECTORY;
	} else if (S_ISLNK(st.st_mode)) {
		h->type = SYMLINK;
		if ((r = readlink(path, h->linkname, sizeof(h->linkname) - 1)) < 0)
			eprintf("readlink %s:", path);
		h->linkname[r] = '\0';
	} else if (S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode)) {
		h->type = S_ISCHR(st.st_mode) ? CHARDEV : BLOCKDEV;
		putoctal(h->major, (unsigned)major(st.st_dev), sizeof(h->major));
		putoctal(h->minor, (unsigned)minor(st.st_dev), sizeof(h->minor));
	} else if (S_ISFIFO(st.st_mode)) {
		h->type = FIFO;
	}

	putoctal(h->chksum, chksum(h), sizeof(h->chksum));
	ewrite(tarfd, b, BLKSIZ);

	if (fd != -1) {
		while ((l = eread(fd, b, BLKSIZ)) > 0) {
			if ((size_t)l < BLKSIZ)
				memset(b + l, 0, BLKSIZ - (size_t)l);
			ewrite(tarfd, b, BLKSIZ);
		}
		close(fd);
	}

	return 0;
}
#endif

static void
skipblk(ssize_t l)
{
	char b[BLKSIZ];

	for (; l > 0; l -= BLKSIZ)
		if (!eread(tarfd, b, BLKSIZ))
			break;
}

static int
unarchive(char *fname, ssize_t l, char b[BLKSIZ])
{
	struct header *h = (struct header *)b;
	struct timespec times[2];
	struct stat st;
	char lname[101], *tmp, *p;
	long mode, major, minor, type, mtime, uid, gid;
	int  fd = -1, lnk = h->type == SYMLINK;

	if (kflag_keep && !Oflag_stdout) {
		if (lstat(fname, &st) == 0) {
			skipblk(l);
			return 0;
		}
	}

	if (!mflag && ((mtime = strtol(h->mtime, &p, 8)) < 0 || *p != '\0'))
		eprintf("strtol %s: invalid mtime\n", h->mtime);

	if (Oflag_stdout) {
		if (h->type == REG || h->type == AREG || h->type == RESERVED) {
			fd = 1;
		} else {
			return 0;
		}
	} else {
		if (strcmp(fname, ".") && strcmp(fname, "./") && remove(fname) < 0)
			if (errno != ENOENT) weprintf("remove %s:", fname);

		tmp = estrdup(fname);
		mkdirp(dirname(tmp), 0777, 0777);
		free(tmp);

		switch (h->type) {
		case REG:
		case AREG:
		case RESERVED:
			if ((mode = strtol(h->mode, &p, 8)) < 0 || *p != '\0')
				eprintf("strtol %s: invalid mode\n", h->mode);
#if FEATURE_TAR_NOFOLLOW
			fd = open(fname, O_WRONLY | O_TRUNC | O_CREAT | O_NOFOLLOW, 0600);
#else
			fd = open(fname, O_WRONLY | O_TRUNC | O_CREAT, 0600);
#endif
			if (fd < 0)
				eprintf("open %s:", fname);
			break;
		case HARDLINK:
		case SYMLINK:
			snprintf(lname, sizeof(lname), "%.*s", (int)sizeof(h->linkname),
			         h->linkname);
			if ((lnk ? symlink:link)(lname, fname) < 0)
				eprintf("%s %s -> %s:", lnk ? "symlink":"link", fname, lname);
			lnk++;
			break;
		case DIRECTORY:
			if ((mode = strtol(h->mode, &p, 8)) < 0 || *p != '\0')
				eprintf("strtol %s: invalid mode\n", h->mode);
			if (mkdir(fname, (mode_t)mode) < 0 && errno != EEXIST)
				eprintf("mkdir %s:", fname);
			pushdirtime(fname, mtime);
			break;
		case CHARDEV:
		case BLOCKDEV:
			if ((mode = strtol(h->mode, &p, 8)) < 0 || *p != '\0')
				eprintf("strtol %s: invalid mode\n", h->mode);
			if ((major = strtol(h->major, &p, 8)) < 0 || *p != '\0')
				eprintf("strtol %s: invalid major device\n", h->major);
			if ((minor = strtol(h->minor, &p, 8)) < 0 || *p != '\0')
				eprintf("strtol %s: invalid minor device\n", h->minor);
			type = (h->type == CHARDEV) ? S_IFCHR : S_IFBLK;
			if (mknod(fname, type | mode, makedev(major, minor)) < 0)
				eprintf("mknod %s:", fname);
			break;
		case FIFO:
			if ((mode = strtol(h->mode, &p, 8)) < 0 || *p != '\0')
				eprintf("strtol %s: invalid mode\n", h->mode);
			if (mknod(fname, S_IFIFO | mode, 0) < 0)
				eprintf("mknod %s:", fname);
			break;
		default:
			eprintf("unsupported tar-filetype %c\n", h->type);
		}
	}

	if (!Oflag_stdout) {
		if ((uid = strtol(h->uid, &p, 8)) < 0 || *p != '\0')
			eprintf("strtol %s: invalid uid\n", h->uid);
		if ((gid = strtol(h->gid, &p, 8)) < 0 || *p != '\0')
			eprintf("strtol %s: invalid gid\n", h->gid);
	}

	if (fd != -1) {
		for (; l > 0; l -= BLKSIZ)
			if (eread(tarfd, b, BLKSIZ) > 0)
				ewrite(fd, b, MIN(l, (ssize_t)BLKSIZ));
		if (fd != 1)
			close(fd);
	}

	if (Oflag_stdout)
		return 0;

	if (lnk == 1)
		return 0;

	times[0].tv_sec = times[1].tv_sec = mtime;
	times[0].tv_nsec = times[1].tv_nsec = 0;
	if (!mflag && utimensat(AT_FDCWD, fname, times, AT_SYMLINK_NOFOLLOW) < 0)
		weprintf("utimensat %s:", fname);
	if (lnk) {
		if (!getuid() && lchown(fname, uid, gid))
			weprintf("lchown %s:", fname);
	} else {
		if (!getuid() && chown(fname, uid, gid))
			weprintf("chown %s:", fname);
		if (chmod(fname, mode) < 0)
			eprintf("fchmod %s:", fname);
	}

	return 0;
}


static int
print(char *fname, ssize_t l, char b[BLKSIZ])
{
	(void)b;
	safe_puts(fname);
	skipblk(l);
	return 0;
}

#if FEATURE_TAR_CREATE
static void
c(int dirfd, const char *name, struct stat *st, void *data, struct recursor *r)
{
	(void)data;
#if FEATURE_TAR_EXCLUDE
	if (is_excluded(r->path))
		return;
#endif
	archive(r->path);
	if (vflag)
		safe_puts(r->path);

	if (S_ISDIR(st->st_mode))
		recurse(dirfd, name, NULL, r);
}
#endif

static void
sanitize(struct header *h)
{
	size_t i, j, l;
	struct {
		char  *f;
		size_t l;
	} fields[] = {
		{ h->mode,   sizeof(h->mode)   },
		{ h->uid,    sizeof(h->uid)    },
		{ h->gid,    sizeof(h->gid)    },
		{ h->size,   sizeof(h->size)   },
		{ h->mtime,  sizeof(h->mtime)  },
		{ h->chksum, sizeof(h->chksum) },
		{ h->major,  sizeof(h->major)  },
		{ h->minor,  sizeof(h->minor)  }
	};

	/* Numeric fields can be terminated with spaces instead of
	 * NULs as per the ustar specification.  Patch all of them to
	 * use NULs so we can perform string operations on them. */
	for (i = 0; i < LEN(fields); i++){
		j = 0, l = fields[i].l - 1;
		for (; j < l && fields[i].f[j] == ' '; j++);
		for (; j <= l; j++)
			if (fields[i].f[j] == ' ')
				fields[i].f[j] = '\0';
		if (fields[i].f[l])
			eprintf("numeric field #%d (%.*s) is not null or space terminated\n",
			        i, l+1, fields[i].f);
	}
}

static void
chktar(struct header *h)
{
	const char *reason;
	char tmp[sizeof h->chksum], *err;
	long sum, i;

	if (h->prefix[0] == '\0' && h->name[0] == '\0') {
		reason = "empty filename";
		goto bad;
	}
	if (h->magic[0] && strncmp("ustar", h->magic, 5)) {
		reason = "not ustar format";
		goto bad;
	}
	memcpy(tmp, h->chksum, sizeof(tmp));
	for (i = sizeof(tmp)-1; i > 0 && tmp[i] == ' '; i--) {
		tmp[i] = '\0';
	}
	sum = strtol(tmp, &err, 8);
	if (sum < 0 || sum >= (long)(BLKSIZ*256) || *err != '\0') {
		reason = "invalid checksum";
		goto bad;
	}
	if (sum != chksum(h)) {
		reason = "incorrect checksum";
		goto bad;
	}
	memcpy(h->chksum, tmp, sizeof(tmp));
	return;
bad:
	eprintf("malformed tar archive: %s\n", reason);
}

static void
xt(int argc, char *argv[], int mode)
{
	long size;
	char b[BLKSIZ], fname[PATH_MAX + 1], *p, *q = NULL, *stripped;
	int i, m, n, match;
	int (*fn)(char *, ssize_t, char[BLKSIZ]) = (mode == 'x') ? unarchive : print;
	struct timespec times[2];
	struct header *h = (struct header *)b;
	struct dirtime *dirtime;
#if FEATURE_TAR_FILES_FROM
	size_t idx;
#endif

	while (eread(tarfd, b, BLKSIZ) > 0 && (h->name[0] || h->prefix[0])) {
		chktar(h);
		sanitize(h);

		if ((size = strtol(h->size, &p, 8)) < 0 || *p != '\0')
			eprintf("strtol %s: invalid size\n", h->size);

		/* Long file path is read directly into fname*/
		if (h->type == 'L' || h->type == 'x' || h->type == 'g') {

			/* Read header only up to size of fname buffer */
			for (q = fname; q < fname+size; q += BLKSIZ) {
				if (q + BLKSIZ >= fname + sizeof fname)
					eprintf("name exceeds buffer: %.*s\n", q-fname, fname);
				eread(tarfd, q, BLKSIZ);
			}

			/* Convert pax x header with 'path=' field into L header */
			if (h->type == 'x') for (q = fname; q < fname+size-16; q += n) {
				if ((n = strtol(q, &p, 10)) < 0 || *p != ' ')
					eprintf("strtol %.*s: invalid number\n", p+1-q, q);
				if (n && strncmp(p+1, "path=", 5) == 0) {
					memmove(fname, p+6, size = q+n - p-6 - 1);
					h->type = 'L';
					break;
				}
			}
			fname[size] = '\0';

			/* Non L-like header (eg. pax 'g') is skipped by setting q=null */
			if (h->type != 'L')
				q = NULL;
			continue;
		}

		/* Ustar path is copied into fname if no L header (ie: q is NULL) */
		if (!q) {
			m = sizeof h->prefix, n = sizeof h->name;
			p = "/" + !h->prefix[0];
			snprintf(fname, sizeof fname, "%.*s%s%.*s", m, h->prefix, p, n, h->name);
		}
		q = NULL;

		/* If argc > 0 or files_from_cnt > 0 then only extract the matching files/dirs */
		if (argc || files_from_cnt) {
			match = 0;
			for (i = 0; i < argc; i++) {
				if (strncmp(argv[i], fname, n = strlen(argv[i])) == 0) {
					if (strchr("/", fname[n]) || argv[i][n-1] == '/') {
						match = 1;
						break;
					}
				}
			}
#if FEATURE_TAR_FILES_FROM
			if (!match) {
				for (idx = 0; idx < files_from_cnt; idx++) {
					if (strncmp(files_from[idx], fname, n = strlen(files_from[idx])) == 0) {
						if (strchr("/", fname[n]) || files_from[idx][n-1] == '/') {
							match = 1;
							break;
						}
					}
				}
			}
#endif
			if (!match) {
				skipblk(size);
				continue;
			}
		}

		stripped = fname;
#if FEATURE_TAR_STRIP_COMPONENTS
		if (mode == 'x' && strip_components_count > 0) {
			stripped = strip_components(fname, strip_components_count);
			if (!stripped || *stripped == '\0') {
				skipblk(size);
				continue;
			}
		}
#endif

		fn(stripped, size, b);
		if (vflag && mode != 't')
			safe_puts(fname);
	}

	if (mode == 'x' && !mflag) {
		while ((dirtime = popdirtime())) {
			times[0].tv_sec = times[1].tv_sec = dirtime->mtime;
			times[0].tv_nsec = times[1].tv_nsec = 0;
			if (utimensat(AT_FDCWD, dirtime->name, times, 0) < 0)
				eprintf("utimensat %s:", fname);
			free(dirtime->name);
		}
		free(dirtimes);
		dirtimes = NULL;
	}
}

char **args;
int argn;

static void
usage(void)
{
#if FEATURE_TAR_CREATE
	eprintf("usage: %s [x | t | -x | -t] [-C dir] [-J | -Z | -a | -j | -z] [-m] [-p] "
	        "[-f file] [file ...]\n"
	        "       %s [c | -c] [-C dir] [-J | -Z | -a | -j | -z] [-h] path ... "
	        "[-f file]\n", argv0, argv0);
#else
	eprintf("usage: %s [x | t | -x | -t] [-C dir] [-J | -Z | -a | -j | -z] [-m] [-p] "
	        "[-f file] [file ...]\n", argv0);
#endif
}

// ?man tar: tape archiver
// ?man arguments: [x | t | -x | -t] [file ...]
// ?man tar [c | -c] [-C dir] [-J | -Z | -a | -j | -z] [-h] [-T file] [-X file] path ... [-f file]
// ?man manipulate tape archive files
int
main(int argc, char *argv[])
{
#if FEATURE_TAR_CREATE
	struct recursor r = { .fn = c, .follow = 'P', .flags = DIRFIRST };
#endif
	struct stat st;
	char *file = NULL, *dir = ".", mode = '\0';
	int fd;
	size_t i;

	argv0 = argv[0];
#if FEATURE_TAR_CREATE
	if (argc > 1 && strchr("cxt", mode = *argv[1]))
#else
	if (argc > 1 && strchr("xt", mode = *argv[1]))
#endif
		*(argv[1]+1) ? *argv[1] = '-' : (*++argv = argv0, --argc);

	ARGBEGIN {
	// ?man -x: extract files from an archive
	case 'x':
#if FEATURE_TAR_CREATE
	// ?man -c: create a new archive
	case 'c':
#endif
	// ?man -t: list the contents of an archive
	case 't':
		mode = ARGC();
		break;
	// ?man -C:dir: specify option flag
	case 'C':
		dir = EARGF(usage());
		break;
	// ?man -f:file: specify archive file
	case 'f':
		file = EARGF(usage());
		break;
	// ?man -m: specify mode or limit
	case 'm':
		mflag = 1;
		break;
	// ?man -J: specify option flag
	case 'J':
	// ?man -Z: specify option flag
	case 'Z':
	// ?man -a: print or show all entries
	case 'a':
	// ?man -j: specify option flag
	case 'j':
	// ?man -z: specify option flag
	case 'z':
		filtermode = ARGC();
		filtertool = filtertools[filtermode];
		break;
	// ?man -h: suppress headers or print help
	case 'h':
#if FEATURE_TAR_CREATE
		r.follow = 'L';
#endif
		break;
	// ?man -v: verbosely list files processed
	case 'v':
		vflag = 1;
		break;
	// ?man -p: preserve file attributes
	case 'p':
		break;  /* do nothing as already default behaviour */
#if FEATURE_TAR_TO_STDOUT
	// ?man -O: extract files to stdout
	case 'O':
		Oflag_stdout = 1;
		break;
#endif
#if FEATURE_TAR_KEEP_OLD
	// ?man -k: keep existing files, do not overwrite
	case 'k':
		kflag_keep = 1;
		break;
#endif
#if FEATURE_TAR_FILES_FROM
	// ?man -T:file: -T file: read filenames from file
	case 'T':
		load_files_from_file(EARGF(usage()));
		break;
#endif
#if FEATURE_TAR_EXCLUDE_FROM
	// ?man -X:file: -X file: exclude patterns in file
	case 'X':
		load_excludes_from_file(EARGF(usage()));
		break;
#endif
#if FEATURE_TAR_STRIP_COMPONENTS
	// ?man -s:num: -strip-components num: strip num components
	case 's':
		if (strcmp(argv[0], "strip-components") == 0) {
			argv[0] = "s";
			strip_components_count = estrtonum(EARGF(usage()), 0, INT_MAX);
			brk_ = 1;
		} else if (strncmp(argv[0], "strip-components=", 17) == 0) {
			strip_components_count = estrtonum(argv[0] + 17, 0, INT_MAX);
			brk_ = 1;
		} else {
			usage();
		}
		break;
#endif
	// ?man --:num: specify - option
	case '-':
#if FEATURE_TAR_EXCLUDE
		if (strncmp(argv[0], "-exclude=", 9) == 0) {
			add_exclude(argv[0] + 9);
			brk_ = 1;
			break;
		} else if (strcmp(argv[0], "-exclude") == 0) {
			argv[0] = "-";
			add_exclude(EARGF(usage()));
			brk_ = 1;
			break;
		}
#endif
#if FEATURE_TAR_EXCLUDE_FROM
		if (strncmp(argv[0], "-exclude-from=", 14) == 0) {
			load_excludes_from_file(argv[0] + 14);
			brk_ = 1;
			break;
		} else if (strcmp(argv[0], "-exclude-from") == 0) {
			argv[0] = "-";
			load_excludes_from_file(EARGF(usage()));
			brk_ = 1;
			break;
		}
#endif
#if FEATURE_TAR_TO_STDOUT
		if (strcmp(argv[0], "-to-stdout") == 0) {
			Oflag_stdout = 1;
			brk_ = 1;
			break;
		}
#endif
#if FEATURE_TAR_KEEP_OLD
		if (strcmp(argv[0], "-keep-old-files") == 0) {
			kflag_keep = 1;
			brk_ = 1;
			break;
		}
#endif
#if FEATURE_TAR_STRIP_COMPONENTS
		if (strncmp(argv[0], "-strip-components=", 18) == 0) {
			strip_components_count = estrtonum(argv[0] + 18, 0, INT_MAX);
			brk_ = 1;
			break;
		} else if (strcmp(argv[0], "-strip-components") == 0) {
			argv[0] = "-";
			strip_components_count = estrtonum(EARGF(usage()), 0, INT_MAX);
			brk_ = 1;
			break;
		}
#endif
#if FEATURE_TAR_FILES_FROM
		if (strncmp(argv[0], "-files-from=", 12) == 0) {
			load_files_from_file(argv[0] + 12);
			brk_ = 1;
			break;
		} else if (strcmp(argv[0], "-files-from") == 0) {
			argv[0] = "-";
			load_files_from_file(EARGF(usage()));
			brk_ = 1;
			break;
		}
#endif
		usage();
		break;
	default:
		usage();
	} ARGEND

	switch (mode) {
#if FEATURE_TAR_CREATE
	// ?man -c: create a new archive
	case 'c':
		if (!argc && !files_from_cnt)
			usage();
		tarfd = 1;
		if (file && *file != '-') {
			tarfd = open(file, O_WRONLY | O_TRUNC | O_CREAT, 0644);
			if (tarfd < 0)
				eprintf("open %s:", file);
			if (lstat(file, &st) < 0)
				eprintf("lstat %s:", file);
			tarinode = st.st_ino;
			tardev = st.st_dev;
		}

		if (filtertool)
			tarfd = comp(tarfd, filtertool, "-cf");

		if (chdir(dir) < 0)
			eprintf("chdir %s:", dir);
		for (; *argv; argc--, argv++)
			recurse(AT_FDCWD, *argv, NULL, &r);
#if FEATURE_TAR_FILES_FROM
		for (i = 0; i < files_from_cnt; i++)
			recurse(AT_FDCWD, files_from[i], NULL, &r);
#endif
		break;
#endif
	// ?man -t: list the contents of an archive
	case 't':
	// ?man -x: extract files from an archive
	case 'x':
		tarfd = 0;
		if (file && *file != '-') {
			tarfd = open(file, O_RDONLY);
			if (tarfd < 0)
				eprintf("open %s:", file);
		}

		if (filtertool) {
			fd = tarfd;
			tarfd = decomp(tarfd, filtertool, "-cdf");
			close(fd);
		}

		if (chdir(dir) < 0)
			eprintf("chdir %s:", dir);
		xt(argc, argv, mode);
		break;
	default:
		usage();
	}

#if FEATURE_TAR_EXCLUDE
	if (excludes) {
		for (i = 0; i < excludes_cnt; i++)
			free(excludes[i]);
		free(excludes);
	}
#endif
#if FEATURE_TAR_FILES_FROM
	if (files_from) {
		for (i = 0; i < files_from_cnt; i++)
			free(files_from[i]);
		free(files_from);
	}
#endif
	return recurse_status;
}
