/* see LICENSE file for copyright and license details */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kmap.h"
#include "../util.h"

void
kmap_path(char *buf, size_t sz, const char *dir, const char *layout, const char *variant)
{
  snprintf(buf, sz, "%s/%s/%s.kmap", dir ? dir : KMAP_DIR, layout,
           variant && *variant ? variant : layout);
}

int
kmap_words(char *line, char **w, int max)
{
  char *r = line, *o = line;
  int n = 0;

  while (*r) {
    while (*r == ' ' || *r == '\t' || *r == '\r' || *r == '\n')
      r++;
    if (!*r || *r == '#')
      break;
    if (n == max)
      break;
    w[n++] = o;
    while (*r && *r != ' ' && *r != '\t' && *r != '\r' && *r != '\n') {
      if (*r != '\'') {
        *o++ = *r++;
        continue;
      }
      // a quoted run: '' inside it is one quote
      for (r++; *r; r++) {
        if (*r == '\'' && r[1] == '\'') {
          *o++ = '\'';
          r++;
        } else if (*r == '\'') {
          r++;
          break;
        } else
          *o++ = *r;
      }
    }
    if (*r)
      r++;
    *o++ = '\0';
  }
  return n;
}

long
kmap_char(const char *s)
{
  const unsigned char *u = (const unsigned char *)s;
  long c;
  int n, i;

  if (!u[0])
    return -1;
  if (u[0] < 0x80)
    return u[1] ? -1 : u[0];
  if ((u[0] & 0xe0) == 0xc0)
    n = 1, c = u[0] & 0x1f;
  else if ((u[0] & 0xf0) == 0xe0)
    n = 2, c = u[0] & 0x0f;
  else if ((u[0] & 0xf8) == 0xf0)
    n = 3, c = u[0] & 0x07;
  else
    return -1;
  for (i = 1; i <= n; i++) {
    if ((u[i] & 0xc0) != 0x80)
      return -1;
    c = c << 6 | (u[i] & 0x3f);
  }
  return u[n + 1] ? -1 : c;
}

static void
copy(char *dst, size_t sz, const char *src)
{
  snprintf(dst, sz, "%s", src);
}

// the rest of the words joined by a space, a text such as a name
static void
join(char *dst, size_t sz, char **w, int n)
{
  int i;

  dst[0] = '\0';
  for (i = 0; i < n; i++) {
    if (i)
      strlcat(dst, " ", sz);
    strlcat(dst, w[i], sz);
  }
}

int
kmap_read(const char *path, struct kmap *k)
{
  char line[1024], *w[16];
  FILE *fp;
  int n, lineno = 0;

  if (!(fp = fopen(path, "r"))) {
    weprintf("open %s:", path);
    return -1;
  }
  while (fgets(line, sizeof(line), fp)) {
    lineno++;
    if (!(n = kmap_words(line, w, 16)))
      continue;
    // a header field keeps its first value: compose.kmap read after a
    // layout does not rename it
    if (!strcmp(w[0], "id")) {
      if (n > 1 && !k->id[0])
        copy(k->id, sizeof(k->id), w[1]);
    } else if (!strcmp(w[0], "layout")) {
      if (n > 1 && !k->layout[0])
        copy(k->layout, sizeof(k->layout), w[1]);
    } else if (!strcmp(w[0], "variant")) {
      if (n > 1 && !k->variant[0])
        copy(k->variant, sizeof(k->variant), w[1]);
    } else if (!strcmp(w[0], "name")) {
      if (!k->name[0])
        join(k->name, sizeof(k->name), w + 1, n - 1);
    }
    else if (!strcmp(w[0], "level3") && n > 1)
      k->level3_ralt = !strcmp(w[1], "ralt");
    else if (!strcmp(w[0], "key") && n > 2) {
      struct kmap_key *key;
      int i;

      k->keys = ereallocarray(k->keys, k->nkeys + 1, sizeof(*k->keys));
      key = &k->keys[k->nkeys++];
      memset(key, 0, sizeof(*key));
      copy(key->name, sizeof(key->name), w[1]);
      for (i = 2; i < n; i++) {
        if (!strcmp(w[i], "caps"))
          key->caps = 1;
        else if (!strncmp(w[i], "type=", 5))
          copy(key->type, sizeof(key->type), w[i] + 5);
        else if (key->nsym < KMAP_LEVELS)
          copy(key->sym[key->nsym++], KMAP_SYM, w[i]);
      }
    } else if (!strcmp(w[0], "compose") && n == 4) {
      struct kmap_compose *c;

      k->compose = ereallocarray(k->compose, k->ncompose + 1, sizeof(*k->compose));
      c = &k->compose[k->ncompose++];
      copy(c->dead, sizeof(c->dead), w[1]);
      copy(c->base, sizeof(c->base), w[2]);
      copy(c->result, sizeof(c->result), w[3]);
    } else if (strcmp(w[0], "kmap"))
      weprintf("%s:%d: %s is not a kmap line, skipped\n", path, lineno, w[0]);
  }
  fclose(fp);
  return 0;
}

