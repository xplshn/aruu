/* Copyright (c) 1985 Ceriel J.H. Jacobs */

#include "display.h"
#include "assert.h"
#include "getline.h"
#include "in_all.h"
#include "machine.h"
#include "main.h"
#include "options.h"
#include "output.h"
#include "process.h"
#include "term.h"
#define getline yap_getline
#include "utf.h"
#undef getline
#include "ansi.h"

int             pagesize;
int             maxpagesize;
int             scrollsize;
struct scr_info scr_info;
int             status;

static char *do_line(char *str, int reallydispl);
static void  flush_display_buffer(char *buf, char **p);
static int   rune_width(Rune r);
static int   decode_cell(const char *s, const char **next, int *width, int *is_underscore);
int          wcwidth(wchar_t wc);

/*
 * Fill n lines of the screen, each with "str".
 */

static void
fillscr(char *str, int n)
{
  while (n-- > 0) {
    putline(str);
  }
}

/*
 * Skip "n" screenlines of line "p", and return what's left of it.
 */

static char *
skiplines(char *p, int n)
{
  while (n-- > 0) {
    p = do_line(p, 0);
    scr_info.currentpos--;
  }
  return p;
}

/*
 * Redraw screen.
 * "n" = 1 if it is a real redraw, 0 if one page must be displayed.
 * It is also called when yap receives a stop signal.
 */

void
redraw(int n)
{
  struct scr_info *p = &scr_info;
  int              i;

  i = pagesize;
  if (n && p->currentpos) {
    i = p->currentpos;
  }
  (void)display(p->firstline, p->nf, i, 1);
}

/*
 * Compute return value for the routines "display" and "scrollf".
 * This return value indicates wether we are at the end of file
 * or at the start, or both.
 * "s" contains that part of the last line that was not displayed.
 */

static int
compretval(const char *s)
{
  int              i;
  struct scr_info *p = &scr_info;

  i = 0;
  if (!s || (!*s && !getline(p->lastline + 1, 1))) {
    i = EOFILE;
  }
  if (p->firstline == 1 && !p->nf) {
    i |= START;
  }
  status = i;
  return i;
}

/*
 * Display nlines, starting at line n, not displaying the first
 * nd screenlines of n.
 * If reallydispl = 0, the actual displaying is not performed,
 * only the computing associated with it is done.
 */

int
display(long n, int nd, int nlines, int reallydispl)
{
  struct scr_info *s = &scr_info;
  char            *p; /* pointer to line to be displayed */

  if (startcomm) { /* No displaying on a command from the
                    * yap command line. In this case, displaying
                    * will be done after executing the command,
                    * by a redraw.
                    */
    reallydispl = 0;
  }
  if (!n) {
    n  = 1L;
    nd = 0;
  }
  if (reallydispl) { /* move cursor to starting point */
    if (stupid) {
      putline(currentfile);
      putline(", line ");
      prnum(n);
      nlines--;
    }
    if (cflag) {
      putline("\r\n");
    } else {
      home();
      clrscreen();
    }
  }
  /*
   * Now, do computations and display
   */
  s->currentpos = 0;
  s->nf         = nd;
  s->head       = s->tail;
  s->tail->cnt  = 0;
  s->tail->line = n;
  p             = skiplines(getline(n, 1), nd);
  while (nlines && p) {
    /*
     * While there is room,
     * and there is something left to display ...
     */
    (s->tail->cnt)++;
    nlines--;
    if (*(p = do_line(p, reallydispl)) == '\0') {
      /*
       * File-line finished, get next one ...
       */
      p = getline(++n, 1);
      if (nlines && p) {
        s->tail       = s->tail->next;
        s->tail->cnt  = 0;
        s->tail->line = n;
      }
    }
  }
  if (!stupid) {
    s->currentpos += nlines;
    if (reallydispl) {
      fillscr("~\r\n", nlines);
      fillscr("\r\n", maxpagesize - s->currentpos);
    }
  }
  return compretval(p);
}

/*
 * Scroll forwards n lines.
 */

