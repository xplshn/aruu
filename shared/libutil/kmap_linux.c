/* see LICENSE file for copyright and license details */
#include "../kmap.h"
#include "../paths.h"
#include "../util.h"

/* the linux console: a key's meaning in one table per held modifier set
 * (KDSKBENT), and an accent table for the dead keys (KDSKBDIACRUC) */
#if defined(__linux__) && !defined(ARUU_FORCE_POSIX)
#include <sys/ioctl.h>

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

// linux/kd.h and linux/keyboard.h, which a libc need not carry
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

// a dead key's console value: the kernel's own six, then the rest as dead2,
// named in the accent table by a latin-1 character, as only those fit
static const struct {
  long     comb; // what kmap_dead gives
  int      type, value;
  unsigned diacr;
} deads[] = {
  {0x300, KT_DEAD, 0, '`'},      {0x301, KT_DEAD, 1, '\''},     {0x302, KT_DEAD, 2, '^'},
  {0x303, KT_DEAD, 3, '~'},      {0x308, KT_DEAD, 4, '"'},      {0x327, KT_DEAD, 5, ','},
  {0x304, KT_DEAD2, 0xaf, 0xaf}, {0x30a, KT_DEAD2, 0xb0, 0xb0}, {0x30c, KT_DEAD2, 0xa6, 0xa6},
  {0x306, KT_DEAD2, 0xa4, 0xa4}, {0x307, KT_DEAD2, 0xb7, 0xb7}, {0x30b, KT_DEAD2, 0xa8, 0xa8},
  {0x328, KT_DEAD2, 0xb8, 0xb8}, {0x323, KT_DEAD2, 0xa7, 0xa7}, {0x309, KT_DEAD2, 0xbf, 0xbf},
  {0x31b, KT_DEAD2, 0xb9, 0xb9}, {0x335, KT_DEAD2, 0xac, 0xac}, {0x326, KT_DEAD2, 0xb5, 0xb5},
  {0x331, KT_DEAD2, 0xb3, 0xb3}, {0x345, KT_DEAD2, 0xbd, 0xbd},
};

static char *ttys[] = {
  ARUU_LINUX_PATH_PROC "/self/fd/0",
  ARUU_PATH_DEVCONSOLE,
  ARUU_PATH_DEVTTY,
  "/dev/tty0",
};

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
  weprintf("no console to set the keyboard of\n");
  return -1;
}

static int
dead_of(const char *s)
{
  long c = kmap_dead(s);
  size_t i;

  for (i = 0; c >= 0 && i < LEN(deads); i++)
    if (deads[i].comb == c)
      return (int)i;
  return -1;
}

// latin-1 as itself (a letter when caps shifts it), the rest of unicode as
// the kernel takes it, dead keys and the few names the console means
// something by, else a hole
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
  if ((c = kmap_unicode(s)) < 0 || c >= 0xf000)
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

int
kmap_load_console(const struct kmap *k)
{
  // the console has four of a key's levels
  static const int tables[4] = {0, T_SHIFT, T_ALTGR, T_SHIFT | T_ALTGR};
  struct kbdiacrsuc acc;
  int fd = console(), rc = 0, l, code;
  size_t i, j;

  if (fd < 0)
    return -1;
  for (i = 0; i < k->nkeys; i++) {
    const struct kmap_key *key = &k->keys[i];
    long c0;

    if ((code = kmap_code(key->name, KMAP_EVDEV)) < 0)
      continue;
    // the keypad's decimal key keeps the console's own, which num lock turns
    // between delete and its character: only which character differs
    if (!strcmp(key->name, "KPDL")) {
      for (l = 0; l < key->nsym; l++)
        if (!strcmp(key->sym[l], ","))
          rc |= set(fd, 0, code, K_PCOMMA);
      continue;
    }
    for (l = 0; l < 4; l++)
      rc |= set(fd, tables[l], code, l < key->nsym ? value_of(key->sym[l], key->caps) : K_HOLE);
    // control and alt follow the letter the key types, so ctrl+a is ^A on
    // any latin layout. on another script they stay where the console had them
    c0 = key->nsym ? kmap_unicode(key->sym[0]) : -1;
    if (c0 >= 'a' && c0 <= 'z') {
      rc |= set(fd, T_CTRL, code, K(KT_LATIN, c0 & 0x1f));
      rc |= set(fd, T_CTRL | T_SHIFT, code, K(KT_LATIN, c0 & 0x1f));
    }
    if (c0 > 0x20 && c0 < 0x7f)
      rc |= set(fd, T_ALT, code, K(KT_META, c0));
  }

  // the accent table: what this layout's dead keys make. the kernel takes
  // one fewer than its table holds
  memset(&acc, 0, sizeof(acc));
  for (j = 0; j < k->ncompose && acc.cnt < LEN(acc.d) - 1; j++) {
    const struct kmap_compose *cp = &k->compose[j];
    long base = kmap_unicode(cp->base), res = kmap_unicode(cp->result);
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
#endif
