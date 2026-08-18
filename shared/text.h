/* see LICENSE file for copyright and license details */

#include <stddef.h>
#include <stdio.h>
#include <sys/types.h>

struct line {
  char  *data;
  size_t len;
};

struct linebuf {
  struct line *lines;
  size_t       nlines;
  size_t       capacity;
};
#define EMPTY_LINEBUF                                                                              \
  {                                                                                                \
      NULL,                                                                                        \
      0,                                                                                           \
      0,                                                                                           \
  }
void getlines(FILE *, struct linebuf *);

int linecmp(struct line *, struct line *);

ssize_t agetline(char **, size_t *, FILE *);
void    fconcat(FILE *, const char *, FILE *, const char *);
