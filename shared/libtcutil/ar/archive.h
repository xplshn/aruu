/* ar archive reading: symbol table and member extraction */

#ifndef ARUU_TCUTIL_ARCHIVE_H
#define ARUU_TCUTIL_ARCHIVE_H

#include <stdint.h>
#include <stdio.h>

#include "../table.h"

struct Vector;

struct ArContent {
  void    *obj;
  size_t   size;
  uint32_t file_offset;
  char     name[16];
};

struct ArSymbol {
  struct ArContent *content;
};

struct Archive {
  FILE            *fp;
  uint32_t         symbol_count;
  struct ArSymbol *symbols;
  struct Table     symbol_table;
  struct Vector   *contents; /* <struct arcontent*> */
};

struct Archive *load_archive(const char *filename);
void           *load_archive_content(
    struct Archive *ar, struct ArSymbol *symbol, void *(*load)(FILE *, const char *, size_t)
);

#endif /* ARUU_TCUTIL_ARCHIVE_H */
