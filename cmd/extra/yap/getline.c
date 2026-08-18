/* copyright (c) 1985 ceriel J.H. jacobs */

#include "getline.h"
#include "assert.h"
#include "display.h"
#include "in_all.h"
#include "main.h"
#include "options.h"
#include "output.h"
#include "process.h"
#include "prompt.h"
#include "term.h"

#define BLOCKSIZE 2048 /* size of blocks */
#define CHUNK     50   /* # of blockheaders allocated at a time */

/*
 * blocks are kept in an array in line-number order. each block stores the raw
 * text and the offsets of the lines parsed from it
 */

struct block {
  int b_flags;    /* contains the following flags: */
#define PARTLY 02 /* block not filled completely (eof) */
  long  b_end;    /* line number of last line in block */
  char *b_info;   /* the block */
  int  *b_offs;   /* line offsets within the block */
};

static struct block *blocklist, /* beginning of the list of blocks */
    *maxblocklist,              /* first free entry in the list */
    *topblocklist;              /* end of allocated core for the list */
static long lastreadline;       /* lineno of last line read */
static int  ENDseen;

static void          nextblock(struct block *pblock);
static char         *re_alloc(char *ptr, unsigned newsize);
static struct block *getblock(long n, int disable_interrupt);

static struct block *
new_block()
{
  struct block *pblock = maxblocklist - 1;

  if (!maxblocklist || !(pblock->b_flags & PARTLY)) {
    /*
 * there is no last block, or it was filled completely,
 * so allocate a new blockheader
 */
    int siz;

    pblock = blocklist;
    if (maxblocklist == topblocklist) {
      /*
 * no blockheaders left. allocate new ones
 */
      siz       = topblocklist - pblock;
      blocklist = pblock =
          (struct block *)re_alloc((char *)pblock, (unsigned)((siz + CHUNK) * sizeof(*pblock)));
      pblock += siz;
      topblocklist = pblock + CHUNK;
      maxblocklist = pblock;
      for (; pblock < topblocklist; pblock++) {
        pblock->b_end   = 0;
        pblock->b_info  = 0;
        pblock->b_flags = 0;
      }
      if (!siz) {
        /*
 * create dummy header cell
 */
        maxblocklist++;
      }
    }
    pblock = maxblocklist++;
  }
  nextblock(pblock);
  return pblock;
}

/*
 * return the block in which line 'n' of the current file can be found
 * if "disable_interrupt" = 0, the call may be interrupted, in which
 * case it returns 0
 */

static struct block *
getblock(long n, int disable_interrupt)
{
  struct block *pblock;

  if (stdf < 0) {
    /*
 * not file descriptor, so return end of file
 */
    return 0;
  }
  pblock = maxblocklist - 1;
  if (n < lastreadline || (n == lastreadline && !(pblock->b_flags & PARTLY))) {
    /*
 * the line asked for has been read already
 * perform binary search in the blocklist to find the block
 * where its in
 */
    struct block *min, *mid;

    min = blocklist + 1;
    do {
      mid = min + (pblock - min) / 2;
      if (n > mid->b_end) {
        min = mid + 1;
      } else
        pblock = mid;
    } while (min < pblock);
    /* found, pblock is now a reference to the block wanted */
    return pblock;
  }

  /*
 * the line was'nt read yet, so read blocks until found
 */
  for (;;) {
    if (interrupt && !disable_interrupt)
      return 0;
    pblock = new_block();
    if (pblock->b_end >= n) {
      return pblock;
    }
    if (pblock->b_flags & PARTLY) {
      /*
 * we did not find it, and the last block could not be
 * read completely, so return 0;
 */
      return 0;
    }
  }
  /* NOTREACHED */
}

char *
getline(long n, int disable_interrupt)
{
  struct block *pblock;

  if (!(pblock = getblock(n, disable_interrupt))) {
    return (char *)0;
  }
  return pblock->b_info + pblock->b_offs[n - ((pblock - 1)->b_end + 1)];
}

/*
 * find the last line of the input, and return its number
 */

