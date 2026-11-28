/* see LICENSE file for copyright and license details */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "../accounts.h"
#include "../paths.h"

#if defined(__linux__) && !defined(ARUU_FORCE_POSIX)
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define LINE_MAX_ACCT 4096
/* the ids people are given; below are the system's */
#define UID_FIRST 1000
#define UID_LAST  60000

/* what a line becomes: NULL drops it, the line itself keeps it, else the
 * new text, written into buf */
typedef const char *edit_fn(const char *line, char *buf, size_t n, void *arg);

/* path written again through a file beside it, renamed over it: each line
 * put through fn (kept when fn is NULL), add appended when not NULL. the
 * file keeps its mode and owner */
static int
rewrite(const char *path, edit_fn *fn, void *arg, const char *add)
{
  char        tmp[PATH_MAX], line[LINE_MAX_ACCT], buf[LINE_MAX_ACCT];
  const char *r;
  struct stat st;
  FILE       *in, *out;
  int         fd, ok = 1;

  if (!(in = fopen(path, "r")))
    return -1;
  if (fstat(fileno(in), &st) < 0 || snprintf(tmp, sizeof(tmp), "%s.acct", path) >= (int)sizeof(tmp)) {
    fclose(in);
    return -1;
  }
  unlink(tmp);
  if ((fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, st.st_mode & 07777)) < 0) {
    fclose(in);
    return -1;
  }
  /* the umask took bits off; root keeps the owner, anyone else cannot */
  fchmod(fd, st.st_mode & 07777);
  if (fchown(fd, st.st_uid, st.st_gid) < 0 && geteuid() == 0)
    ok = 0;
  if (!(out = fdopen(fd, "w"))) {
    close(fd);
    unlink(tmp);
    fclose(in);
    return -1;
  }
  while (ok && fgets(line, sizeof(line), in)) {
    line[strcspn(line, "\n")] = '\0';
    r = fn ? fn(line, buf, sizeof(buf), arg) : line;
    if (r && fprintf(out, "%s\n", r) < 0)
      ok = 0;
  }
  if (ok && add && fprintf(out, "%s\n", add) < 0)
    ok = 0;
  ok = ok && !ferror(in);
  fclose(in);
  ok = fflush(out) == 0 && fsync(fd) == 0 && ok;
  ok = fclose(out) == 0 && ok;
  if (!ok || rename(tmp, path) < 0) {
    unlink(tmp);
    return -1;
  }
  return 0;
}

/* whether line is the entry of name */
static int
named(const char *line, const char *name)
{
  size_t n = strlen(name);

  return !strncmp(line, name, n) && line[n] == ':';
}

static int
has(const char *path, const char *name)
{
  char  line[LINE_MAX_ACCT];
  FILE *f = fopen(path, "r");
  int   found = 0;

  if (!f)
    return 0;
  while (!found && fgets(line, sizeof(line), f))
    found = named(line, name);
  fclose(f);
  return found;
}

/* the first id from UID_FIRST that neither passwd nor group uses */
static long
free_id(void)
{
  const char *paths[] = { ARUU_PATH_PASSWD, ARUU_PATH_GROUP };
  char        line[LINE_MAX_ACCT], *f;
  long        top = UID_FIRST - 1, id;
  FILE       *fp;

  for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
    if (!(fp = fopen(paths[i], "r")))
      continue;
    while (fgets(line, sizeof(line), fp)) {
      /* the third field: name:x:id */
      if (!(f = strchr(line, ':')) || !(f = strchr(f + 1, ':')))
        continue;
      id = strtol(f + 1, NULL, 10);
      if (id > top && id < UID_LAST)
        top = id;
    }
    fclose(fp);
  }
  return top + 1 < UID_LAST ? top + 1 : -1;
}

static const char *
drop(const char *line, char *buf, size_t n, void *arg)
{
  (void)buf;
  (void)n;
  return named(line, arg) ? NULL : line;
}

struct membership {
  const char *group, *name;
  int         in;
};

/* a group line with name taken out of its members, and put in when it is
 * the group asked for and in says so */
static const char *
members(const char *line, char *buf, size_t n, void *arg)
{
  const struct membership *m = arg;
  char        list[LINE_MAX_ACCT], *tok, *save;
  const char *at = line;
  size_t      len = 0;
  int         put = 0;

  /* name:x:gid: then the members */
  for (int colons = 0; colons < 3; colons++)
    if (!(at = strchr(at, ':')))
      return line;
    else
      at++;
  if (m->group && !named(line, m->group))
    return line;
  len = (size_t)(at - line);
  if (len >= n)
    return line;
  memcpy(buf, line, len);
  buf[len] = '\0';
  snprintf(list, sizeof(list), "%s", at);
  for (tok = strtok_r(list, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
    if (!*tok || !strcmp(tok, m->name))
      continue;
    len += (size_t)snprintf(buf + len, len < n ? n - len : 0, "%s%s", put++ ? "," : "", tok);
  }
  if (m->group && m->in)
    len += (size_t)snprintf(buf + len, len < n ? n - len : 0, "%s%s", put ? "," : "", m->name);
  return len < n ? buf : line;
}

int
acct_add(const char *name, const char *home, const char *shell, const char *hash, uid_t *uid)
{
  char line[LINE_MAX_ACCT];
  long id;

  if (!acct_valid_name(name) || strchr(home, ':') || strchr(shell, ':') || strchr(hash, ':')) {
    errno = EINVAL;
    return -1;
  }
  if (has(ARUU_PATH_PASSWD, name) || has(ARUU_PATH_GROUP, name)) {
    errno = EEXIST;
    return -1;
  }
  if ((id = free_id()) < 0) {
    errno = ENOSPC;
    return -1;
  }
  /* the group and the password first: a person is there once passwd says so */
  snprintf(line, sizeof(line), "%s:x:%ld:", name, id);
  if (rewrite(ARUU_PATH_GROUP, NULL, NULL, line) < 0)
    return -1;
  snprintf(line, sizeof(line), "%s:%s:%ld:0:99999:7:::", name, hash, (long)(time(NULL) / 86400));
  if (rewrite(ARUU_PATH_SHADOW, NULL, NULL, line) < 0)
    return -1;
  snprintf(line, sizeof(line), "%s:x:%ld:%ld::%s:%s", name, id, id, home, shell);
  if (rewrite(ARUU_PATH_PASSWD, NULL, NULL, line) < 0)
    return -1;
  if (uid)
    *uid = (uid_t)id;
  return 0;
}

int
acct_remove(const char *name)
{
  struct membership m = { NULL, name, 0 };

  if (!acct_valid_name(name) || !has(ARUU_PATH_PASSWD, name)) {
    errno = ENOENT;
    return -1;
  }
  /* passwd first: a person is gone once passwd says so */
  if (rewrite(ARUU_PATH_PASSWD, drop, (void *)name, NULL) < 0 ||
      rewrite(ARUU_PATH_SHADOW, drop, (void *)name, NULL) < 0 ||
      rewrite(ARUU_PATH_GROUP, drop, (void *)name, NULL) < 0)
    return -1;
  return rewrite(ARUU_PATH_GROUP, members, &m, NULL);
}

int
acct_member(const char *group, const char *name, int in)
{
  struct membership m = { group, name, in };

  if (!has(ARUU_PATH_GROUP, group) || !has(ARUU_PATH_PASSWD, name)) {
    errno = ENOENT;
    return -1;
  }
  return rewrite(ARUU_PATH_GROUP, members, &m, NULL);
}
#endif
