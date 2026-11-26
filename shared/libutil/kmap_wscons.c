/* see LICENSE file for copyright and license details */
#include "../kmap.h"
#include "../util.h"

/* wscons, netbsd's and openbsd's console: the keyboard's whole map read
 * (WSKBDIO_GETMAP), the layout's keys put in it, and written back. a
 * keysym is 16 bit unicode and a dead key the combining character it puts
 * on the next, as kmap_dead gives it; wscons composes them itself. group1
 * holds levels 1 and 2, group2 levels 3 and 4, reached with Mode_switch */
#if (defined(__NetBSD__) || defined(__OpenBSD__)) && !defined(ARUU_FORCE_POSIX)
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>

#include <dev/wscons/wsconsio.h>
#include <dev/wscons/wsksymdef.h>
#include <dev/wscons/wsksymvar.h>

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *kbds[] = {"/dev/wskbd", "/dev/wskbd0"};

// right alt, where a layout's level 3 is reached from: its pc scancode
// (0x38 behind e0, wscons numbers it 0xb8) and its hid usage
#define PC_RALT  0xb8
#define HID_RALT 0xe6

static keysym_t
keysym_of(const char *s)
{
  long c;

  if ((c = kmap_dead(s)) >= 0)
    return (keysym_t)c;
  if (!strcmp(s, "ISO_Level3_Shift"))
    return KS_Mode_switch;
  if (!strcmp(s, "Multi_key"))
    return KS_Multi_key;
  if (!strcmp(s, "Return"))
    return KS_Return;
  if ((c = kmap_unicode(s)) < 0 || c >= 0xf000)
    return KS_voidSymbol;
  return (keysym_t)c;
}

int
kmap_load_console(const struct kmap *k)
{
  struct wskbd_map_data md;
  struct wscons_keymap *map;
  enum kmap_codes scheme;
  u_int type;
  size_t i;
  int fd = -1, code, l, rc = 0;

  for (i = 0; i < LEN(kbds) && fd < 0; i++)
    fd = open(kbds[i], O_RDWR);
  if (fd < 0) {
    weprintf("open %s:", kbds[LEN(kbds) - 1]);
    return -1;
  }
  // a usb keyboard's keys are numbered by hid usage, a pc keyboard's by
  // its scancodes
  scheme = ioctl(fd, WSKBDIO_GTYPE, &type) == 0 && type == WSKBD_TYPE_USB ? KMAP_HID : KMAP_PC;
  map = ecalloc(KS_NUMKEYCODES, sizeof(*map));
  md.maplen = KS_NUMKEYCODES;
  md.map = map;
  if (ioctl(fd, WSKBDIO_GETMAP, &md) != 0) {
    weprintf("WSKBDIO_GETMAP:");
    goto fail;
  }
  for (i = 0; i < k->nkeys; i++) {
    const struct kmap_key *key = &k->keys[i];
    keysym_t s[4];

    if ((code = kmap_code(key->name, scheme)) < 0 || (u_int)code >= md.maplen)
      continue;
    for (l = 0; l < 4; l++)
      s[l] = l < key->nsym ? keysym_of(key->sym[l]) : KS_voidSymbol;
    // a key with one level types it shifted too, as wscons keymaps write it
    if (s[1] == KS_voidSymbol)
      s[1] = s[0];
    map[code].group1[0] = s[0];
    map[code].group1[1] = s[1];
    map[code].group2[0] = s[2];
    map[code].group2[1] = s[3] == KS_voidSymbol ? s[2] : s[3];
  }
  if (k->level3_ralt) {
    code = scheme == KMAP_HID ? HID_RALT : PC_RALT;
    if ((u_int)code < md.maplen) {
      map[code].command = KS_voidSymbol;
      map[code].group1[0] = map[code].group1[1] = KS_Mode_switch;
      map[code].group2[0] = map[code].group2[1] = KS_Mode_switch;
    }
  }
  if (ioctl(fd, WSKBDIO_SETMAP, &md) != 0) {
    weprintf("WSKBDIO_SETMAP:");
    goto fail;
  }
  free(map);
  close(fd);
  return rc;
fail:
  free(map);
  close(fd);
  return -1;
}
#endif
