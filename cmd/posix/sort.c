/* see LICENSE file for copyright and license details */

#include "queue.h"
#include "text.h"
#include "utf.h"
#include "util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct keydef {
  int start_column;
  int end_column;
  int start_char;
  int end_char;
  int flags;
  TAILQ_ENTRY(keydef) entry;
};

struct column {
  struct line line;
  size_t      cap;
};

enum {
  MOD_N      = 1 << 0,
  MOD_STARTB = 1 << 1,
  MOD_ENDB   = 1 << 2,
  MOD_R      = 1 << 3,
  MOD_D      = 1 << 4,
  MOD_F      = 1 << 5,
  MOD_I      = 1 << 6,
};

static TAILQ_HEAD(kdhead, keydef) kdhead = TAILQ_HEAD_INITIALIZER(kdhead);

static int Cflag = 0, cflag = 0, uflag = 0;
#if FEATURE_SORT_BIG
static int zflag = 0;
#endif
#if FEATURE_SORT_STABLE
static int sflag = 0;
#endif
static char         *fieldsep    = NULL;
static size_t        fieldseplen = 0;
static struct column col1, col2;

static void
skipblank(struct line *a)
{
  while (a->len && (*(a->data) == ' ' || *(a->data) == '\t')) {
    a->data++;
    a->len--;
  }
}

static void
skipnonblank(struct line *a)
{
  while (a->len && (*(a->data) != '\n' && *(a->data) != ' ' && *(a->data) != '\t')) {
    a->data++;
    a->len--;
  }
}

static void
skipcolumn(struct line *a, int skip_to_next_col)
{
  char *s;

  if (fieldsep) {
    if ((s = memmem(a->data, a->len, fieldsep, fieldseplen))) {
      if (skip_to_next_col)
        s += fieldseplen;
      a->len -= s - a->data;
      a->data = s;
    } else {
      a->data += a->len - 1;
      a->len = 1;
    }
  } else {
    skipblank(a);
    skipnonblank(a);
  }
}

static void
columns(struct line *line, const struct keydef *kd, struct column *col)
{
  Rune        r;
  struct line start, end;
  size_t      utflen, rlen;
  int         i;

  start.data = line->data;
  start.len  = line->len;
  for (i = 1; i < kd->start_column; i++)
    skipcolumn(&start, 1);
  if (kd->flags & MOD_STARTB)
    skipblank(&start);
  for (utflen = 0; start.len > 1 && utflen < (size_t)(kd->start_char - 1);) {
    rlen = chartorune(&r, start.data);
    start.data += rlen;
    start.len -= rlen;
    utflen++;
  }

  end.data = line->data;
  end.len  = line->len;
  if (kd->end_column) {
    for (i = 1; i < kd->end_column; i++)
      skipcolumn(&end, 1);
    if (kd->flags & MOD_ENDB)
      skipblank(&end);
    if (kd->end_char) {
      for (utflen = 0; end.len > 1 && utflen < (size_t)kd->end_char;) {
        rlen = chartorune(&r, end.data);
        end.data += rlen;
        end.len -= rlen;
        utflen++;
      }
    } else {
      skipcolumn(&end, 0);
    }
  } else {
    end.data += end.len - 1;
    end.len = 1;
  }
  col->line.len = MAX(0, end.data - start.data);
  if (!(col->line.data) || col->cap < col->line.len + 1) {
    free(col->line.data);
    col->line.data = emalloc(col->line.len + 1);
    col->cap       = col->line.len + 1;
  }
  memcpy(col->line.data, start.data, col->line.len);
  col->line.data[col->line.len] = '\0';
}

