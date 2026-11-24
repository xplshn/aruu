/* see LICENSE file for copyright and license details */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "mgint.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *tartype[] = {"tar archive", "POSIX tar archive", "POSIX tar archive (GNU)"};

int
mg_add(struct MagicOut *o, const char *s, size_t n)
{
  char  *p;
  size_t cap;

  if (o->err)
    return -1;
  if (o->n + n + 1 > o->cap) {
    for (cap = o->cap ? o->cap : 128; cap < o->n + n + 1; cap *= 2)
      ;
    if (!(p = realloc(o->s, cap))) {
      o->err = 1;
      return -1;
    }
    o->s   = p;
    o->cap = cap;
  }
  memcpy(o->s + o->n, s, n);
  o->n += n;
  o->s[o->n] = '\0';
  return 0;
}

int
mg_puts(struct MagicOut *o, const char *s)
{
  return mg_add(o, s, strlen(s));
}

/* only for the fixed formats of this library, never for text from a database */
int
mg_printf(struct MagicOut *o, const char *fmt, ...)
{
  va_list ap;
  char    buf[256];
  int     n;

  va_start(ap, fmt);
  n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n < 0)
    return -1;
  return mg_add(o, buf, (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1);
}

/* copy at most slen bytes of str up to its nul, with unprintable bytes as \ooo */
void
mg_printable(char *buf, size_t bufsiz, const char *str, size_t slen)
{
  const unsigned char *s = (const unsigned char *)str, *es = s + slen;
  char                *p = buf, *ep = buf + bufsiz - 1;

  for (; p < ep && s < es && *s; s++) {
    if (isprint(*s)) {
      *p++ = (char)*s;
      continue;
    }
    if (p >= ep - 3)
      break;
    *p++ = '\\';
    *p++ = (char)(((*s >> 6) & 7) + '0');
    *p++ = (char)(((*s >> 3) & 7) + '0');
    *p++ = (char)((*s & 7) + '0');
  }
  *p = '\0';
}

/* expand ${x?yes:no} by the executable bits, -1 when str is malformed or too long */
int
mg_varexpand(char *buf, size_t len, const char *str, int x)
{
  const char *ptr, *s = str, *t, *et, *e, *ee;
  size_t      l;

  while ((ptr = strstr(s, "${"))) {
    l = (size_t)(ptr - s);
    if (l >= len)
      return -1;
    memcpy(buf, s, l);
    buf += l;
    len -= l;
    ptr += 2;
    if (!*ptr || ptr[1] != '?')
      return -1;
    for (et = t = ptr + 2; *et && *et != ':'; et++)
      ;
    if (*et != ':')
      return -1;
    for (ee = e = et + 1; *ee && *ee != '}'; ee++)
      ;
    if (*ee != '}' || *ptr != 'x')
      return -1;
    if (x) {
      ptr = t;
      l   = (size_t)(et - t);
    } else {
      ptr = e;
      l   = (size_t)(ee - e);
    }
    if (l >= len)
      return -1;
    memcpy(buf, ptr, l);
    buf += l;
    len -= l;
    s = ee + 1;
  }
  if ((l = strlen(s)) >= len)
    return -1;
  memcpy(buf, s, l + 1);
  return 0;
}

int
mg_replace_end(struct MagicOut *o, const char *pat, const char *rep)
{
  size_t n = strlen(pat);

  if (o->err || o->n < n || memcmp(o->s + o->n - n, pat, n) != 0)
    return 0;
  o->n -= n;
  o->s[o->n] = '\0';
  mg_puts(o, rep);
  return 1;
}

/* read from the file, or from the buffer when there is no file */
long long
mg_pread(struct MagicRun *r, void *buf, size_t n, long long off)
{
  ssize_t got;
  size_t  done = 0;

  if (off < 0)
    return -1;
  if (r->fd < 0) {
    if ((unsigned long long)off >= r->head.n)
      return 0;
    if (n > r->head.n - (size_t)off)
      n = r->head.n - (size_t)off;
    memcpy(buf, r->head.p + off, n);
    return (long long)n;
  }
  while (done < n) {
    if ((got = pread(r->fd, (char *)buf + done, n - done, (off_t)(off + (long long)done))) < 0) {
      if (errno == EINTR)
        continue;
      return done ? (long long)done : -1;
    }
    if (!got)
      break;
    done += (size_t)got;
  }
  return (long long)done;
}

