#pragma once

#include <stddef.h> /* size_t */

#include "table.h"

/* string table */
struct Strtab {
  struct Table offsets;
  size_t       size;
};

void   strtab_init(struct Strtab *strtab);
size_t strtab_add(struct Strtab *strtab, const struct Name *name);
void  *strtab_dump(const struct Strtab *strtab);
