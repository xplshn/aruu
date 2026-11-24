/* see LICENSE file for copyright and license details */
#include "label.h"
#include "gpt.h"
#include "../util.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define MBR_SLOTS 4

static const struct label_type *types_cache;

static void
set_type(struct label_part *p, const char *code)
{
  const struct GptType *t = NULL;
  int                   i;

  for (i = 0; i < gpt_ntypes; i++)
    if (strcasecmp(gpt_types[i].code, code) == 0)
      t = &gpt_types[i];
  memset(p->type, 0, sizeof p->type);
  if (!t)
    return;
  snprintf(p->type, sizeof p->type, "%s", t->code);
  parse_guid_string(t->guid, p->type_guid);
  p->mbr_type = t->mbr;
}

const char *
label_type_name(const struct label_part *p)
{
  int i;

  for (i = 0; i < gpt_ntypes; i++)
    if (p->type[0] && strcasecmp(gpt_types[i].code, p->type) == 0)
      return gpt_types[i].name;
  return "unknown";
}

int
label_types(const struct label_type **out)
{
  static struct label_type table[16];
  int                      i;

  if (!types_cache) {
    for (i = 0; i < gpt_ntypes && i < 16; i++) {
      table[i].code     = gpt_types[i].code;
      table[i].name     = gpt_types[i].name;
      table[i].gpt_only = gpt_types[i].mbr == 0;
    }
    types_cache = table;
  }
  *out = types_cache;
  return gpt_ntypes;
}

static uint64_t
gpt_array_sectors(size_t sec_size)
{
  return (uint64_t)(GPT_ENTRIES * GPT_ENTRY_SZ + sec_size - 1) / sec_size;
}

int
label_init(struct label *l, const struct BlockDev *dev, enum label_kind kind)
{
  memset(l, 0, sizeof *l);
  l->kind     = kind;
  l->sec_size = dev->sec_size;
  l->sectors  = dev->size / dev->sec_size;
  if (kind == LABEL_GPT) {
    uint64_t arr = gpt_array_sectors(l->sec_size);

    /* the primary header and array in front, the backup behind */
    if (l->sectors < 2 + arr + 1 + arr + 1 + LABEL_ALIGN)
      return -1;
    l->first = 2 + arr;
    l->last  = l->sectors - 2 - arr;
    l->slots = LABEL_SLOTS;
    if (gpt_random_guid(l->disk_guid) < 0)
      return -1;
  } else if (kind == LABEL_MBR) {
    if (l->sectors <= LABEL_ALIGN)
      return -1;
    l->first = LABEL_ALIGN;
    l->last  = l->sectors - 1;
    l->slots = MBR_SLOTS;
    {
      uint8_t r[16];

      if (gpt_random_guid(r) < 0)
        return -1;
      memcpy(&l->mbr_id, r, sizeof l->mbr_id);
    }
  }
  return 0;
}

static int
read_gpt(struct BlockDev *dev, struct label *l)
{
  struct GptHeader     hdr;
  struct GptPartition *parts;
  uint32_t             i;
  int                  n = 0;

  if (!(parts = calloc(GPT_ENTRIES, GPT_ENTRY_SZ)))
    return -1;
  if (gpt_read(dev, &hdr, parts) < 0 || hdr.part_size != GPT_ENTRY_SZ || hdr.num_parts > GPT_ENTRIES) {
    free(parts);
    return 0; /* no GPT */
  }
  l->kind     = LABEL_GPT;
  l->slots    = LABEL_SLOTS;
  l->first    = hdr.first_usable;
  l->last     = hdr.last_usable;
  memcpy(l->disk_guid, hdr.disk_guid, 16);
  for (i = 0; i < hdr.num_parts; i++) {
    struct label_part *p = &l->part[i];
    const struct GptType *t;
    int                   c;

    if (parts[i].start_lba == 0 && parts[i].end_lba == 0)
      continue;
    p->used  = 1;
    p->start = parts[i].start_lba;
    p->end   = parts[i].end_lba;
    memcpy(p->type_guid, parts[i].type_guid, 16);
    memcpy(p->guid, parts[i].part_guid, 16);
    if ((t = gpt_type_of(parts[i].type_guid))) {
      snprintf(p->type, sizeof p->type, "%s", t->code);
      p->mbr_type = t->mbr;
    }
    /* names are UTF-16, narrowed: what the installer writes is ASCII */
    for (c = 0; c < 36 && c < (int)sizeof p->name - 1 && parts[i].name[c]; c++)
      p->name[c] = parts[i].name[c] < 128 ? (char)parts[i].name[c] : '?';
    n++;
  }
  free(parts);
  return n >= 0 ? 1 : 0;
}