void
kmap_free(struct kmap *k)
{
  free(k->keys);
  free(k->compose);
  memset(k, 0, sizeof(*k));
}

// a symbol as xkb writes it: latin-1 by its code, which is its keysym, other
// characters as Uxxxx, names as they are
static void
xkb_sym(FILE *fp, const char *s)
{
  long c = kmap_char(s);

  if (!strcmp(s, "none"))
    fputs("NoSymbol", fp);
  else if (c < 0)
    fputs(s, fp);
  else if (c < 0x100)
    fprintf(fp, "0x%02lx", c);
  else
    fprintf(fp, "U%04lX", c);
}

// the includes an xkb option stands for in section (symbols, compat or
// types) of the rules file, written as include lines. an option the rules
// do not know is said
static int
option_includes(FILE *fp, const char *rules, const char *option, const char *section)
{
  char line[512], *w[8];
  FILE *rf;
  int in = 0, found = 0, n;

  if (!(rf = fopen(rules, "r")))
    return 0;
  while (fgets(line, sizeof(line), rf)) {
    if (line[0] == '!') {
      n = kmap_words(line + 1, w, 8);
      in = n == 3 && !strcmp(w[0], "option") && !strcmp(w[1], "=") && !strcmp(w[2], section);
      continue;
    }
    if (!in || kmap_words(line, w, 8) != 3 || strcmp(w[0], option) || strcmp(w[1], "="))
      continue;
    // +a(b)+c(d): each part is a file and map of its own
    for (char *p = strtok(w[2], "+|"); p; p = strtok(NULL, "+|"))
      fprintf(fp, "    include \"%s\"\n", p);
    found = 1;
  }
  fclose(rf);
  return found;
}

static void
put_options(FILE *fp, const char *rules, const char *options, const char *section)
{
  const char *o, *end;
  char one[128];

  if (!options || !rules)
    return;
  // split by hand: option_includes tokenizes the rules lines with strtok
  for (o = options; *o; o = *end ? end + 1 : end) {
    end = strchr(o, ',');
    if (!end)
      end = o + strlen(o);
    if (end == o || (size_t)(end - o) >= sizeof(one))
      continue;
    snprintf(one, sizeof(one), "%.*s", (int)(end - o), o);
    if (!option_includes(fp, rules, one, section) && !strcmp(section, "symbols"))
      weprintf("xkb option %s is not in %s\n", one, rules);
  }
}

int
kmap_write_xkb(FILE *fp, const struct kmap *maps, int n, const char *options, const char *rules)
{
  const char **names = NULL;
  size_t i, j, nnames = 0;
  int g, l, ralt = 0;

  for (g = 0; g < n; g++)
    ralt |= maps[g].level3_ralt;
  fprintf(fp, "xkb_keymap {\n"
              "  xkb_keycodes { include \"evdev+aliases(qwerty)\" };\n"
              "  xkb_types {\n"
              "    include \"complete\"\n");
  put_options(fp, rules, options, "types");
  fprintf(fp, "  };\n"
              "  xkb_compat {\n"
              "    include \"complete\"\n");
  put_options(fp, rules, options, "compat");
  fprintf(fp, "  };\n"
              "  xkb_symbols {\n"
              "    include \"pc+inet(evdev)\"\n");
  if (ralt)
    fputs("    include \"level3(ralt_switch)\"\n", fp);
  for (g = 0; g < n; g++) {
    fprintf(fp, "    name[Group%d] = \"", g + 1);
    for (i = 0; maps[g].name[i]; i++)
      if (maps[g].name[i] != '"' && maps[g].name[i] != '\\')
        fputc(maps[g].name[i], fp);
    fputs("\";\n", fp);
  }
  // every key any of the layouts names, in the order they first name it
  for (g = 0; g < n; g++)
    for (i = 0; i < maps[g].nkeys; i++) {
      for (j = 0; j < nnames && strcmp(names[j], maps[g].keys[i].name); j++)
        ;
      if (j == nnames) {
        names = ereallocarray(names, nnames + 1, sizeof(*names));
        names[nnames++] = maps[g].keys[i].name;
      }
    }
  for (j = 0; j < nnames; j++) {
    int first = 1;

    // replace, so a level the layout leaves empty is not filled from pc
    fprintf(fp, "    replace key <%s> { ", names[j]);
    for (g = 0; g < n; g++) {
      const struct kmap_key *key = NULL;

      for (i = 0; i < maps[g].nkeys && !key; i++)
        if (!strcmp(maps[g].keys[i].name, names[j]))
          key = &maps[g].keys[i];
      if (!key)
        continue;
      if (key->type[0]) {
        fprintf(fp, "%stype[Group%d] = \"%s\"", first ? "" : ", ", g + 1, key->type);
        first = 0;
      }
      fprintf(fp, "%ssymbols[Group%d] = [ ", first ? "" : ", ", g + 1);
      for (l = 0; l < key->nsym; l++) {
        if (l)
          fputs(", ", fp);
        xkb_sym(fp, key->sym[l]);
      }
      fputs(" ]", fp);
      first = 0;
    }
    fputs(" };\n", fp);
  }
  free(names);
  // the options last, over the layouts' keys, as xkb's rules put them
  put_options(fp, rules, options, "symbols");
  fputs("  };\n};\n", fp);
  return ferror(fp) ? -1 : 0;
}

