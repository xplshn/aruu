#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "make.h"

#define MAXREPL  30
#define TABSIZ   64
#define MAXTOKEN FILENAME_MAX
#define ITEM     128

typedef struct macro Macro;

enum inputype {
  FTFILE,
  FTEXPAN,
};

enum {
  STBEGIN,
  STINTERNAL,
  STREPLACE,
  STTO,
  STEND,
};

struct input {
  int siz;
  int type;

  FILE      *fp;
  struct loc loc;

  int   pos;
  char *buf;

  struct input *prev;
};

struct macro {
  char *name;
  char *value;
  int   where;

  struct macro *next;
};

static struct input *input;
static char          token[MAXTOKEN];
static int           tok;
static Macro        *htab[TABSIZ];

void
dumpmacros(void)
{
  Macro **pp, *p;

  for (pp = htab; pp < &htab[TABSIZ]; ++pp) {
    for (p = *pp; p; p = p->next)
      printf("%s = %s\n", p->name, getmacro(p->name));
  }
}

static Macro *
lookup(char *name)
{
  Macro *mp;
  int    h = hash(name) & (TABSIZ - 1);

  for (mp = htab[h]; mp && strcmp(mp->name, name); mp = mp->next)
    ;

  if (mp)
    return mp;

  mp        = emalloc(sizeof(*mp));
  mp->name  = estrdup(name);
  mp->value = estrdup("");
  mp->next  = htab[h];
  mp->where = UNDEF;
  htab[h]   = mp;

  return mp;
}

static char *
macroinfo(char *name, int *pwhere, Macro **mpp)
{
  char  *s, *t;
  int    hide, where;
  Macro *mp = lookup(name);

  hide = 0;
  if (!strcmp(name, "SHELL") || !strcmp(name, "MAKEFLAGS"))
    hide = 1;

  s     = mp->value;
  where = mp->where;

  if (!hide && (where == UNDEF || where == INTERNAL || eflag)) {
    t = getenv(name);
    if (t) {
      where = ENVIRON;
      s     = t;
    }
  }

  if (pwhere)
    *pwhere = where;
  if (mpp)
    *mpp = mp;

  return s;
}

char *
getmacro(char *name)
{
  return macroinfo(name, NULL, NULL);
}

void
setmacro(char *name, char *val, int where, int export)
{
  int    owhere, set;
  char  *s;
  Macro *mp;

  assert(where != ENVIRON);

  s = macroinfo(name, &owhere, &mp);

  /*
 * default values are defined before anything else, and marked
 * as INTERNAL because they are injected as parseable text, and
 * MAKEFILE and INTERNAL variables are always overriden. ENVIRON
 * macros are generated in macroinfo() and this is why this function
 * should not receive a where == ENVIRON ever
 */
  switch (owhere) {
    case UNDEF:
    case INTERNAL:
    case MAKEFILE:
      set = 1;
      break;
    case ENVIRON:
      set = (where == MAKEFLAGS || where == CMDLINE);
      set |= (where == MAKEFILE && !eflag);
      break;
    case MAKEFLAGS:
      set = (where == CMDLINE || where == MAKEFLAGS);
      break;
    case CMDLINE:
      set = (where == CMDLINE);
      break;
    default:
      abort();
  }

  if (!set) {
    debug("hidding override of %s from '%s' to '%s'", name, s, val);
  } else {
    debug("override %s from '%s' to '%s'", name, s, val);
    free(mp->value);
    mp->value = estrdup(val);
    mp->where = where;

    if (export && strcmp(name, "SHELL") != 0) {
      debug("exporting macro %s", name);
      exportvar(name, val);
    }
  }
}

void
freeloc(struct loc *loc)
{
  free(loc->fname);
}

static struct loc *
getloc(void)
{
  struct input *ip;

  for (ip = input; ip && ip->type != FTFILE; ip = ip->prev)
    ;
  if (!ip)
    return NULL;

  return &ip->loc;
}

