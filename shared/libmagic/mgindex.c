/* see LICENSE file for copyright and license details */
#include "mgint.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

/*
 * what the loaded tests need of a file, worked out once: for each group
 * (a top level test and its continuations) how far into the file it can
 * read, and which mime types it can give. a reach of -1 is not known
 * before the file is read (an indirect offset, one relative to a match,
 * a named test used): such a group sees what the head holds
 */

/* the bytes a regex looks at when its line gives no range, as file(1) */
#define REGEX_REACH MG_REGEX_MAX

static long long
line_reach(const struct MagicLine *l, int from_end)
{
  long long off = l->offset;

  /* the end of the file is read apart from the head */
  if (from_end || (l->flag & MF_OFFNEG))
    return 0;
  if (l->flag & (MF_INDIR | MF_OFFADD | MF_INDIROFFADD))
    return -1;
  switch (l->type) {
    case MT_DEFAULT:
    case MT_CLEAR:
    case MT_NAME:
    case MT_OFFSET:
      return 0;
    case MT_INDIRECT:
    case MT_USE:
    case MT_DER:
      return -1;
    case MT_SEARCH:
      return off + (long long)l->range + l->vallen;
    case MT_REGEX:
      if (l->sflags & SF_REGEX_LINES)
        return -1;
      return off + (long long)(l->range ? l->range : REGEX_REACH);
    case MT_PSTRING:
      return off + (long long)mg_pstrlen_size(l->sflags) + MG_STRLEN;
    case MT_STRING:
    case MT_BESTRING16:
    case MT_LESTRING16:
    case MT_OCTAL:
      return off + (long long)l->vallen * (l->type == MT_STRING || l->type == MT_OCTAL ? 1 : 2);
    default:
      return off + (long long)mg_typesize(l->type);
  }
}

static int
by_mime(const void *a, const void *b)
{
  const struct MagicMime *x = a, *y = b;
  int                     c = strcasecmp(x->mime, y->mime);

  if (c)
    return c;
  return (x->group > y->group) - (x->group < y->group);
}

static int
add_mime(struct MagicIndex *ix, size_t *cap, const char *mime, size_t g)
{
  struct MagicMime *more;

  /* a group names a type once, however many of its lines do */
  for (size_t i = ix->nmime; i > 0 && ix->mime[i - 1].group == g; i--)
    if (!strcasecmp(ix->mime[i - 1].mime, mime))
      return 0;
  if (ix->nmime == *cap) {
    *cap = *cap ? *cap * 2 : 1024;
    if (!(more = realloc(ix->mime, *cap * sizeof(*more))))
      return -1;
    ix->mime = more;
  }
  ix->mime[ix->nmime].mime  = mime;
  ix->mime[ix->nmime].group = g;
  ix->nmime++;
  return 0;
}

static int
compile(struct MagicLine *l)
{
  if (l->type != MT_REGEX || l->rx || l->rxbad)
    return 0;
  if (!(l->rx = malloc(sizeof(*l->rx))))
    return -1;
  if (regcomp(l->rx, l->value.s, REG_EXTENDED | REG_NEWLINE | (l->sflags & SF_IGNORE_CASE ? REG_ICASE : 0))) {
    free(l->rx);
    l->rx    = NULL;
    l->rxbad = 1;
  }
  return 0;
}

static int
index_db(struct MagicIndex *ix, struct MagicDb *db, size_t *gcap, size_t *mcap)
{
  struct MagicLine  *v = db->v[0];
  struct MagicGroup *g, *more;
  size_t             i, j, n = db->n[0];
  long long          r;
  int                from_end;

  for (i = 0; i < n; i = j) {
    for (j = i + 1; j < n && v[j].cont; j++)
      ;
    if (ix->ngroup == *gcap) {
      *gcap = *gcap ? *gcap * 2 : 1024;
      if (!(more = realloc(ix->group, *gcap * sizeof(*more))))
        return -1;
      ix->group = more;
    }
    g        = &ix->group[ix->ngroup];
    g->db    = db;
    g->first = i;
    g->n     = j - i;
    g->reach = 0;
    /* continuations of a test at the end read from the end as well */
    from_end = (v[i].flag & MF_OFFNEG) != 0;
    for (size_t k = i; k < j; k++) {
      /* a test from the end and what follows it read within the tail,
       * as far back as the test's own offset */
      if ((v[k].flag & MF_OFFNEG) && (size_t)(uint32_t)v[k].offset + mg_typesize(v[k].type) + v[k].vallen > ix->tail)
        ix->tail = (size_t)(uint32_t)v[k].offset + mg_typesize(v[k].type) + v[k].vallen;
      r = line_reach(&v[k], from_end && !(v[k].flag & MF_OFFPOS));
      if (r < 0 || g->reach < 0)
        g->reach = -1;
      else if (r > g->reach)
        g->reach = r;
      if (v[k].mime[0] && add_mime(ix, mcap, v[k].mime, ix->ngroup) < 0)
        return -1;
    }
    if (g->reach > ix->reach)
      ix->reach = g->reach;
    if (g->reach > MAGIC_CHARSET_MAX) {
      size_t *more = realloc(ix->far, (ix->nfar + 1) * sizeof(*more));

      if (!more)
        return -1;
      ix->far              = more;
      ix->far[ix->nfar++] = ix->ngroup;
    }
    ix->ngroup++;
  }
  return 0;
}