/* the order file(1) tries things in: archives and structured text first, magic, then text */
static int
classify(struct MagicRun *r)
{
  struct MagicOut elf;
  const char     *code, *code_mime, *ftype;
  int             text, t, rv;

  if (r->head.n == 0) {
    mg_puts(&r->out, r->mime ? "inode/x-empty" : "empty");
    return 0;
  }
  if (r->head.n == 1) {
    mg_puts(&r->out, r->mime ? "application/octet-stream" : "very short file (no magic)");
    return 0;
  }
  if ((text = mg_encoding(r->head.p, r->head.n, NULL, NULL, &code, &code_mime, &ftype)) < 0)
    return -1;
  if ((t = mg_istar(r->head.p, r->head.n)) != 0) {
    mg_puts(&r->out, r->mime ? "application/x-tar" : tartype[t - 1]);
    return 0;
  }
  if ((t = mg_isjson(r->head.p, r->head.n)) != 0) {
    if (r->mime)
      mg_puts(&r->out, t == 1 ? "application/json" : "application/x-ndjson");
    else
      mg_puts(&r->out, t == 1 ? "JSON text data" : "New Line Delimited JSON text data");
    return 0;
  }
  if (text && mg_iscsv(r->head.p, r->head.n)) {
    if (r->mime)
      mg_puts(&r->out, "text/csv");
    else
      mg_printf(&r->out, "CSV %s text", code);
    return 0;
  }
  memset(&elf, 0, sizeof(elf));
  if (mg_elf(r, &elf) < 0) {
    free(elf.s);
    elf.s = NULL;
    elf.n = 0;
  }
  rv = mg_softmagic(r, &r->head, MF_BINTEST, text);
  if (rv == 1 && elf.s)
    mg_add(&r->out, elf.s, elf.n);
  free(elf.s);
  if (rv < 0)
    return -1;
  if (rv)
    return 0;
  if ((rv = mg_ascmagic(r, text)) < 0)
    return -1;
  if (rv)
    return 0;
  mg_puts(&r->out, r->mime ? "application/octet-stream" : "data");
  return 0;
}

static int
utf8_seqlen(const unsigned char *s, size_t n)
{
  size_t len, i;

  if (s[0] >= 0xc2 && s[0] <= 0xdf)
    len = 2;
  else if (s[0] >= 0xe0 && s[0] <= 0xef)
    len = 3;
  else if (s[0] >= 0xf0 && s[0] <= 0xf4)
    len = 4;
  else
    return 0;
  if (len > n)
    return 0;
  for (i = 1; i < len; i++) {
    if ((s[i] & 0xc0) != 0x80)
      return 0;
  }
  return (int)len;
}

/* hand out a result as file(1) prints it: unprintable bytes as \ooo, utf-8 kept */
static void
deliver(const struct MagicOut *o, char *dst, size_t dn)
{
  const unsigned char *s = (const unsigned char *)(o->s ? o->s : "");
  size_t               i, k = 0, n = o->s ? o->n : 0;
  int                  len;

  if (!dst || !dn)
    return;
  for (i = 0; i < n && k + 1 < dn;) {
    if (s[i] >= 0x80 && (len = utf8_seqlen(s + i, n - i)) > 0) {
      if (k + (size_t)len + 1 > dn)
        break;
      memcpy(dst + k, s + i, (size_t)len);
      k += (size_t)len;
      i += (size_t)len;
    } else if (isprint(s[i])) {
      dst[k++] = (char)s[i++];
    } else {
      if (k + 5 > dn)
        break;
      dst[k++] = '\\';
      dst[k++] = (char)(((s[i] >> 6) & 3) + '0');
      dst[k++] = (char)(((s[i] >> 3) & 7) + '0');
      dst[k++] = (char)((s[i] & 7) + '0');
      i++;
    }
  }
  dst[k] = '\0';
}

static int
run_pass(struct MagicRun *r, int mime, int xmode, char *dst, size_t dn)
{
  int rv;

  if (!dst || !dn)
    return 0;
  memset(&r->out, 0, sizeof(r->out));
  r->mime  = mime;
  r->xmode = xmode;
  rv       = classify(r);
  if (r->out.err)
    rv = -1;
  if (rv == 0)
    deliver(&r->out, dst, dn);
  free(r->out.s);
  memset(&r->out, 0, sizeof(r->out));
  return rv;
}