static int
skipmodcmp(struct line *a, struct line *b, int flags)
{
  Rune   r1, r2;
  size_t offa = 0, offb = 0;

  do {
    offa += chartorune(&r1, a->data + offa);
    offb += chartorune(&r2, b->data + offb);

    if (flags & MOD_D && flags & MOD_I) {
      while (offa < a->len && ((!isblankrune(r1) && !isalnumrune(r1)) || (!isprintrune(r1))))
        offa += chartorune(&r1, a->data + offa);
      while (offb < b->len && ((!isblankrune(r2) && !isalnumrune(r2)) || (!isprintrune(r2))))
        offb += chartorune(&r2, b->data + offb);
    } else if (flags & MOD_D) {
      while (offa < a->len && !isblankrune(r1) && !isalnumrune(r1))
        offa += chartorune(&r1, a->data + offa);
      while (offb < b->len && !isblankrune(r2) && !isalnumrune(r2))
        offb += chartorune(&r2, b->data + offb);
    } else if (flags & MOD_I) {
      while (offa < a->len && !isprintrune(r1))
        offa += chartorune(&r1, a->data + offa);
      while (offb < b->len && !isprintrune(r2))
        offb += chartorune(&r2, b->data + offb);
    }
    if (flags & MOD_F) {
      r1 = toupperrune(r1);
      r2 = toupperrune(r2);
    }
  } while (r1 && r1 == r2);

  return r1 - r2;
}

#if FEATURE_SORT_BIG
static void
getlines_z(FILE *fp, struct linebuf *b)
{
  char   *line = NULL;
  size_t  size = 0, linelen = 0;
  ssize_t len;

  while ((len = getdelim(&line, &size, '\0', fp)) > 0) {
    if (++b->nlines > b->capacity) {
      b->capacity += 512;
      b->lines = ereallocarray(b->lines, b->capacity, sizeof(*b->lines));
    }
    linelen                      = len;
    b->lines[b->nlines - 1].data = memcpy(emalloc(linelen + 1), line, linelen + 1);
    b->lines[b->nlines - 1].len  = linelen;
  }
  free(line);
  if (b->lines && b->nlines && linelen && b->lines[b->nlines - 1].data[linelen - 1] != '\0') {
    b->lines[b->nlines - 1].data              = erealloc(b->lines[b->nlines - 1].data, linelen + 2);
    b->lines[b->nlines - 1].data[linelen]     = '\0';
    b->lines[b->nlines - 1].data[linelen + 1] = '\0';
    b->lines[b->nlines - 1].len++;
  }
}
#endif

static int
slinecmp(struct line *a, struct line *b)
{
  int            res = 0;
  double         x, y;
  struct keydef *kd;

  TAILQ_FOREACH(kd, &kdhead, entry)
  {
    columns(a, kd, &col1);
    columns(b, kd, &col2);

    /* if -u is given, dont use default key definition
 * unless it is the only one */
    if (uflag && kd == TAILQ_LAST(&kdhead, kdhead)
        && TAILQ_LAST(&kdhead, kdhead) != TAILQ_FIRST(&kdhead)) {
      res = 0;
    } else if (kd->flags & MOD_N) {
      x   = strtod(col1.line.data, NULL);
      y   = strtod(col2.line.data, NULL);
      res = (x < y) ? -1 : (x > y);
    } else if (kd->flags & (MOD_D | MOD_F | MOD_I)) {
      res = skipmodcmp(&col1.line, &col2.line, kd->flags);
    } else {
      res = linecmp(&col1.line, &col2.line);
    }

    if (kd->flags & MOD_R)
      res = -res;
    if (res)
      break;
  }

  return res;
}

#if FEATURE_SORT_STABLE
struct sort_item {
  struct line line;
  size_t      index;
};

static int
slinecmp_stable(const struct sort_item *a, const struct sort_item *b)
{
  int res = slinecmp((struct line *)&a->line, (struct line *)&b->line);
  if (res == 0)
    return (a->index < b->index) ? -1 : 1;
  return res;
}
#endif

static int
check(FILE *fp, const char *fname)
{
  static struct line prev, cur, tmp;
  static size_t      prevsize, cursize, tmpsize;
  ssize_t            len;
  int                delim = '\n';

#if FEATURE_SORT_BIG
  if (zflag)
    delim = '\0';
#endif

  if (!prev.data) {
    if ((len = getdelim(&prev.data, &prevsize, delim, fp)) < 0) {
      if (feof(fp))
        return 0;
      eprintf("getdelim:");
    }
    prev.len = len;
  }
  while ((len = getdelim(&cur.data, &cursize, delim, fp)) > 0) {
    cur.len = len;
    if (uflag > slinecmp(&cur, &prev)) {
      if (!Cflag) {
        weprintf("disorder %s: ", fname);
        fwrite(cur.data, 1, cur.len, stderr);
      }
      return 1;
    }
    tmp      = cur;
    tmpsize  = cursize;
    cur      = prev;
    cursize  = prevsize;
    prev     = tmp;
    prevsize = tmpsize;
  }

  return 0;
}

