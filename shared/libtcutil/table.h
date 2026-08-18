/* interned names and open-addressing hash table */

#ifndef ARUU_TCUTIL_TABLE_H
#define ARUU_TCUTIL_TABLE_H

#include <stdint.h> /* uint32_t */

/* interned name: chars and hash are immutable once allocated
 * equality is pointer identity (see equal_name) */
struct Name {
  const char *chars;
  int         bytes;
  uint32_t    hash;
};

const struct Name *alloc_name(const char *begin, const char *end, int make_copy);
const struct Name *alloc_cname(const char *cstr);
int                equal_name(const struct Name *name1, const struct Name *name2);

/* for printf: printf("%.*s\n", names(name)) */
#define NAMES(name) ((name)->bytes), ((name)->chars)

/* open-addressing hash table with linear probing */
struct TableEntry {
  const struct Name *key;
  void              *value;
};

struct Table {
  struct TableEntry *entries;
  int                capacity;
  int                count;
  int                used; /* includes tombstones */
};

struct Table *alloc_table(void);
void          table_init(struct Table *table);
void         *table_get(struct Table *table, const struct Name *key);
int           table_try_get(struct Table *table, const struct Name *key, void **output);
int           table_put(struct Table *table, const struct Name *key, void *value);
int           table_delete(struct Table *table, const struct Name *key);
int           table_iterate(
    const struct Table *table, int iterator, const struct Name **name, void **value
); /* returns -1 at end */

#endif /* ARUU_TCUTIL_TABLE_H */
