/* copyright (c) 1985 ceriel J.H. jacobs */

/*
 * command reader, also executes shell escapes
 */

#include "getcomm.h"
#include "assert.h"
#include "commands.h"
#include "display.h"
#include "getline.h"
#include "in_all.h"
#include "keys.h"
#include "machine.h"
#include "main.h"
#include "output.h"
#include "process.h"
#include "prompt.h"
#include "term.h"
#include <ctype.h>

static int  killchar(int c);
static void sigquit(int signo);

/*
 * read a line from the terminal, doing line editing
 * the parameter s contains the prompt for the line
 */

char *
readline(char *s)
{
  static char buf[80];
  char       *p = buf;
  int         ch;
  int         pos;

  clrbline();
  putline(s);
  pos = strlen(s);
  while ((ch = getch()) != '\n' && ch != '\r') {
    if (ch == -1) {
      /*
 * can only occur because of an interrupted read
 */
      ch        = erasech;
      interrupt = 0;
    }
    if (ch == erasech) {
      /*
 * erase last char
 */
      if (p == buf) {
        /*
 * there was none, so return
 */
        return (char *)0;
      }
      pos -= killchar(*--p);
      if (*p != '\\')
        continue;
    }
    if (ch == killch) {
      /*
 * erase the whole line
 */
      if (!(p > buf && *(p - 1) == '\\')) {
        while (p > buf) {
          pos -= killchar(*--p);
        }
        continue;
      }
      pos -= killchar(*--p);
    }
    if (p > &buf[78] || pos >= COLS - 2) {
      /*
 * line does not fit
 * simply refuse to make it any longer
 */
      pos -= killchar(*--p);
    }
    *p++ = ch;
    if (ch < ' ' || ch >= 0177) {
      fputch('^');
      pos++;
      ch ^= 0100;
    }
    fputch(ch);
    pos++;
  }
  fputch('\r');
  *p++ = '\0';
  flush();
  return buf;
}

/*
 * erase a character from the command line
 */

static int
killchar(int c)
{
  backspace();
  putch(' ');
  backspace();
  if (c < ' ' || c >= 0177) {
    (void)killchar(' ');
    return 2;
  }
  return 1;
}

/*
 * do a shell escape, after expanding '%' and '!'
 */

void
shellescape(char *p, int esc_char)
{
  char *p2;        /* walks through command */
  int   id;        /* procid of child */
  int   cnt;       /* prevent array bound errors */
  int   lastc = 0; /* will contain the previous char */
#ifdef SIGTSTP
  void (*savetstp)(int);
#endif
  static char previous[256]; /* previous command */
  char        comm[256];     /* space for command */
  int         piped[2];

  p2    = comm;
  *p2++ = esc_char;
  cnt   = 253;
  while (*p) {
    /*
 * expand command
 */
    switch (*p++) {
      case '!':
        /*
 * an unescaped ! expands to the previous
 * command, but disappears if there is none
 */
        if (lastc != '\\') {
          if (*previous) {
            id = strlen(previous);
            if ((cnt -= id) <= 0)
              break;
            (void)strcpy(p2, previous);
            p2 += id;
          }
        } else {
          *(p2 - 1) = '!';
        }
        continue;
      case '%':
        /*
 * an unescaped % will expand to the current
 * filename, but disappears is there is none
 */
        if (lastc != '\\') {
          if (nopipe) {
            id = strlen(currentfile);
            if ((cnt -= id) <= 0)
              break;
            (void)strcpy(p2, currentfile);
            p2 += id;
          }
        } else {
          *(p2 - 1) = '%';
        }
        continue;
      default:
        lastc = *(p - 1);
        if (cnt-- <= 0)
          break;
        *p2++ = lastc;
        continue;
    }
    break;
  }
  clrbline();
  *p2 = '\0';
  if (!stupid) {
    /*
 * display expanded command
 */
    cputline(comm);
    putline("\r\n");
  }
  flush();
  (void)strcpy(previous, comm + 1);
  resettty();
  if (esc_char == '|' && pipe(piped) < 0) {
    error("Cannot create pipe");
    return;
  }
  if ((id = fork()) < 0) {
    error("Cannot fork");
    return;
  }
  if (id == 0) {
    if (esc_char == '|') {
      close(piped[1]);
      close(0);
      fcntl(piped[0], F_DUPFD, 0);
      close(piped[0]);
    }
    execl("/bin/sh", "sh", "-c", comm + 1, (char *)0);
    exit(1);
  }
  (void)signal(SIGINT, SIG_IGN);
  (void)signal(SIGQUIT, SIG_IGN);
#ifdef SIGTSTP
  if ((savetstp = signal(SIGTSTP, SIG_IGN)) != SIG_IGN) {
    (void)signal(SIGTSTP, SIG_DFL);
  }
#endif
  if (esc_char == '|') {
    (void)close(piped[0]);
    (void)signal(SIGPIPE, SIG_IGN);
    wrt_fd(piped[1]);
    (void)close(piped[1]);
  }
  while ((lastc = wait((int *)0)) != id && lastc >= 0) {
    /*
 * wait for child, making sure it is the one we expected ...
 */
  }
  (void)signal(SIGINT, catchdel);
  (void)signal(SIGQUIT, sigquit);
#ifdef SIGTSTP
  (void)signal(SIGTSTP, savetstp);
#endif
  inittty();
}

static void
sigquit(int signo)
{
  (void)signo;
  quit();
}

/*
 * get all those commands ...
 */

int
getcomm(long *plong)
{
  int   c;
  long  count;
  char *p;
  int   i;
  int   j;
  char  buf[10];

  for (;;) {
    count = 0;
    give_prompt();
    while (isdigit((c = getch()))) {
      count = count * 10 + (c - '0');
    }
    *plong = count;
    p      = buf;
    for (;;) {
      if (c == -1) {
        /*
 * this should never happen, but it does,
 * when the user gives a TSTP signal (^z) or
 * an interrupt while the program is trying
 * to read a character from the terminal
 * in this case, the read is interrupted, so
 * we end up here
 * right, we will have to read again
 */
        if (interrupt)
          return 1;
        break;
      }
      *p++ = c;
      *p   = 0;
      if ((i = match(buf, &j, currmap->k_mach)) > 0) {
        /*
 * the key sequence matched. we have a command
 */
        return j;
      }
      if (i == 0)
        return 0;
      /*
 * we have a prefix of a command
 */
      assert(i == FSM_ISPREFIX);
      c = getch();
    }
  }
  /* NOTREACHED */
}