void
error(char *fmt, ...)
{
  va_list     va;
  struct loc *loc;

  fprintf(stderr, "make: error: ");
  if ((loc = getloc()) != NULL)
    fprintf(stderr, "%s:%d: ", loc->fname, loc->lineno);

  va_start(va, fmt);
  vfprintf(stderr, fmt, va);
  va_end(va);
  putc('\n', stderr);

  exit(EXIT_FAILURE);
}

void
warning(char *fmt, ...)
{
  va_list     va;
  struct loc *loc;

  fprintf(stderr, "make: warning: ");
  if ((loc = getloc()) != NULL)
    fprintf(stderr, "%s:%d: ", loc->fname, loc->lineno);

  va_start(va, fmt);
  vfprintf(stderr, fmt, va);
  va_end(va);
  putc('\n', stderr);
}

static void
pop(void)
{
  struct input *ip = input->prev;

  if (input->type == FTFILE) {
    if (input->fp)
      fclose(input->fp);
    freeloc(&input->loc);
  }
  free(input->buf);
  free(input);

  input = ip;
}

static void
push(int type, ...)
{
  int           line, len, pos;
  FILE         *fp = NULL;
  char         *buf, *s, *fname = NULL;
  va_list       va;
  struct input *ip;

  va_start(va, type);
  switch (type) {
    case FTFILE:
      fp    = va_arg(va, FILE *);
      s     = va_arg(va, char *);
      line  = va_arg(va, int);
      fname = estrdup(s);
      buf   = emalloc(BUFSIZ);
      pos = len = BUFSIZ;
      break;
    case FTEXPAN:
      s    = va_arg(va, char *);
      buf  = estrdup(s);
      line = pos = 0;
      len        = strlen(s);
      break;
  }
  va_end(va);

  ip             = emalloc(sizeof(*ip));
  ip->siz        = len;
  ip->buf        = buf;
  ip->type       = type;
  ip->fp         = fp;
  ip->loc.fname  = fname;
  ip->loc.lineno = line;
  ip->pos        = pos;
  ip->prev       = input;

  input = ip;
}

static char *
trim(char *s)
{
  size_t len;

  while (isspace(*s))
    s++;

  for (len = strlen(s); len > 0 && isspace(s[len - 1]); --len)
    s[len - 1] = '\0';

  return s;
}

static void
include(char *s)
{
  FILE *fp;
  char *fil, *t, *end;

  s = trim(s);
  if (*s == '<' || *s == '"') {
    char delim = (*s == '<') ? '>' : '"';
    s++;
    end = strchr(s, delim);
    if (end)
      *end = '\0';
  }
  fil = expandstring(s, NULL, getloc());

  t = trim(fil);
  if (strlen(t) != 0) {
    debug("including '%s'", t);
    if ((fp = fopen(t, "r")) == NULL)
      error("opening %s:%s", t, strerror(errno));
    push(FTFILE, fp, t, 0);
  }

  free(fil);
}

static void
sinclude(char *s)
{
  FILE *fp;
  char *fil, *t, *end;

  s = trim(s);
  if (*s == '<' || *s == '"') {
    char delim = (*s == '<') ? '>' : '"';
    s++;
    end = strchr(s, delim);
    if (end)
      *end = '\0';
  }
  fil = expandstring(s, NULL, getloc());

  t = trim(fil);
  if (strlen(t) != 0) {
    debug("trying to include '%s'", t);
    if ((fp = fopen(t, "r")) != NULL)
      push(FTFILE, fp, t, 0);
    else
      debug("skipping missing include %s", t);
  }

  free(fil);
}

#define MAXCOND 32
static struct {
  int active;
  int matched;
  int parent_active;
} condstack[MAXCOND];
static int conddepth = 0;

static int
cond_active(void)
{
  if (conddepth == 0)
    return 1;
  return condstack[conddepth].active && condstack[conddepth].parent_active;
}

