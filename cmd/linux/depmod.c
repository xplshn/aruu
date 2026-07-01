

#include "fs.h"
#include "wexec.h"
#include "util.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>

struct ModuleNode {
	char *name;
	char *path;
	char **deps;
	size_t ndeps;
#if FEATURE_DEPMOD_ALIAS
	char **aliases;
	size_t naliases;
#endif
#if FEATURE_DEPMOD_SYMBOLS
	char **symbols;
	size_t nsymbols;
#endif
	struct ModuleNode *next;
};

static struct ModuleNode *modules_head = NULL;

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

static struct ModuleNode *
find_module(const char *name)
{
	struct ModuleNode *m;

	for (m = modules_head; m; m = m->next) {
		if (strcmp(m->name, name) == 0)
			return m;
	}
	return NULL;
}

static char *
get_str(const char *buf, size_t len, size_t offset, size_t maxlen)
{
	size_t i;

	for (i = 0; i < maxlen && offset + i < len; i++) {
		if (buf[offset + i] == '\0') {
			if (i == 0)
				return NULL;
			return estrndup(buf + offset, i);
		}
		if (buf[offset + i] < 32 || buf[offset + i] > 126)
			return NULL;
	}
	return NULL;
}

static void
parse_ko(struct ModuleNode *m, const char *buf, size_t len)
{
	size_t i;
	char *val, *tok, *save;

	for (i = 0; i + 8 < len; i++) {
		if (memcmp(buf + i, "depends=", 8) == 0) {
			val = get_str(buf, len, i + 8, 1024);
			if (val) {
				save = val;
				while ((tok = strsep(&save, ","))) {
					if (*tok) {
						m->deps = reallocarray(m->deps, m->ndeps + 1, sizeof(char *));
						m->deps[m->ndeps] = estrdup(tok);
						normalize_name(m->deps[m->ndeps], tok);
						m->ndeps++;
					}
				}
				i += 8 + strlen(val);
				free(val);
			}
		}
#if FEATURE_DEPMOD_ALIAS
		else if (memcmp(buf + i, "alias=", 6) == 0) {
			val = get_str(buf, len, i + 6, 512);
			if (val) {
				m->aliases = reallocarray(m->aliases, m->naliases + 1, sizeof(char *));
				m->aliases[m->naliases] = val;
				i += 6 + strlen(val);
				m->naliases++;
			}
		}
#endif
#if FEATURE_DEPMOD_SYMBOLS
		else if (memcmp(buf + i, "__ksymtab_", 10) == 0) {
			val = get_str(buf, len, i + 10, 256);
			if (val) {
				m->symbols = reallocarray(m->symbols, m->nsymbols + 1, sizeof(char *));
				m->symbols[m->nsymbols] = val;
				i += 10 + strlen(val);
				m->nsymbols++;
			}
		}
#endif
	}
}

static char *
read_all(FILE *fp, size_t *out_len)
{
	char *buf = NULL;
	size_t cap = 0;
	size_t len = 0;
	size_t n;

	while (1) {
		if (len >= cap) {
			cap = cap ? cap * 2 : 65536;
			buf = erealloc(buf, cap);
		}
		n = fread(buf + len, 1, cap - len, fp);
		if (n == 0)
			break;
		len += n;
	}
	*out_len = len;
	return buf;
}

static void
scan_cb(int fd, const char *name, struct stat *st, void *data, struct recursor *r)
{
	struct ModuleNode *m;
	char *buf;
	size_t len;
	const char *ext;
	const char *comp;
	FILE *fp = NULL;
	int is_pipe = 0;
	char cmd[PATH_MAX + 32];

	(void)fd;
	(void)data;
	(void)r;

	if (S_ISDIR(st->st_mode)) {
		recurse(fd, name, NULL, r);
		return;
	}

	if (!S_ISREG(st->st_mode))
		return;

	ext = strstr(r->path, ".ko");
	if (!ext)
		return;

	comp = ext + 3;
	if (strcmp(comp, "") == 0) {
		fp = fopen(r->path, "r");
	} else if (strcmp(comp, ".gz") == 0) {
		snprintf(cmd, sizeof(cmd), "gzip -dc '%s'", r->path);
		fp = wpopen(cmd, NULL, "r");
		is_pipe = 1;
	} else if (strcmp(comp, ".xz") == 0) {
		snprintf(cmd, sizeof(cmd), "xz -dc '%s'", r->path);
		fp = wpopen(cmd, NULL, "r");
		is_pipe = 1;
	} else if (strcmp(comp, ".zst") == 0) {
		snprintf(cmd, sizeof(cmd), "zstd -dc '%s'", r->path);
		fp = wpopen(cmd, NULL, "r");
		is_pipe = 1;
	} else {
		fp = fopen(r->path, "r");
	}

	if (!fp) {
		weprintf("open %s:", r->path);
		return;
	}

	buf = read_all(fp, &len);
	if (is_pipe)
		wpclose(fp);
	else
		fclose(fp);

	if (len == 0) {
		free(buf);
		return;
	}

	m = ecalloc(1, sizeof(*m));
	m->path = estrdup(r->path);
	if (strncmp(m->path, "./", 2) == 0)
		memmove(m->path, m->path + 2, strlen(m->path) - 1);
	m->name = emalloc(256);
	normalize_name(m->name, r->path);

	parse_ko(m, buf, len);
	free(buf);

	m->next = modules_head;
	modules_head = m;
}