// the keys a layout places and where they are: evdev codes, which are the
// pc scancodes of set 1 but for the two japanese keys, and usb hid usages
static const struct {
  const char *name;
  int         evdev, pc, hid;
} codes[] = {
  {"TLDE", 41, 41, 53},   {"AE01", 2, 2, 30},     {"AE02", 3, 3, 31},     {"AE03", 4, 4, 32},
  {"AE04", 5, 5, 33},     {"AE05", 6, 6, 34},     {"AE06", 7, 7, 35},     {"AE07", 8, 8, 36},
  {"AE08", 9, 9, 37},     {"AE09", 10, 10, 38},   {"AE10", 11, 11, 39},   {"AE11", 12, 12, 45},
  {"AE12", 13, 13, 46},   {"AE13", 124, 125, 137}, {"AD01", 16, 16, 20},  {"AD02", 17, 17, 26},
  {"AD03", 18, 18, 8},    {"AD04", 19, 19, 21},   {"AD05", 20, 20, 23},   {"AD06", 21, 21, 28},
  {"AD07", 22, 22, 24},   {"AD08", 23, 23, 12},   {"AD09", 24, 24, 18},   {"AD10", 25, 25, 19},
  {"AD11", 26, 26, 47},   {"AD12", 27, 27, 48},   {"BKSL", 43, 43, 49},   {"AC01", 30, 30, 4},
  {"AC02", 31, 31, 22},   {"AC03", 32, 32, 7},    {"AC04", 33, 33, 9},    {"AC05", 34, 34, 10},
  {"AC06", 35, 35, 11},   {"AC07", 36, 36, 13},   {"AC08", 37, 37, 14},   {"AC09", 38, 38, 15},
  {"AC10", 39, 39, 51},   {"AC11", 40, 40, 52},   {"LSGT", 86, 86, 100},  {"AB01", 44, 44, 29},
  {"AB02", 45, 45, 27},   {"AB03", 46, 46, 6},    {"AB04", 47, 47, 25},   {"AB05", 48, 48, 5},
  {"AB06", 49, 49, 17},   {"AB07", 50, 50, 16},   {"AB08", 51, 51, 54},   {"AB09", 52, 52, 55},
  {"AB10", 53, 53, 56},   {"AB11", 89, 115, 135}, {"SPCE", 57, 57, 44},   {"KPDL", 83, 83, 99},
};

int
kmap_code(const char *name, enum kmap_codes scheme)
{
  size_t i;

  for (i = 0; i < sizeof(codes) / sizeof(codes[0]); i++)
    if (!strcmp(codes[i].name, name))
      return scheme == KMAP_PC ? codes[i].pc : scheme == KMAP_HID ? codes[i].hid : codes[i].evdev;
  return -1;
}

// characters a layout writes by name
static const struct {
  const char *name;
  long        c;
} named[] = {
  {"space", ' '},
  {"nobreakspace", 0xa0},
  {"combining_grave", 0x300},
  {"combining_acute", 0x301},
  {"combining_tilde", 0x303},
  {"combining_hook", 0x309},
  {"combining_belowdot", 0x323},
};

long
kmap_unicode(const char *s)
{
  long c = kmap_char(s);
  size_t i;
  char *end;

  if (c >= 0)
    return c;
  for (i = 0; i < sizeof(named) / sizeof(named[0]); i++)
    if (!strcmp(named[i].name, s))
      return named[i].c;
  if (s[0] == 'U' && s[1]) {
    c = strtol(s + 1, &end, 16);
    if (!*end && c > 0)
      return c;
  }
  return -1;
}

// the dead keys and the combining characters they stand for
static const struct {
  const char *name;
  long        c;
} deads[] = {
  {"dead_grave", 0x300},       {"dead_acute", 0x301},       {"dead_circumflex", 0x302},
  {"dead_tilde", 0x303},       {"dead_macron", 0x304},      {"dead_breve", 0x306},
  {"dead_abovedot", 0x307},    {"dead_diaeresis", 0x308},   {"dead_hook", 0x309},
  {"dead_abovering", 0x30a},   {"dead_doubleacute", 0x30b}, {"dead_caron", 0x30c},
  {"dead_doublegrave", 0x30f}, {"dead_invertedbreve", 0x311}, {"dead_abovecomma", 0x313},
  {"dead_abovereversedcomma", 0x314}, {"dead_horn", 0x31b}, {"dead_belowdot", 0x323},
  {"dead_belowcomma", 0x326},  {"dead_cedilla", 0x327},     {"dead_ogonek", 0x328},
  {"dead_belowmacron", 0x331}, {"dead_stroke", 0x335},      {"dead_iota", 0x345},
};

long
kmap_dead(const char *s)
{
  size_t i;

  for (i = 0; i < sizeof(deads) / sizeof(deads[0]); i++)
    if (!strcmp(deads[i].name, s))
      return deads[i].c;
  return -1;
}
