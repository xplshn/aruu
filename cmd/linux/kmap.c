/* see LICENSE file for copyright and license details */
#include <sys/ioctl.h>
#include <sys/types.h>

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "arg.h"
#include "kmap.h"
#include "paths.h"
#include "util.h"

// the kernel's console keyboard interface (linux/kd.h, linux/keyboard.h)
#define KDSKBENT     0x4B47
#define KDSKBDIACRUC 0x4BFB
#define KDGKBTYPE    0x4B33

#define K(t, v)   (((t) << 8) | (v))
#define KT_LATIN  0
#define KT_SPEC   2
#define KT_PAD    3
#define KT_DEAD   4
#define KT_SHIFT  7
#define KT_META   8
#define KT_LETTER 11
#define KT_DEAD2  13
#define KG_ALTGR  1
#define K_HOLE    K(KT_SPEC, 0)
#define K_ENTER   K(KT_SPEC, 1)
#define K_COMPOSE K(KT_SPEC, 14)
#define K_PCOMMA  K(KT_PAD, 15)

// the tables the kernel keeps a key's meaning in, one per held modifier set
#define T_SHIFT 1
#define T_ALTGR 2
#define T_CTRL  4
#define T_ALT   8

struct kbentry {
  unsigned char  table;
  unsigned char  index;
  unsigned short value;
};

struct kbdiacruc {
  unsigned int diacr, base, result;
};

struct kbdiacrsuc {
  unsigned int     cnt;
  struct kbdiacruc d[256];
};

// a key's xkb name and its evdev code, which the console's keycodes are
static const struct {
  const char *name;
  int         code;
} codes[] = {
  {"TLDE", 41}, {"AE01", 2},  {"AE02", 3},  {"AE03", 4},  {"AE04", 5},  {"AE05", 6},
  {"AE06", 7},  {"AE07", 8},  {"AE08", 9},  {"AE09", 10}, {"AE10", 11}, {"AE11", 12},
  {"AE12", 13}, {"AE13", 124}, {"AD01", 16}, {"AD02", 17}, {"AD03", 18}, {"AD04", 19},
  {"AD05", 20}, {"AD06", 21}, {"AD07", 22}, {"AD08", 23}, {"AD09", 24}, {"AD10", 25},
  {"AD11", 26}, {"AD12", 27}, {"BKSL", 43}, {"AC01", 30}, {"AC02", 31}, {"AC03", 32},
  {"AC04", 33}, {"AC05", 34}, {"AC06", 35}, {"AC07", 36}, {"AC08", 37}, {"AC09", 38},
  {"AC10", 39}, {"AC11", 40}, {"LSGT", 86}, {"AB01", 44}, {"AB02", 45}, {"AB03", 46},
  {"AB04", 47}, {"AB05", 48}, {"AB06", 49}, {"AB07", 50}, {"AB08", 51}, {"AB09", 52},
  {"AB10", 53}, {"AB11", 89}, {"SPCE", 57}, {"KPDL", 83},
};

// dead keys: the kernel's own six, then the rest as dead2 with a latin-1
// character naming them in the accent table, as only those fit its byte
static const struct {
  const char *name;
  int         type, value;
  unsigned    diacr; // what the accent table calls it
} deads[] = {
  {"dead_grave", KT_DEAD, 0, '`'},          {"dead_acute", KT_DEAD, 1, '\''},
  {"dead_circumflex", KT_DEAD, 2, '^'},     {"dead_tilde", KT_DEAD, 3, '~'},
  {"dead_diaeresis", KT_DEAD, 4, '"'},      {"dead_cedilla", KT_DEAD, 5, ','},
  {"dead_macron", KT_DEAD2, 0xaf, 0xaf},    {"dead_abovering", KT_DEAD2, 0xb0, 0xb0},
  {"dead_caron", KT_DEAD2, 0xa6, 0xa6},     {"dead_breve", KT_DEAD2, 0xa4, 0xa4},
  {"dead_abovedot", KT_DEAD2, 0xb7, 0xb7},  {"dead_doubleacute", KT_DEAD2, 0xa8, 0xa8},
  {"dead_ogonek", KT_DEAD2, 0xb8, 0xb8},    {"dead_belowdot", KT_DEAD2, 0xa7, 0xa7},
  {"dead_hook", KT_DEAD2, 0xbf, 0xbf},      {"dead_horn", KT_DEAD2, 0xb9, 0xb9},
  {"dead_stroke", KT_DEAD2, 0xac, 0xac},    {"dead_belowcomma", KT_DEAD2, 0xb5, 0xb5},
  {"dead_belowmacron", KT_DEAD2, 0xb3, 0xb3}, {"dead_iota", KT_DEAD2, 0xbd, 0xbd},
};

