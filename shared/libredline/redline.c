/* see license file for copyright and license details */

#include "redline.h"
#include "../paths.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#define REDLINE_DEFAULT_HISTORY_MAX_LEN 100
#define REDLINE_MAX_LINE                (1024 * 1024)
#define REDLINE_INITIAL_BUFLEN          4096

#define ENTER     13
#define CTRL_A    1
#define CTRL_B    2
#define CTRL_C    3
#define CTRL_D    4
#define CTRL_E    5
#define CTRL_F    6
#define CTRL_H    8
#define CTRL_K    11
#define CTRL_L    12
#define CTRL_N    14
#define CTRL_P    16
#define CTRL_T    20
#define CTRL_U    21
#define CTRL_W    23
#define CTRL_Y    25
#define CTRL_Z    26
#define CTRL_QUIT 28
#define BACKSPACE 127
#define ESC       27

struct redlineState {
  int         in_completion;
  int         ifd;
  int         ofd;
  char       *buf;
  size_t      buflen;
  const char *prompt;
  size_t      plen;
  size_t      pos;
  size_t      oldpos;
  size_t      len;
  size_t      cols;
  size_t      oldrows;
  int         oldrpos;
  int         history_index;
};

struct abuf {
  char *b;
  int   len;
};

static struct termios        orig_termios;
static int                   rawmode         = 0;
static int                   mlmode          = 0;
static int                   history_max_len = REDLINE_DEFAULT_HISTORY_MAX_LEN;
static int                   history_len     = 0;
static char                **history         = NULL;
static char                 *kill_buffer     = NULL;
static volatile sig_atomic_t winch_received  = 0;
static struct sigaction      orig_sigwinch;

static void redlineEditStop(struct redlineState *l);

static void (*completionCallback)(const char *, struct redlineCompletions *) = NULL;

/* return the number of bytes that compose the utf-8 character starting at c */
static int
utf8ByteLen(char c)
{
  unsigned char uc = (unsigned char)c;
  if ((uc & 0x80) == 0)
    return 1;
  if ((uc & 0xE0) == 0xC0)
    return 2;
  if ((uc & 0xF0) == 0xE0)
    return 3;
  if ((uc & 0xF8) == 0xF0)
    return 4;
  return 1;
}

/* decode character starting at s */
static uint32_t
utf8DecodeChar(const char *s, size_t *len)
{
  uint32_t cp = 0;
  int      l  = utf8ByteLen(*s);
  *len        = l;
  if (l == 1) {
    cp = ((unsigned char)*s);
  } else if (l == 2) {
    cp = ((unsigned char)*s & 0x1F) << 6;
    cp |= ((unsigned char)*(s + 1) & 0x3F);
  } else if (l == 3) {
    cp = ((unsigned char)*s & 0x0F) << 12;
    cp |= ((unsigned char)*(s + 1) & 0x3F) << 6;
    cp |= ((unsigned char)*(s + 2) & 0x3F);
  } else if (l == 4) {
    cp = ((unsigned char)*s & 0x07) << 18;
    cp |= ((unsigned char)*(s + 1) & 0x3F) << 12;
    cp |= ((unsigned char)*(s + 2) & 0x3F) << 6;
    cp |= ((unsigned char)*(s + 3) & 0x3F);
  }
  return cp;
}

static int
isZWJ(uint32_t cp)
{
  return cp == 0x200D;
}

static int
isCombiningMark(uint32_t cp)
{
  return (cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x1DC0 && cp <= 0x1DFF)
         || (cp >= 0x20D0 && cp <= 0x20FF) || (cp >= 0xFE20 && cp <= 0xFE2F);
}

static int
isVariationSelector(uint32_t cp)
{
  return (cp >= 0xFE00 && cp <= 0xFE0F) || (cp >= 0xE0100 && cp <= 0xE01EF);
}

static int
isSkinToneModifier(uint32_t cp)
{
  return cp >= 0x1F3FB && cp <= 0x1F3FF;
}

static int
isGraphemeExtend(uint32_t cp)
{
  return isCombiningMark(cp) || isVariationSelector(cp) || isSkinToneModifier(cp);
}

/* decode character going backward from pos */
static uint32_t
utf8DecodePrev(const char *buf, size_t pos, size_t *cplen)
{
  size_t i = 1;
  while (pos >= i && i <= 4) {
    unsigned char uc = (unsigned char)buf[pos - i];
    if ((uc & 0x80) == 0) {
      if (i == 1) {
        *cplen = 1;
        return uc;
      }
      break;
    }
    if ((uc & 0xC0) == 0xC0) {
      int l = utf8ByteLen(buf[pos - i]);
      if ((size_t)l == i) {
        *cplen = i;
        return utf8DecodeChar(buf + pos - i, cplen);
      }
      break;
    }
    i++;
  }
  *cplen = 1;
  return (unsigned char)buf[pos - 1];
}

