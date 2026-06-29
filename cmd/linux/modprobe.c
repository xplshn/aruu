
#include "arg.h"
#include "fs.h"
#include "util.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>

#if FEATURE_MODPROBE_SYSLOG
#include <syslog.h>
#endif

#define HASH_SIZE 256
#ifndef LINE_MAX
#define LINE_MAX 4096
#endif

enum ModFlags {
	MOD_LOADED      = 1 << 0,
	MOD_BLACKLISTED = 1 << 1,
	MOD_QUEUED      = 1 << 2,
	MOD_SEEN_DEP    = 1 << 3,
};

struct StrNode {
	char *str;
	struct StrNode *next;
};

struct Module {
	char *name;
	char *path;
	char *options;
	struct StrNode *deps;
	struct StrNode *aliases;
	int flags;
	struct Module *next;
};

static struct Module *mod_db[HASH_SIZE];
static struct StrNode *probes = NULL;
static struct StrNode *moddirs = NULL;
static char *cmdopts = NULL;

static int aflag = 0;
static int rflag = 0;
static int qflag = 0;
static int vflag = 0;
static int lflag = 0;

#if FEATURE_MODPROBE_SHOW_DEPENDS
static int Dflag = 0;
#endif

#if FEATURE_MODPROBE_BLACKLIST
static int bflag = 0;
#endif

#if FEATURE_MODPROBE_SYSLOG
static int sflag = 0;
#endif

/* logging wrappers: handles syslog delegation and quiet mode suppression */

static void
pr_warn(const char *fmt, ...)
{
	va_list ap;
	int saved_errno = errno;
#if FEATURE_MODPROBE_SYSLOG
	char buf[1024];
#endif

	if (qflag)
		return;

	va_start(ap, fmt);
#if FEATURE_MODPROBE_SYSLOG
	if (sflag) {
		vsnprintf(buf, sizeof(buf), fmt, ap);
		if (fmt[0] && fmt[strlen(fmt)-1] == ':')
			syslog(LOG_ERR, "%s %s", buf, strerror(saved_errno));
		else
			syslog(LOG_ERR, "%s", buf);
		va_end(ap);
		return;
	}
#endif
	fprintf(stderr, "%s: ", argv0);
	vfprintf(stderr, fmt, ap);
	if (fmt[0] && fmt[strlen(fmt)-1] == ':')
		fprintf(stderr, " %s\n", strerror(saved_errno));
	else
		fputc('\n', stderr);
	va_end(ap);
}

static void
pr_info(const char *fmt, ...)
{
	va_list ap;

	if (qflag)
		return;

	va_start(ap, fmt);
#if FEATURE_MODPROBE_SYSLOG
	if (sflag) {
		vsyslog(LOG_INFO, fmt, ap);
		va_end(ap);
		return;
	}
#endif
	vprintf(fmt, ap);
	putchar('\n');
	va_end(ap);
}

/* string structures: singly linked lists used for directories, deps, and aliases */

static void
strlist_append(struct StrNode **list, const char *str)
{
	struct StrNode *n, *tail;

	n = ecalloc(1, sizeof(*n));
	n->str = estrdup(str);

	if (!*list) {
		*list = n;
		return;
	}
	for (tail = *list; tail->next; tail = tail->next)
		;
	tail->next = n;
}

static char *
append_opts(char *opts, const char *add)
{
	size_t olen, alen;
	char *newopts;

	if (!add)
		return opts;
	if (!opts)
		return estrdup(add);

	olen = strlen(opts);
	alen = strlen(add);
	newopts = ecalloc(1, olen + alen + 2);
	memcpy(newopts, opts, olen);
	newopts[olen] = ' ';
	memcpy(newopts + olen + 1, add, alen);

	return newopts;
}

/* module database: hash table allows fast realname queries for aliases */

static unsigned int
hash(const char *s)
{
	unsigned int h = 5381;

	while (*s)
		h = ((h << 5) + h) + *s++;
	return h % HASH_SIZE;
}

static void
normalize_name(char *dst, const char *src)
{
	const char *base;
	int i;

	base = strrchr(src, '/');
	if (base)
		base++;
	else
		base = src;

	for (i = 0; i < 255 && base[i] && base[i] != '.'; i++)
		dst[i] = (base[i] == '-') ? '_' : base[i];
	dst[i] = '\0';
}

