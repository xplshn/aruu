#include "ninqu.h"

#include <ctype.h>
#include <fnmatch.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

long
mtime_of(const char *path)
{
  struct stat st;
  if (stat(path, &st) != 0)
    return -1;
  return (long)st.st_mtime;
}

int
file_exists(const char *path)
{
  return mtime_of(path) >= 0;
}

int
file_contains(const char *path, const char *needle)
{
  FILE  *fp;
  char   buf[8192];
  size_t n, needlen = strlen(needle), taillen = 0;
  char   tail[256];
  int    found = 0;

  if (!needle[0])
    return 1;
  fp = fopen(path, "r");
  if (!fp)
    return 0;
  tail[0] = '\0';
  while (!found && (n = fread(buf, 1, sizeof buf - 1, fp)) > 0) {
    char joined[8192 + 256];
    buf[n] = '\0';
    snprintf(joined, sizeof joined, "%s%s", tail, buf);
    if (strstr(joined, needle))
      found = 1;
    taillen = strlen(buf);
    if (taillen > needlen)
      taillen = needlen - 1;
    snprintf(tail, sizeof tail, "%s", buf + strlen(buf) - taillen);
  }
  fclose(fp);
  return found;
}

/* recurse_dir lacks a userdata slot, use statics for one call */
static const char     *rg_suffix;
static struct StrList *rg_out;

static void
rg_visit(const char *path)
{
  struct stat st;

  if (lstat(path, &st) != 0)
    return;
  if (S_ISDIR(st.st_mode)) {
    recurse_dir(path, rg_visit);
    return;
  }
  if (S_ISREG(st.st_mode) && fnmatch(rg_suffix, base_of(path), 0) == 0)
    sl_push(rg_out, path);
}

static void
recurse_glob(const char *dir, const char *suffix, struct StrList *out)
{
  rg_suffix = suffix;
  rg_out    = out;
  recurse_dir(dir, rg_visit);
}

/* double-star-slash recurses, anything else goes to glob(3) */
void
glob_expand(const char *pattern, struct StrList *out)
{
  const char *rec = strstr(pattern, "**/");
  if (rec) {
    char   prefix[1024], suffix[1024];
    size_t plen = (size_t)(rec - pattern);
    if (plen >= sizeof prefix)
      plen = sizeof prefix - 1;
    memcpy(prefix, pattern, plen);
    prefix[plen] = '\0';
    if (plen > 0 && prefix[plen - 1] == '/')
      prefix[plen - 1] = '\0';
    strlcpy(suffix, rec + 3, sizeof suffix);
    recurse_glob(prefix[0] ? prefix : ".", suffix, out);
  } else {
    glob_t g;
    size_t i;
    if (glob(pattern, 0, NULL, &g) == 0)
      for (i = 0; i < g.gl_pathc; i++)
        sl_push(out, g.gl_pathv[i]);
    globfree(&g);
  }
}

void
glob_all(struct Rule *r, struct StrList *out)
{
  int i;
  for (i = 0; i < r->globs.n; i++) {
    char *pat = kv_expand(r->globs.v[i]);
    glob_expand(pat, out);
    free(pat);
  }
}

int
is_wildcard_pattern(const char *pattern)
{
  return strpbrk(pattern, "*?[") != NULL;
}

/* true if every glob on r is a literal path */
int
rule_is_literal(struct Rule *r)
{
  int i;
  if (r->globs.n == 0)
    return 0;
  for (i = 0; i < r->globs.n; i++)
    if (is_wildcard_pattern(r->globs.v[i]))
      return 0;
  return 1;
}

/* broad wildcard steps aside for literal paths so overrides need no (skip) */
int
path_claimed_elsewhere(const char *path, struct Rule *self)
{
  int i, j;
  for (i = 0; i < nrules; i++) {
    struct Rule *other = &rules[i];
    if (other == self || !rule_is_literal(other))
      continue;
    for (j = 0; j < other->globs.n; j++) {
      char *exp = kv_expand(other->globs.v[j]);
      int   hit = strcmp(exp, path) == 0;
      free(exp);
      if (hit)
        return 1;
    }
  }
  return 0;
}

void
splitext(const char *base, char *stem, size_t stemsz, char *ext, size_t extsz)
{
  const char *dot = strrchr(base, '.');
  if (dot && dot != base) {
    size_t sl = (size_t)(dot - base);
    if (sl >= stemsz)
      sl = stemsz - 1;
    memcpy(stem, base, sl);
    stem[sl] = '\0';
    strlcpy(ext, dot, extsz);
  } else {
    strlcpy(stem, base, stemsz);
    ext[0] = '\0';
  }
}

/* normalize so a gate keying off BASESTEM matches genconfig output */
void
to_ident(const char *s, char *out, size_t outsz)
{
  size_t i;
  for (i = 0; s[i] && i + 1 < outsz; i++)
    out[i] = (s[i] == '-' || s[i] == '.') ? '_' : s[i];
  out[i] = '\0';
}