/* calculate width of utf-8 char pos */
static size_t
utf8PrevCharLen(const char *buf, size_t pos)
{
  size_t   len      = 0;
  size_t   next_len = 0;
  uint32_t cp;
  if (pos == 0)
    return 0;
  cp = utf8DecodePrev(buf, pos, &len);
  pos -= len;
  while (pos > 0 && isGraphemeExtend(cp)) {
    cp = utf8DecodePrev(buf, pos, &next_len);
    len += next_len;
    pos -= next_len;
  }
  if (pos > 0 && isZWJ(cp)) {
    size_t j = utf8PrevCharLen(buf, pos);
    if (j > 0)
      len += j;
  }
  return len;
}

/* calculate width of next utf-8 char */
static size_t
utf8NextCharLen(const char *buf, size_t pos, size_t len)
{
  size_t   clen   = 0;
  size_t   offset = 0;
  uint32_t cp;
  if (pos >= len)
    return 0;
  cp     = utf8DecodeChar(buf + pos, &clen);
  offset = clen;
  while (pos + offset < len) {
    size_t   next_len = 0;
    uint32_t next_cp  = utf8DecodeChar(buf + pos + offset, &next_len);
    if (isGraphemeExtend(next_cp)) {
      offset += next_len;
    } else if (isZWJ(cp)) {
      offset += next_len;
      cp = next_cp;
    } else {
      break;
    }
  }
  return offset;
}

/* get columns needed to display char */
static int
utf8CharWidth(uint32_t cp)
{
  if (cp == 0)
    return 0;
  if (cp < 0x20 || (cp >= 0x7f && cp < 0xa0))
    return 0;
  if ((cp >= 0x1100 && cp <= 0x115f) || (cp >= 0x2e80 && cp <= 0xa4cf && cp != 0x303f)
      || (cp >= 0xac00 && cp <= 0xd7a3) || (cp >= 0xf900 && cp <= 0xfaff)
      || (cp >= 0xfe10 && cp <= 0xfe19) || (cp >= 0xfe30 && cp <= 0xfe6f)
      || (cp >= 0xff00 && cp <= 0xff60) || (cp >= 0xffe0 && cp <= 0xffe6)
      || (cp >= 0x20000 && cp <= 0x2fffd) || (cp >= 0x30000 && cp <= 0x3fffd)) {
    return 2;
  }
  return 1;
}

/* get ansi escape sequence length */
static size_t
ansiEscapeLen(const char *s, size_t len)
{
  size_t i = 0;
  if (len < 2 || s[0] != '\x1b' || s[1] != '[')
    return 0;
  i = 2;
  while (i < len) {
    char c = s[i];
    if ((c >= '0' && c <= '9') || c == ';' || c == '?' || c == '"') {
      i++;
    } else if (c >= 'A' && c <= 'Z') {
      return i + 1;
    } else if (c >= 'a' && c <= 'z') {
      return i + 1;
    } else {
      break;
    }
  }
  return 0;
}

/* calculate width of string */
static size_t
utf8StrWidth(const char *s, size_t len)
{
  size_t width = 0;
  size_t i     = 0;
  while (i < len) {
    size_t elen = ansiEscapeLen(s + i, len - i);
    if (elen > 0) {
      i += elen;
      continue;
    }
    size_t   clen = 0;
    uint32_t cp   = utf8DecodeChar(s + i, &clen);
    width += utf8CharWidth(cp);
    i += clen;
  }
  return width;
}

/* get single character width */
static int
utf8SingleCharWidth(const char *s, size_t len)
{
  size_t   clen = 0;
  uint32_t cp   = utf8DecodeChar(s, &clen);
  (void)len;
  return utf8CharWidth(cp);
}

static int
isUnsupportedTerm(void)
{
  char        *term = getenv("TERM");
  int          i;
  static char *unsupported[] = {"dumb", "cons25", "emacs", NULL};
  if (term == NULL)
    return 0;
  for (i = 0; unsupported[i]; i++) {
    if (strcasecmp(term, unsupported[i]) == 0)
      return 1;
  }
  return 0;
}

static void
sigwinchHandler(int sig)
{
  (void)sig;
  winch_received = 1;
}

static int
enableRawMode(int fd)
{
  struct termios   raw;
  struct sigaction sa;

  if (!isatty(STDIN_FILENO))
    return -1;
  if (tcgetattr(fd, &orig_termios) == -1)
    return -1;

  raw = orig_termios;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN]  = 1;
  raw.c_cc[VTIME] = 0;

  if (tcsetattr(fd, TCSAFLUSH, &raw) < 0)
    return -1;

  rawmode = 1;

  /* register sigwinch handler */
  sa.sa_handler = sigwinchHandler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGWINCH, &sa, &orig_sigwinch);

  return 0;
}

static void
disableRawMode(int fd)
{
  if (rawmode) {
    tcsetattr(fd, TCSAFLUSH, &orig_termios);
    sigaction(SIGWINCH, &orig_sigwinch, NULL);
    rawmode = 0;
  }
}

static int
getCursorPosition(int ifd, int ofd)
{
  char         buf[32];
  int          cols, rows;
  unsigned int i = 0;

  if (write(ofd, "\x1b[6n", 4) != 4)
    return -1;

  while (i < sizeof(buf) - 1) {
    if (read(ifd, buf + i, 1) != 1)
      break;
    if (buf[i] == 'R')
      break;
    i++;
  }
  buf[i] = '\0';

  if (buf[0] != 27 || buf[1] != '[')
    return -1;
  if (sscanf(buf + 2, "%d;%d", &rows, &cols) != 2)
    return -1;
  return cols;
}