static void
cond_if(int truth)
{
  int parent;
  if (conddepth >= MAXCOND)
    error("too many nested .if directives");
  parent = cond_active();
  conddepth++;
  condstack[conddepth].parent_active = parent;
  condstack[conddepth].matched       = truth;
  condstack[conddepth].active        = truth;
}

static void
cond_elif(int truth)
{
  if (conddepth == 0)
    error(".elif without .if");
  if (condstack[conddepth].matched)
    condstack[conddepth].active = 0;
  else {
    condstack[conddepth].matched = condstack[conddepth].matched || truth;
    condstack[conddepth].active  = truth;
  }
}

static void
cond_else(void)
{
  if (conddepth == 0)
    error(".else without .if");
  condstack[conddepth].active  = !condstack[conddepth].matched;
  condstack[conddepth].matched = 1;
}

static void
cond_endif(void)
{
  if (conddepth == 0)
    error(".endif without .if");
  conddepth--;
}

static int
eval_if(char *expr)
{
  char *s, *p, *var, *val, *end;
  int   neg = 0, result;

  s = trim(expr);
  if (!*s)
    return 0;

  while (*s == '(') {
    end = s + strlen(s);
    if (end > s && end[-1] == ')')
      end[-1] = '\0';
    s++;
    s = trim(s);
  }

  if (*s == '!') {
    neg = 1;
    s++;
    s = trim(s);
  }

  if (strncmp(s, "defined", 7) == 0 && (s[7] == '(' || s[7] == ' ')) {
    var = trim(s + 7);
    if (*var == '(')
      var++;
    end = var + strlen(var);
    if (end > var && end[-1] == ')')
      end[-1] = '\0';
    var    = trim(var);
    result = getmacro(var)[0] != '\0';
    return neg ? !result : result;
  }

  if (strncmp(s, "make", 4) == 0 && (s[4] == '(' || s[4] == ' ')) {
    var = trim(s + 4);
    if (*var == '(')
      var++;
    end = var + strlen(var);
    if (end > var && end[-1] == ')')
      end[-1] = '\0';
    var    = trim(var);
    result = getmacro(var)[0] != '\0';
    return neg ? !result : result;
  }

  {
    char *expanded = expandstring(s, NULL, getloc());
    s              = expanded;
  }

  p = strstr(s, "==");
  if (!p)
    p = strstr(s, "!=");
  if (p) {
    int eq = (p[1] == '=');
    *p     = '\0';
    val    = trim(p + 2);
    var    = trim(s);
    {
      char *ev = expandstring(var, NULL, getloc());
      result   = strcmp(ev, val) == 0;
      free(ev);
    }
    if (!eq)
      result = !result;
    free(s);
    return neg ? !result : result;
  }

  /* bare expression: true if non-empty after expansion */
  result = s[0] != '\0';
  free(s);
  return neg ? !result : result;
}

