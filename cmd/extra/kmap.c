/* see LICENSE file for copyright and license details */
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "arg.h"
#include "kmap.h"
#include "paths.h"
#include "util.h"

static void
usage(void)
{
  eprintf("usage: %s [-d dir] [-l [layout]] [-n] [-x [-o options]]\n"
          "       [layout[,layout] [variant[,variant]]]\n", argv0);
}

static int
by_name(const void *a, const void *b)
{
  return strcmp(*(char *const *)a, *(char *const *)b);
}

// the layouts under dir, or one layout's variants, a line each with its name
static int
list(const char *dir, const char *layout)
{
  char path[PATH_MAX], **names = NULL;
  size_t n = 0, i;
  DIR *d;
  struct dirent *e;

  snprintf(path, sizeof(path), "%s%s%s", dir, layout ? "/" : "", layout ? layout : "");
  if (!(d = opendir(path)))
    eprintf("open %s:", path);
  while ((e = readdir(d))) {
    size_t l = strlen(e->d_name);

    if (e->d_name[0] == '.' || !strcmp(e->d_name, "compose.kmap"))
      continue;
    if (layout && (l < 6 || strcmp(e->d_name + l - 5, ".kmap")))
      continue;
    names = ereallocarray(names, n + 1, sizeof(*names));
    names[n++] = layout ? estrndup(e->d_name, l - 5) : estrdup(e->d_name);
  }
  closedir(d);
  qsort(names, n, sizeof(*names), by_name);
  for (i = 0; i < n; i++) {
    struct kmap k;

    memset(&k, 0, sizeof(k));
    kmap_path(path, sizeof(path), dir, layout ? layout : names[i], layout ? names[i] : NULL);
    if (kmap_read(path, &k) == 0)
      printf("%s\t%s\n", names[i], k.name);
    kmap_free(&k);
    free(names[i]);
  }
  free(names);
  return 0;
}

// "layout [variant]" from the file the system keeps its keyboard in
static int
configured(char *layout, size_t lsz, char *variant, size_t vsz)
{
  char line[256], *w[3];
  FILE *fp = fopen(ARUU_PATH_ETC "/kmap", "r");
  int n = 0;

  if (!fp)
    return -1;
  while (fgets(line, sizeof(line), fp) && !(n = kmap_words(line, w, 3)))
    ;
  fclose(fp);
  if (!n)
    return -1;
  snprintf(layout, lsz, "%s", w[0]);
  snprintf(variant, vsz, "%s", n > 1 ? w[1] : "");
  return 0;
}

// the n-th comma separated word of list into buf, "" past its end
static void
nth(char *buf, size_t sz, const char *list, int n)
{
  const char *e;

  while (n-- > 0 && list && (list = strchr(list, ',')))
    list++;
  if (!list) {
    buf[0] = '\0';
    return;
  }
  e = strchr(list, ',');
  snprintf(buf, sz, "%.*s", e ? (int)(e - list) : (int)strlen(list), list);
}

