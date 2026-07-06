#include "ninqu.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

void
sl_push(struct StrList *sl, const char *s)
{
  GROW(sl->v, sl->n, sl->cap, 8);
  sl->v[sl->n++] = estrdup(s);
}

int
sl_has(struct StrList *sl, const char *s)
{
  int i;
  for (i = 0; i < sl->n; i++)
    if (strcmp(sl->v[i], s) == 0)
      return 1;
  return 0;
}

void
sl_split(struct StrList *sl, const char *s)
{
  const char *p = s;

  sl->n = 0;
  while (*p) {
    const char *start;
    char        buf[4096];
    size_t      len;

    while (*p && isspace((unsigned char)*p))
      p++;
    if (!*p)
      break;
    start = p;
    while (*p && !isspace((unsigned char)*p))
      p++;
    len = (size_t)(p - start);
    if (len >= sizeof buf)
      len = sizeof buf - 1;
    memcpy(buf, start, len);
    buf[len] = '\0';
    sl_push(sl, buf);
  }
}

char *
sl_join(struct StrList *sl)
{
  size_t cap = 64, len = 0;
  char  *out = emalloc(cap);
  int    i;

  out[0] = '\0';
  for (i = 0; i < sl->n; i++) {
    size_t addlen = strlen(sl->v[i]) + 1;
    while (len + addlen + 1 > cap) {
      cap *= 2;
      out = erealloc(out, cap);
    }
    if (i > 0)
      out[len++] = ' ';
    memcpy(out + len, sl->v[i], strlen(sl->v[i]));
    len += strlen(sl->v[i]);
    out[len] = '\0';
  }
  return out;
}

const char *
base_of(const char *path)
{
  const char *p = strrchr(path, '/');
  return p ? p + 1 : path;
}

/* null-terminate the array so execvp stops scanning */
char **
sl_argv(struct StrList *sl)
{
  char **av = emalloc((size_t)(sl->n + 1) * sizeof *av);
  int    i;
  for (i = 0; i < sl->n; i++)
    av[i] = sl->v[i];
  av[sl->n] = NULL;
  return av;
}

/* noreturn so callers do not need to guard the dead path after the
 * fork child calls this */
void
child_execvp(char **av)
{
  execvp(av[0], av);
  fprintf(stderr, "%s: exec %s: %s\n", argv0, av[0], strerror(errno));
  _exit(127);
}
