#include "table.h"

#include <assert.h>
#include <stdlib.h> /* malloc */
#include <string.h>

/* fnv-1a */
static uint32_t
hash_string(const char *key, int length)
{
  const unsigned char *u    = (const unsigned char *)key;
  uint32_t             hash = 2166136261u;
  int                  i;

  for (i = 0; i < length; ++i)
    hash = (hash ^ u[i]) * 16777619u;
  return hash;
}

/* struct name: global interning table */

static struct Table name_table;

static const struct Name *
find_name_table(const char *chars, int bytes, uint32_t hash)
{
  const struct Table *table;
  uint32_t            index;
  struct TableEntry  *entry;
  const struct Name  *key;

  table = &name_table;
  if (table->count == 0)
    return NULL;

  for (index = hash % table->capacity;; index = (index + 1) % table->capacity) {
    entry = &table->entries[index];
    key   = entry->key;
    if (key == NULL) {
      if (entry->value == NULL)
        return NULL;
    } else if (key->bytes == bytes && key->hash == hash && memcmp(key->chars, chars, bytes) == 0) {
      return key;
    }
  }
}

const struct Name *
alloc_name(const char *begin, const char *end, int make_copy)
{
  int                bytes;
  uint32_t           hash;
  const struct Name *name;
  char              *new_str;
  struct Name       *new_name;

  bytes = end != NULL ? (int)(end - begin) : (int)strlen(begin);
  hash  = hash_string(begin, bytes);
  name  = find_name_table(begin, bytes, hash);
  if (name == NULL) {
    if (make_copy) {
      new_str = malloc(bytes);
      if (new_str == NULL)
        return NULL;
      memcpy(new_str, begin, bytes);
      begin = new_str;
    }
    new_name = malloc(sizeof(*new_name));
    if (new_name == NULL) {
      if (make_copy)
        free((void *)begin);
    } else {
      new_name->chars = begin;
      new_name->bytes = bytes;
      new_name->hash  = hash;
      table_put(&name_table, new_name, new_name);
      name = new_name;
    }
  }
  return name;
}

const struct Name *
alloc_cname(const char *cstr)
{
  return alloc_name(cstr, NULL, 0);
}

int
equal_name(const struct Name *name1, const struct Name *name2)
{
  /* all names are interned, compare by pointer */
  return name1 == name2;
}

/* struct table: open-addressing hash table */

static struct TableEntry *
find_entry(struct TableEntry *entries, int capacity, const struct Name *key)
{
  struct TableEntry *tombstone;
  uint32_t           index;
  struct TableEntry *entry;

  tombstone = NULL;
  for (index = key->hash % capacity;; index = (index + 1) % capacity) {
    entry = &entries[index];
    if (entry->key == NULL) {
      if (entry->value == NULL)
        return tombstone != NULL ? tombstone : entry;
      /* tombstone */
      if (tombstone == NULL)
        tombstone = entry;
    } else if (entry->key == key) {
      return entry;
    }
  }
}

static void
adjust_capacity(struct Table *table, int new_capacity)
{
  struct TableEntry *new_entries;
  int                i;
  struct TableEntry *old_entries;
  int                old_capacity;
  int                new_count;

  new_entries = malloc(sizeof(struct TableEntry) * new_capacity);
  if (new_entries == NULL)
    return;
  for (i = 0; i < new_capacity; ++i) {
    new_entries[i].key   = NULL;
    new_entries[i].value = NULL;
  }

  old_entries  = table->entries;
  old_capacity = table->capacity;
  new_count    = 0;
  for (i = 0; i < old_capacity; ++i) {
    struct TableEntry *entry;
    struct TableEntry *dest;

    entry = &old_entries[i];
    if (entry->key == NULL)
      continue;

    dest        = find_entry(new_entries, new_capacity, entry->key);
    dest->key   = entry->key;
    dest->value = entry->value;
    ++new_count;
  }

  free(old_entries);
  table->entries  = new_entries;
  table->capacity = new_capacity;
  table->count = table->used = new_count;
}

struct Table *
alloc_table(void)
{
  struct Table *table = malloc(sizeof(*table));
  if (table != NULL)
    table_init(table);
  return table;
}

void
table_init(struct Table *table)
{
  assert(table != NULL);
  table->entries  = NULL;
  table->count    = 0;
  table->used     = 0;
  table->capacity = 0;
}

void *
table_get(struct Table *table, const struct Name *key)
{
  struct TableEntry *entry;

  assert(table != NULL);
  if (table->count == 0)
    return NULL;

  entry = find_entry(table->entries, table->capacity, key);
  if (entry->key == NULL)
    return NULL;

  return entry->value;
}

int
table_try_get(struct Table *table, const struct Name *key, void **output)
{
  struct TableEntry *entry;

  assert(table != NULL);
  if (table->count == 0)
    return 0;

  entry = find_entry(table->entries, table->capacity, key);
  if (entry->key == NULL)
    return 0;

  if (output != NULL)
    *output = entry->value;
  return 1;
}

int
table_put(struct Table *table, const struct Name *key, void *value)
{
  const int          min_capacity = 15;
  int                capacity;
  struct TableEntry *entry;
  int                is_new_key;

  assert(table != NULL);
  if (table->used >= table->capacity / 2) {
    capacity = table->capacity * 2 - 1; /* keep odd */
    if (capacity < min_capacity)
      capacity = min_capacity;
    adjust_capacity(table, capacity);
  }

  entry      = find_entry(table->entries, table->capacity, key);
  is_new_key = entry->key == NULL;
  if (is_new_key) {
    ++table->count;
    if (entry->value == NULL)
      ++table->used;
  }

  entry->key   = key;
  entry->value = value;
  return is_new_key;
}

int
table_delete(struct Table *table, const struct Name *key)
{
  struct TableEntry *entry;

  assert(table != NULL);
  if (table->count == 0)
    return 0;

  entry = find_entry(table->entries, table->capacity, key);
  if (entry->key == NULL)
    return 0;

  --table->count;
  /* mark as tombstone */
  entry->key   = NULL;
  entry->value = entry;

  return 1;
}

int
table_iterate(const struct Table *table, int iterator, const struct Name **pkey, void **pvalue)
{
  int                      capacity;
  const struct TableEntry *entry;
  const struct Name       *key;

  assert(table != NULL);
  capacity = table->capacity;
  for (; iterator < capacity; ++iterator) {
    entry = &table->entries[iterator];
    key   = entry->key;
    if (key != NULL) {
      if (pkey != NULL)
        *pkey = key;
      if (pvalue != NULL)
        *pvalue = entry->value;
      return iterator + 1;
    }
  }
  return -1;
}