static int
getColumns(int ifd, int ofd)
{
  struct winsize ws;
  char          *cols_env;
  int            tty_fd;
  int            cols = 0;

  if (ioctl(ofd, TIOCGWINSZ, &ws) == 0 && ws.ws_col >= 20)
    return ws.ws_col;
  if (ioctl(ifd, TIOCGWINSZ, &ws) == 0 && ws.ws_col >= 20)
    return ws.ws_col;
  if (ioctl(STDERR_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col >= 20)
    return ws.ws_col;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col >= 20)
    return ws.ws_col;
  if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col >= 20)
    return ws.ws_col;

  tty_fd = open(ARUU_PATH_DEVTTY, O_RDWR | O_NOCTTY);
  if (tty_fd >= 0) {
    if (ioctl(tty_fd, TIOCGWINSZ, &ws) == 0 && ws.ws_col >= 20) {
      cols = ws.ws_col;
    }
    close(tty_fd);
    if (cols >= 20)
      return cols;
  }

  cols_env = getenv("COLUMNS");
  if (cols_env) {
    cols = atoi(cols_env);
    if (cols >= 20)
      return cols;
  }

  /* fallback to cursor position query */
  int start;

  if (!isatty(ifd) || !isatty(ofd))
    return 80;

  start = getCursorPosition(ifd, ofd);
  if (start == -1)
    return 80;

  if (write(ofd, "\x1b[999C", 6) != 6)
    return 80;
  cols = getCursorPosition(ifd, ofd);
  if (cols == -1)
    return 80;

  if (cols > start) {
    char seq[32];
    snprintf(seq, sizeof(seq), "\x1b[%dD", cols - start);
    if (write(ofd, seq, strlen(seq)) == -1) {}
  }
  if (cols < 20)
    return 80;
  return cols;
}

static void
redlineBeep(void)
{
  fprintf(stderr, "\x7");
  fflush(stderr);
}

static void
freeCompletions(struct redlineCompletions *lc)
{
  size_t i;
  if (lc->cvec) {
    for (i = 0; i < lc->len; i++) {
      free(lc->cvec[i]);
    }
    free(lc->cvec);
  }
}

static size_t
longestCommonPrefix(struct redlineCompletions *lc)
{
  size_t i, j;
  if (lc->len == 0)
    return 0;
  for (i = 0;; i++) {
    char c = lc->cvec[0][i];
    if (c == '\0')
      return i;
    for (j = 1; j < lc->len; j++) {
      if (lc->cvec[j][i] != c) {
        return i;
      }
    }
  }
}

static void
printCompletions(struct redlineState *ls, struct redlineCompletions *lc)
{
  size_t      max_len = 0;
  size_t      i, j, k;
  size_t      col_width, num_cols, num_rows;
  size_t      sp, len, idx;
  const char *comp;
  const char *name;

  for (i = 0; i < lc->len; i++) {
    comp = lc->cvec[i];
    sp   = strlen(comp);
    if (sp > 0 && comp[sp - 1] == ' ') {
      sp--;
    }
    while (sp > 0 && comp[sp - 1] != ' ') {
      sp--;
    }
    len = strlen(comp + sp);
    if (len > 0 && (comp + sp)[len - 1] == ' ') {
      len--;
    }
    if (len > max_len) {
      max_len = len;
    }
  }

  col_width = max_len + 2;
  num_cols  = ls->cols / col_width;
  if (num_cols == 0)
    num_cols = 1;
  num_rows = (lc->len + num_cols - 1) / num_cols;

  if (write(ls->ofd, "\r\n", 2) == -1) {}
  for (i = 0; i < num_rows; i++) {
    for (j = 0; j < num_cols; j++) {
      idx = j * num_rows + i;
      if (idx < lc->len) {
        comp = lc->cvec[idx];
        sp   = strlen(comp);
        if (sp > 0 && comp[sp - 1] == ' ') {
          sp--;
        }
        while (sp > 0 && comp[sp - 1] != ' ') {
          sp--;
        }
        name = comp + sp;
        len  = strlen(name);
        if (len > 0 && name[len - 1] == ' ') {
          len--;
        }
        if (write(ls->ofd, name, len) == -1) {}
        if (j < num_cols - 1) {
          for (k = len; k < col_width; k++) {
            if (write(ls->ofd, " ", 1) == -1) {}
          }
        }
      }
    }
    if (write(ls->ofd, "\r\n", 2) == -1) {}
  }
}

static void
abInit(struct abuf *ab)
{
  ab->b   = NULL;
  ab->len = 0;
}

static void
abAppend(struct abuf *ab, const char *s, int len)
{
  char *new = realloc(ab->b, ab->len + len);
  if (new == NULL)
    return;
  memcpy(new + ab->len, s, len);
  ab->b = new;
  ab->len += len;
}

static void
abFree(struct abuf *ab)
{
  free(ab->b);
}