// ?man kmap: set the keyboard layout of the console, or give it to xkb
// ?man arguments: [-d dir] [-l [layout]] [-n] [-x [-o options]] [layout[,layout] [variant[,variant]]]
// ?man kmap loads a kmap layout (see kmap.h: portable text, every layout
// ?man alpine ships) into the console's keyboard: every console takes it,
// ?man through the system's own interface (linux's console, wscons, syscons
// ?man and vt).
// ?man with no layout it loads what /etc/kmap names ("layout variant").
// ?man with -x it writes the layouts as one xkb keymap instead, a group
// ?man each, for wayland compositors and x: what wlde's keyboard is made from
int
main(int argc, char *argv[])
{
  const char *dir = KMAP_DIR, *options = NULL;
  char path[PATH_MAX], layouts[256], variants[256];
  struct kmap pc, opts[8];
  int nopts = 0;
  int lflag = 0, xflag = 0, nflag = 0, rc = 0, n, i;
  struct kmap maps[4];

  ARGBEGIN
  {
    // ?man -d:str: -d dir: where the layouts are, /share/kmap when not given
    case 'd':
      dir = EARGF(usage());
      break;
    // ?man -l: list the layouts, or with a layout its variants
    case 'l':
      lflag = 1;
      break;
    // ?man -n: read the layout and say whether it is good, set nothing
    case 'n':
      nflag = 1;
      break;
    // ?man -o:str: -o options: xkb options for -x, comma separated
    case 'o':
      options = EARGF(usage());
      break;
    // ?man -x: write the layouts as an xkb keymap
    case 'x':
      xflag = 1;
      break;
    default:
      usage();
  }
  ARGEND

  if (lflag)
    return argc > 1 ? (usage(), 1) : list(dir, argc ? argv[0] : NULL);
  if (argc > 2)
    usage();
  if (argc) {
    snprintf(layouts, sizeof(layouts), "%s", argv[0]);
    snprintf(variants, sizeof(variants), "%s", argc > 1 ? argv[1] : "");
  } else if (configured(layouts, sizeof(layouts), variants, sizeof(variants)) != 0) {
    // no layout chosen: the console keeps its own, and xkb is given us
    if (!xflag)
      return 0;
    snprintf(layouts, sizeof(layouts), "us");
    variants[0] = '\0';
  }


  // the console has one layout: the first of them
  memset(maps, 0, sizeof(maps));
  memset(&pc, 0, sizeof(pc));
  memset(opts, 0, sizeof(opts));
  for (n = 0; n < (xflag ? (int)LEN(maps) : 1); n++) {
    char layout[64], variant[128];

    nth(layout, sizeof(layout), layouts, n);
    if (!layout[0])
      break;
    nth(variant, sizeof(variant), variants, n);
    kmap_path(path, sizeof(path), dir, layout, variant);
    if (kmap_read(path, &maps[n]) != 0) {
      rc = 1;
      goto done;
    }
  }
  if (!xflag) {
    snprintf(path, sizeof(path), "%s/compose.kmap", dir);
    if (access(path, F_OK) == 0 && kmap_read(path, &maps[0]) != 0) {
      rc = 1;
      goto done;
    }
  }
  if (xflag) {
    // the pc keyboard under the layouts, and each option, xkb's name for it
    // (caps:escape) the file options/caps/escape.kmap
    snprintf(path, sizeof(path), "%s/pc.kmap", dir);
    if (kmap_read(path, &pc) != 0) {
      rc = 1;
      goto done;
    }
    for (const char *o = options; o && *o && nopts < (int)LEN(opts);) {
      const char *end = strchr(o, ','), *colon;
      size_t len = end ? (size_t)(end - o) : strlen(o);
      char one[128];

      snprintf(one, sizeof(one), "%.*s", (int)(len < sizeof(one) ? len : sizeof(one) - 1), o);
      o = end ? end + 1 : o + len;
      if (!one[0])
        continue;
      if (!(colon = strchr(one, ':'))) {
        weprintf("%s: an option is group:name\n", one);
        rc = 1;
        goto done;
      }
      snprintf(path, sizeof(path), "%s/options/%.*s/%s.kmap", dir, (int)(colon - one), one, colon + 1);
      if (access(path, F_OK) != 0) {
        weprintf("%s: kmap has no such option\n", one);
        rc = 1;
        goto done;
      }
      if (kmap_read(path, &opts[nopts++]) != 0) {
        rc = 1;
        goto done;
      }
    }
    rc = kmap_write_xkb(stdout, dir, &pc, maps, n, opts, nopts);
  }
  else if (!nflag)
    rc = kmap_load_console(&maps[0]);
done:
  for (i = 0; i < (int)LEN(maps); i++)
    kmap_free(&maps[i]);
  for (i = 0; i < nopts; i++)
    kmap_free(&opts[i]);
  kmap_free(&pc);
  return rc != 0;
}