static char *
nextline(void)
{
  int   c;
  FILE *fp;
  char *s, *lim, d[BUFSIZ];

  assert(input->type == FTFILE);

repeat:
  fp = input->fp;
  if (!fp || feof(fp))
    return NULL;

  lim = &input->buf[input->siz];
  for (s = input->buf; s < lim; *s++ = c) {
    c = getc(fp);
    if (c == '\n' || c == EOF) {
      input->loc.lineno++;
      *s++ = '\n';
      break;
    }
    if (c > UCHAR_MAX || c < 0)
      error("invalid character '%c' (%d)", c, c);
  }

  if (s == lim)
    error("too long line");
  if (ferror(fp))
    error(strerror(errno));
  *s = '\0';

  if (!strcmp(input->buf, ""))
    goto repeat;

  /* bmake style .include / .sinclude / .-include */
  if (input->buf[0] == '.') {
    size_t dl;
    /* copy and strip trailing whitespace/newline so strcmp works */
    strncpy(d, input->buf + 1, sizeof(d) - 1);
    d[sizeof(d) - 1] = '\0';
    dl               = strlen(d);
    while (dl > 0 && isspace((unsigned char)d[dl - 1]))
      d[--dl] = '\0';
    if (strncmp(d, "include", 7) == 0 && isblank(d[7])) {
      input->pos = input->siz;
      if (cond_active())
        include(d + 7);
      goto repeat;
    }
    if (strncmp(d, "sinclude", 8) == 0 && isblank(d[8])) {
      input->pos = input->siz;
      if (cond_active())
        sinclude(d + 8);
      goto repeat;
    }
    if (strncmp(d, "-include", 8) == 0 && isblank(d[8])) {
      input->pos = input->siz;
      if (cond_active())
        sinclude(d + 8);
      goto repeat;
    }
    if (strncmp(d, "optinclude", 10) == 0 && isblank(d[10])) {
      input->pos = input->siz;
      if (cond_active())
        sinclude(d + 10);
      goto repeat;
    }
    /* conditionals */
    if (strncmp(d, "if", 2) == 0 && (isblank(d[2]) || d[2] == '\0')) {
      if (cond_active()) {
        if (d[2] == '\0')
          cond_if(eval_if(""));
        else
          cond_if(eval_if(d + 3));
      } else {
        cond_if(0);
      }
      goto repeat;
    }
    if (strncmp(d, "ifdef", 5) == 0 && (isblank(d[5]) || d[5] == '\0')) {
      if (cond_active()) {
        char *v = trim(d[5] ? d + 6 : "");
        cond_if(getmacro(v)[0] != '\0');
      } else {
        cond_if(0);
      }
      goto repeat;
    }
    if (strncmp(d, "ifndef", 6) == 0 && (isblank(d[6]) || d[6] == '\0')) {
      if (cond_active()) {
        char *v = trim(d[6] ? d + 7 : "");
        cond_if(getmacro(v)[0] == '\0');
      } else {
        cond_if(0);
      }
      goto repeat;
    }
    if (strncmp(d, "elif", 4) == 0 && (isblank(d[4]) || d[4] == '\0')) {
      if (condstack[conddepth].parent_active)
        cond_elif(eval_if(d[4] ? d + 5 : ""));
      else
        cond_elif(0);
      goto repeat;
    }
    if (strcmp(d, "else") == 0) {
      cond_else();
      goto repeat;
    }
    if (strcmp(d, "endif") == 0) {
      cond_endif();
      goto repeat;
    }
    if (strncmp(d, "error", 5) == 0 && isblank(d[5])) {
      if (cond_active())
        error("%s", trim(d + 6));
      goto repeat;
    }
    if (strncmp(d, "warning", 7) == 0 && isblank(d[7])) {
      if (cond_active())
        warning("%s", trim(d + 8));
      goto repeat;
    }
  }

  /* POSIX include */
  if (!strncmp(input->buf, "include", 7) && isblank(input->buf[7])) {
    input->pos = input->siz;
    if (cond_active())
      include(input->buf + 7);
    goto repeat;
  }

  /* sinclude / -include (GNU/POSIX style without dot) */
  if ((!strncmp(input->buf, "sinclude", 8) && isblank(input->buf[8]))
      || (!strncmp(input->buf, "-include", 8) && isblank(input->buf[8]))) {
    input->pos = input->siz;
    if (cond_active())
      sinclude(input->buf + 8);
    goto repeat;
  }

  /* if we are inside an inactive conditional branch, skip this line */
  if (!cond_active()) {
    input->pos = input->siz;
    goto repeat;
  }

  input->pos = 0;

  return input->buf;
}

static int
empty(struct input *ip)
{
  return ip->pos == ip->siz || ip->buf[ip->pos] == '\0';
}

static int
moreinput(void)
{
  while (input) {
    if (!empty(input))
      break;

    switch (input->type) {
      case FTEXPAN:
        pop();
        break;
      case FTFILE:
        if (!nextline())
          pop();
        break;
    }
  }

  return input != NULL;
}

static int
nextc(void)
{
  if (!moreinput())
    return EOF;

  return input->buf[input->pos++];
}