static void
refreshSingleLine(struct redlineState *l, int flags)
{
  char        seq[64];
  size_t      pwidth = utf8StrWidth(l->prompt, l->plen);
  int         fd     = l->ofd;
  char       *buf    = l->buf;
  size_t      len    = l->len;
  size_t      pos    = l->pos;
  size_t      poscol;
  size_t      lencol;
  struct abuf ab;

  poscol = utf8StrWidth(buf, pos);
  lencol = utf8StrWidth(buf, len);

  while (pwidth + poscol >= l->cols) {
    size_t clen   = utf8NextCharLen(buf, 0, len);
    int    cwidth = utf8SingleCharWidth(buf, clen);
    buf += clen;
    len -= clen;
    pos -= clen;
    poscol -= cwidth;
    lencol -= cwidth;
  }

  while (pwidth + lencol > l->cols) {
    size_t clen   = utf8PrevCharLen(buf, len);
    int    cwidth = utf8SingleCharWidth(buf + len - clen, clen);
    len -= clen;
    lencol -= cwidth;
  }

  abInit(&ab);
  snprintf(seq, sizeof(seq), "\r");
  abAppend(&ab, seq, strlen(seq));

  if (flags & 1) {
    abAppend(&ab, l->prompt, l->plen);
    abAppend(&ab, buf, len);
  }

  snprintf(seq, sizeof(seq), "\x1b[0K");
  abAppend(&ab, seq, strlen(seq));

  if (flags & 1) {
    snprintf(seq, sizeof(seq), "\r\x1b[%dC", (int)(poscol + pwidth));
    abAppend(&ab, seq, strlen(seq));
  }

  if (write(fd, ab.b, ab.len) == -1) {}
  abFree(&ab);
}

static void
refreshMultiLine(struct redlineState *l, int flags)
{
  char        seq[64];
  size_t      pwidth = utf8StrWidth(l->prompt, l->plen);
  size_t      bufwidth;
  size_t      poswidth;
  int         rows;
  int         rpos2;
  int         col;
  int         old_rows = l->oldrows;
  int         rpos     = l->oldrpos;
  int         fd       = l->ofd, j;
  struct abuf ab;

  (void)flags;

  bufwidth   = utf8StrWidth(l->buf, l->len);
  poswidth   = utf8StrWidth(l->buf, l->pos);
  rows       = (pwidth + bufwidth + l->cols - 1) / l->cols;
  l->oldrows = rows;

  abInit(&ab);

  /* move cursor up to the first row, column 0 of the input area */
  if (rpos > 1) {
    snprintf(seq, 64, "\r\x1b[%dA", rpos - 1);
    abAppend(&ab, seq, strlen(seq));
  } else {
    abAppend(&ab, "\r", 1);
  }

  /* clear all old rows */
  for (j = 0; j < old_rows; j++) {
    abAppend(&ab, "\x1b[0K", 4);
    if (j < old_rows - 1) {
      abAppend(&ab, "\n\r", 2);
    }
  }

  /* move cursor back to the first row, column 0 */
  if (old_rows > 1) {
    snprintf(seq, 64, "\r\x1b[%dA", old_rows - 1);
    abAppend(&ab, seq, strlen(seq));
  } else {
    abAppend(&ab, "\r", 1);
  }

  /* print prompt and new buffer */
  abAppend(&ab, l->prompt, l->plen);
  abAppend(&ab, l->buf, l->len);

  /* if cursor is at the end of the line and wraps, print a newline */
  if (l->pos && l->pos == l->len && (poswidth + pwidth) % l->cols == 0) {
    abAppend(&ab, "\n\r", 2);
    rows++;
    if (rows > (int)l->oldrows)
      l->oldrows = rows;
  }

  /* calculate cursor row and column */
  rpos2 = (pwidth + poswidth + l->cols) / l->cols;
  col   = (pwidth + poswidth) % l->cols;

  /* move cursor to the correct row and column */
  if (rows - rpos2 > 0) {
    snprintf(seq, 64, "\x1b[%dA", rows - rpos2);
    abAppend(&ab, seq, strlen(seq));
  }
  if (col) {
    snprintf(seq, 64, "\r\x1b[%dC", col);
    abAppend(&ab, seq, strlen(seq));
  } else {
    abAppend(&ab, "\r", 1);
  }

  l->oldpos  = l->pos;
  l->oldrpos = rpos2;

  if (write(fd, ab.b, ab.len) == -1) {}
  abFree(&ab);
}

static void
refreshLineWithFlags(struct redlineState *l, int flags)
{
  if (mlmode)
    refreshMultiLine(l, flags);
  else
    refreshSingleLine(l, flags);
}

static void
refreshLine(struct redlineState *l)
{
  refreshLineWithFlags(l, 1);
}

