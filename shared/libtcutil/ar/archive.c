#include "archive.h"

#include <ar.h>
#include <assert.h>
#include <stdlib.h> /* strtoul */
#include <string.h>

#include "../tcutil.h"

/* read 4-byte big-endian uint32 */
static uint32_t
read4be(FILE *fp)
{
  unsigned char buf[4];
  read_or_die(fp, buf, -1, sizeof(buf), "read4be");
  return (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
}

static uint32_t *
read_file_offsets(FILE *fp, uint32_t symbol_count)
{
  uint32_t      *file_offsets;
  uint32_t      *p;
  uint32_t       i;
  unsigned char *q;
  uint32_t       offset;

  file_offsets = malloc_or_die(sizeof(*file_offsets) * symbol_count);
  read_or_die(fp, file_offsets, -1, sizeof(*file_offsets) * symbol_count, "Offsets");
  /* convert big endian to machine endian */
  p = file_offsets;
  for (i = 0; i < symbol_count; ++i) {
    q      = (unsigned char *)p;
    offset = (q[0] << 24) | (q[1] << 16) | (q[2] << 8) | q[3];
    *p++   = offset;
  }
  return file_offsets;
}

static int
compare_uint32(const void *a, const void *b)
{
  uint32_t          x       = *(const uint32_t *)a;
  struct ArContent *content = (struct ArContent *)b;
  uint32_t          y       = content->file_offset;
  return x < y ? -1 : x > y ? 1 : 0;
}

/* build unique sorted array of arcontent from raw file offsets */
static struct ArContent *
allocate_contents_buffer(const uint32_t *file_offsets, uint32_t symbol_count, size_t *plen)
{
  uint32_t         *offsets;
  ssize_t           len, capa;
  uint32_t          i;
  struct ArContent *contents;
  ssize_t           j;

  offsets = NULL;
  len     = 0;
  capa    = 0;

  for (i = 0; i < symbol_count; ++i) {
    uint32_t value = file_offsets[i];
    ssize_t  lo = -1, hi = len;
    ssize_t  m;

    /* binary search for insertion point */
    while (hi - lo > 1) {
      m = lo + ((hi - lo) >> 1);
      if (offsets[m] < value)
        lo = m;
      else
        hi = m;
    }

    if (hi >= len || offsets[hi] != value) {
      if (capa <= len) {
        capa <<= 1;
        if (capa <= 0)
          capa = 8;
        offsets = realloc_or_die(offsets, sizeof(*offsets) * capa);
      }
      memmove(&offsets[hi + 1], &offsets[hi], (len - hi) * sizeof(*offsets));
      offsets[hi] = value;
      ++len;
    }
  }

  contents = calloc_or_die(sizeof(*contents) * len);
  for (j = 0; j < len; ++j) {
    contents[j].obj         = NULL;
    contents[j].file_offset = offsets[j];
  }
  free(offsets);

  *plen = len;
  return contents;
}

struct Archive *
load_archive(const char *filename)
{
  FILE             *fp;
  struct Archive   *ar;
  char              mag[SARMAG];
  struct ar_hdr     ghdr;
  uint32_t          symbol_count;
  uint32_t         *file_offsets;
  size_t            content_count;
  struct ArContent *contents;
  struct ArSymbol  *symbols;
  uint32_t          i;
  size_t            pos;
  size_t            strtablen;
  char             *strtab;
  char             *p;

  if (!is_file(filename) || (fp = fopen(filename, "rb")) == NULL)
    return NULL;

  ar               = calloc_or_die(sizeof(*ar));
  ar->fp           = fp;
  ar->symbol_count = 0;
  ar->symbols      = NULL;
  table_init(&ar->symbol_table);
  ar->contents = new_vector();

  read_or_die(fp, mag, -1, sizeof(mag), "Magic");
  if (memcmp(mag, ARMAG, sizeof(mag)) != 0)
    error("Magic expected");

  read_or_die(fp, &ghdr, -1, sizeof(ghdr), "Global header");
  if (memcmp(ghdr.ar_fmag, ARFMAG, sizeof(ghdr.ar_fmag)) != 0)
    error("FMagic expected");

  symbol_count     = read4be(fp);
  ar->symbol_count = symbol_count;
  if (symbol_count > 0) {
    file_offsets = read_file_offsets(fp, symbol_count);
    contents     = allocate_contents_buffer(file_offsets, symbol_count, &content_count);

    symbols     = malloc_or_die(sizeof(*symbols) * symbol_count);
    ar->symbols = symbols;
    for (i = 0; i < symbol_count; ++i) {
      uint32_t          value = file_offsets[i];
      struct ArContent *result;
      uint32_t          index;

      result = bsearch(&value, contents, content_count, sizeof(*contents), compare_uint32);
      assert(result != NULL);
      index              = result - contents;
      symbols[i].content = &contents[index];
    }

    pos = ftell(fp);
    assert(pos < contents[0].file_offset);
    strtablen = contents[0].file_offset - pos;
    strtab    = malloc_or_die(strtablen); /* freed locally, not stored */
    read_or_die(fp, strtab, -1, strtablen, "struct Strtab");
    p = strtab;
    for (i = 0; i < symbol_count; ++i) {
      char              *q;
      struct ArSymbol   *symbol;
      const struct Name *name;

      q = memchr(p, '\0', &strtab[strtablen] - p);
      if (q == NULL)
        error("Illegal strtab");

      symbol = &symbols[i];
      name   = alloc_name(p, q, 0);
      table_put(&ar->symbol_table, name, symbol);

      p = q + 1;
    }

    free(file_offsets);
  }
  return ar;
}

void *
load_archive_content(
    struct Archive *ar, struct ArSymbol *symbol, void *(*load)(FILE *, const char *, size_t)
)
{
  struct ArContent *content;
  struct ar_hdr     hdr;
  char             *p;
  char              sizestr[sizeof(hdr.ar_size) + 1];
  void             *obj;

  content = symbol->content;
  if (content->obj != NULL)
    return content->obj;

  fseek(ar->fp, content->file_offset, SEEK_SET);

  read_or_die(ar->fp, &hdr, -1, sizeof(hdr), "hdr");
  if (memcmp(hdr.ar_fmag, ARFMAG, sizeof(hdr.ar_fmag)) != 0)
    error("Malformed archive");

  memcpy(content->name, hdr.ar_name, sizeof(hdr.ar_name));
  p = memchr(content->name, '/', sizeof(hdr.ar_name));
  if (p != NULL)
    *p = '\0';

  memcpy(sizestr, hdr.ar_size, sizeof(hdr.ar_size));
  sizestr[sizeof(hdr.ar_size)] = '\0';
  content->size                = strtoul(sizestr, NULL, 10);

  obj = (*load)(ar->fp, content->name, content->size);
  if (obj == NULL)
    error("Failed to extract .o: %.*s", (int)sizeof(content->name), content->name);
  content->obj = obj;
  vec_push(ar->contents, content);

  return obj;
}
