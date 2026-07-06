#include "ninqu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct KvStore  kv;
struct KvStore *local_overlay;
struct StrList  kv_locked;
struct Map     *maps;
int             nmaps, mapscap;

char *
kv_get_raw(struct KvStore *s, const char *key)
{
  int i;
  for (i = 0; i < s->n; i++)
    if (strcmp(s->v[i].key, key) == 0)
      return s->v[i].val;
  return NULL;
}

void
kv_set_raw(struct KvStore *s, const char *key, const char *val)
{
  int i;
  for (i = 0; i < s->n; i++) {
    if (strcmp(s->v[i].key, key) == 0) {
      free(s->v[i].val);
      s->v[i].val = estrdup(val);
      return;
    }
  }
  GROW(s->v, s->n, s->cap, 16);
  s->v[s->n].key = estrdup(key);
  s->v[s->n].val = estrdup(val);
  s->n++;
}

/* overlay beats global beats environ, so a per-match $(BASESTEM) can
 * shadow a (set BASESTEM ...) without clobbering it for other matches */
char *
kv_get(const char *key)
{
  char *v;
  if (local_overlay) {
    v = kv_get_raw(local_overlay, key);
    if (v)
      return v;
  }
  v = kv_get_raw(&kv, key);
  if (v)
    return v;
  return getenv(key);
}

const char *
kv_get_or(const char *key, const char *def)
{
  char *v = kv_get(key);
  return v ? v : def;
}

void
kv_set(const char *key, const char *val)
{
  kv_set_raw(&kv, key, val);
}

/* -D on the cli locks the name so a later (set) in the file cannot
 * overwrite it */
void
kv_set_manifest(const char *key, const char *val)
{
  if (sl_has(&kv_locked, key))
    return;
  kv_set(key, val);
}

void
kv_set_cli(const char *key, const char *val)
{
  kv_set(key, val);
  sl_push(&kv_locked, key);
}

struct Map *
map_find(const char *name)
{
  int i;
  for (i = 0; i < nmaps; i++)
    if (strcmp(maps[i].name, name) == 0)
      return &maps[i];
  return NULL;
}

struct Map *
map_new(const char *name)
{
  struct Map *m;
  GROW(maps, nmaps, mapscap, 8);
  m = &maps[nmaps++];
  memset(m, 0, sizeof *m);
  strlcpy(m->name, name, sizeof m->name);
  return m;
}

/* $(NAME) and $(NAME:KEY). KEY is expanded first, the map value is
 * re-expanded once so $(LDLIBS_TLS) inside a map entry resolves. the
 * result is not recursively expanded, matching simply-expanded vars */
char *
kv_expand(const char *s)
{
  size_t      cap = strlen(s) * 2 + 64, len = 0;
  char       *out = emalloc(cap);
  const char *p   = s;

  while (*p) {
    if (p[0] == '$' && p[1] == '(') {
      const char *inner        = p + 2;
      const char *close        = NULL;
      char       *val          = NULL;
      char       *expanded_val = NULL;

      close = find_dollar_close(inner);

      if (close) {
        char   name[4096];
        size_t nl = (size_t)(close - inner);
        char  *colon;

        if (nl >= sizeof name)
          nl = sizeof name - 1;
        memcpy(name, inner, nl);
        name[nl] = '\0';

        colon = strchr(name, ':');
        if (colon) {
          char        mapname[128];
          char       *keypart;
          struct Map *m;
          size_t      mnl = (size_t)(colon - name);

          if (mnl >= sizeof mapname)
            mnl = sizeof mapname - 1;
          memcpy(mapname, name, mnl);
          mapname[mnl] = '\0';
          m            = map_find(mapname);
          keypart      = kv_expand(colon + 1);
          if (m) {
            char *raw = kv_get_raw(&m->entries, keypart);
            if (raw)
              expanded_val = kv_expand(raw);
          }
          free(keypart);
          val = expanded_val;
        } else {
          val = kv_get(name);
        }

        if (!val)
          val = "";
        while (len + strlen(val) + 1 >= cap) {
          cap *= 2;
          out = erealloc(out, cap);
        }
        memcpy(out + len, val, strlen(val));
        len += strlen(val);
        free(expanded_val);
        p = close + 1;
        continue;
      }
    }
    if (len + 2 >= cap) {
      cap *= 2;
      out = erealloc(out, cap);
    }
    out[len++] = *p++;
  }
  out[len] = '\0';
  return out;
}