int
scrollf(int n, int reallydispl)
{
  struct scr_info *s = &scr_info;
  char            *p;
  long             ll;
  int              i;

  /*
   * First, find out how many screenlines of the last line were already
   * on the screen, and possibly above it.
   */

  if (n <= 0 || (status & EOFILE))
    return status;
  if (startcomm)
    reallydispl = 0;
  /*
   * Find out where to begin displaying
   */
  i = s->tail->cnt;
  if ((ll = s->lastline) == s->firstline)
    i += s->nf;
  p = skiplines(getline(ll, 1), i);
  /*
   * Now, place the cursor at the first free line
   */
  if (reallydispl && !stupid) {
    clrbline();
    mgoto(s->currentpos);
  }
  /*
   * Now display lines, keeping track of which lines are on the screen.
   */
  while (n-- > 0) { /* There are still rows to be displayed */
    if (!*p) {      /* End of line, get next one */
      if (!(p = getline(++ll, 1))) {
        /*
         * No lines left. At end of file
         */
        break;
      }
      s->tail       = s->tail->next;
      s->tail->cnt  = 0;
      s->tail->line = ll;
    }
    if (s->currentpos >= maxpagesize) {
      /*
       * No room, delete first screen-line
       */
      s->currentpos--;
      s->nf++;
      if (--(s->head->cnt) == 0) {
        /*
         * The first file-line on the screen is wiped
         * out completely; update administration
         * accordingly.
         */
        s->nf   = 0;
        s->head = s->head->next;
        assert(s->head->cnt > 0);
      }
    }
    s->tail->cnt++;
    p = do_line(p, reallydispl);
  }
  return compretval(p);
}

/*
 * Scroll back n lines
 */

int
scrollb(int n, int reallydispl)
{
  struct scr_info *s = &scr_info;
  char            *p; /* Holds string to be displayed */
  int              i;
  int              count;
  long             ln; /* a line number */
  int              nodispl;
  int              cannotscroll; /* stupid or no insert-line */

  /*
   * First, find out where to start
   */
  if ((count = n) <= 0 || (status & START))
    return status;
  if (startcomm)
    reallydispl = 0;
  cannotscroll = stupid || (!*AL && !*SR);
  ln           = s->firstline;
  nodispl      = s->nf;
  while (count) { /* While scrolling back ... */
    i = nodispl;
    if (i) {
      /*
       * There were screen-lines of s->firstline that were not
       * displayed.
       * We can use them now, but only "count" of them.
       */
      if (i > count)
        i = count;
      s->currentpos += i;
      nodispl -= i;
      count -= i;
    } else { /* Get previous line */
      if (ln == 1)
        break; /* isn't there ... */
      p = getline(--ln, 1);
      /*
       * Make it the first line of the screen and compute
       * how many screenlines it takes. These lines are not
       * displayed, but nodispl is set to this count, so
       * that it will be nonzero next time around
       */
      nodispl = 0;
      do { /* Find out how many screenlines */
        nodispl++;
        p = skiplines(p, 1);
      } while (*p);
    }
  }
  n -= count;
  if ((i = s->currentpos) > maxpagesize)
    i = maxpagesize;
  if (reallydispl && hardcopy)
    i = n;
  /*
   * Now that we know where to start, we can use "display" to do the
   * rest of the computing for us, and maybe even the displaying ...
   */
  i = display(ln, nodispl, i, reallydispl && cannotscroll);
  if (cannotscroll || !reallydispl) {
    /*
     * Yes, "display" did the displaying, or we did'nt have to
     * display at all.
     * I like it, but the user obviously does not.
     * Let him buy another (smarter) terminal ...
     */
    return i;
  }
  /*
   * Now, all we have to do is the displaying. And we are dealing with
   * a smart terminal (it can insert lines or scroll back).
   */
  home();
  /*
   * Insert lines all at once
   */
  for (i = n; i; i--) {
    if (DB && *CE) {
      /*
       * Grumble..., terminal retains lines below, so we have
       * to clear the lines that we push off the screen
       */
      clrbline();
      home();
    }
    if (*SR) {
      scrollreverse();
    } else {
      insert_line(0);
    }
  }
  p = skiplines(getline(ln = s->firstline, 1), s->nf);
  for (i = 0; i < n; i++) {
    p = do_line(p, 1);
    s->currentpos--;
    if (!*p) {
      p = getline(++ln, 1);
    }
  }
  return count;
}