// names of characters, the rest of what a layout may write as a name
static const struct {
  const char *name;
  unsigned    c;
} named[] = {
  {"space", ' '},           {"nobreakspace", 0xa0},   {"combining_grave", 0x300},
  {"combining_acute", 0x301}, {"combining_tilde", 0x303}, {"combining_hook", 0x309},
  {"combining_belowdot", 0x323},
};

static char *ttys[] = {
  ARUU_LINUX_PATH_PROC "/self/fd/0",
  ARUU_PATH_DEVCONSOLE,
  ARUU_PATH_DEVTTY,
  "/dev/tty0",
};

static void
usage(void)
{
  eprintf("usage: %s [-d dir] [-l [layout]] [-n] [-x [-o options] [-r rules]]\n"
          "       [layout[,layout] [variant[,variant]]]\n", argv0);
}

// a descriptor of a console that takes keyboard ioctls
static int
console(void)
{
  char type;
  size_t i;
  int fd;

  for (i = 0; i < LEN(ttys); i++) {
    if ((fd = open(ttys[i], O_RDWR)) < 0 && (fd = open(ttys[i], O_RDONLY)) < 0)
      continue;
    if (ioctl(fd, KDGKBTYPE, &type) == 0)
      return fd;
    close(fd);
  }
  eprintf("no console to set the keyboard of\n");
  return -1;
}

static int
dead_of(const char *s)
{
  size_t i;

  for (i = 0; i < LEN(deads); i++)
    if (!strcmp(deads[i].name, s))
      return (int)i;
  return -1;
}

// the unicode character a symbol stands for, -1 for none
static long
char_of(const char *s)
{
  long c = kmap_char(s);
  size_t i;
  char *end;

  if (c >= 0)
    return c;
  for (i = 0; i < LEN(named); i++)
    if (!strcmp(named[i].name, s))
      return named[i].c;
  if (s[0] == 'U' && s[1]) {
    c = strtol(s + 1, &end, 16);
    if (!*end)
      return c;
  }
  return -1;
}

// the console value of a symbol: latin-1 as itself (a letter when caps
// shifts it), the rest of unicode as the kernel takes it, dead keys and the
// few names the console has a meaning for, else a hole
static unsigned short
value_of(const char *s, int caps)
{
  long c;
  int d;

  if ((d = dead_of(s)) >= 0)
    return K(deads[d].type, deads[d].value);
  if (!strcmp(s, "ISO_Level3_Shift"))
    return K(KT_SHIFT, KG_ALTGR);
  if (!strcmp(s, "Multi_key"))
    return K_COMPOSE;
  if (!strcmp(s, "Return"))
    return K_ENTER;
  if ((c = char_of(s)) < 0 || c >= 0xf000)
    return K_HOLE;
  if (c < 0x100)
    return K(caps ? KT_LETTER : KT_LATIN, c);
  return (unsigned short)(c ^ 0xf000);
}

static int
set(int fd, int table, int code, unsigned short v)
{
  struct kbentry e = {table, code, v};

  if (ioctl(fd, KDSKBENT, &e) != 0) {
    weprintf("KDSKBENT table %d key %d:", table, code);
    return -1;
  }
  return 0;
}

static int
code_of(const char *name)
{
  size_t i;

  for (i = 0; i < LEN(codes); i++)
    if (!strcmp(codes[i].name, name))
      return codes[i].code;
  return -1;
}

