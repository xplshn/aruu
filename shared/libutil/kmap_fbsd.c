/* see LICENSE file for copyright and license details */
#include "../kmap.h"
#include "../util.h"

/* syscons and vt, freebsd's consoles: the keyboard's map
 * read (GIO_KEYMAP), the layout's keys put in it and written back, and an
 * accent map made of the layout's dead keys (PIO_DEADKEYMAP). a key has
 * eight states, plain, shift, ctrl, ctrl shift, then the same with alt,
 * which is where these consoles keep level 3 and 4, right alt reaching them */
#if defined(__FreeBSD__) && !defined(ARUU_FORCE_POSIX)
#include <sys/ioctl.h>
#include <sys/kbio.h>
#include <sys/types.h>

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// the states the four levels go in, and those control and alt get
#define S_PLAIN      0
#define S_SHIFT      1
#define S_CTRL       2
#define S_CTRL_SHIFT 3
#define S_ALT        4
#define S_ALT_SHIFT  5

// right alt in these consoles' numbering
#define KC_RALT 93

static const char *ttys[] = {"/dev/ttyv0", "/dev/console"};

// the console's accent keys and the combining characters kmap names them by,
// with the spacing character the accent map gives each
static const struct {
  long     comb;
  u_int    key;
  u_int    spacing;
} accs[] = {
  {0x300, DGRA, '`'},  {0x301, DACU, 0xb4}, {0x302, DCIR, '^'},  {0x303, DTIL, '~'},
  {0x304, DMAC, 0xaf}, {0x306, DBRE, 0x2d8}, {0x307, DDOT, 0x2d9}, {0x308, DDIA, 0xa8},
  {0x335, DSLA, '/'},  {0x30a, DRIN, 0xb0}, {0x327, DCED, 0xb8}, {0x30b, DDAC, 0x2dd},
  {0x328, DOGO, 0x2db}, {0x30c, DCAR, 0x2c7},
};

static int
acc_of(const char *s)
{
  long c = kmap_dead(s);
  size_t i;

  for (i = 0; c >= 0 && i < LEN(accs); i++)
    if (accs[i].comb == c)
      return (int)i;
  return -1;
}

// a state of a key: a character, or a special key (a dead key, alt gr)
static void
put(struct keyent_t *e, int state, const char *s)
{
  long c;
  int a;

  e->spcl &= ~(0x80 >> state);
  if ((a = acc_of(s)) >= 0) {
    e->map[state] = accs[a].key;
    e->spcl |= 0x80 >> state;
  } else if (!strcmp(s, "ISO_Level3_Shift")) {
    e->map[state] = RALT;
    e->spcl |= 0x80 >> state;
  } else if ((c = kmap_unicode(s)) >= 0)
    e->map[state] = (u_int)c;
  else {
    e->map[state] = NOP;
    e->spcl |= 0x80 >> state;
  }
}

int
kmap_load_console(const struct kmap *k)
{
  static const int states[4] = {S_PLAIN, S_SHIFT, S_ALT, S_ALT_SHIFT};
  keymap_t *km = ecalloc(1, sizeof(*km));
  accentmap_t *am = ecalloc(1, sizeof(*am));
  size_t i, j;
  int fd = -1, code, l, rc = -1;

  if (ioctl(0, GIO_KEYMAP, km) == 0)
    fd = 0;
  for (i = 0; i < LEN(ttys) && fd < 0; i++)
    if ((fd = open(ttys[i], O_RDWR)) >= 0 && ioctl(fd, GIO_KEYMAP, km) != 0) {
      close(fd);
      fd = -1;
    }
  if (fd < 0) {
    weprintf("no console to set the keyboard of\n");
    goto done;
  }
  for (i = 0; i < k->nkeys; i++) {
    const struct kmap_key *key = &k->keys[i];
    struct keyent_t *e;
    long c0;

    if ((code = kmap_code(key->name, KMAP_PC)) < 0 || code >= km->n_keys)
      continue;
    e = &km->key[code];
    for (l = 0; l < 4; l++)
      put(e, states[l], l < key->nsym ? key->sym[l] : (l & 1 ? key->sym[l - 1] : "none"));
    e->flgs = key->caps ? FLAG_LOCK_C : 0;
    // control follows the letter the key types, so ctrl+a is ^A on any latin
    // layout. on another script it stays where the console had it
    c0 = key->nsym ? kmap_unicode(key->sym[0]) : -1;
    if (c0 >= 'a' && c0 <= 'z') {
      e->map[S_CTRL] = e->map[S_CTRL_SHIFT] = (u_int)(c0 & 0x1f);
      e->spcl &= ~((0x80 >> S_CTRL) | (0x80 >> S_CTRL_SHIFT));
    }
  }
  if (k->level3_ralt && KC_RALT < km->n_keys) {
    for (l = 0; l < NUM_STATES; l++)
      km->key[KC_RALT].map[l] = RALT;
    km->key[KC_RALT].spcl = 0xff;
    km->key[KC_RALT].flgs = 0;
  }

  // the accent map: for each dead key of the layout, what it makes
  memset(am, 0, sizeof(*am));
  am->n_accs = NUM_DEADKEYS;
  for (j = 0; j < k->ncompose; j++) {
    const struct kmap_compose *cp = &k->compose[j];
    long base = kmap_unicode(cp->base), res = kmap_unicode(cp->result);
    int a = acc_of(cp->dead), n;
    struct acc_t *acc;

    if (a < 0 || base < 0 || res < 0)
      continue;
    acc = &am->acc[accs[a].key - F_ACC];
    acc->accchar = accs[a].spacing;
    for (n = 0; n < NUM_ACCENTCHARS && acc->map[n][0]; n++)
      ;
    if (n < NUM_ACCENTCHARS) {
      acc->map[n][0] = (u_int)base;
      acc->map[n][1] = (u_int)res;
    }
  }

  rc = 0;
  if (ioctl(fd, PIO_KEYMAP, km) != 0) {
    weprintf("PIO_KEYMAP:");
    rc = -1;
  }
  if (ioctl(fd, PIO_DEADKEYMAP, am) != 0) {
    weprintf("PIO_DEADKEYMAP:");
    rc = -1;
  }
  if (fd > 0)
    close(fd);
done:
  free(km);
  free(am);
  return rc;
}
#endif