static int
parse_flags(char **s, int *flags, int bflag)
{
  while (isalpha((int)**s)) {
    switch (*((*s)++)) {
        // ?man -b: specify block size or base directory
      case 'b':
        *flags |= bflag;
        break;
        // ?man -d: specify directory
      case 'd':
        *flags |= MOD_D;
        break;
        // ?man -f: force the operation
      case 'f':
        *flags |= MOD_F;
        break;
        // ?man -i: interactive mode or prompt for confirmation
      case 'i':
        *flags |= MOD_I;
        break;
        // ?man -n: print line numbers or counts
      case 'n':
        *flags |= MOD_N;
        break;
        // ?man -r: operate recursively
      case 'r':
        *flags |= MOD_R;
        break;
      default:
        return -1;
    }
  }

  return 0;
}

static void
addkeydef(char *kdstr, int flags)
{
  struct keydef *kd;

  kd = enmalloc(2, sizeof(*kd));

  /* parse key definition kdstr with format
 * start_column[.start_char][flags][,end_column[.end_char][flags]]
 */
  kd->start_column = 1;
  kd->start_char   = 1;
  kd->end_column   = 0; /* 0 means end of line */
  kd->end_char     = 0; /* 0 means end of column */
  kd->flags        = flags;

  if ((kd->start_column = strtol(kdstr, &kdstr, 10)) < 1)
    enprintf(2, "invalid start column in key definition\n");

  if (*kdstr == '.') {
    if ((kd->start_char = strtol(kdstr + 1, &kdstr, 10)) < 1)
      enprintf(
          2,
          "invalid start character in key "
          "definition\n"
      );
  }
  if (parse_flags(&kdstr, &kd->flags, MOD_STARTB) < 0)
    enprintf(2, "invalid start flags in key definition\n");

  if (*kdstr == ',') {
    if ((kd->end_column = strtol(kdstr + 1, &kdstr, 10)) < 0)
      enprintf(2, "invalid end column in key definition\n");
    if (*kdstr == '.') {
      if ((kd->end_char = strtol(kdstr + 1, &kdstr, 10)) < 0)
        enprintf(
            2,
            "invalid end character in key "
            "definition\n"
        );
    }
    if (parse_flags(&kdstr, &kd->flags, MOD_ENDB) < 0)
      enprintf(2, "invalid end flags in key definition\n");
  }

  if (*kdstr != '\0')
    enprintf(2, "invalid key definition\n");

  TAILQ_INSERT_TAIL(&kdhead, kd, entry);
}

static void
usage(void)
{
  enprintf(
      2,
      "usage: %s [-Cbcdfimnru"
#if FEATURE_SORT_STABLE
      "s"
#endif
#if FEATURE_SORT_BIG
      "z"
#endif
      "] [-o outfile] [-t delim] [-k def]... [file ...]\n",
      argv0
  );
}

