/* see LICENSE file for copyright and license details */
#ifndef KMAP_H
#define KMAP_H

#include <stddef.h>
#include <stdio.h>

/*
 * kmap: a keyboard layout as portable text, one file a layout or variant.
 * lines are a key and words, rc quoted ('...' one word, '' a quote inside it),
 * # starts a comment:
 *
 *   kmap 1
 *   id de-nodeadkeys
 *   layout de
 *   variant nodeadkeys
 *   name German (no dead keys)
 *   level3 ralt
 *   key AD01 q Q @ Ω caps
 *   key SPCE space
 *
 * a key is named by its xkb position (AE01 the key under 1, AD01 under q on a
 * us board, TLDE, BKSL, LSGT, SPCE, KP7), which no system owns. its symbols
 * are its levels: base, shift, level 3 (altgr), level 3 with shift, and to
 * eight for the layouts that reach level 5. type=NAME, on the keys that
 * need it, is the xkb type that says which modifiers reach which level (a
 * keypad's second level is num lock's: type=KEYPAD); without it the levels
 * are xkb's own choice for them. a symbol
 * is the one character it types, or a keysym name for what is not one
 * (space, dead_acute, KP_Delete), none for nothing. caps marks a key caps
 * lock shifts. level3 ralt says right alt reaches level 3.
 *
 * compose.kmap, beside the layouts, says what a dead key makes:
 *
 *   compose dead_acute e é
 *
 * the layouts live in KMAP_DIR/<layout>/<variant>.kmap, the base layout in
 * KMAP_DIR/<layout>/<layout>.kmap
 */

#define KMAP_DIR    "/share/kmap"
#define KMAP_LEVELS 8
#define KMAP_SYM    32

struct kmap_key {
  char name[8];
  char sym[KMAP_LEVELS][KMAP_SYM];
  int  nsym;
  int  caps;
  char type[48]; // the xkb type, "" when xkb's own choice is the layout's
};

struct kmap_compose {
  char dead[KMAP_SYM];
  char base[KMAP_SYM];
  char result[KMAP_SYM];
};

struct kmap {
  char id[64], layout[32], variant[64], name[256];
  int  level3_ralt;
  struct kmap_key     *keys;
  size_t               nkeys;
  struct kmap_compose *compose;
  size_t               ncompose;
};

/* path of layout's variant (NULL or "" for the base layout) under dir */
void kmap_path(char *buf, size_t sz, const char *dir, const char *layout, const char *variant);

/* reads path into k, adding to what k has: a layout, then compose.kmap, whose
   own id and name do not replace the layout's. k starts zeroed. 0, or -1
   with the reason said */
int kmap_read(const char *path, struct kmap *k);
void kmap_free(struct kmap *k);

/* the unicode character sym is, -1 when it is a name */
long kmap_char(const char *sym);

/* the words of line, rc unquoted in place, into w (max of them). their number */
int kmap_words(char *line, char **w, int max);

/* the ways systems number keys: evdev codes (linux), pc scancodes of set 1
   (wscons for pc keyboards, syscons and vt), and usb hid usages (wscons for
   usb keyboards) */
enum kmap_codes {
  KMAP_EVDEV,
  KMAP_PC,
  KMAP_HID,
};

/* where a key is in one of those numberings, -1 for a key kmap does not
   place there */
int kmap_code(const char *name, enum kmap_codes scheme);

/* the unicode character a symbol types: a character, a named one (space,
   nobreakspace, combining_acute), or Uxxxx. -1 for anything else */
long kmap_unicode(const char *sym);

/* the combining character a dead key puts on the next one (U+0301 for
   dead_acute), -1 when sym is no dead key */
long kmap_dead(const char *sym);

/* the console's keyboard set to k, through the system's own interface: the
   linux console (kmap_linux.c), wscons on netbsd and openbsd
   (kmap_wscons.c), syscons and vt on freebsd (kmap_fbsd.c). 0, or -1 with
   the reason said, as on a system kmap has no backend for (kmap_posix.c) */
int kmap_load_console(const struct kmap *k);

/* the n layouts of maps as one xkb keymap, a group each, the keys they do
   not name from the pc layout. options are xkb's (caps:escape,
   grp:alt_shift_toggle), looked up in rules (xkeyboard-config's
   rules/evdev); either may be NULL */
int kmap_write_xkb(FILE *fp, const struct kmap *maps, int n, const char *options,
                   const char *rules);

#endif