static int
load(const struct kmap *k)
{
  // the console has four of a key's levels
  static const int tables[4] = {0, T_SHIFT, T_ALTGR, T_SHIFT | T_ALTGR};
  struct kbdiacrsuc acc;
  int fd = console(), rc = 0, l, code;
  size_t i, j;

  for (i = 0; i < k->nkeys; i++) {
    const struct kmap_key *key = &k->keys[i];
    long c0;

    if ((code = code_of(key->name)) < 0)
      continue;
    // the keypad's decimal key keeps the console's own, which numlock
    // turns between delete and its character: only which one it types differs
    if (!strcmp(key->name, "KPDL")) {
      for (l = 0; l < key->nsym; l++)
        if (!strcmp(key->sym[l], ","))
          rc |= set(fd, 0, code, K_PCOMMA);
      continue;
    }
    for (l = 0; l < 4; l++)
      rc |= set(fd, tables[l], code,
                l < key->nsym ? value_of(key->sym[l], key->caps) : K_HOLE);
    // control and alt follow the letter the key types, so ctrl+a is ^A on any
    // latin layout. on another script they stay where the console had them
    c0 = key->nsym ? char_of(key->sym[0]) : -1;
    if (c0 >= 'a' && c0 <= 'z') {
      rc |= set(fd, T_CTRL, code, K(KT_LATIN, c0 & 0x1f));
      rc |= set(fd, T_CTRL | T_SHIFT, code, K(KT_LATIN, c0 & 0x1f));
    }
    if (c0 > 0x20 && c0 < 0x7f)
      rc |= set(fd, T_ALT, code, K(KT_META, c0));
  }

  // the accent table: what each dead key of this layout makes, as many as
  // the kernel holds
  memset(&acc, 0, sizeof(acc));
  // the kernel takes one fewer than its table holds
  for (j = 0; j < k->ncompose && acc.cnt < LEN(acc.d) - 1; j++) {
    const struct kmap_compose *cp = &k->compose[j];
    long base = char_of(cp->base), res = char_of(cp->result);
    int d = dead_of(cp->dead), used = 0;

    if (d < 0 || base < 0 || res < 0)
      continue;
    for (i = 0; i < k->nkeys && !used; i++)
      for (l = 0; l < k->keys[i].nsym && !used; l++)
        used = !strcmp(k->keys[i].sym[l], cp->dead);
    if (!used)
      continue;
    acc.d[acc.cnt].diacr = deads[d].diacr;
    acc.d[acc.cnt].base = base;
    acc.d[acc.cnt].result = res;
    acc.cnt++;
  }
  if (ioctl(fd, KDSKBDIACRUC, &acc) != 0) {
    weprintf("KDSKBDIACRUC:");
    rc = -1;
  }
  close(fd);
  return rc;
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
// ?man arguments: [-d dir] [-l [layout]] [-n] [-x [-o options] [-r rules]] [layout[,layout] [variant[,variant]]]
// ?man kmap loads a kmap layout (see kmap.h: portable text, every layout
// ?man alpine ships) into the console's keyboard: every console takes it.
// ?man with no layout it loads what /etc/kmap names ("layout variant").
// ?man with -x it writes the layouts as one xkb keymap instead, a group
// ?man each, for wayland compositors and x: what wlde's keyboard is made from
int
main(int argc, char *argv[])
{
  const char *dir = KMAP_DIR, *options = NULL, *rules = NULL;
  char path[PATH_MAX], layouts[256], variants[256], rulesbuf[PATH_MAX];
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
    // ?man -r:str: -r rules: the xkb rules file options are looked up in
    case 'r':
      rules = EARGF(usage());
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
    // no layout chosen: the console keeps its own
    return 0;
  }
  if (!rules) {
    const char *root = getenv("XKB_CONFIG_ROOT");

    snprintf(rulesbuf, sizeof(rulesbuf), "%s/rules/evdev", root && *root ? root : "/share/X11/xkb");
    rules = rulesbuf;
  }

  // the console has one layout: the first of them
  memset(maps, 0, sizeof(maps));
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
  if (xflag)
    rc = kmap_write_xkb(stdout, maps, n, options, rules);
  else if (!nflag)
    rc = load(&maps[0]);
done:
  for (i = 0; i < (int)LEN(maps); i++)
    kmap_free(&maps[i]);
  return rc != 0;
}