// ?man sort: sort lines
// ?man arguments: -Cbcdfimnru
// ?man sort or merge lines of text files
int
main(int argc, char *argv[])
{
  FILE          *fp, *ofp = stdout;
  struct linebuf linebuf = EMPTY_LINEBUF;
  size_t         i;
  int            global_flags = 0, ret = 0;
  char          *outfile = NULL;

  ARGBEGIN
  {
    // ?man -C: specify option flag
    case 'C':
      Cflag = 1;
      break;
    // ?man -b: specify block size or base directory
    case 'b':
      global_flags |= MOD_STARTB | MOD_ENDB;
      break;
    // ?man -c: print count or perform stdout action
    case 'c':
      cflag = 1;
      break;
    // ?man -d: specify directory
    case 'd':
      global_flags |= MOD_D;
      break;
    // ?man -f: force the operation
    case 'f':
      global_flags |= MOD_F;
      break;
    // ?man -i: interactive mode or prompt for confirmation
    case 'i':
      global_flags |= MOD_I;
      break;
    // ?man -k:str: specify option flag
    case 'k':
      addkeydef(EARGF(usage()), global_flags);
      break;
    // ?man -m: specify mode or limit
    case 'm':
      /* more or less for free, but for performance-reasons,
 * we should keep this flag in mind and maybe some later
 * day implement it properly so we dont run out of memory
 * while merging large sorted files
 */
      break;
    // ?man -n: print line numbers or counts
    case 'n':
      global_flags |= MOD_N;
      break;
    // ?man -o:file: specify output file
    case 'o':
      outfile = EARGF(usage());
      break;
    // ?man -r: operate recursively
    case 'r':
      global_flags |= MOD_R;
      break;
#if FEATURE_SORT_STABLE
    // ?man -s: silent mode or print summary
    case 's':
      sflag = 1;
      break;
#endif
    // ?man -t:str: sort or specify timestamp
    case 't':
      fieldsep = EARGF(usage());
      if (!*fieldsep)
        eprintf("empty delimiter\n");
      fieldseplen = unescape(fieldsep);
      break;
    // ?man -u: unbuffered output
    case 'u':
      uflag = 1;
      break;
#if FEATURE_SORT_BIG
    // ?man -z: specify option flag
    case 'z':
      zflag = 1;
      break;
#endif
    default:
      usage();
  }
  ARGEND

  /* -b shall only apply to custom key definitions */
  if (TAILQ_EMPTY(&kdhead) && global_flags)
    addkeydef("1", global_flags & ~(MOD_STARTB | MOD_ENDB));
  if (TAILQ_EMPTY(&kdhead) || (!Cflag && !cflag))
    addkeydef("1", global_flags & MOD_R);

  if (!argc) {
    if (Cflag || cflag) {
      if (check(stdin, "<stdin>") && !ret)
        ret = 1;
    } else {
#if FEATURE_SORT_BIG
      if (zflag)
        getlines_z(stdin, &linebuf);
      else
#endif
        getlines(stdin, &linebuf);
    }
  } else
    for (; *argv; argc--, argv++) {
      if (!strcmp(*argv, "-")) {
        *argv = "<stdin>";
        fp    = stdin;
      } else if (!(fp = fopen(*argv, "r"))) {
        enprintf(2, "fopen %s:", *argv);
        continue;
      }
      if (Cflag || cflag) {
        if (check(fp, *argv) && !ret)
          ret = 1;
      } else {
#if FEATURE_SORT_BIG
        if (zflag)
          getlines_z(fp, &linebuf);
        else
#endif
          getlines(fp, &linebuf);
      }
      if (fp != stdin && fshut(fp, *argv))
        ret = 2;
    }

  if (!Cflag && !cflag) {
    if (outfile && !(ofp = fopen(outfile, "w")))
      eprintf("fopen %s:", outfile);

#if FEATURE_SORT_STABLE
    if (sflag) {
      struct sort_item *items = ecalloc(linebuf.nlines, sizeof(*items));
      for (i = 0; i < linebuf.nlines; i++) {
        items[i].line  = linebuf.lines[i];
        items[i].index = i;
      }
      qsort(
          items,
          linebuf.nlines,
          sizeof(*items),
          (int (*)(const void *, const void *))slinecmp_stable
      );
      for (i = 0; i < linebuf.nlines; i++) {
        linebuf.lines[i] = items[i].line;
      }
      free(items);
    } else {
#endif
      qsort(
          linebuf.lines,
          linebuf.nlines,
          sizeof(*linebuf.lines),
          (int (*)(const void *, const void *))slinecmp
      );
#if FEATURE_SORT_STABLE
    }
#endif

    for (i = 0; i < linebuf.nlines; i++) {
      if (!uflag || i == 0 || slinecmp(&linebuf.lines[i], &linebuf.lines[i - 1])) {
        fwrite(linebuf.lines[i].data, 1, linebuf.lines[i].len, ofp);
      }
    }
  }

  if (fshut(stdin, "<stdin>") | fshut(stdout, "<stdout>") | fshut(stderr, "<stderr>"))
    ret = 2;

  return ret;
}