/*
 * Process a line.
 * If reallydispl > 0 then display it.
 */

static char *
do_line(char *str, int reallydispl)
{
  char           buf[1024];
  char          *p     = buf;
  int            pos   = COLS;
  int            do_ul = 0, do_hl = 0;
  int            lastmode = 0, lasthlmode = 0;
  int            c2;
  int            cell_width;
  int            tab_width;
  int            is_underscore;
  int            cell_len;
  int            next_len;
  int            next_is_underscore;
  unsigned char  uc;
  const char    *cell_start;
  const char    *cursor;
  const char    *next;
  enum ansi_kind ansi_kind;

  while (*str && pos > 0) {
    uc = (unsigned char)*str;
    if (uc == 0x1b && ansi_escape(str, &next, &ansi_kind)) {
      if (ansi_kind == ANSI_SGR) {
        cell_len = next - str;
        if (reallydispl && p + cell_len >= &buf[sizeof(buf) - 1]) {
          flush_display_buffer(buf, &p);
        }
        if (reallydispl) {
          (void)memcpy(p, str, (size_t)cell_len);
          p += cell_len;
        }
      }
      str = (char *)next;
      continue;
    }
    if (uc < ' ' && (cell_len = match(str, &c2, sppat)) > 0) {
      /*
       * We found a string that matches, and thus must be
       * echoed literally
       */
      if ((pos - c2) <= 0) {
        /*
         * It did not fit
         */
        break;
      }
      pos -= c2;
      if (reallydispl) {
        flush_display_buffer(buf, &p);
        cell_start = str;
        str += cell_len;
        uc   = (unsigned char)*str;
        *str = '\0';
        putline((char *)cell_start);
        *str = (char)uc;
      } else {
        str += cell_len;
      }
      continue;
    }

    cell_start = str;
    cell_len   = decode_cell(str, &next, &cell_width, &is_underscore);
    cursor     = next;
    do_hl      = 0;
    if (*cursor == '\b' && *(cursor + 1) != '\0') {
      next_len = decode_cell(cursor + 1, &next, &tab_width, &next_is_underscore);
      if (!(is_underscore && *(cursor + 1 + next_len) != '\b')) {
        while (*cursor == '\b' && *(cursor + 1) != '\0') {
          cell_start = cursor + 1;
          cell_len   = decode_cell(cell_start, &next, &cell_width, &is_underscore);
          cursor     = next;
          do_hl      = 1;
        }
      }
    }
    do_ul = 1;
    /*
     * Find underline sequences ...
     */
    if (is_underscore && *cursor == '\b' && *(cursor + 1) != '\0') {
      cell_start = cursor + 1;
      cell_len   = decode_cell(cell_start, &next, &cell_width, &is_underscore);
      cursor     = next;
    } else {
      if (*cursor == '\b' && *(cursor + 1) == '_') {
        cursor += 2;
      } else {
        do_ul = 0;
      }
    }
    if (cell_width == -1) {
      tab_width = 8 - ((COLS - pos) & 0x07);
      if (pos - tab_width < 0) {
        break;
      }
    } else if (cell_width == -2) {
      if (pos <= 1) {
        break;
      }
    } else if (cell_width > pos) {
      break;
    }
    if (reallydispl && do_hl != lasthlmode) {
      flush_display_buffer(buf, &p);
      if (do_hl) {
        bold();
      } else {
        end_bold();
      }
    }
    lasthlmode = do_hl;
    if (reallydispl && do_ul != lastmode) {
      flush_display_buffer(buf, &p);
      if (do_ul) {
        underline();
      } else {
        end_underline();
      }
    }
    lastmode = do_ul;
    if (cell_width == -1) {
      pos -= tab_width;
      if (reallydispl) {
        if (expandtabs) {
          while (tab_width-- > 0) {
            *p++ = ' ';
          }
        } else {
          flush_display_buffer(buf, &p);
          givetab();
        }
      }
      str = (char *)cursor;
      continue;
    }
    if (reallydispl && p + cell_len >= &buf[sizeof(buf) - 1]) {
      flush_display_buffer(buf, &p);
    }
    if (cell_width == -2) {
      if (reallydispl) {
        if (p + 2 >= &buf[sizeof(buf) - 1]) {
          flush_display_buffer(buf, &p);
        }
        *p++ = '^';
        *p++ = *cell_start ^ 0x40;
      }
      pos -= 2;
      str = (char *)cursor;
      continue;
    }
    if (reallydispl) {
      (void)memcpy(p, cell_start, (size_t)cell_len);
      p += cell_len;
    }
    if (cell_width >= 0) {
      pos -= cell_width;
      if (reallydispl && do_ul && *UC && cell_width == 1 && pos > 0) {
        /*
         * Underlining apparently is done one
         * character at a time.
         */
        flush_display_buffer(buf, &p);
        backspace();
        underchar();
      }
      str = (char *)cursor;
      continue;
    }
    str = (char *)cursor;
  }
  if (reallydispl) {
    flush_display_buffer(buf, &p);
    if (pos > 0 || (pos <= 0 && (!AM || XN))) {
      putline("\r\n");
    }
    /*
     * The next should be here! I.e. it may not be before printing
     * the newline. This has to do with XN. We don't know exactly
     * WHEN the terminal will stop ignoring the newline.
     * I have for example a terminal (Ampex a230) that will
     * continue to ignore the newline after a clear to end of line
     * sequence, but not after an end_underline sequence.
     */
    if (lastmode) {
      end_underline();
    }
    if (lasthlmode) {
      end_bold();
    }
  }
  scr_info.currentpos++;
  return str;
}

