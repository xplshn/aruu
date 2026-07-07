#include "ninqu.h"
#include "wexec.h"

#include <ctype.h>
#include <limits.h>
#include <string.h>

/* one entry per generator format. adding a backend means writing its
 * own emit fn elsewhere and adding a row here, nothing else in ninqu
 * needs to change */
static const struct Backend backends[] = {
    {"ninja", "NINJA_TRY", "build.ninja", emit_ninja},
};
#define NBACKENDS (int)(sizeof backends / sizeof backends[0])

const struct Backend *
backend_by_name(const char *name)
{
  int i;

  for (i = 0; i < NBACKENDS; i++)
    if (strcmp(backends[i].name, name) == 0)
      return &backends[i];
  return NULL;
}

/* try_key is a comma list of binary names, tried in order, first
 * one found on PATH wins. bin is set to that binarys own path */
const struct Backend *
backend_resolve(char **bin)
{
  int i;

  for (i = 0; i < NBACKENDS; i++) {
    const char *p = kv_get_or(backends[i].try_key, "");

    while (*p) {
      char        name[PATH_MAX];
      const char *start;
      char       *found;
      size_t      len;

      while (*p == ',' || isspace((unsigned char)*p))
        p++;
      start = p;
      while (*p && *p != ',')
        p++;
      len = (size_t)(p - start);
      while (len && isspace((unsigned char)start[len - 1]))
        len--;
      if (len && len < sizeof name) {
        memcpy(name, start, len);
        name[len] = '\0';
        found     = wwhich(name);
        if (found) {
          *bin = found;
          return &backends[i];
        }
      }
    }
  }
  return NULL;
}

void
backend_exec(const struct Backend *be, const char *bin, struct StrList *wanted)
{
  struct StrList argv = {0};
  int            i;

  sl_push(&argv, base_of(bin));
  sl_push(&argv, "-f");
  sl_push(&argv, be->file);
  for (i = 0; i < wanted->n; i++)
    sl_push(&argv, wanted->v[i]);

  wexecv_self(bin, sl_argv(&argv));
}