static int
completeLine(struct redlineState *ls, int keypressed)
{
  struct redlineCompletions lc = {0, NULL};
  size_t                    lcp_len;
  int                       c       = keypressed;
  int                       proceed = 1;
  char                      query[128];
  char                      answer = 0;

  if (c != 9) {
    ls->in_completion = 0;
    return c;
  }

  completionCallback(ls->buf, &lc);
  if (lc.len == 0) {
    redlineBeep();
    ls->in_completion = 0;
    c                 = 0;
  } else if (lc.len == 1) {
    size_t nwritten = snprintf(ls->buf, ls->buflen, "%s", lc.cvec[0]);
    ls->len = ls->pos = nwritten;
    refreshLine(ls);
    ls->in_completion = 0;
    c                 = 0;
  } else {
    lcp_len = longestCommonPrefix(&lc);
    if (lcp_len > ls->len) {
      size_t nwritten = snprintf(ls->buf, ls->buflen, "%.*s", (int)lcp_len, lc.cvec[0]);
      ls->len = ls->pos = nwritten;
      refreshLine(ls);
      ls->in_completion = 1;
      c                 = 0;
    } else {
      /* prefix cannot be expanded further */
      if (ls->in_completion == 0) {
        /* first tab: beep and wait for the second tab
 */
        redlineBeep();
        ls->in_completion = 1;
        c                 = 0;
      } else {
        /* second tab: display possibilities */
        if (lc.len > 100) {
          snprintf(
              query,
              sizeof(query),
              "\r\nDisplay all %d "
              "possibilities? (y or n) ",
              (int)lc.len
          );
          if (write(ls->ofd, query, strlen(query)) == -1) {}
          while (1) {
            if (read(ls->ifd, &answer, 1) != 1) {
              proceed = 0;
              break;
            }
            if (answer == 'y' || answer == 'Y' || answer == ' ' || answer == '\t') {
              proceed = 1;
              break;
            }
            if (answer == 'n' || answer == 'N' || answer == 27 || answer == 3 || answer == 4) {
              proceed = 0;
              break;
            }
            redlineBeep();
          }
        }
        if (proceed) {
          printCompletions(ls, &lc);
        } else {
          if (write(ls->ofd, "\r\n", 2) == -1) {}
        }
        ls->oldrows = 0;
        refreshLine(ls);
        ls->in_completion = 0;
        c                 = 0;
      }
    }
  }

  freeCompletions(&lc);
  return c;
}

static int
redlineEditInsert(struct redlineState *l, const char *c, int clen)
{
  if (l->len + clen >= l->buflen) {
    return 0;
  }
  if (l->len == l->pos) {
    memcpy(l->buf + l->pos, c, clen);
    l->pos += clen;
    l->len += clen;
    l->buf[l->len] = '\0';
    refreshLine(l);
  } else {
    memmove(l->buf + l->pos + clen, l->buf + l->pos, l->len - l->pos);
    memcpy(l->buf + l->pos, c, clen);
    l->pos += clen;
    l->len += clen;
    l->buf[l->len] = '\0';
    refreshLine(l);
  }
  return 1;
}

static void
redlineEditBackspace(struct redlineState *l)
{
  if (l->pos > 0 && l->len > 0) {
    size_t clen = utf8PrevCharLen(l->buf, l->pos);
    memmove(l->buf + l->pos - clen, l->buf + l->pos, l->len - l->pos);
    l->pos -= clen;
    l->len -= clen;
    l->buf[l->len] = '\0';
    refreshLine(l);
  }
}

static void
redlineEditDelete(struct redlineState *l)
{
  if (l->len > 0 && l->pos < l->len) {
    size_t clen = utf8NextCharLen(l->buf, l->pos, l->len);
    memmove(l->buf + l->pos, l->buf + l->pos + clen, l->len - l->pos - clen);
    l->len -= clen;
    l->buf[l->len] = '\0';
    refreshLine(l);
  }
}

static void
redlineEditMoveLeft(struct redlineState *l)
{
  if (l->pos > 0) {
    l->pos -= utf8PrevCharLen(l->buf, l->pos);
    refreshLine(l);
  }
}

static void
redlineEditMoveRight(struct redlineState *l)
{
  if (l->pos != l->len) {
    l->pos += utf8NextCharLen(l->buf, l->pos, l->len);
    refreshLine(l);
  }
}

static void
redlineEditMoveHome(struct redlineState *l)
{
  if (l->pos != 0) {
    l->pos = 0;
    refreshLine(l);
  }
}

static void
redlineEditMoveEnd(struct redlineState *l)
{
  if (l->pos != l->len) {
    l->pos = l->len;
    refreshLine(l);
  }
}

static void
redlineEditMoveWordLeft(struct redlineState *l)
{
  if (l->pos > 0) {
    while (l->pos > 0 && l->buf[l->pos - 1] == ' ')
      l->pos -= utf8PrevCharLen(l->buf, l->pos);
    while (l->pos > 0 && l->buf[l->pos - 1] != ' ')
      l->pos -= utf8PrevCharLen(l->buf, l->pos);
    refreshLine(l);
  }
}

static void
redlineEditMoveWordRight(struct redlineState *l)
{
  if (l->pos < l->len) {
    while (l->pos < l->len && l->buf[l->pos] == ' ')
      l->pos += utf8NextCharLen(l->buf, l->pos, l->len);
    while (l->pos < l->len && l->buf[l->pos] != ' ')
      l->pos += utf8NextCharLen(l->buf, l->pos, l->len);
    refreshLine(l);
  }
}

static void
killBufferSave(const char *text, size_t len)
{
  free(kill_buffer);
  kill_buffer = malloc(len + 1);
  if (kill_buffer) {
    memcpy(kill_buffer, text, len);
    kill_buffer[len] = '\0';
  }
}