/*
 * this function only can be called after a call to nextc
 * that didn't return EOF. it can return '\0', but as
 * it is used only to check against '$' then it is not
 * a problem
 */
static int
ahead(void)
{
  return input->buf[input->pos];
}

static int
back(int c)
{
  if (c == EOF)
    return c;
  assert(input->pos > 0);
  return input->buf[--input->pos] = c;
}

static void
comment(void)
{
  int c;

  while ((c = nextc()) != EOF && c != '\n') {
    if (c == '\\' && nextc() == EOF)
      break;
  }
}

static void
skipspaces(void)
{
  int c;

  for (c = nextc(); c == ' ' || c == '\t'; c = nextc())
    ;
  back(c);
}

static int
validchar(int c)
{
  if (c == EOF)
    return 0;
  return c == '.' || c == '/' || c == '_' || c == '-' || isalnum(c);
}

static char *
expandmacro(char *name)
{
  char *s;

  s = expandstring(getmacro(name), NULL, getloc());
  debug("macro %s expanded to '%s'", name, s);

  return s;
}

static void
replace(char *line, char *repl, char *to)
{
  int   siz, at, len, replsiz, tosiz, pos;
  char *oline, *cur, *buf;

  debug("replacing '%s', with '%s' to '%s'", line, repl, to);
  oline   = line;
  tosiz   = strlen(to);
  replsiz = strlen(repl);

  buf = NULL;
  for (pos = 0; *line; pos += siz) {
    cur = NULL;
    siz = 0;

    for (siz = 0; *line == ' ' || *line == '\t'; ++siz) {
      cur      = erealloc(cur, siz + 1);
      cur[siz] = *line++;
    }

    len = strcspn(line, " \t");
    at  = len - replsiz;
    if (at < 0 || memcmp(line + at, repl, replsiz)) {
      cur = erealloc(cur, siz + len);
      memcpy(cur + siz, line, len);
      siz += len;
    } else {
      cur = erealloc(cur, siz + at + tosiz);
      memcpy(cur + siz, line, at);
      memcpy(cur + siz + at, to, tosiz);
      siz += at + tosiz;
    }

    line += len;
    buf = erealloc(buf, pos + siz);
    memcpy(buf + pos, cur, siz);
    free(cur);
  }

  if (pos > 0) {
    buf      = erealloc(buf, pos + 1);
    buf[pos] = '\0';
    debug("\treplace '%s' with '%s'", oline, buf);
    push(FTEXPAN, buf);
  }

  free(buf);
}

static void
expandsimple(Target *tp)
{
  char    *s;
  Target **p;
  int      len, c, first;

  switch (c = nextc()) {
    case '@':
      if (!tp || !tp->target)
        return;
      push(FTEXPAN, tp->target);
      break;
    case '<':
      if (!tp || !tp->req)
        return;
      push(FTEXPAN, tp->req);
      break;
    case '*':
      if (!tp || !tp->target)
        return;
      s = strrchr(tp->target, '.');
      if (!s) {
        push(FTEXPAN, tp->target);
        return;
      }

      len = s - tp->target;
      s   = emalloc(len + 1);
      memcpy(s, tp->target, len);
      s[len] = '\0';
      push(FTEXPAN, s);
      free(s);
      break;
    case '?':
      if (!tp)
        return;

      if (tp->req && stamp(tp->req) > tp->stamp) {
        push(FTEXPAN, " ");
        push(FTEXPAN, tp->req);
      }

      for (p = tp->deps; p && *p; ++p) {
        if (stamp((*p)->name) > tp->stamp) {
          push(FTEXPAN, " ");
          push(FTEXPAN, (*p)->name);
        }
      }
      break;
    case '>':
    case '^':
      if (!tp)
        return;
      first = 1;
      if (tp->req) {
        push(FTEXPAN, tp->req);
        first = 0;
      }
      for (p = tp->deps; p && *p; ++p) {
        if (!first)
          push(FTEXPAN, " ");
        push(FTEXPAN, (*p)->name);
        first = 0;
      }
      break;
    default:
      token[0] = c;
      token[1] = '\0';
      s        = expandmacro(token);
      push(FTEXPAN, s);
      free(s);
      break;
  }
}