long
to_lastline()
{
  for (;;) {
    if (!getline(lastreadline + 1, 0)) {
      /*
 * "lastreadline" always contains the linenumber of
 * the last line read. so, if the call to getline
 * succeeds, "lastreadline" is affected
 */
      if (interrupt)
        return -1L;
      return lastreadline;
    }
  }
  /* NOTREACHED */
}

char *
alloc(unsigned size)
{
  char *pmem;

  pmem = malloc(size);
  if (!pmem && size != 0) {
    panic("No core");
  }
  return pmem;
}

/*
 * re-allocate the memorychunk pointed to by ptr, to let it
 * grow or shrink
 */

static char *
re_alloc(char *ptr, unsigned newsize)
{
  char *pmem;

  pmem = realloc(ptr, newsize);
  if (!pmem && newsize != 0) {
    panic("No core");
  }
  return pmem;
}

static char *saved;
static long  filldegree;

/*
 * try to read the block indicated by pblock
 */

static void
nextblock(struct block *pblock)
{
  char *c,                   /* run through pblock->b_info */
      *c1;                   /* indicate end of pblock->b_info */
  int            *poff;      /* pointer in line-offset list */
  int             cnt;       /* # of characters read */
  unsigned        siz;       /* size of allocated line-offset list */
  static unsigned savedsiz;  /* saved "siz" */
  static int     *savedpoff; /* saved "poff" */
  static char    *savedc1;   /* saved "c1" */

  if (pblock->b_flags & PARTLY) {
    /*
 * the block was already partly filled. initialize locals
 * accordingly
 */
    poff            = savedpoff;
    siz             = savedsiz;
    pblock->b_flags = 0;
    c1              = savedc1;
    if (c1 == pblock->b_info || *(c1 - 1)) {
      /*
 * we had incremented "lastreadline" temporarily,
 * because the last line could not be completely read
 * last time we tried. undo this increment
 */
      poff--;
      --lastreadline;
    }
  } else {
    if (saved) {
      /*
 * there were leftovers from the previous block
 */
      pblock->b_info = saved;
      c1             = savedc1;
      saved          = 0;
    } else { /* allocate new block */
      pblock->b_info = c1 = alloc(BLOCKSIZE + 1);
    }
    /*
 * allocate some space for line-offsets
 */
    pblock->b_offs = poff = (int *)alloc((unsigned)(100 * sizeof(int)));
    siz                   = 99;
    *poff++               = 0;
  }
  c = c1;
  for (;;) {
    /*
 * read loop
 */
    cnt = read(stdf, c1, BLOCKSIZE - (c1 - pblock->b_info));
    if (cnt < 0) {
      /*
 * interrupted read
 */
      if (errno == EINTR)
        continue;
      error("Could not read input file");
      cnt = 0;
    }
    c1 += cnt;
    if (c1 != pblock->b_info + BLOCKSIZE) {
      ENDseen = 1;
      pblock->b_flags |= PARTLY;
    }
    break;
  }
  assert(c <= c1);
  while (c < c1) {
    /*
 * now process the block
 */
    if (*c == '\n') {
      /*
 * newlines are replaced by '\0', so that "getline"
 * can deliver one line at a time
 */
      *c = 0;
      lastreadline++;
      /*
 * remember the line-offset
 */
      if (poff == pblock->b_offs + siz) {
        /*
 * no space for it, allocate some more
 */
        pblock->b_offs =
            (int *)re_alloc((char *)pblock->b_offs, (unsigned)((siz + 51) * sizeof(int)));
        poff = pblock->b_offs + siz;
        siz += 50;
      }
      *poff++ = c - pblock->b_info + 1;
    }
    c++;
  }
  assert(c == c1);
  *c = 0;
  if (c != pblock->b_info && *(c - 1) != 0) {
    /*
 * the last line read does not end with a newline, so add one
 */
    lastreadline++;
    *poff++ = c - pblock->b_info + 1;
    if (!(pblock->b_flags & PARTLY) && *(poff - 2) != 0) {
      /*
 * save the started line; it will be in the next block
 * remove the newline we added just now
 */
      saved = c1 = alloc(BLOCKSIZE + 1);
      c          = pblock->b_info + *(--poff - 1);
      while (*c)
        *c1++ = *c++;
      c       = pblock->b_info + *(poff - 1);
      savedc1 = c1;
      --lastreadline;
    }
  }
  pblock->b_end = lastreadline;
  if (pblock->b_flags & PARTLY) {
    /*
 * take care, that we can call "nextblock" again, to fill in
 * the rest of this block
 */
    savedsiz  = siz;
    savedpoff = poff;
    savedc1   = c;
    if (c == pblock->b_info) {
      lastreadline++;
      pblock->b_end = 0;
    }
  } else {
    cnt        = pblock - blocklist;
    filldegree = ((c - pblock->b_info) + (cnt - 1) * filldegree) / cnt;
  }
  assert(pblock->b_end - (pblock - 1)->b_end <= poff - pblock->b_offs);
}