static struct Module *
get_module(const char *path_or_name, int create)
{
	char name[256];
	unsigned int h;
	struct Module *m;

	normalize_name(name, path_or_name);
	h = hash(name);

	for (m = mod_db[h]; m; m = m->next) {
		if (strcmp(m->name, name) == 0)
			return m;
	}

	if (!create)
		return NULL;

	m = ecalloc(1, sizeof(*m));
	m->name = estrdup(name);
	m->next = mod_db[h];
	mod_db[h] = m;

	return m;
}

/* config parsing: recursively loads aliases and module options from dir tree */

static void
parse_config_file(const char *path)
{
	FILE *fp;
	char line[LINE_MAX];
	char *p, *cmd, *arg1, *arg2;
	struct Module *m;

	if (!(fp = fopen(path, "r")))
		return;

	while (fgets(line, sizeof(line), fp)) {
		p = strchr(line, '#');
		if (p)
			*p = '\0';

		cmd = strtok(line, " \t\n");
		if (!cmd)
			continue;

		arg1 = strtok(NULL, " \t\n");
		if (!arg1)
			continue;

		if (strcmp(cmd, "alias") == 0) {
			arg2 = strtok(NULL, " \t\n");
			if (!arg2)
				continue;
			m = get_module(arg1, 1);
			strlist_append(&m->aliases, arg2);
		} else if (strcmp(cmd, "options") == 0) {
			arg2 = strtok(NULL, "\n");
			if (!arg2)
				continue;
			while (*arg2 == ' ' || *arg2 == '\t')
				arg2++;
			m = get_module(arg1, 1);
			m->options = append_opts(m->options, arg2);
		}
#if FEATURE_MODPROBE_BLACKLIST
		else if (strcmp(cmd, "blacklist") == 0) {
			m = get_module(arg1, 1);
			m->flags |= MOD_BLACKLISTED;
		}
#endif
	}
	fclose(fp);
}

static void
config_cb(int fd, const char *path, struct stat *st, void *data, struct recursor *r)
{
	size_t len;

	(void)fd;
	(void)data;
	(void)r;

	if (S_ISREG(st->st_mode)) {
		len = strlen(path);
		if (len > 5 && strcmp(path + len - 5, ".conf") == 0)
			parse_config_file(path);
	}
}

static void
read_configs(void)
{
	struct recursor r = { .fn = config_cb, .maxdepth = 1, .follow = 'H', .flags = DIRFIRST };
	struct stat st;

	parse_config_file("/etc/modprobe.conf");

	if (stat("/etc/modprobe.d", &st) == 0 && S_ISDIR(st.st_mode))
		recurse(AT_FDCWD, "/etc/modprobe.d", NULL, &r);
}

static void
parse_dep_file(const char *path)
{
	FILE *fp;
	char line[LINE_MAX];
	char *p, *tok;
	struct Module *m;

	if (!(fp = fopen(path, "r"))) {
		pr_warn("fopen %s:", path);
		return;
	}

	while (fgets(line, sizeof(line), fp)) {
		p = strchr(line, ':');
		if (!p)
			continue;
		*p = '\0';
		p++;

		m = get_module(line, 1);
		if (!m->path)
			m->path = estrdup(line);
		m->flags |= MOD_SEEN_DEP;

		while ((tok = strtok(p, " \t\n"))) {
			p = NULL;
			if (*tok)
				strlist_append(&m->deps, tok);
		}
	}
	fclose(fp);
}

static void
mark_loaded(void)
{
	FILE *fp;
	char line[LINE_MAX];
	char *p;
	struct Module *m;

	if (!(fp = fopen("/proc/modules", "r")))
		return;

	while (fgets(line, sizeof(line), fp)) {
		p = strchr(line, ' ');
		if (p)
			*p = '\0';
		else {
			p = strchr(line, '\n');
			if (p)
				*p = '\0';
		}
		m = get_module(line, 1);
		m->flags |= MOD_LOADED;
	}
	fclose(fp);
}

static void
list_modules(const char *pattern)
{
	FILE *fp;
	char line[LINE_MAX];
	char *p, *name;

	if (!(fp = fopen("modules.dep", "r"))) {
		pr_warn("fopen modules.dep:");
		return;
	}

	while (fgets(line, sizeof(line), fp)) {
		p = strchr(line, ':');
		if (!p)
			continue;
		*p = '\0';

		name = strrchr(line, '/');
		name = name ? name + 1 : line;

		p = strrchr(name, '.');
		if (p)
			*p = '\0';

		if (!pattern || fnmatch(pattern, name, 0) == 0) {
			if (p)
				*p = '.';
			printf("%s\n", line); /* intended output, not a log */
		}
	}
	fclose(fp);
}

