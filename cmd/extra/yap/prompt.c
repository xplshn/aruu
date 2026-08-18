/* copyright (c) 1985 ceriel J.H. jacobs */

#include "prompt.h"
#include "assert.h"
#include "commands.h"
#include "display.h"
#include "getcomm.h"
#include "getline.h"
#include "in_all.h"
#include "keys.h"
#include "machine.h"
#include "main.h"
#include "options.h"
#include "output.h"
#include "process.h"
#include "term.h"

static char *errorgiven; /* set to error message, if there is one */

static char *display_basename(char *path, char *scratch, size_t scratch_size);

char *
copy(char *p, const char *ep, char *s)
{
  while (p < ep && *s) {
    *p++ = *s++;
  }
  return p;
}

/*
 * display the prompt and refresh the screen
 */

void
give_prompt()
{
  char           **name;
  struct scr_info *p = &scr_info;
  char             buf[256];
  char             filebuf[256];
  char            *pb = buf;

  if (startcomm)
    return;
  flush();
  if (window()) {
    redraw(0);
    flush();
  }
  if (!stupid) {
    /*
 * fancy prompt
 */
    clrbline();
    standout();
    pb = copy(pb, &buf[255], display_basename(currentfile, filebuf, sizeof(filebuf)));
    if (stdf >= 0) {
      pb = copy(pb, &buf[255], ", ");
      pb = copy(pb, &buf[255], getnum(p->firstline));
      pb = copy(pb, &buf[255], "-");
      pb = copy(pb, &buf[255], getnum(p->lastline));
    }
  } else {
    *pb++ = '\007'; /* stupid terminal, stupid prompt */
  }
  if (errorgiven) {
    /*
 * display error message
 */
    pb = copy(pb, &buf[255], " ");
    pb = copy(pb, &buf[255], errorgiven);
    if (stupid) {
      pb = copy(pb, &buf[255], "\r\n");
    }
    errorgiven = 0;
  } else if (!stupid && (status || maxpos)) {
    pb   = copy(pb, &buf[255], " (");
    name = &filenames[filecount];
    if (status) {
      /*
 * indicate top and/or bottom
 */
      if (status & START) {
        if (!*(name - 1)) {
          pb = copy(pb, &buf[255], "Top");
        } else {
          pb = copy(pb, &buf[255], "Previous: ");
          pb = copy(pb, &buf[255], display_basename(*(name - 1), filebuf, sizeof(filebuf)));
        }
        if (status & EOFILE) {
          pb = copy(pb, &buf[255], ", ");
        }
      }
      if (status & EOFILE) {
        if (!*(name + 1)) {
          pb = copy(pb, &buf[255], "Bottom");
        } else {
          pb = copy(pb, &buf[255], "Next: ");
          pb = copy(pb, &buf[255], display_basename(*(name + 1), filebuf, sizeof(filebuf)));
        }
      }
    } else { /* display percentage */
      pb = copy(pb, &buf[255], getnum((100 * getpos(p->lastline)) / maxpos));
      pb = copy(pb, &buf[255], "%");
    }
    pb = copy(pb, &buf[255], ")");
  }
  *pb = '\0';
  if (!stupid) {
    buf[COLS - 1] = 0;
    putline(buf);
    standend();
  } else
    putline(buf);
}

static char *
display_basename(char *path, char *scratch, size_t scratch_size)
{
  char *base;

  if (!path || scratch_size == 0) {
    return "";
  }
  (void)strncpy(scratch, path, scratch_size - 1);
  scratch[scratch_size - 1] = '\0';
  base                      = basename(scratch);
  return base ? base : scratch;
}

/*
 * remember error message
 */

void
error(char *str)
{
  errorgiven = str;
}

void
ret_to_continue()
{ /* obvious */
  int         c;
  static char buf[2];

  for (;;) {
    clrbline();
    standout();
    if (errorgiven) {
      putline(errorgiven);
      putline(" ");
      errorgiven = 0;
    }
    putline("[Type anything to continue]");
    standend();
    if (is_escape(c = getch())) {
      buf[0] = c;
      (void)match(buf, &c, currmap->k_mach);
      assert(c > 0);
      do_comm(c, -1L);
    } else
      break;
  }
  clrbline();
}
