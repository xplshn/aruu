/* Copyright (c) 1985 Ceriel J.H. Jacobs */

/*
 * Handle output to screen
 */

#include "output.h"
#include "in_all.h"
#include "main.h"

int   _ocnt;
char *_optr;

#define OBUFSIZ 64 * 128

static char _outbuf[OBUFSIZ];

void
flush()
{ /* Flush output buffer, by writing it */
  char *p = _outbuf;

  _ocnt = OBUFSIZ;
  if (_optr)
    (void)write(1, p, _optr - p);
  _optr = p;
}

void
nflush()
{ /* Flush output buffer, ignoring it */

  _ocnt = OBUFSIZ;
  _optr = _outbuf;
}

int
fputch(int ch)
{ /* print a character */
  putch(ch);
  return ch;
}

void
putline(char *s)
{ /* Print string s */

  if (!s)
    return;
  while (*s) {
    putch(*s++);
  }
}

/*
 * A safe version of putline. All control characters are echoed as ^X
 */

void
cputline(char *s)
{
  int c;

  while ((c = *s++) != 0) {
    if ((unsigned char)c < ' ' || (unsigned char)c == 0x7f) {
      putch('^');
      c ^= 0x40;
    }
    putch(c);
  }
}

/*
 * Simple minded routine to print a number
 */

void
prnum(long n)
{
  putline(getnum(n));
}

static char *
fillnum(long n, char *p)
{
  if (n >= 10) {
    p = fillnum(n / 10, p);
  }
  *p++ = (int)(n % 10) + '0';
  *p   = '\0';
  return p;
}

char *
getnum(long n)
{
  static char buf[20];

  fillnum(n, buf);
  return buf;
}