/* kernel interaction */

static int
load_module(const char *path, const char *opts)
{
	int fd, ret;

	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		pr_warn("open %s:", path);
		return -1;
	}
	ret = syscall(__NR_finit_module, fd, opts ? opts : "", 0);
	close(fd);
	return ret;
}

static int
unload_module(const char *name)
{
	return syscall(__NR_delete_module, name, O_NONBLOCK);
}

/* modprobe actions: load and unload requested module graph */

static void
process_module(struct Module *m)
{
	struct StrNode *dep;
	struct Module *dm;
	char *opts;

	if (!m->path) {
		pr_warn("module %s not found in modules.dep", m->name);
		return;
	}

	if (rflag) {
		if (m->flags & MOD_LOADED) {
			if (unload_module(m->name) == 0)
				m->flags &= ~MOD_LOADED;
			else
				pr_warn("unload %s:", m->name);
		}
		return;
	}

	for (dep = m->deps; dep; dep = dep->next) {
		dm = get_module(dep->str, 0);
		if (dm && !(dm->flags & MOD_LOADED))
			process_module(dm);
	}

	if (m->flags & MOD_LOADED) {
		if (vflag)
			pr_info("%s already loaded", m->name);
		return;
	}

	opts = m->options;
	if (cmdopts && probes && strcmp(probes->str, m->name) == 0)
		opts = append_opts(opts, cmdopts);

#if FEATURE_MODPROBE_SHOW_DEPENDS
	if (Dflag) {
		printf(opts ? "insmod %s %s\n" : "insmod %s\n", m->path, opts); /* output data */
		if (opts != m->options)
			free(opts);
		return;
	}
#endif

	if (load_module(m->path, opts) == 0) {
		m->flags |= MOD_LOADED;
		if (vflag)
			pr_info("loaded %s '%s'", m->path, opts ? opts : "");
	} else {
		pr_warn("load %s:", m->path);
	}

	if (opts != m->options)
		free(opts);
}

static void
do_probe(const char *name)
{
	struct Module *m, *am;
	struct StrNode *alias;
	char norm[256];

	normalize_name(norm, name);
	m = get_module(norm, 1);

#if FEATURE_MODPROBE_BLACKLIST
	if (bflag && (m->flags & MOD_BLACKLISTED))
		return;
#endif

	if (!m->aliases) {
		if (vflag)
			pr_info("probing %s by name", norm);
		process_module(m);
		return;
	}

	for (alias = m->aliases; alias; alias = alias->next) {
		am = get_module(alias->str, 1);
#if FEATURE_MODPROBE_BLACKLIST
		if (am->flags & MOD_BLACKLISTED)
			continue;
#endif
		if (vflag)
			pr_info("probing alias %s -> %s", norm, alias->str);
		process_module(am);
	}
}

static void
usage(void)
{
	eprintf("usage: %s [-alqrv"
#if FEATURE_MODPROBE_SHOW_DEPENDS
		"D"
#endif
#if FEATURE_MODPROBE_BLACKLIST
		"b"
#endif
#if FEATURE_MODPROBE_SYSLOG
		"s"
#endif
		"] "
#if FEATURE_MODPROBE_DIR_OVERRIDE
		"[-d dir] "
#endif
		"module [symbol=value ...]\n", argv0);
}