static int
read_mbr(struct BlockDev *dev, struct label *l)
{
  struct MbrHeader mbr;
  int              i;

  if (blockdev_read(dev, 0, &mbr, 1) < 0)
    return -1;
  if (mbr.sig[0] != 0x55 || mbr.sig[1] != 0xAA)
    return 0;
  /* a protective MBR over a GPT that did not read is no table of its own */
  if (mbr.parts[0].type == 0xEE)
    return 0;
  l->kind  = LABEL_MBR;
  l->slots = MBR_SLOTS;
  l->first = LABEL_ALIGN;
  l->last  = l->sectors - 1;
  memcpy(l->mbr_code, mbr.code, sizeof l->mbr_code);
  memcpy(&l->mbr_id, mbr.code + 440, 4);
  for (i = 0; i < MBR_SLOTS; i++) {
    struct label_part *p = &l->part[i];
    int                 t;

    if (mbr.parts[i].type == 0 || mbr.parts[i].size == 0)
      continue;
    p->used     = 1;
    p->start    = mbr.parts[i].start_lba;
    p->end      = (uint64_t)mbr.parts[i].start_lba + mbr.parts[i].size - 1;
    p->mbr_type = mbr.parts[i].type;
    p->active   = mbr.parts[i].boot == 0x80;
    /* 0x83 stands for both Linux types: the plain one's code, not the home */
    for (t = 0; t < gpt_ntypes; t++) {
      if (gpt_types[t].mbr == p->mbr_type && strcmp(gpt_types[t].code, "8302") != 0) {
        snprintf(p->type, sizeof p->type, "%s", gpt_types[t].code);
        parse_guid_string(gpt_types[t].guid, p->type_guid);
        break;
      }
    }
  }
  return 1;
}

int
label_read(struct BlockDev *dev, struct label *l)
{
  int got;

  memset(l, 0, sizeof *l);
  l->sec_size = dev->sec_size;
  l->sectors  = dev->size / dev->sec_size;
  if ((got = read_gpt(dev, l)) < 0)
    return -1;
  if (got > 0)
    return 0;
  if ((got = read_mbr(dev, l)) < 0)
    return -1;
  return 0;
}

static int
overlaps(const struct label *l, uint64_t start, uint64_t end, int except)
{
  int i;

  for (i = 0; i < l->slots; i++)
    if (i != except && l->part[i].used && start <= l->part[i].end && end >= l->part[i].start)
      return 1;
  return 0;
}

static int
by_start(const void *a, const void *b)
{
  const struct label_part *x = *(const struct label_part *const *)a, *y = *(const struct label_part *const *)b;

  return x->start < y->start ? -1 : x->start > y->start;
}

int
label_gaps(const struct label *l, struct label_gap *out, int max)
{
  const struct label_part *order[LABEL_SLOTS];
  int                      i, n = 0, g = 0;
  uint64_t                 from = l->first;

  if (l->kind == LABEL_NONE)
    return 0;
  for (i = 0; i < l->slots; i++)
    if (l->part[i].used)
      order[n++] = &l->part[i];
  qsort(order, (size_t)n, sizeof order[0], by_start);
  for (i = 0; i <= n && g < max; i++) {
    uint64_t to    = i < n ? order[i]->start : l->last + 1;
    uint64_t start = (from + LABEL_ALIGN - 1) / LABEL_ALIGN * LABEL_ALIGN;

    /* the end is the sector before the next partition, rounded down so the next starts aligned */
    if (to > start && to - start >= LABEL_ALIGN) {
      out[g].start = start;
      out[g].end   = to - 1;
      g++;
    }
    if (i < n && order[i]->end + 1 > from)
      from = order[i]->end + 1;
  }
  return g;
}

int
label_add(struct label *l, uint64_t start, uint64_t sectors, const char *type, const char *name)
{
  struct label_gap   gaps[LABEL_SLOTS + 1];
  struct label_part *p;
  int                n, i, slot = -1, pick = -1;
  uint64_t           end;

  if (l->kind == LABEL_NONE)
    return -EINVAL;
  for (i = 0; i < l->slots && slot < 0; i++)
    if (!l->part[i].used)
      slot = i;
  if (slot < 0)
    return -EEXIST;
  n = label_gaps(l, gaps, LABEL_SLOTS + 1);
  if (start == 0) {
    /* the first gap that holds it, or with no size the biggest */
    for (i = 0; i < n; i++) {
      uint64_t room = gaps[i].end - gaps[i].start + 1;

      if (sectors && room >= sectors) {
        pick = i;
        break;
      }
      if (!sectors && (pick < 0 || room > gaps[pick].end - gaps[pick].start + 1))
        pick = i;
    }
    if (pick < 0)
      return -ENOSPC;
    start = gaps[pick].start;
    end   = sectors ? start + sectors - 1 : gaps[pick].end;
  } else if (sectors) {
    end = start + sectors - 1;
  } else {
    /* to the end of the gap the start is in */
    end = 0;
    for (i = 0; i < n; i++)
      if (start >= gaps[i].start && start <= gaps[i].end)
        end = gaps[i].end;
    if (!end)
      return -EINVAL;
  }
  if (start < l->first || end > l->last || end < start || overlaps(l, start, end, -1))
    return -EINVAL;
  p = &l->part[slot];
  memset(p, 0, sizeof *p);
  p->used  = 1;
  p->start = start;
  p->end   = end;
  set_type(p, type ? type : "8300");
  if (!p->type[0])
    set_type(p, "8300");
  if (name)
    snprintf(p->name, sizeof p->name, "%s", name);
  if (l->kind == LABEL_GPT && gpt_random_guid(p->guid) < 0) {
    memset(p, 0, sizeof *p);
    return -EIO;
  }
  return slot;
}