static int
internal(int ch)
{
  switch (ch) {
    case '@':
    case '?':
    case '*':
    case '<':
    case '>':
    case '^':
      return 1;
    default:
      return 0;
  }
}

static void
expansion(Target *tp)
{
  int   delim, c, repli, toi, namei, st;
  char  name[MAXTOKEN], repl[MAXREPL], to[MAXREPL];
  char *s, *erepl;

  c = nextc();
  if (c == '(')
    delim = ')';
  else if (c == '{')
    delim = '}';
  else
    delim = 0;

  if (!delim) {
    back(c);
    expandsimple(tp);
    return;
  }

  s     = NULL;
  namei = repli = toi = 0;
  st                  = STBEGIN;

  while (st != STEND && (c = nextc()) != EOF) {
    switch (st) {
      case STBEGIN:
        if (c == ':') {
          st          = STREPLACE;
          name[namei] = '\0';
          s           = expandmacro(name);
          break;
        }
        if (c == delim) {
          name[namei] = '\0';
          s           = expandmacro(name);
          goto no_replace;
        }
        if (namei == MAXTOKEN - 1)
          error("expansion text too long");

        if (namei == 0 && internal(c)) {
          name[namei++] = '$';
          name[namei++] = c;
          name[namei]   = '\0';
          st            = STINTERNAL;
          s             = expandstring(name, tp, getloc());
          break;
        }

        if (!validchar(c))
          error("invalid macro name in expansion");
        name[namei++] = c;
        break;
      case STINTERNAL:
        if (c == delim)
          goto no_replace;
        if (c != ':')
          error("invalid internal macro in expansion");
        st = STREPLACE;
        break;
      case STREPLACE:
        if (c == '=') {
          st = STTO;
          break;
        }
        if (c == delim)
          error(
              "invalid replacement pattern in "
              "expansion"
          );
        if (repli == MAXREPL - 1)
          error("macro replacement too big");
        repl[repli++] = c;
        break;
      case STTO:
        if (c == delim) {
          st = STEND;
          break;
        }

        if (toi == MAXREPL - 1)
          error("macro substiturion too big");
        to[toi++] = c;
        break;
    }
  }

  if (c == EOF)
    error("found eof while parsing expansion");

  repl[repli] = '\0';
  to[toi]     = '\0';

  erepl = expandstring(repl, tp, getloc());
  replace(s, erepl, to);

  free(erepl);
  free(s);
  return;

no_replace:
  push(FTEXPAN, s);
  free(s);
}

/*
 * horrible hack to do string expansion
 * we cannot use normal push and nextc because that
 * would consume characters of the current file too
 * for that reason it cleans the input and it recovers
 * it later
 */
char *
expandstring(char *line, Target *tp, struct loc *loc)
{
  int           c, n;
  char         *s;
  struct input *ip = input;

  input = NULL;
  push(FTFILE, NULL, loc->fname, loc->lineno);
  push(FTEXPAN, line);

  n = 0;
  s = NULL;
  while ((c = nextc()) != EOF) {
    if (c != '$') {
      s        = erealloc(s, ++n);
      s[n - 1] = c;
      continue;
    }

    if ((c = nextc()) == '$') {
      s        = erealloc(s, n += 2);
      s[n - 2] = '$';
      s[n - 1] = '$';
    } else {
      back(c);
      expansion(tp);
    }
  }

  s     = erealloc(s, n + 1);
  s[n]  = '\0';
  input = ip;

  return s;
}