static void
redlineEditDeleteWordRight(struct redlineState *l)
{
  size_t old_pos = l->pos;
  size_t diff;
  if (l->pos < l->len) {
    while (l->pos < l->len && l->buf[l->pos] == ' ')
      l->pos += utf8NextCharLen(l->buf, l->pos, l->len);
    while (l->pos < l->len && l->buf[l->pos] != ' ')
      l->pos += utf8NextCharLen(l->buf, l->pos, l->len);
    diff   = l->pos - old_pos;
    l->pos = old_pos;
    killBufferSave(l->buf + l->pos, diff);
    memmove(l->buf + l->pos, l->buf + l->pos + diff, l->len - l->pos - diff + 1);
    l->len -= diff;
    refreshLine(l);
  }
}

static void
redlineEditDeletePrevWord(struct redlineState *l)
{
  size_t old_pos = l->pos;
  size_t diff;
  if (l->pos > 0) {
    while (l->pos > 0 && l->buf[l->pos - 1] == ' ')
      l->pos -= utf8PrevCharLen(l->buf, l->pos);
    while (l->pos > 0 && l->buf[l->pos - 1] != ' ')
      l->pos -= utf8PrevCharLen(l->buf, l->pos);
    diff = old_pos - l->pos;
    killBufferSave(l->buf + l->pos, diff);
    memmove(l->buf + l->pos, l->buf + old_pos, l->len - old_pos + 1);
    l->len -= diff;
    refreshLine(l);
  }
}

void
redlineHistoryAdd(const char *line)
{
  char *linecopy;
  if (history_max_len == 0)
    return;
  if (history == NULL) {
    history = malloc(sizeof(char *) * history_max_len);
    if (history == NULL)
      return;
    memset(history, 0, sizeof(char *) * history_max_len);
  }
  if (history_len && strcmp(history[history_len - 1], line) == 0)
    return;
  linecopy = strdup(line);
  if (!linecopy)
    return;
  if (history_len == history_max_len) {
    free(history[0]);
    memmove(history, history + 1, sizeof(char *) * (history_max_len - 1));
    history_len--;
  }
  history[history_len] = linecopy;
  history_len++;
}

void
redlineHistorySetMaxLen(int len)
{
  char **new;
  if (len < 1)
    return;
  if (history) {
    int tocopy = history_len;
    new        = malloc(sizeof(char *) * len);
    if (new == NULL)
      return;
    if (len < tocopy) {
      int j;
      for (j = 0; j < tocopy - len; j++)
        free(history[j]);
      tocopy = len;
    }
    memset(new, 0, sizeof(char *) * len);
    memcpy(new, history + (history_len - tocopy), sizeof(char *) * tocopy);
    free(history);
    history = new;
  }
  history_max_len = len;
  if (history_len > history_max_len)
    history_len = history_max_len;
}

int
redlineHistorySave(const char *filename)
{
  mode_t old_umask = umask(S_IXUSR | S_IRWXG | S_IRWXO);
  FILE  *fp;
  int    j;

  fp = fopen(filename, "w");
  umask(old_umask);
  if (fp == NULL)
    return -1;
  chmod(filename, S_IRUSR | S_IWUSR);
  for (j = 0; j < history_len; j++) {
    fprintf(fp, "%s\n", history[j]);
  }
  fclose(fp);
  return 0;
}

int
redlineHistoryLoad(const char *filename)
{
  FILE *fp = fopen(filename, "r");
  char  buf[REDLINE_INITIAL_BUFLEN];
  if (fp == NULL)
    return -1;
  while (fgets(buf, sizeof(buf), fp) != NULL) {
    char *p = strchr(buf, '\r');
    if (!p)
      p = strchr(buf, '\n');
    if (p)
      *p = '\0';
    redlineHistoryAdd(buf);
  }
  fclose(fp);
  return 0;
}

char *
redlineHistoryGet(int idx)
{
  if (idx >= 0 && idx < history_len)
    return history[idx];
  return NULL;
}

int
redlineHistoryLen(void)
{
  return history_len;
}

static void
redlineEditHistoryNext(struct redlineState *l, int dir)
{
  if (history_len > 1) {
    const char *src;
    size_t      len;
    free(history[history_len - 1 - l->history_index]);
    history[history_len - 1 - l->history_index] = strdup(l->buf);
    l->history_index += (dir == 1) ? 1 : -1;
    if (l->history_index < 0) {
      l->history_index = 0;
      return;
    } else if (l->history_index >= history_len) {
      l->history_index = history_len - 1;
      return;
    }
    src = history[history_len - 1 - l->history_index];
    len = strlen(src);
    if (len >= l->buflen)
      len = l->buflen - 1;
    memcpy(l->buf, src, len);
    l->buf[len] = '\0';
    l->len = l->pos = len;
    refreshLine(l);
  }
}

static char *
redlineReadLine(FILE *fp)
{
  char  *line = NULL;
  size_t len = 0, cap = 0;
  while (1) {
    if (len + 1 >= cap) {
      size_t newcap = cap ? cap * 2 : 16;
      char *new     = realloc(line, newcap);
      if (!new) {
        free(line);
        return NULL;
      }
      line = new;
      cap  = newcap;
    }
    int c = fgetc(fp);
    if (c == EOF || c == '\n') {
      if (c == EOF && len == 0) {
        free(line);
        return NULL;
      }
      line[len] = '\0';
      return line;
    }
    line[len++] = c;
  }
}