static void
flush_display_buffer(char *buf, char **p)
{
  if (*p == buf) {
    return;
  }
  **p = '\0';
  putline(buf);
  *p = buf;
}

static int
rune_width(Rune r)
{
  int width;

  width = wcwidth((wchar_t)r);
  if (width < 0) {
    return 1;
  }
  return width;
}

static int
decode_cell(const char *s, const char **next, int *width, int *is_underscore)
{
  const char *p;
  Rune        r;
  Rune        mark;
  int         len;
  int         mark_len;
  int         mark_width;

  len = chartorune(&r, s);
  if (len <= 0) {
    len = 1;
    r   = Runeerror;
  }
  *is_underscore = (len == 1 && *s == '_');
  if (*s == '\t') {
    *width = -1;
    *next  = s + 1;
    return 1;
  }
  if ((unsigned char)*s < ' ' || (unsigned char)*s == 0x7f) {
    *width = -2;
    *next  = s + 1;
    return 1;
  }
  *width = rune_width(r);
  p      = s + len;
  while (*p) {
    if (*p == '\b' || *p == '\t' || (unsigned char)*p < ' ' || (unsigned char)*p == 0x7f) {
      break;
    }
    mark_len = chartorune(&mark, p);
    if (mark_len <= 0) {
      break;
    }
    mark_width = rune_width(mark);
    if (mark_width != 0) {
      break;
    }
    p += mark_len;
    len += mark_len;
  }
  *next = p;
  return len;
}

/* ARGSUSED */
int
setmark(long cnt)
{ /* Set a mark on the current page */
  struct scr_info *p = &scr_info;
  (void)cnt;

  p->savfirst = p->firstline;
  p->savnf    = p->nf;
  return 0;
}

/* ARGSUSED */
int
tomark(long cnt)
{ /* Go to the mark */
  struct scr_info *p = &scr_info;
  (void)cnt;

  (void)display(p->savfirst, p->savnf, pagesize, 1);
  return 0;
}

/* ARGSUSED */
int
exgmark(long cnt)
{ /* Exchange mark and current page */
  struct scr_info *p = &scr_info;
  long             svfirst;
  int              svnf;
  (void)cnt;

  svfirst = p->firstline;
  svnf    = p->nf;
  tomark(0L);
  p->savfirst = svfirst;
  p->savnf    = svnf;
  return 0;
}

void
d_clean()
{ /* Clean up */
  struct scr_info *p = &scr_info;

  p->savnf      = 0;
  p->savfirst   = 0;
  p->head       = p->tail;
  p->head->line = 0;
  p->currentpos = 0;
}