/*
 * called after processing a file
 * free all core
 */

void
do_clean()
{
  struct block *pblock;
  char         *p;

  for (pblock = blocklist; pblock < maxblocklist; pblock++) {
    if ((p = pblock->b_info) != 0) {
      free(p);
      free((char *)pblock->b_offs);
    }
  }
  if ((p = (char *)blocklist) != 0) {
    free(p);
  }
  blocklist    = 0;
  maxblocklist = 0;
  topblocklist = 0;
  lastreadline = 0;
  filldegree   = 0;
  ENDseen      = 0;
  if ((p = saved) != 0)
    free(p);
  saved = 0;
}

/*
 * get a character. if possible, do some workahead
 */

int
getch()
{
  int         flags, bytes_ready, bytes_read;
  struct stat buf;
  char        c;

  flush();
  if (startcomm) {
    /*
 * command line option command
 */
    if (*startcomm)
      return *startcomm++;
    return '\n';
  }
  if (stdf >= 0) {
    /*
 * make reads from the terminal non-blocking, so that
 * we can see if the user typed something
 */
    flags = fcntl(0, F_GETFL, 0);
    if (flags != -1 && fcntl(0, F_SETFL, flags | O_NONBLOCK) != -1) {
      bytes_read = 0;
      while (!ENDseen &&
			       ((bytes_read = read(0, &c, 1)) == 0
#ifdef EWOULDBLOCK
			        || (bytes_read < 0 && errno == EWOULDBLOCK)
#endif
#ifdef EAGAIN
			        || (bytes_read < 0 && errno == EAGAIN)
#endif
			            ) &&
			       (nopipe ||
			        (fstat(stdf, &buf) >= 0 && buf.st_size > 0))) {
        /*
 * do some read ahead, after making sure there
 * is input and the user did not type a command
 */
        new_block();
      }
      (void)fcntl(0, F_SETFL, flags);
      if (bytes_read < 0) {
        /*
 * could this have happened?
 * i'm not sure, because the read is
 * nonblocking. can it be interrupted then?
 */
        return -1;
      }
      if (bytes_read > 0)
        return c & 0x7f;
    }
  }
  if (ioctl(0, FIONREAD, (char *)&bytes_ready) >= 0 && stdf >= 0) {
    while (!ENDseen && bytes_ready == 0
           && (nopipe || (fstat(stdf, &buf) >= 0 && buf.st_size > 0))) {
      if (interrupt)
        return -1;
      new_block();
      if (ioctl(0, FIONREAD, (char *)&bytes_ready) < 0) {
        break;
      }
    }
  }
  if (read(0, &c, 1) <= 0)
    return -1;
  return c & 0x7f;
}

/*
 * get the position of line "ln" in the file
 */

long
getpos(long ln)
{
  struct block *pblock;
  long          i;

  pblock = getblock(ln, 1);
  assert(pblock != 0);
  i = filldegree * (pblock - blocklist);
  return i - (filldegree - pblock->b_offs[ln - (pblock - 1)->b_end]);
}