static char *
redlineNoTTY(void)
{
  return redlineReadLine(stdin);
}

void
redlineClearScreen(void)
{
  if (write(STDOUT_FILENO, "\x1b[H\x1b[2J", 7) == -1) {}
}

static char *
redlineEditFeed(struct redlineState *l)
{
  char   c;
  int    nread;
  char   seq[3];
  char   param[8];
  size_t plen;
  char   final;
  char   p;
  int    is_word_jump;
  char   tmp[32];
  size_t prevlen;
  size_t currlen;
  size_t prevstart;
  char   utf8[4];
  int    utf8len;
  int    i;

  if (!isatty(l->ifd) && !getenv("REDLINE_ASSUME_TTY"))
    return redlineNoTTY();

  while (1) {
    nread = read(l->ifd, &c, 1);
    if (nread < 0) {
      if (errno == EINTR) {
        if (winch_received) {
          winch_received = 0;
          l->cols        = getColumns(l->ifd, l->ofd);
          refreshLine(l);
        }
        continue;
      }
      return (errno == EAGAIN || errno == EWOULDBLOCK) ? "more" : NULL;
    } else if (nread == 0) {
      return NULL;
    }
    break;
  }

  if ((l->in_completion || c == 9) && completionCallback != NULL) {
    int retval = completeLine(l, c);
    if (retval == 0)
      return "more";
    c = retval;
  }

  switch (c) {
    case 10:
    case ENTER:
      if (mlmode)
        redlineEditMoveEnd(l);
      return strdup(l->buf);
    case CTRL_C:
      if (write(l->ofd, "^C", 2) == -1) {}
      errno = EAGAIN;
      return NULL;
    case CTRL_Z:
      break;
    case CTRL_QUIT:
      if (write(l->ofd, "^\\", 2) == -1) {}
      redlineEditStop(l);
      fflush(stdout);
      {
        struct sigaction sa, osa;
        sa.sa_handler = SIG_DFL;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGQUIT, &sa, &osa);
        kill(getpid(), SIGQUIT);
        sigaction(SIGQUIT, &osa, NULL);
      }
      enableRawMode(l->ifd);
      refreshLine(l);
      break;
    case BACKSPACE:
    case 8:
      redlineEditBackspace(l);
      break;
    case CTRL_D:
      if (l->len > 0) {
        redlineEditDelete(l);
      } else {
        errno = ENOENT;
        return NULL;
      }
      break;
    case CTRL_T:
      if (l->pos > 0 && l->pos < l->len) {
        prevlen   = utf8PrevCharLen(l->buf, l->pos);
        currlen   = utf8NextCharLen(l->buf, l->pos, l->len);
        prevstart = l->pos - prevlen;
        if (prevlen > sizeof(tmp) || currlen > sizeof(tmp))
          break;
        memcpy(tmp, l->buf + l->pos, currlen);
        memmove(l->buf + prevstart + currlen, l->buf + prevstart, prevlen);
        memcpy(l->buf + prevstart, tmp, currlen);
        if (l->pos + currlen <= l->len)
          l->pos += currlen;
        refreshLine(l);
      }
      break;
    case CTRL_B:
      redlineEditMoveLeft(l);
      break;
    case CTRL_F:
      redlineEditMoveRight(l);
      break;
    case CTRL_P:
      redlineEditHistoryNext(l, 1);
      break;
    case CTRL_N:
      redlineEditHistoryNext(l, 0);
      break;
    case ESC:
      if (read(l->ifd, seq, 1) == -1)
        break;
      if (seq[0] == '[' || seq[0] == 'O') {
        if (read(l->ifd, seq + 1, 1) == -1)
          break;
        if (seq[0] == '[') {
          if (seq[1] >= '0' && seq[1] <= '9') {
            plen     = 1;
            final    = 0;
            param[0] = seq[1];
            while (plen < sizeof(param)) {
              if (read(l->ifd, &p, 1) != 1)
                break;
              if ((p >= '0' && p <= '9') || p == ';') {
                param[plen++] = p;
              } else {
                final = p;
                break;
              }
            }
            if (final == '~') {
              if (plen == 1 && param[0] == '3') {
                redlineEditDelete(l);
              }
            } else if (final == 'D' || final == 'C') {
              is_word_jump = 0;
              if (plen == 3 && param[0] == '1' && param[1] == ';'
                  && (param[2] == '5' || param[2] == '3')) {
                is_word_jump = 1;
              } else if (plen == 1 && (param[0] == '5' || param[0] == '3')) {
                is_word_jump = 1;
              }
              if (is_word_jump) {
                if (final == 'D') {
                  redlineEditMoveWordLeft(l);
                } else {
                  redlineEditMoveWordRight(l);
                }
              }
            }
          } else {
            switch (seq[1]) {
              case 'A':
                redlineEditHistoryNext(l, 1);
                break;
              case 'B':
                redlineEditHistoryNext(l, 0);
                break;
              case 'C':
                redlineEditMoveRight(l);
                break;
              case 'D':
                redlineEditMoveLeft(l);
                break;
              case 'H':
                redlineEditMoveHome(l);
                break;
              case 'F':
                redlineEditMoveEnd(l);
                break;
            }
          }
        } else if (seq[0] == 'O') {
          switch (seq[1]) {
            case 'H':
              redlineEditMoveHome(l);
              break;
            case 'F':
              redlineEditMoveEnd(l);
              break;
          }
        }
      } else {
        if (seq[0] == 'b' || seq[0] == 'B') {
          redlineEditMoveWordLeft(l);
        } else if (seq[0] == 'f' || seq[0] == 'F') {
          redlineEditMoveWordRight(l);
        } else if (seq[0] == 'd' || seq[0] == 'D') {
          redlineEditDeleteWordRight(l);
        } else if (seq[0] == 127 || seq[0] == 8) {
          redlineEditDeletePrevWord(l);
        }
      }
      break;
    default:
      if (c < 32)
        break;
      utf8len = utf8ByteLen(c);
      utf8[0] = c;
      if (utf8len > 1) {
        for (i = 1; i < utf8len; i++) {
          if (read(l->ifd, utf8 + i, 1) != 1)
            break;
        }
      }
      if (redlineEditInsert(l, utf8, utf8len) == 0)
        return NULL;
      break;
    case CTRL_U:
      killBufferSave(l->buf, l->pos);
      memmove(l->buf, l->buf + l->pos, l->len - l->pos + 1);
      l->len -= l->pos;
      l->pos = 0;
      refreshLine(l);
      break;
    case CTRL_K:
      killBufferSave(l->buf + l->pos, l->len - l->pos);
      l->buf[l->pos] = '\0';
      l->len         = l->pos;
      refreshLine(l);
      break;
    case CTRL_A:
      redlineEditMoveHome(l);
      break;
    case CTRL_E:
      redlineEditMoveEnd(l);
      break;
    case CTRL_L:
      redlineClearScreen();
      refreshLine(l);
      break;
    case CTRL_W:
      redlineEditDeletePrevWord(l);
      break;
    case CTRL_Y:
      if (kill_buffer) {
        redlineEditInsert(l, kill_buffer, strlen(kill_buffer));
      }
      break;
  }
  return "more";
}