static void
resolve_deps(struct ModuleNode *m, char ***out_list, size_t *out_count)
{
	size_t i, j;
	struct ModuleNode *dep_node;
	int already_exists;

	for (i = 0; i < m->ndeps; i++) {
		dep_node = find_module(m->deps[i]);
		if (!dep_node)
			continue;

		already_exists = 0;
		for (j = 0; j < *out_count; j++) {
			if (strcmp((*out_list)[j], dep_node->path) == 0) {
				already_exists = 1;
				break;
			}
		}

		if (!already_exists) {
			resolve_deps(dep_node, out_list, out_count);
			*out_list = reallocarray(*out_list, *out_count + 1, sizeof(char *));
			(*out_list)[*out_count] = estrdup(dep_node->path);
			(*out_count)++;
		}
	}
}

static void
usage(void)
{
	eprintf("usage: %s [-n] [-b basedir] [version]\n", argv0);
}

// ?man depmod: generate modules.dep and map files
// ?man arguments: version
// ?man depmod generates modules.dep containing dependency information for modprobe
// ?man // ?man -n: dry run print results to stdout instead of writing files
// ?man // ?man -b basedir: use basedir as prefix for module directories
int
main(int argc, char *argv[])
{
	struct utsname uts;
	struct recursor r = { .fn = scan_cb, .maxdepth = 0, .follow = 'H', .flags = DIRFIRST };
	struct ModuleNode *m;
	char *basedir = "/";
	char *version = NULL;
	char path[PATH_MAX];
	int nflag = 0;
	size_t i;
	char **resolved;
	size_t nresolved;
	FILE *f_dep, *f_alias, *f_sym;

	ARGBEGIN {
	// ?man -n: specify n option
	case 'n':
		nflag = 1;
		break;
	// ?man -b:dir: specify b option
	case 'b':
		basedir = EARGF(usage());
		break;
	default:
		usage();
	} ARGEND;

	if (argc > 1)
		usage();

	if (argc == 1) {
		version = argv[0];
	} else {
		if (uname(&uts) < 0)
			eprintf("uname:");
		version = uts.release;
	}

	snprintf(path, sizeof(path), "%s/lib/modules/%s", basedir, version);
	if (chdir(path) < 0)
		eprintf("chdir %s:", path);

	recurse(AT_FDCWD, ".", NULL, &r);

	f_dep = stdout;
	f_alias = stdout;
	f_sym = stdout;

	if (!nflag) {
		f_dep = fopen("modules.dep", "w");
		if (!f_dep)
			eprintf("fopen modules.dep:");
#if FEATURE_DEPMOD_ALIAS
		f_alias = fopen("modules.alias", "w");
		if (!f_alias)
			eprintf("fopen modules.alias:");
#endif
#if FEATURE_DEPMOD_SYMBOLS
		f_sym = fopen("modules.symbols", "w");
		if (!f_sym)
			eprintf("fopen modules.symbols:");
#endif
	}

	/* write modules.dep */
	for (m = modules_head; m; m = m->next) {
		resolved = NULL;
		nresolved = 0;
		resolve_deps(m, &resolved, &nresolved);

		fprintf(f_dep, "%s:", m->path);
		for (i = 0; i < nresolved; i++) {
			fprintf(f_dep, " %s", resolved[i]);
			free(resolved[i]);
		}
		free(resolved);
		fprintf(f_dep, "\n");
	}

#if FEATURE_DEPMOD_ALIAS
	/* write modules.alias */
	for (m = modules_head; m; m = m->next) {
		for (i = 0; i < m->naliases; i++)
			fprintf(f_alias, "alias %s %s\n", m->aliases[i], m->name);
	}
#endif

#if FEATURE_DEPMOD_SYMBOLS
	/* write modules.symbols */
	for (m = modules_head; m; m = m->next) {
		for (i = 0; i < m->nsymbols; i++)
			fprintf(f_sym, "alias symbol:%s %s\n", m->symbols[i], m->name);
	}
#endif

	if (!nflag) {
		fclose(f_dep);
#if FEATURE_DEPMOD_ALIAS
		fclose(f_alias);
#endif
#if FEATURE_DEPMOD_SYMBOLS
		fclose(f_sym);
#endif
	}

	/* free memory */
	while (modules_head) {
		m = modules_head;
		modules_head = m->next;
		free(m->name);
		free(m->path);
		for (i = 0; i < m->ndeps; i++)
			free(m->deps[i]);
		free(m->deps);
#if FEATURE_DEPMOD_ALIAS
		for (i = 0; i < m->naliases; i++)
			free(m->aliases[i]);
		free(m->aliases);
#endif
#if FEATURE_DEPMOD_SYMBOLS
		for (i = 0; i < m->nsymbols; i++)
			free(m->symbols[i]);
		free(m->symbols);
#endif
		free(m);
	}

	if (fshut(stdin, "<stdin>") | fshut(stdout, "<stdout>"))
		return 2;

	return 0;
}