static int
item(void)
{
  int   c;
  char *s;
  char  buf[MAXTOKEN];

  for (s = buf; s < &buf[MAXTOKEN] - 1;) {
    c = nextc();
    if (c == '$' && ahead() != '$')
      expansion(NULL);
    else if (validchar(c))
      *s++ = c;
    else
      break;
  }
  back(c);

  if (s >= &buf[MAXTOKEN] - 1)
    error("token too long");
  if (s == buf)
    error("invalid empty token");
  *s++ = '\0';
  memcpy(token, buf, s - buf);

  return ITEM;
}

static int
next(void)
{
  int c;

repeat:
  /*
 * it is better to avoid skipspaces() here, because
 * it can generate the need for 2 calls to back(),
 * and we need the character anyway
 */
  c = nextc();
  if (c == ' ' || c == '\t')
    goto repeat;

  if (c == '\\') {
    if ((c = nextc()) == '\n')
      goto repeat;
    back(c);
    c = '\\';
  }

  switch (c) {
    case EOF:
      strcpy(token, "<EOF>");
      tok = EOF;
      break;
    case '$':
      if ((c = nextc()) == '$')
        goto single;
      back(c);
      expansion(NULL);
      goto repeat;
    case '#':
      comment();
      c = '\n';
      /* fallthrough */
    case ';':
    case ':':
    case '=':
    case '\n':
    single:
      token[0] = c;
      token[1] = '\0';
      tok      = c;
      break;
    case '+':
      if (nextc() == '=') {
        token[0] = '+';
        token[1] = '=';
        token[2] = '\0';
        tok      = '+';
        break;
      }
      error("unexpected character '+'");
    case '!':
      if (nextc() == '=') {
        token[0] = '!';
        token[1] = '=';
        token[2] = '\0';
        tok      = '!';
        break;
      }
      error("unexpected character '!'");
    default:
      if (!validchar(c))
        error("unexpected character '%c'", c);
      back(c);
      tok = item();
      break;
  }

  return tok;
}

static char *
readmacrodef(void)
{
  int   n, c;
  char *line;

  n    = 0;
  line = NULL;
  while ((c = nextc()) != EOF) {
    line = erealloc(line, n + 1);
    if (c == '\n')
      break;
    if (c == '#') {
      comment();
      break;
    }
    if (c == '\\') {
      if ((c = nextc()) != '\n') {
        back(c);
        c = '\\';
      } else {
        skipspaces();
        c = ' ';
      }
    }

    line[n++] = c;
  }
  if (c == EOF)
    error("EOF while looking for end of line");
  line[n] = '\0';

  return line;
}

static struct action
readcmd(void)
{
  int           n, c;
  struct loc   *loc;
  struct action act;

  skipspaces();

  loc            = getloc();
  act.loc.fname  = estrdup(loc->fname);
  act.loc.lineno = loc->lineno;

  n        = 0;
  act.line = NULL;
  while ((c = nextc()) != EOF) {
    act.line = erealloc(act.line, n + 1);
    if (c == '\n')
      break;
    if (c == '\\') {
      if ((c = nextc()) == '\n') {
        if ((c = nextc()) != '\t')
          back(c);
        continue;
      }
      back(c);
      c = '\\';
    }
    act.line[n++] = c;
  }
  if (c == EOF)
    error("EOF while looking for end of command");
  act.line[n] = '\0';

  return act;
}

static void
rule(char *targets[], int ntargets)
{
  int            c, i, j, ndeps, nactions;
  struct action *acts;
  char         **deps = NULL;

  if (ntargets == 0)
    error("missing target");

  for (ndeps = 0; next() == ITEM; ++ndeps) {
    deps        = erealloc(deps, (ndeps + 1) * sizeof(char *));
    deps[ndeps] = estrdup(token);
  }

  if (tok != '\n' && tok != ';')
    error("garbage at the end of the line");

  nactions = 0;
  acts     = NULL;
  if (tok == ';') {
    nactions++;
    acts               = erealloc(acts, nactions * sizeof(*acts));
    acts[nactions - 1] = readcmd();
  }

  for (;;) {
    if ((c = nextc()) == '#') {
      comment();
      continue;
    }
    if (c != '\t')
      break;
    nactions++;
    acts               = erealloc(acts, nactions * sizeof(*acts));
    acts[nactions - 1] = readcmd();
  }
  back(c);

  for (i = 0; i < ntargets; i++) {
    addtarget(targets[i], ndeps);
    for (j = 0; j < ndeps; j++)
      adddep(targets[i], deps[j]);
    if (nactions > 0)
      addrule(targets[i], acts, nactions);
  }

  for (i = 0; i < ndeps; i++)
    free(deps[i]);
  free(deps);

  for (i = 0; i < nactions; i++) {
    free(acts[i].line);
    freeloc(&acts[i].loc);
  }
  free(acts);
}