int
magic_prepare(struct Magic *m)
{
  struct MagicIndex *ix;
  struct MagicDb    *db;
  size_t             gcap = 0, mcap = 0;

  if (m->ix)
    return 0;
  if (!(ix = calloc(1, sizeof(*ix))))
    return -1;
  for (db = m->db; db; db = db->next) {
    for (int s = 0; s < MG_SETS; s++)
      for (size_t i = 0; i < db->n[s]; i++)
        if (compile(&db->v[s][i]) < 0)
          goto fail;
    if (index_db(ix, db, &gcap, &mcap) < 0)
      goto fail;
  }
  qsort(ix->mime, ix->nmime, sizeof(*ix->mime), by_mime);
  m->ix = ix;
  return 0;
fail:
  mg_index_free(ix);
  errno = ENOMEM;
  return -1;
}

void
mg_index_free(struct MagicIndex *ix)
{
  if (!ix)
    return;
  free(ix->group);
  free(ix->mime);
  free(ix->far);
  free(ix);
}

/*
 * the groups that can give mime, in the order they are tried, into
 * *out (to free), and how far they reach: -1 when one of them cannot
 * say. 0 groups when none gives it
 */
size_t
mg_index_pick(const struct MagicIndex *ix, const char *mime, size_t **out, long long *reach)
{
  size_t lo = 0, hi = ix->nmime, n = 0, *v;

  *out   = NULL;
  *reach = 0;
  while (lo < hi) {
    size_t mid = (lo + hi) / 2;

    if (strcasecmp(ix->mime[mid].mime, mime) < 0)
      lo = mid + 1;
    else
      hi = mid;
  }
  for (hi = lo; hi < ix->nmime && !strcasecmp(ix->mime[hi].mime, mime); hi++)
    ;
  if (hi == lo || !(v = malloc((hi - lo) * sizeof(*v))))
    return 0;
  for (size_t i = lo; i < hi; i++) {
    const struct MagicGroup *g = &ix->group[ix->mime[i].group];

    v[n++] = ix->mime[i].group;
    if (g->reach < 0 || *reach < 0)
      *reach = -1;
    else if (g->reach > *reach)
      *reach = g->reach;
  }
  *out = v;
  return n;
}

/* the bytes of line l, at most n into the file, into buf from r's file */
static int
read_line(struct MagicRun *r, const struct MagicLine *l, unsigned char *buf, size_t have, size_t n)
{
  long long end = line_reach(l, 0), off = l->offset;

  if (off < (long long)have)
    off = (long long)have;
  if (end <= 0 || off >= end || (size_t)off >= n)
    return 0;
  if ((size_t)end > n)
    end = (long long)n;
  return mg_pread(r, buf + off, (size_t)(end - off), off) < 0 ? -1 : 0;
}

/*
 * the far groups that can still match, into alive: a group whose top
 * level test fails needs nothing more read. those that hold get the
 * bytes their other lines look at read into buf, n long, past the have
 * bytes it holds; the rest of buf is left as it is. how many, or -1
 * when the file cannot be read
 */
size_t
mg_index_probe(struct MagicRun *r, unsigned char *buf, size_t have, size_t n, size_t *alive)
{
  const struct MagicIndex *ix = r->m->ix;
  struct MagicBuf          b = {buf, n, 1};
  size_t                   k = 0;

  for (size_t i = 0; i < ix->nfar; i++) {
    const struct MagicGroup *g = &ix->group[ix->far[i]];
    struct MagicLine        *v = g->db->v[0] + g->first;
    int                      holds;

    if (read_line(r, &v[0], buf, have, n) < 0)
      return (size_t)-1;
    if ((holds = mg_top_holds(r, &v[0], &b)) < 0)
      return (size_t)-1;
    if (!holds)
      continue;
    for (size_t l = 1; l < g->n; l++)
      if (read_line(r, &v[l], buf, have, n) < 0)
        return (size_t)-1;
    alive[k++] = ix->far[i];
  }
  return k;
}

int
magic_knows(const struct Magic *m, const char *mime)
{
  size_t   *pick, n;
  long long reach;

  if (!m->ix || !mime)
    return 0;
  n = mg_index_pick(m->ix, mime, &pick, &reach);
  free(pick);
  return n > 0;
}
