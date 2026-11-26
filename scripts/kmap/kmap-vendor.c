// kmap-vendor.c: writes every keyboard layout of xkeyboard-config's default
// ruleset (base.lst's layouts and variants, the set alpine ships as
// kbd-bkeymaps) as a kmap file, the portable text kmap reads, with the pc
// keyboard under them (pc.kmap), the xkb options that change keys
// (options/<group>/<name>.kmap) and the key codes, types and compatibility
// every xkb keymap shares (xkb/), so kmap -x writes whole keymaps alone. it was run once,
// on a machine with libxkbcommon, to write share/kmap, which is kept as it
// came out and installed as it is; nothing builds or runs this:
//
//   cc -o kmap-vendor scripts/kmap/kmap-vendor.c -lxkbcommon -lxkbregistry
//   ./kmap-vendor share/kmap
//
// a kmap names keys by their xkb position (AE01 is the key under 1), which no
// system owns, and gives each its levels: base, shift, level3 (altgr), level3
// with shift, and up to eight for the layouts that have them. the typing block
// is written always, any other key only where the layout makes it other than
// us does. see aruu's shared/kmap.h for the format
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbregistry.h>

// the keys layouts differ in: the typing block, the key between left shift
// and z, and the keypad's decimal key. their evdev codes, xkb's minus 8
static const struct {
  const char *name;
  int evdev;
} keys[] = {
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

static void
put_utf8(FILE *fp, unsigned c)
{
  if (c < 0x80)
    fputc(c, fp);
  else if (c < 0x800)
    fprintf(fp, "%c%c", 0xc0 | c >> 6, 0x80 | (c & 0x3f));
  else if (c < 0x10000)
    fprintf(fp, "%c%c%c", 0xe0 | c >> 12, 0x80 | (c >> 6 & 0x3f), 0x80 | (c & 0x3f));
  else
    fprintf(fp, "%c%c%c%c", 0xf0 | c >> 18, 0x80 | (c >> 12 & 0x3f), 0x80 | (c >> 6 & 0x3f),
            0x80 | (c & 0x3f));
}

// a symbol as a word: the character it types when it types one that reads as
// itself, else its keysym name. a quote or # is rc quoted
// every keysym written by name, which the vendored compatibility must know:
// the dead keys, the modifiers, the group switches
static xkb_keysym_t named[4096];
static int nnamed;

static void
add_named(xkb_keysym_t ks)
{
  for (int i = 0; i < nnamed; i++)
    if (named[i] == ks)
      return;
  if (nnamed < (int)(sizeof(named) / sizeof(named[0])))
    named[nnamed++] = ks;
}

static void
put_sym(FILE *fp, xkb_keysym_t ks)
{
  char name[64];
  unsigned c;

  if (ks == XKB_KEY_NoSymbol) {
    fputs(" none", fp);
    return;
  }
  c = xkb_keysym_to_utf32(ks);
  fputc(' ', fp);
  // the character only when it is that character's own keysym: KP_1 types
  // 1 but is the keypad's, and xkb tells them apart
  if (xkb_utf32_to_keysym(c) == ks && c > 0x20 && c != 0x7f && !(c >= 0x80 && c < 0xa0) && c != 0xa0 &&
      !(c >= 0x300 && c < 0x370) && !(c >= 0x200b && c <= 0x200f)) {
    if (c == '\'')
      fputs("''''", fp);
    else if (c == '#')
      fputs("'#'", fp);
    else
      put_utf8(fp, c);
    return;
  }
  xkb_keysym_get_name(ks, name, sizeof(name));
  fputs(name, fp);
  add_named(ks);
}

static int
is_main(int evdev)
{
  unsigned i;

  for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
    if (keys[i].evdev == evdev)
      return 1;
  return 0;
}

static struct xkb_keymap *us;
static struct xkb_context *ctx;

// the xkb types a key may have, from the complete set xkeyboard-config
// compiles every keymap with
static const char *types[] = {
  "ONE_LEVEL", "TWO_LEVEL", "ALPHABETIC", "KEYPAD", "SHIFT+ALT", "CTRL+ALT",
  "THREE_LEVEL", "FOUR_LEVEL", "FOUR_LEVEL_ALPHABETIC", "FOUR_LEVEL_SEMIALPHABETIC",
  "FOUR_LEVEL_MIXED_KEYPAD", "FOUR_LEVEL_X", "FOUR_LEVEL_KEYPAD", "FOUR_LEVEL_PLUS_LOCK",
  "FOUR_LEVEL_LOCKABLE_LEVEL2", "SEPARATE_CAPS_AND_SHIFT_ALPHABETIC", "EIGHT_LEVEL",
  "EIGHT_LEVEL_ALPHABETIC", "EIGHT_LEVEL_SEMIALPHABETIC", "EIGHT_LEVEL_WITH_LEVEL5_LOCK",
  "EIGHT_LEVEL_ALPHABETIC_WITH_LEVEL5_LOCK", "PC_ALT_LEVEL2", "PC_CONTROL_LEVEL2",
  "PC_SUPER_LEVEL2", "PC_SHIFT_SUPER_LEVEL2", "PC_CONTROL_SUPER_LEVEL2",
};
#define NTYPES (sizeof(types) / sizeof(types[0]))

struct key {
  char          name[8];
  xkb_keycode_t kc;
  xkb_keysym_t  lv[8];
  int           n, caps;
  const char   *type; // NULL when xkb chooses the one the layout has
};

// how a key's levels are reached: for each, the modifier sets that select it
struct sig {
  int            levels;
  int            cnt[8];
  xkb_mod_mask_t m[8][16];
};

static void
signature(struct xkb_keymap *km, xkb_keycode_t kc, struct sig *s)
{
  int l, i, j;

  memset(s, 0, sizeof(*s));
  s->levels = xkb_keymap_num_levels_for_key(km, kc, 0);
  if (s->levels > 8)
    s->levels = 8;
  for (l = 0; l < s->levels; l++) {
    s->cnt[l] = xkb_keymap_key_get_mods_for_level(km, kc, 0, l, s->m[l], 16);
    for (i = 1; i < s->cnt[l]; i++)
      for (j = i; j > 0 && s->m[l][j - 1] > s->m[l][j]; j--) {
        xkb_mod_mask_t t = s->m[l][j];
        s->m[l][j] = s->m[l][j - 1];
        s->m[l][j - 1] = t;
      }
  }
}

// a keymap of the pc board with keys as given, each of type type (NULL:
// xkb's own choice): what the generated kmap compiles to, but for the types
static struct xkb_keymap *
probe(const struct key *k, int nkeys, const char *type)
{
  char *buf;
  size_t len;
  FILE *fp = open_memstream(&buf, &len);
  struct xkb_keymap *km;
  int i, l;

  fputs("xkb_keymap { xkb_keycodes { include \"evdev+aliases(qwerty)\" };"
        " xkb_types { include \"complete\" }; xkb_compat { include \"complete\" };"
        " xkb_symbols { include \"pc+inet(evdev)\"\n", fp);
  for (i = 0; i < nkeys; i++) {
    fprintf(fp, "replace key <%s> { ", k[i].name);
    if (type)
      fprintf(fp, "type = \"%s\", ", type);
    fputs("[ ", fp);
    for (l = 0; l < k[i].n; l++)
      fprintf(fp, "%s0x%x", l ? ", " : "", k[i].lv[l]);
    fputs(" ] };\n", fp);
  }
  fputs("}; };\n", fp);
  fclose(fp);
  km = xkb_keymap_new_from_string(ctx, buf, XKB_KEYMAP_FORMAT_TEXT_V1, 0);
  free(buf);
  return km;
}

static int
collect(struct xkb_keymap *km, xkb_keycode_t kc, const char *name, struct key *k)
{
  const xkb_keysym_t *syms;
  unsigned l;

  memset(k, 0, sizeof(*k));
  snprintf(k->name, sizeof(k->name), "%s", name);
  k->kc = kc;
  for (l = 0; l < 8; l++)
    if (xkb_keymap_key_get_syms_by_level(km, kc, 0, l, &syms) >= 1) {
      k->lv[l] = syms[0];
      k->n = l + 1;
    }
  // caps lock shifts it when its second level is the first's capital
  k->caps = k->lv[0] && k->lv[1] && k->lv[0] != k->lv[1] &&
            xkb_keysym_to_upper(k->lv[0]) == k->lv[1];
  // a key the layout leaves without symbols where the pc keyboard has some
  // is written as none, else the pc keyboard's would show through
  if (!k->n && km != us && xkb_keymap_num_levels_for_key(us, kc, 0) > 0) {
    const xkb_keysym_t *s0;

    if (xkb_keymap_key_get_syms_by_level(us, kc, 0, 0, &s0) >= 1)
      k->n = 1;
  }
  return k->n > 0;
}

// whether km gives kc other symbols than us does
static int
differs(struct xkb_keymap *km, xkb_keycode_t kc)
{
  const xkb_keysym_t *a, *b;
  unsigned l;

  for (l = 0; l < 8; l++) {
    int na = xkb_keymap_key_get_syms_by_level(km, kc, 0, l, &a);
    int nb = xkb_keymap_key_get_syms_by_level(us, kc, 0, l, &b);

    if ((na > 0 ? a[0] : 0) != (nb > 0 ? b[0] : 0))
      return 1;
  }
  return 0;
}

// the type each key needs written: none where xkb's own choice reaches the
// levels the way the layout does, else the first standard type that does
static void
find_types(struct xkb_keymap *ref, struct key *k, int n)
{
  struct xkb_keymap *auto_km = probe(k, n, NULL);
  struct sig want, got;
  int i, left = 0;
  unsigned t;

  if (!auto_km)
    return;
  for (i = 0; i < n; i++) {
    signature(ref, k[i].kc, &want);
    signature(auto_km, k[i].kc, &got);
    if (memcmp(&want, &got, sizeof(want))) {
      k[i].type = "?";
      left++;
    }
  }
  xkb_keymap_unref(auto_km);
  for (t = 0; t < NTYPES && left; t++) {
    struct xkb_keymap *km = probe(k, n, types[t]);

    if (!km)
      continue;
    for (i = 0; i < n; i++) {
      if (!k[i].type || strcmp(k[i].type, "?"))
        continue;
      signature(ref, k[i].kc, &want);
      signature(km, k[i].kc, &got);
      if (!memcmp(&want, &got, sizeof(want))) {
        k[i].type = types[t];
        left--;
      }
    }
    xkb_keymap_unref(km);
  }
  for (i = 0; i < n; i++)
    if (k[i].type && !strcmp(k[i].type, "?")) {
      fprintf(stderr, "kmap-vendor: no type reaches %s's levels as the layout does\n", k[i].name);
      k[i].type = NULL;
    }
}

// key lines: the name, the levels, caps when caps lock shifts it, and the
// type when xkb would choose another
static void
put_keys(FILE *fp, const struct key *k, int n)
{
  for (int i = 0; i < n; i++) {
    fprintf(fp, "key %s", k[i].name);
    for (int l = 0; l < k[i].n; l++)
      put_sym(fp, k[i].lv[l]);
    if (k[i].caps)
      fputs(" caps", fp);
    if (k[i].type)
      fprintf(fp, " type=%s", k[i].type);
    fputc('\n', fp);
  }
}

static int
write_layout(const char *dir, const char *layout, const char *variant, const char *desc)
{
  struct xkb_rule_names names = {"evdev", "pc105", layout, variant, NULL};
  struct xkb_keymap *km = xkb_keymap_new_from_names(ctx, &names, 0);
  static struct key k[1024];
  char path[1024], id[256];
  const xkb_keysym_t *syms;
  int n = 0, i;
  FILE *fp;

  if (!km) {
    fprintf(stderr, "kmap-vendor: %s %s does not compile, left out\n", layout,
            variant ? variant : "");
    return -1;
  }
  for (i = 0; i < (int)(sizeof(keys) / sizeof(keys[0])); i++)
    n += collect(km, keys[i].evdev + 8, keys[i].name, &k[n]);
  // the rest of the board where the layout changes it: the keypad, the
  // modifiers some layouts move. the virtual keys stay as xkb keeps them
  for (xkb_keycode_t kc = 8; kc <= xkb_keymap_max_keycode(km) && n < 1024; kc++) {
    const char *kn = xkb_keymap_key_get_name(km, kc);

    if (!kn || is_main(kc - 8))
      continue;
    // right alt as a bare level 3 switch is the level3 line; anything more
    // (a second level, another modifier) is written as the key
    if (kc == 100 + 8 && xkb_keymap_num_levels_for_key(km, kc, 0) == 1 &&
        xkb_keymap_key_get_syms_by_level(km, kc, 0, 0, &syms) == 1 &&
        syms[0] == XKB_KEY_ISO_Level3_Shift)
      continue;
    if (!strcmp(kn, "LVL3") || !strcmp(kn, "MDSW"))
      continue;
    if (differs(km, kc))
      n += collect(km, kc, kn, &k[n]);
  }
  find_types(km, k, n);

  snprintf(id, sizeof(id), "%s%s%s", layout, variant ? "-" : "", variant ? variant : "");
  snprintf(path, sizeof(path), "%s/%s", dir, layout);
  if (mkdir(path, 0755) != 0 && errno != EEXIST)
    return -1;
  snprintf(path, sizeof(path), "%s/%s/%s.kmap", dir, layout, variant ? variant : layout);
  if (!(fp = fopen(path, "w")))
    return -1;
  fprintf(fp, "kmap 1\nid %s\nlayout %s\n", id, layout);
  if (variant)
    fprintf(fp, "variant %s\n", variant);
  // a name with a quote in it is rc quoted whole
  fputs("name ", fp);
  if (strchr(desc, '\'')) {
    fputc('\'', fp);
    for (const char *c = desc; *c; c++)
      fputs(*c == '\'' ? "''" : (char[2]){*c, 0}, fp);
    fputc('\'', fp);
  } else
    fputs(desc, fp);
  fputc('\n', fp);
  if (xkb_keymap_key_get_syms_by_level(km, 100 + 8, 0, 0, &syms) == 1 &&
      syms[0] == XKB_KEY_ISO_Level3_Shift)
    fputs("level3 ralt\n", fp);
  put_keys(fp, k, n);
  fclose(fp);
  xkb_keymap_unref(km);
  return 0;
}

// the parts of a serialized keymap that are no layout's: its key codes, key
// types and compatibility, written once as they are, xkb_<part> heads and all
static int
write_section(const char *dir, const char *text, const char *head, const char *file)
{
  const char *a = strstr(text, head), *b;
  char path[1024];
  FILE *fp;

  if (!a || !(b = strstr(a, "\n};\n")))
    return -1;
  snprintf(path, sizeof(path), "%s/xkb/%s", dir, file);
  if (!(fp = fopen(path, "w")))
    return -1;
  fwrite(a, 1, (size_t)(b - a) + 3, fp);
  fputc('\n', fp);
  return fclose(fp);
}

// the keys km has, every one with symbols
static int
all_keys(struct xkb_keymap *km, struct key *k, int max)
{
  int n = 0;

  for (xkb_keycode_t kc = xkb_keymap_min_keycode(km); kc <= xkb_keymap_max_keycode(km) && n < max; kc++) {
    const char *kn = xkb_keymap_key_get_name(km, kc);

    if (kn)
      n += collect(km, kc, kn, &k[n]);
  }
  return n;
}

// the modifier maps of a serialized keymap, by the keysyms of the keys they
// list: a layout or option that moves a keysym moves its modifier with it,
// as xkeyboard-config's own maps do
static void
put_modmaps(FILE *fp, struct xkb_keymap *km, const char *text)
{
  const char *p = text;

  while ((p = strstr(p, "modifier_map "))) {
    char mod[16], name[64];
    const char *q = p + strlen("modifier_map "), *end = strchr(p, ';');
    xkb_keysym_t seen[64];
    int nseen = 0;

    if (!end || sscanf(q, "%15s", mod) != 1)
      break;
    fprintf(fp, "modmap %s", mod);
    for (q = strchr(q, '<'); q && q < end; q = strchr(q + 1, '<')) {
      xkb_keycode_t kc;

      if (sscanf(q, "<%63[^>]>", name) != 1 || (kc = xkb_keymap_key_by_name(km, name)) == XKB_KEYCODE_INVALID)
        continue;
      for (int l = 0; l < 8; l++) {
        const xkb_keysym_t *syms;
        char ks[64];
        int dup = 0;

        if (xkb_keymap_key_get_syms_by_level(km, kc, 0, l, &syms) < 1 || syms[0] == XKB_KEY_NoSymbol)
          continue;
        for (int i = 0; i < nseen; i++)
          dup |= seen[i] == syms[0];
        if (dup || nseen == 64)
          continue;
        seen[nseen++] = syms[0];
        xkb_keysym_get_name(syms[0], ks, sizeof(ks));
        fprintf(fp, " %s", ks);
      }
    }
    fputc('\n', fp);
    p = end;
  }
}

// a keymap that keeps all of xkb's types and every compatibility entry the
// vendored keymaps can need: xkb writes out only what its keys use, so spare
// key codes carry each type and each named keysym. its key types and
// compatibility are what kmap -x writes, whole
static char *
full_base(void)
{
  char *buf;
  size_t len;
  FILE *fp = open_memstream(&buf, &len);
  struct xkb_keymap *km;
  unsigned t;
  int i, nkeys = (int)NTYPES + (nnamed + 7) / 8;

  // key codes of its own, past every real one: only this keymap has them,
  // and only its types and compatibility are kept
  fputs("xkb_keymap { xkb_keycodes { include \"evdev+aliases(qwerty)\"\n", fp);
  for (i = 0; i < nkeys; i++)
    fprintf(fp, "<Z%03d> = %d;\n", i, 2000 + i);
  fputs("}; xkb_types { include \"complete\" }; xkb_compat { include \"complete\" };"
        " xkb_symbols { include \"pc+us+inet(evdev)\"\n", fp);
  for (t = 0; t < NTYPES; t++)
    fprintf(fp, "key <Z%03u> { type = \"%s\", [ a ] };\n", t, types[t]);
  for (i = 0; i < nnamed; i += 8) {
    fprintf(fp, "key <Z%03d> { type = \"EIGHT_LEVEL\", [ ", (int)NTYPES + i / 8);
    for (int l = 0; l < 8; l++)
      fprintf(fp, "%s0x%x", l ? ", " : "", i + l < nnamed ? named[i + l] : XKB_KEY_NoSymbol);
    fputs(" ] };\n", fp);
  }
  fputs("}; };\n", fp);
  fclose(fp);
  km = xkb_keymap_new_from_string(ctx, buf, XKB_KEYMAP_FORMAT_TEXT_V1, 0);
  free(buf);
  if (!km) {
    fprintf(stderr, "kmap-vendor: the base keymap does not compile\n");
    return NULL;
  }
  buf = xkb_keymap_get_as_string(km, XKB_KEYMAP_FORMAT_TEXT_V1);
  xkb_keymap_unref(km);
  return buf;
}

// the whole pc keyboard as kmap's own keys and modifier maps, and the xkb
// parts every keymap shares: what kmap -x builds a complete keymap from
static int
write_pc(const char *dir, const char *full)
{
  static struct key k[1024];
  char *text = xkb_keymap_get_as_string(us, XKB_KEYMAP_FORMAT_TEXT_V1);
  char path[1024];
  FILE *fp;
  int n;

  snprintf(path, sizeof(path), "%s/xkb", dir);
  if (!text || (mkdir(path, 0755) != 0 && errno != EEXIST))
    return -1;
  if (write_section(dir, text, "xkb_keycodes", "keycodes.xkb") || write_section(dir, full, "xkb_types", "types.xkb") ||
      write_section(dir, full, "xkb_compatibility", "compat.xkb"))
    return -1;
  n = all_keys(us, k, 1024);
  find_types(us, k, n);
  snprintf(path, sizeof(path), "%s/pc.kmap", dir);
  if (!(fp = fopen(path, "w")))
    return -1;
  fputs("kmap 1\nid pc\nname the pc keyboard, its keys and modifiers, under every layout\n", fp);
  put_keys(fp, k, n);
  put_modmaps(fp, us, strstr(text, "xkb_symbols"));
  free(text);
  return fclose(fp);
}

// whether every line of [a, ae) is a line of [b, be)
static int
lines_within(const char *a, const char *ae, const char *b, const char *be)
{
  if (!a || !ae || !b || !be)
    return 0;
  while (a < ae) {
    const char *nl = memchr(a, '\n', (size_t)(ae - a)), *p;
    size_t len = nl ? (size_t)(nl - a) : (size_t)(ae - a);
    int found = 0;

    // the virtual modifiers declared are listed whole, and differ by which
    // a keymap's parts use: not a part of their own
    if (strstr(a, "virtual_modifiers") && strstr(a, "virtual_modifiers") < a + len) {
      a += len + 1;
      continue;
    }

    for (p = b; p < be && !found;) {
      const char *q = memchr(p, '\n', (size_t)(be - p));
      size_t l2 = q ? (size_t)(q - p) : (size_t)(be - p);

      found = l2 == len && !memcmp(p, a, len);
      p = q ? q + 1 : be;
    }
    if (!found)
      return 0;
    a += len + 1;
  }
  return 1;
}

// an xkb option as the keys it changes on the pc keyboard, a kmap laid over
// the layouts. an option that changes the key types or compatibility too is
// left out, said, as kmap writes those as they are
static int
write_option(const char *dir, const char *name, const char *base_text, int dry)
{
  struct xkb_rule_names names = {"evdev", "pc105", "us", NULL, name};
  struct xkb_keymap *km = xkb_keymap_new_from_names(ctx, &names, 0);
  static struct key k[1024];
  char *mine, *base, path[1024], group[64];
  const char *colon = strchr(name, ':');
  int n = 0, rc = 0;
  FILE *fp;

  if (!km || !colon)
    return km ? (xkb_keymap_unref(km), 0) : 0;
  mine = xkb_keymap_get_as_string(km, XKB_KEYMAP_FORMAT_TEXT_V1);
  base = strdup(base_text ? base_text : "");
  // the first pass only learns the keysyms the option's keys carry
  if (dry) {
    for (xkb_keycode_t kc = xkb_keymap_min_keycode(km); kc <= xkb_keymap_max_keycode(km); kc++) {
      const xkb_keysym_t *syms;

      if (!xkb_keymap_key_get_name(km, kc) || !differs(km, kc))
        continue;
      for (int l = 0; l < 8; l++)
        if (xkb_keymap_key_get_syms_by_level(km, kc, 0, l, &syms) >= 1 && !xkb_keysym_to_utf32(syms[0]))
          add_named(syms[0]);
    }
    goto done;
  }
  // xkb writes only the compatibility its keys use, so an option that takes
  // a key away has less of it: what matters is that it adds none
  if (!lines_within(strstr(mine, "xkb_types"), strstr(mine, "xkb_symbols"), strstr(base, "xkb_types"),
                    strstr(base, "xkb_symbols"))) {
    fprintf(stderr, "kmap-vendor: option %s changes more than keys, left out\n", name);
    goto done;
  }
  for (xkb_keycode_t kc = xkb_keymap_min_keycode(km); kc <= xkb_keymap_max_keycode(km) && n < 1024; kc++) {
    const char *kn = xkb_keymap_key_get_name(km, kc);

    if (kn && differs(km, kc))
      n += collect(km, kc, kn, &k[n]);
  }
  if (!n)
    goto done;
  find_types(km, k, n);
  snprintf(group, sizeof(group), "%.*s", (int)(colon - name), name);
  snprintf(path, sizeof(path), "%s/options/%s", dir, group);
  if (mkdir(path, 0755) != 0 && errno != EEXIST)
    goto done;
  snprintf(path, sizeof(path), "%s/options/%s/%s.kmap", dir, group, colon + 1);
  if (!(fp = fopen(path, "w")))
    goto done;
  fprintf(fp, "kmap 1\nid %s\n", name);
  put_keys(fp, k, n);
  fclose(fp);
  rc = 1;
done:
  free(mine);
  free(base);
  xkb_keymap_unref(km);
  return rc;
}

int
main(int argc, char *argv[])
{
  struct rxkb_context *rx;
  struct rxkb_layout *lay;
  int n = 0, bad = 0;

  if (argc != 2) {
    fprintf(stderr, "usage: kmap-vendor dir\n");
    return 1;
  }
  if (mkdir(argv[1], 0755) != 0 && errno != EEXIST) {
    perror(argv[1]);
    return 1;
  }
  rx = rxkb_context_new(RXKB_CONTEXT_NO_FLAGS);
  ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  {
    struct xkb_rule_names un = {"evdev", "pc105", "us", NULL, NULL};

    us = ctx ? xkb_keymap_new_from_names(ctx, &un, 0) : NULL;
  }
  if (!rx || !ctx || !rxkb_context_parse_default_ruleset(rx)) {
    fprintf(stderr, "kmap-vendor: no xkb data\n");
    return 1;
  }
  for (lay = rxkb_layout_first(rx); lay; lay = rxkb_layout_next(lay)) {
    if (write_layout(argv[1], rxkb_layout_get_name(lay), rxkb_layout_get_variant(lay),
                     rxkb_layout_get_description(lay)) == 0)
      n++;
    else
      bad++;
  }
  fprintf(stderr, "kmap-vendor: %d layouts, %d left out\n", n, bad);
  {
    char path[1024], *full;
    int nopt = 0;

    // the options' keysyms join the layouts' before the base is made,
    // then each option is checked against that base and written
    for (struct rxkb_option_group *g = rxkb_option_group_first(rx); g; g = rxkb_option_group_next(g))
      for (struct rxkb_option *o = rxkb_option_first(g); o; o = rxkb_option_next(o))
        write_option(argv[1], rxkb_option_get_name(o), NULL, 1);
    if (!(full = full_base()) || write_pc(argv[1], full) != 0) {
      fprintf(stderr, "kmap-vendor: could not write the pc keyboard\n");
      return 1;
    }
    snprintf(path, sizeof(path), "%s/options", argv[1]);
    mkdir(path, 0755);
    for (struct rxkb_option_group *g = rxkb_option_group_first(rx); g; g = rxkb_option_group_next(g))
      for (struct rxkb_option *o = rxkb_option_first(g); o; o = rxkb_option_next(o))
        nopt += write_option(argv[1], rxkb_option_get_name(o), full, 0);
    fprintf(stderr, "kmap-vendor: %d options that change keys\n", nopt);
    free(full);
  }
  return 0;
}