// ?man modprobe: add or remove modules from the Linux kernel
// ?man arguments: module [symbol=value ...]
// ?man modprobe loads or removes kernel modules from the running system.
// ?man It reads modules.dep, modules.alias, and modules.symbols from the appropriate
// ?man /lib/modules/release directory to resolve module names and dependencies,
// ?man loading prerequisites first.
// ?man Without -r, modprobe loads the named module (and any required dependencies)
// ?man into the kernel.
int
main(int argc, char *argv[])
{
	struct utsname uts;
	struct StrNode *pn;
	char path[PATH_MAX];
	int i, ret = 0;

	ARGBEGIN {
	// ?man -a: specify a option
	case 'a':
		// ?man -a: Load all modules named on the command line (rather than stopping after the first).
		aflag = 1; break;
	// ?man -r: specify r option
	case 'r':
		// ?man -r: Remove the named modules from the kernel.  Dependencies are not automatically removed.
		rflag = 1; break;
	// ?man -q: specify q option
	case 'q':
		// ?man -q: Quiet mode.  Suppress error messages.
		qflag = 1; break;
	// ?man -v: specify v option
	case 'v':
		// ?man -v: Verbose mode.  Print each action taken.
		vflag = 1; break;
	// ?man -l: specify l option
	case 'l':
		// ?man -l: List available modules matching the optional _pattern_ (a fnmatch(3) glob).
		lflag = 1; break;
#if FEATURE_MODPROBE_SHOW_DEPENDS
	// ?man -D: specify D option
	case 'D':
		// ?man -D: Print the sequence of insmod commands that would be used to load the module, without actually loading anything.
		Dflag = 1; break;
#endif
#if FEATURE_MODPROBE_BLACKLIST
	// ?man -b: specify b option
	case 'b':
		// ?man -b: Skip modules listed as blacklist in /etc/modprobe.d/.
		bflag = 1; break;
#endif
#if FEATURE_MODPROBE_SYSLOG
	// ?man -s: specify s option
	case 's':
		// ?man -s: Log messages to syslog(3) (facility LOG_DAEMON) instead of standard error.
		sflag = 1; break;
#endif
#if FEATURE_MODPROBE_DIR_OVERRIDE
	// ?man -d:file: specify d option
	case 'd':
		// ?man -d dir: Use dir as the base directory for module files instead of /lib/modules/release.
		strlist_append(&moddirs, EARGF(usage())); break;
#endif
	default:
		usage();
	} ARGEND

	if (!argc && !rflag && !lflag)
		usage();

#if FEATURE_MODPROBE_SYSLOG
	if (sflag)
		openlog("modprobe", LOG_PID, LOG_DAEMON);
#endif

	if (!moddirs) {
		if (uname(&uts) < 0)
			eprintf("uname:");
		snprintf(path, sizeof(path), "/lib/modules/%s", uts.release);
		strlist_append(&moddirs, path);
	}

	mark_loaded();
	read_configs();

	/* traverse all provided or default base directories for symbol, alias, and dependency tracking */
	for (pn = moddirs; pn; pn = pn->next) {
		if (chdir(pn->str) < 0) {
			pr_warn("chdir %s:", pn->str);
			continue;
		}

		if (lflag) {
			list_modules(argc ? argv[0] : NULL);
			continue;
		}

		parse_config_file("modules.symbols");
		parse_config_file("modules.alias");
		parse_dep_file("modules.dep");
	}

	if (lflag)
		goto end;

	if (aflag || rflag) {
		for (i = 0; i < argc; i++)
			strlist_append(&probes, argv[i]);
	} else if (argc > 0) {
		strlist_append(&probes, argv[0]);
		for (i = 1; i < argc; i++)
			cmdopts = append_opts(cmdopts, argv[i]);
	}

	if (rflag && !argc) {
		if (syscall(__NR_delete_module, NULL, O_NONBLOCK) < 0)
			eprintf("delete_module:");
		goto end;
	}

	for (pn = probes; pn; pn = pn->next) {
		do_probe(pn->str);
	}

end:
#if FEATURE_MODPROBE_SYSLOG
	if (sflag)
		closelog();
#endif
	if (fshut(stdin, "<stdin>") | fshut(stdout, "<stdout>"))
		ret = 2;

	// ?man
	// ?man ## FILES
	// ?man
	// ?man `/etc/modprobe.conf`
	// ?man : Global configuration file.
	// ?man
	// ?man Files under `/etc/modprobe.d/`
	// ?man : Per-module configuration snippets.
	// ?man
	// ?man `/lib/modules/`_release_`/modules.dep`
	// ?man : Module dependency map generated by `depmod(8)`.
	// ?man
	// ?man ## EXIT STATUS
	// ?man
	// ?man 0
	// ?man : Success.
	// ?man
	// ?man 1
	// ?man : Module not found or kernel rejected the operation.
	// ?man
	// ?man 2
	// ?man : I/O error on stdout or stdin.
	// ?man
	// ?man ## SEE ALSO
	// ?man
	// ?man insmod(8), rmmod(8), lsmod(8), depmod(8)
	// ?man

	return ret;
}
