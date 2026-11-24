/* see LICENSE file for copyright and license details */
#ifndef ARUU_LIBDISK_LABEL_H
#define ARUU_LIBDISK_LABEL_H

/* a disk's partition table as a list of partitions: read it, change it in
 * memory, write it. nothing is written to the disk until label_write, so a
 * program can offer to undo every change. GPT and MBR (four primary
 * partitions); the byte layout of each is gpt.c's and mbr in label.c's.
 * fdisk edits the tables directly, a graphical partitioner or an installer
 * goes through this */

#include "../diskutil.h"

#include <stdint.h>

#define LABEL_SLOTS 128
#define LABEL_ALIGN 2048 /* sectors: partitions start on 1 MiB at 512 byte sectors */

enum label_kind { LABEL_NONE, LABEL_GPT, LABEL_MBR };

struct label_part {
  int      used;
  uint64_t start, end; /* sectors, the last one included */
  char     type[8];    /* gdisk's four hex digits, "" when unknown */
  uint8_t  type_guid[16];
  uint8_t  guid[16]; /* the partition's own, GPT */
  uint8_t  mbr_type;
  char     name[40];
  int      active; /* MBR's boot flag */
};

struct label {
  enum label_kind   kind;
  uint64_t          sectors; /* the disk */
  size_t            sec_size;
  uint64_t          first, last; /* usable */
  int               slots;       /* 128 for GPT, 4 for MBR */
  struct label_part part[LABEL_SLOTS];
  /* what a rewrite keeps of the table that was there */
  uint8_t  disk_guid[16];
  uint8_t  mbr_code[446];
  uint32_t mbr_id;
};

/* the table on dev: kind LABEL_NONE when it has none. -1 when it cannot be read */
int label_read(struct BlockDev *dev, struct label *l);
/* an empty table of kind for a disk of dev's size, in memory only. -1 for a disk too small */
int label_init(struct label *l, const struct BlockDev *dev, enum label_kind kind);
/* a partition of sectors (0: all of the biggest gap) at start (0: the first gap it fits), of type
 * (a code of label_types) and name. its slot, a negative errno: -ENOSPC no gap, -EINVAL a start
 * that overlaps, -EEXIST all slots used */
int label_add(struct label *l, uint64_t start, uint64_t sectors, const char *type, const char *name);
int label_delete(struct label *l, int slot);
int label_set_type(struct label *l, int slot, const char *type);
int label_set_name(struct label *l, int slot, const char *name);

/* the free stretches between partitions, aligned and at least LABEL_ALIGN
 * sectors long: how many, at most max */
struct label_gap {
  uint64_t start, end;
};
int label_gaps(const struct label *l, struct label_gap *out, int max);

/* the table written to dev, backup copy and protective MBR included. 0, or -1 */
int label_write(struct BlockDev *dev, const struct label *l);

/* the partition types, for a list to choose from and to name one */
struct label_type {
  const char *code, *name;
  int         gpt_only; /* MBR has no byte for it */
};
int                label_types(const struct label_type **out);
const char        *label_type_name(const struct label_part *p);

#endif