int
label_delete(struct label *l, int slot)
{
  if (slot < 0 || slot >= l->slots || !l->part[slot].used)
    return -EINVAL;
  memset(&l->part[slot], 0, sizeof l->part[slot]);
  return 0;
}

int
label_set_type(struct label *l, int slot, const char *type)
{
  if (slot < 0 || slot >= l->slots || !l->part[slot].used)
    return -EINVAL;
  set_type(&l->part[slot], type);
  return l->part[slot].type[0] ? 0 : -EINVAL;
}

int
label_set_name(struct label *l, int slot, const char *name)
{
  if (slot < 0 || slot >= l->slots || !l->part[slot].used)
    return -EINVAL;
  snprintf(l->part[slot].name, sizeof l->part[slot].name, "%s", name ? name : "");
  return 0;
}

static int
write_gpt(struct BlockDev *dev, const struct label *l)
{
  struct GptHeader     hdr;
  struct GptPartition *parts;
  uint64_t             arr = gpt_array_sectors(l->sec_size);
  int                  i, c, rc;

  if (!(parts = calloc(GPT_ENTRIES, GPT_ENTRY_SZ)))
    return -1;
  memset(&hdr, 0, sizeof hdr);
  memcpy(hdr.sig, "EFI PART", 8);
  hdr.rev          = 0x00010000;
  hdr.size         = 92;
  hdr.current_lba  = 1;
  hdr.backup_lba   = l->sectors - 1;
  hdr.first_usable = 2 + arr;
  hdr.last_usable  = l->sectors - 2 - arr;
  memcpy(hdr.disk_guid, l->disk_guid, 16);
  hdr.partition_lba = 2;
  hdr.num_parts     = GPT_ENTRIES;
  hdr.part_size     = GPT_ENTRY_SZ;
  for (i = 0; i < LABEL_SLOTS; i++) {
    const struct label_part *p = &l->part[i];

    if (!p->used)
      continue;
    memcpy(parts[i].type_guid, p->type_guid, 16);
    memcpy(parts[i].part_guid, p->guid, 16);
    parts[i].start_lba = p->start;
    parts[i].end_lba   = p->end;
    for (c = 0; c < 35 && p->name[c]; c++)
      parts[i].name[c] = (uint16_t)(unsigned char)p->name[c];
  }
  rc = gpt_write(dev, &hdr, parts);
  free(parts);
  return rc;
}

/* the GPT headers of dev, primary and backup, zeroed when there are any */
static void
drop_gpt(struct BlockDev *dev)
{
  unsigned char sector[4096];
  uint64_t      at[2] = { 1, dev->size / dev->sec_size - 1 };
  int           i;

  if (dev->sec_size > sizeof sector)
    return;
  for (i = 0; i < 2; i++) {
    if (blockdev_read(dev, at[i], sector, 1) < 0 || memcmp(sector, "EFI PART", 8) != 0)
      continue;
    memset(sector, 0, sizeof sector);
    blockdev_write(dev, at[i], sector, 1);
  }
}

static int
write_mbr(struct BlockDev *dev, const struct label *l)
{
  struct MbrHeader mbr;
  int              i;

  memset(&mbr, 0, sizeof mbr);
  memcpy(mbr.code, l->mbr_code, sizeof mbr.code);
  memcpy(mbr.code + 440, &l->mbr_id, 4);
  for (i = 0; i < MBR_SLOTS; i++) {
    const struct label_part *p = &l->part[i];

    if (!p->used)
      continue;
    mbr.parts[i].boot      = p->active ? 0x80 : 0;
    mbr.parts[i].type      = p->mbr_type;
    mbr.parts[i].start_lba = (uint32_t)p->start;
    mbr.parts[i].size      = (uint32_t)(p->end - p->start + 1);
    /* CHS is not used: the largest value says "ask the LBA" */
    memset(mbr.parts[i].start_chs, 0xff, 3);
    memset(mbr.parts[i].end_chs, 0xff, 3);
  }
  mbr.sig[0] = 0x55;
  mbr.sig[1] = 0xAA;
  /* a GPT left behind would be read in preference to this table */
  drop_gpt(dev);
  return blockdev_write(dev, 0, &mbr, 1);
}

int
label_write(struct BlockDev *dev, const struct label *l)
{
  int rc;

  if (l->kind == LABEL_GPT)
    rc = write_gpt(dev, l);
  else if (l->kind == LABEL_MBR)
    rc = write_mbr(dev, l);
  else
    return -1;
  if (rc == 0)
    dev->dirty = 1;
  return rc;
}
