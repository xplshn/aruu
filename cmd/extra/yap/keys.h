#ifndef KEYS_H
#define KEYS_H

extern struct keymap {
  char          k_help[80];
  struct state *k_mach;
  char          k_esc[10];
} *currmap, *othermap;

void initkeys(void);
void setused(int key);
int  isused(int key);
int  is_escape(int c);

#endif
