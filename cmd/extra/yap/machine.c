/* copyright (c) 1985 ceriel J.H. jacobs */

#include "machine.h"
#include "assert.h"
#include "getline.h"
#include "in_all.h"
#include <ctype.h>

/*
 * add part of finite state machine to recognize the string s
 */

static int
addtomach(char *s, int cnt, struct state **list)
{
  struct state *l;
  int           i = FSM_OKE; /* return value */
  int           j;

  for (;;) {
    l = *list;
    if (!l) {
      /*
 * create new list element
 */
      *list = l     = (struct state *)alloc(sizeof(*l));
      l->s_char     = *s;
      l->s_endstate = 0;
      l->s_match    = 0;
      l->s_next     = 0;
    }
    if (l->s_char == *s) {
      /*
 * continue with next character
 */
      if (!*++s) {
        /*
 * no next character
 */
        j             = l->s_endstate;
        l->s_endstate = 1;
        if (l->s_match || j) {
          /*
 * if the state already was an endstate,
 * or has a successor, the currently
 * added string is a prefix of an
 * already recognized string
 */
          return FSM_ISPREFIX;
        }
        l->s_cnt = cnt;
        return i;
      }
      if (l->s_endstate) {
        /*
 * in this case, the currently added string has
 * a prefix that is an already recognized
 * string
 */
        i = FSM_HASPREFIX;
      }
      list = &(l->s_match);
      continue;
    }
    list = &(l->s_next);
  }
  /* NOTREACHED */
}

/*
 * add a string to the FSM
 */

int
addstring(char *s, int cnt, struct state **machine)
{
  if (!s || !*s) {
    return FSM_ISPREFIX;
  }
  return addtomach(s, cnt, machine);
}

/*
 * match string s with the finite state machine
 * if it matches, the number of characters actually matched is returned,
 * and the count is put in the word pointed to by i
 * if the string is a prefix of a string that could be matched,
 * FSM_ISPREFIX is returned. otherwise, 0 is returned
 */

int
match(char *s, int *i, struct state *mach)
{
  char         *s1    = s; /* walk through string */
  struct state *mach1 = 0;
  /* keep track of previous state */

  while (mach && *s1) {
    if (mach->s_char == *s1) {
      /*
 * current character matches. carry on with next
 * character and next state
 */
      mach1 = mach;
      mach  = mach->s_match;
      s1++;
      continue;
    }
    mach = mach->s_next;
  }
  if (!mach1) {
    /*
 * no characters matched
 */
    return 0;
  }
  if (mach1->s_endstate) {
    /*
 * the string matched
 */
    *i = mach1->s_cnt;
    return s1 - s;
  }
  if (!*s1) {
    /*
 * the string matched a prefix
 */
    return FSM_ISPREFIX;
  }
  return 0;
}
