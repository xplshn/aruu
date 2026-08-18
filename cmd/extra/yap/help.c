/* copyright (c) 1985 ceriel J.H. jacobs */

#include "help.h"
#include "commands.h"
#include "display.h"
#include "in_all.h"
#include "keys.h"
#include "machine.h"
#include "main.h"
#include "options.h"
#include "output.h"
#include "prompt.h"
#include "term.h"

static int           h_cnt;  /* count # of lines */
static struct state *origin; /* keep track of startstate */

/*
 * print a key sequence
 * we arrived at an endstate. the s_next link in the state structure now
 * leads us from "origin" to the current state, so that we can print the key
 * sequence easily
 */

static void
pr_comm()
{
  struct state *p = origin;
  char         *pb;
  int           c;
  char          buf[30];
  int           i = 0; /* how many characters printed? */

  pb = buf;
  for (;;) {
    c = p->s_char & 0177;
    if (c < ' ' || c == 0177) {
      /*
 * will take an extra position
 */
      i++;
    }
    *pb++ = c;
    i++;
    if (!p->s_match)
      break;
    p = p->s_next;
  }
  do {
    *pb++ = ' ';
  } while (++i < 12);
  *pb = 0;
  cputline(buf);
}

/*
 * print out a description of the keymap. this is done, by temporarily using
 * the s_next field in the state structure indicate the state matching the
 * next character, so that we can walk from "origin" to an endstate
 */

static void
pr_mach(struct state *currstate, struct state *back)
{
  struct state *save;

  while (currstate) {
    if (interrupt)
      break;
    if (back) {
      save         = back->s_next; /* save original link */
      back->s_next = currstate;
    }
    if (!currstate->s_match) {
      /*
 * end state, print command
 */
      pr_comm();
      putline(commands[currstate->s_cnt].c_descr);
      putline("\r\n");
      if (++h_cnt >= maxpagesize) {
        ret_to_continue();
        h_cnt = 0;
      }
    } else
      pr_mach(currstate->s_match, currstate);
    currstate = currstate->s_next;
    if (back)
      back->s_next = save; /* restore */
    else
      origin = currstate;
  }
}

/* ARGSUSED */
int
do_help(long i)
{ /* the help command */
  (void)i;

  startcomm = 0;
  h_cnt     = 2;
  putline("\r\nSummary of yap commands:\r\n");
  origin = currmap->k_mach;
  pr_mach(currmap->k_mach, (struct state *)0);
  if (h_cnt) {
    ret_to_continue();
  }
  if (!hardcopy && scr_info.currentpos)
    redraw(1);
  return 0;
}