static int
redlineEditStart(
    struct redlineState *l,
    int                  stdin_fd,
    int                  stdout_fd,
    char                *buf,
    size_t               buflen,
    const char          *prompt
)
{
  l->in_completion = 0;
  l->ifd           = stdin_fd;
  l->ofd           = stdout_fd;
  l->buf           = buf;
  l->buflen        = buflen;
  l->prompt        = prompt;
  l->plen          = strlen(prompt);
  l->pos           = 0;
  l->oldpos        = 0;
  l->len           = 0;
  l->cols          = getColumns(stdin_fd, stdout_fd);
  l->oldrows       = 0;
  l->oldrpos       = 0;
  l->history_index = 0;
  l->buf[0]        = '\0';

  if (enableRawMode(l->ifd) == -1)
    return -1;
  refreshLine(l);
  return 0;
}

static void
redlineEditStop(struct redlineState *l)
{
  if (!isatty(l->ifd) && !getenv("REDLINE_ASSUME_TTY"))
    return;
  disableRawMode(l->ifd);
  printf("\n");
}

char *
redline(const char *prompt)
{
  struct redlineState l;
  char               *buf;
  char               *res;

  if (!isatty(STDIN_FILENO) || isUnsupportedTerm()) {
    if (write(STDOUT_FILENO, prompt, strlen(prompt)) == -1) {}
    return redlineNoTTY();
  }

  buf = malloc(REDLINE_INITIAL_BUFLEN);
  if (buf == NULL)
    return NULL;
  if (redlineEditStart(&l, STDIN_FILENO, STDOUT_FILENO, buf, REDLINE_INITIAL_BUFLEN, prompt)
      == -1) {
    free(buf);
    return NULL;
  }
  redlineHistoryAdd("");
  while (1) {
    res = redlineEditFeed(&l);
    if (res == NULL || strcmp(res, "more") != 0) {
      break;
    }
  }
  redlineEditStop(&l);
  if (history_len > 0) {
    history_len--;
    free(history[history_len]);
  }
  free(l.buf);
  if (res == NULL && errno == EAGAIN) {
    kill(getpid(), SIGINT);
  }
  return res;
}

void
redlineSetCompletionCallback(void (*cb)(const char *, struct redlineCompletions *))
{
  completionCallback = cb;
}

void
redlineAddCompletion(struct redlineCompletions *lc, const char *str)
{
  size_t len = strlen(str);
  char  *copy, **cvec;

  copy = malloc(len + 1);
  if (copy == NULL)
    return;
  memcpy(copy, str, len + 1);
  cvec = realloc(lc->cvec, sizeof(char *) * (lc->len + 1));
  if (cvec == NULL) {
    free(copy);
    return;
  }
  lc->cvec            = cvec;
  lc->cvec[lc->len++] = copy;
}

void
redlineSetMultiLine(int ml)
{
  mlmode = ml;
}
