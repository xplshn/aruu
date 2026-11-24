/* see LICENSE file for copyright and license details */
#ifndef ARUU_LIBDISK_GPT_H
#define ARUU_LIBDISK_GPT_H

/* the tables as they are on a disk, and reading and writing the GPT
 * itself. fdisk edits these directly; libdisk's label.c is the same
 * tables as a list of partitions, for anything that is not a command
 * line over them */

#include "../diskutil.h"

#include <stddef.h>
#include <stdint.h>

struct MbrPartition {
  uint8_t  boot;
  uint8_t  start_chs[3];
  uint8_t  type;
  uint8_t  end_chs[3];
  uint32_t start_lba;
  uint32_t size;
} __attribute__((packed));

struct MbrHeader {
  uint8_t             code[446];
  struct MbrPartition parts[4];
  uint8_t             sig[2];
} __attribute__((packed));

struct GptHeader {
  uint8_t  sig[8];
  uint32_t rev;
  uint32_t size;
  uint32_t crc;
  uint32_t reserved;
  uint64_t current_lba;
  uint64_t backup_lba;
  uint64_t first_usable;
  uint64_t last_usable;
  uint8_t  disk_guid[16];
  uint64_t partition_lba;
  uint32_t num_parts;
  uint32_t part_size;
  uint32_t parts_crc;
} __attribute__((packed));

struct GptPartition {
  uint8_t  type_guid[16];
  uint8_t  part_guid[16];
  uint64_t start_lba;
  uint64_t end_lba;
  uint64_t flags;
  uint16_t name[36];
} __attribute__((packed));

#define GPT_ENTRIES  128
#define GPT_ENTRY_SZ 128
/* the table of known partition types, gdisk's four hex digit codes */
struct GptType {
  const char *code;
  const char *name;
  const char *guid; /* canonical form */
  uint8_t     mbr;  /* the byte MBR uses for it, 0 for none */
};
extern const struct GptType gpt_types[];
extern const int            gpt_ntypes;

uint32_t disk_crc32(uint32_t crc, const void *buf, size_t len);
int      parse_guid_string(const char *s, uint8_t out[16]);
void     format_guid(const uint8_t guid[16], char out[37]);
/* the GUID of a type code or canonical GUID string; unknown ones give the
 * linux filesystem type, with a warning */
void gpt_type_guid(const char *type, uint8_t out[16]);
/* the type of a GUID: its table entry, NULL when it is none of them */
const struct GptType *gpt_type_of(const uint8_t guid[16]);
int  gpt_random_guid(uint8_t out[16]);
void gpt_set_name(uint16_t name[36], const char *s);
int  gpt_read(struct BlockDev *dev, struct GptHeader *hdr, struct GptPartition *parts);
int  gpt_write(struct BlockDev *dev, struct GptHeader *hdr, struct GptPartition *parts);

#endif