static void
assign(char *macros[], int where, int n)
{
  char *defs;

  if (n != 1)
    error("invalid macro definition");

  skipspaces();
  defs = readmacrodef();
  setmacro(*macros, defs, where, NOEXPORT);
  free(defs);
}

static void
assign_append(char *macros[], int where, int n)
{
  char  *defs, *old, *new;
  size_t olen, dlen;

  if (n != 1)
    error("invalid macro definition");

  skipspaces();
  defs = readmacrodef();
  old  = getmacro(*macros);
  olen = strlen(old);
  dlen = strlen(defs);
  /* old + " " + defs + '\0' */
  new = emalloc(olen + (olen > 0 ? 1 : 0) + dlen + 1);
  memcpy(new, old, olen);
  if (olen > 0)
    new[olen++] = ' ';
  memcpy(new + olen, defs, dlen);
  new[olen + dlen] = '\0';
  setmacro(*macros, new, where, NOEXPORT);
  free(new);
  free(defs);
}

static void
assign_shell(char *macros[], int where, int n)
{
  char  *cmd, *out;
  FILE  *p;
  size_t cap, len;
  int    c;

  if (n != 1)
    error("invalid macro definition");

  skipspaces();
  cmd = readmacrodef();
  {
    char *expanded = expandstring(cmd, NULL, getloc());
    free(cmd);
    cmd = expanded;
  }
  p = wpopen(cmd, NULL, "r");
  if (!p)
    error("popen: %s", strerror(errno));
  cap = 64;
  out = emalloc(cap);
  len = 0;
  while ((c = getc(p)) != EOF) {
    if (len + 1 >= cap) {
      cap *= 2;
      out = erealloc(out, cap);
    }
    if (c == '\n')
      break;
    out[len++] = c;
  }
  out[len] = '\0';
  wpclose(p);
  setmacro(*macros, out, where, NOEXPORT);
  free(out);
  free(cmd);
}

void
parseinput(int where)
{
  int    i, n;
  char **targets;

  while (moreinput()) {
    n       = 0;
    targets = NULL;

    next();
    if (tok == '\n')
      continue;

    while (tok == ITEM) {
      n++;
      targets        = erealloc(targets, n * sizeof(char *));
      targets[n - 1] = estrdup(token);
      next();
    }

    switch (tok) {
      case ':':
        rule(targets, n);
        break;
      case '=':
        assign(targets, where, n);
        break;
      case '+':
        assign_append(targets, where, n);
        break;
      case '!':
        assign_shell(targets, where, n);
        break;
      default:
        error("unexpected token '%s'(%d)", token, tok);
    }

    for (i = 0; i < n; i++)
      free(targets[i]);
    free(targets);
  }
}

int
parse(char *fname)
{
  FILE *fp;

  if (!fname) {
    fp    = stdin;
    fname = "<stdin>";
  } else if ((fp = fopen(fname, "r")) == NULL) {
    return 0;
  }

  debug("parsing %s", fname);
  push(FTFILE, fp, fname, 0);
  parseinput(MAKEFILE);

  return 1;
}

void
inject(char *s)
{
  push(FTFILE, NULL, "<internal>", 0);
  push(FTEXPAN, s);
  parseinput(INTERNAL);
}