static int
run(struct Magic *m, struct MagicRun *r, int xmode, char *desc, size_t dn, char *mime, size_t mn)
{
  locale_t old = (locale_t)0;
  int      rv;

  if (desc && dn)
    desc[0] = '\0';
  if (mime && mn)
    mime[0] = '\0';
  r->m = m;
  if (m->cloc)
    old = uselocale(m->cloc);
  rv = run_pass(r, 0, xmode, desc, dn);
  if (rv == 0)
    rv = run_pass(r, 1, xmode, mime, mn);
  if (m->cloc)
    uselocale(old);
  if (r->tstate == 1)
    free(r->tail);
  if (rv < 0)
    errno = ENOMEM;
  return rv;
}

struct Magic *
magic_open(const char *path)
{
  struct Magic *m;
  locale_t      old = (locale_t)0;
  int           e;

  if (!path && !(path = getenv("MAGIC")))
    path = MAGIC_PATH_DEFAULT;
  if (!(m = calloc(1, sizeof(*m)))) {
    errno = ENOMEM;
    return NULL;
  }
  m->cloc = newlocale(LC_ALL_MASK, "C", (locale_t)0);
  if (m->cloc)
    old = uselocale(m->cloc);
  e = mg_load(m, path) < 0 ? errno : 0;
  if (m->cloc)
    uselocale(old);
  if (e) {
    magic_close(m);
    errno = e;
    return NULL;
  }
  return m;
}

void
magic_close(struct Magic *m)
{
  if (!m)
    return;
  mg_free(m->db);
  if (m->cloc)
    freelocale(m->cloc);
  free(m);
}

int
magic_buffer(
    struct Magic *m, const void *buf, size_t n, char *desc, size_t dn, char *mime, size_t mn
)
{
  struct MagicRun *r;
  int              rv;

  if (!(r = calloc(1, sizeof(*r)))) {
    errno = ENOMEM;
    return -1;
  }
  r->head.p = buf;
  r->head.n = buf ? n : 0;
  r->fd     = -1;
  r->fsize  = (long long)r->head.n;
  rv        = run(m, r, 0, desc, dn, mime, mn);
  free(r);
  return rv;
}

int
magic_fd(struct Magic *m, int fd, char *desc, size_t dn, char *mime, size_t mn)
{
  struct MagicRun *r;
  struct stat      st;
  unsigned char   *buf;
  size_t           n = 0;
  ssize_t          got;
  int              rv, seekable, e;

  if (fstat(fd, &st) < 0)
    return -1;
  if (!(buf = malloc(MAGIC_BYTES_MAX)))
    return -1;
  seekable = lseek(fd, 0, SEEK_CUR) != (off_t)-1;
  while (n < MAGIC_BYTES_MAX) {
    if (seekable)
      got = pread(fd, buf + n, MAGIC_BYTES_MAX - n, (off_t)n);
    else
      got = read(fd, buf + n, MAGIC_BYTES_MAX - n);
    if (got < 0) {
      if (errno == EINTR)
        continue;
      e = errno;
      free(buf);
      errno = e;
      return -1;
    }
    if (!got)
      break;
    n += (size_t)got;
  }
  if (!(r = calloc(1, sizeof(*r)))) {
    free(buf);
    errno = ENOMEM;
    return -1;
  }
  r->head.p = buf;
  r->head.n = n;
  /* a pipe cant be read again, so only its head counts */
  if (seekable && S_ISREG(st.st_mode)) {
    r->head.file = 1;
    r->fd        = fd;
    r->fsize     = (long long)st.st_size;
  } else {
    r->fd    = -1;
    r->fsize = (long long)n;
  }
  rv = run(m, r, (st.st_mode & 0111) != 0, desc, dn, mime, mn);
  free(r);
  free(buf);
  return rv;
}

const char *
magic_charset(const void *buf, size_t n)
{
  const char *code, *code_mime = "binary", *type;

  if (!buf || n <= 1)
    return "binary";
  if (mg_encoding(buf, n, NULL, NULL, &code, &code_mime, &type) < 0)
    return "binary";
  return code_mime;
}
