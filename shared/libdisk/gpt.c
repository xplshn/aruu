/* see LICENSE file for copyright and license details */
#include "gpt.h"
#include "../util.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

static uint32_t crc32_table[256];
static int      crc32_table_initialized = 0;

static void
init_crc32_table(void)
{
  uint32_t i, j, c;
  for (i = 0; i < 256; i++) {
    c = i;
    for (j = 0; j < 8; j++) {
      if (c & 1)
        c = 0xedb88320U ^ (c >> 1);
      else
        c = c >> 1;
    }
    crc32_table[i] = c;
  }
  crc32_table_initialized = 1;
}

uint32_t
disk_crc32(uint32_t crc, const void *buf, size_t len)
{
  const uint8_t *p = buf;
  if (!crc32_table_initialized)
    init_crc32_table();
  while (len--)
    crc = (crc >> 8) ^ crc32_table[(crc ^ *p++) & 0xFF];
  return crc;
}

const struct GptType gpt_types[] = {
    /* short two-byte codes match gdisk/sgdisk's own convention, since
     * every caller already writing "-t ef00" etc against those tools
     * expects the same codes here. the last is the default */
    {"ef00", "EFI system",         "c12a7328-f81f-11d2-ba4b-00a0c93ec93b", 0xef},
    {"ef02", "BIOS boot",          "21686148-6449-6e6f-744e-656564454649", 0x00},
    {"8200", "Linux swap",         "0657fd6d-a4ab-43c4-84e5-0933c84b4f4f", 0x82},
    {"0700", "Basic data",         "ebd0a0a2-b9e5-4433-87c0-68b6b72699c7", 0x07},
    {"8e00", "Linux LVM",          "e6d6d379-f507-44c2-a23c-238f2a3df928", 0x8e},
    {"fd00", "Linux RAID",         "a19d880f-05fc-4d3b-a006-743f0f84911e", 0xfd},
    {"8302", "Linux home",         "933ac7e1-2eb4-4f13-b844-0e14e2aef915", 0x83},
    {"8300", "Linux filesystem",   "0fc63daf-8483-4772-8e79-3d69d8477de4", 0x83},
};
const int gpt_ntypes = (int)(sizeof(gpt_types) / sizeof(gpt_types[0]));

/* canonical XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX form: the first three
 * groups store little-endian, the last two store big-endian, the same
 * mixed layout every other GUID field in this file already uses */
int
parse_guid_string(const char *s, uint8_t out[16])
{
  static const int  group_len[5] = {8, 4, 4, 4, 12};
  static const int  group_le[5]  = {1, 1, 1, 0, 0};
  int               g, i, hi, lo, byte_idx = 0;
  const char       *p = s;

  for (g = 0; g < 5; g++) {
    uint8_t bytes[6];
    int     nbytes = group_len[g] / 2;
    for (i = 0; i < nbytes; i++) {
      hi = hexval(p[i * 2]);
      lo = hexval(p[i * 2 + 1]);
      if (hi < 0 || lo < 0)
        return -1;
      bytes[i] = (uint8_t)((hi << 4) | lo);
    }
    if (group_le[g]) {
      for (i = nbytes - 1; i >= 0; i--)
        out[byte_idx++] = bytes[i];
    } else {
      for (i = 0; i < nbytes; i++)
        out[byte_idx++] = bytes[i];
    }
    p += group_len[g];
    if (g < 4) {
      if (*p != '-')
        return -1;
      p++;
    }
  }
  return *p == '\0' ? 0 : -1;
}

/* a GUID as text, in the canonical form parse_guid_string reads */
void
format_guid(const uint8_t g[16], char out[37])
{
  snprintf(out, 37, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", g[3], g[2], g[1], g[0], g[5],
           g[4], g[7], g[6], g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
}

const struct GptType *
gpt_type_of(const uint8_t guid[16])
{
  uint8_t want[16];
  int     i;

  for (i = 0; i < gpt_ntypes; i++)
    if (parse_guid_string(gpt_types[i].guid, want) == 0 && memcmp(want, guid, 16) == 0)
      return &gpt_types[i];
  return NULL;
}

/* type is a short gdisk-style code (see gpt_types above), a full
 * canonical GUID string, or unset: unset (or unrecognized, with a
 * warning) keeps the long-standing default of "linux filesystem data",
 * matching what every caller got before -t existed */
void
gpt_type_guid(const char *type, uint8_t out[16])
{
  int i;

  if (type) {
    for (i = 0; i < gpt_ntypes; i++) {
      if (strcasecmp(type, gpt_types[i].code) == 0) {
        parse_guid_string(gpt_types[i].guid, out);
        return;
      }
    }
    if (parse_guid_string(type, out) == 0)
      return;
    weprintf("unrecognized GPT type '%s', using linux filesystem data\n", type);
  }
  parse_guid_string(gpt_types[gpt_ntypes - 1].guid, out);
}

/* a random GUID, version 4: the disk and each partition must have a
 * GUID of their own, and a loader such as limine ignores a partition
 * whose GUID is zero */
int
gpt_random_guid(uint8_t out[16])
{
  int     fd = open(ARUU_PATH_DEVURANDOM, O_RDONLY);
  ssize_t n  = fd < 0 ? -1 : read(fd, out, 16);

  if (fd >= 0)
    close(fd);
  if (n != 16)
    return -1;
  out[6] = (out[6] & 0x0f) | 0x40;
  out[8] = (out[8] & 0x3f) | 0x80;
  return 0;
}

/* GPT partition names are UTF-16LE, 36 code units, null-padded: every
 * caller so far only ever needs a plain ASCII label, so this widens
 * each byte rather than pulling in a real UTF-8 decoder for it */
void
gpt_set_name(uint16_t name[36], const char *s)
{
  size_t i;

  memset(name, 0, 36 * sizeof(uint16_t));
  if (!s)
    return;
  for (i = 0; i < 35 && s[i]; i++)
    name[i] = (uint16_t)(unsigned char)s[i];
}

int
gpt_read(struct BlockDev *dev, struct GptHeader *hdr, struct GptPartition *parts)
{
  unsigned char sector[512];
  size_t        part_sectors;

  if (blockdev_read(dev, 1, sector, 1) < 0)
    return -1;

  memcpy(hdr, sector, sizeof(*hdr));

  if (memcmp(hdr->sig, "EFI PART", 8) != 0)
    return -1;

  part_sectors = (hdr->num_parts * hdr->part_size + dev->sec_size - 1) / dev->sec_size;
  if (blockdev_read(dev, hdr->partition_lba, parts, part_sectors) < 0)
    return -1;

  return 0;
}

int
gpt_write(struct BlockDev *dev, struct GptHeader *hdr, struct GptPartition *parts)
{
  unsigned char    sector[512];
  struct MbrHeader pmbr;
  struct GptHeader backup_hdr;
  uint64_t         limit;
  size_t           part_sectors;

  part_sectors = (hdr->num_parts * hdr->part_size + dev->sec_size - 1) / dev->sec_size;

  hdr->parts_crc = disk_crc32(~0U, parts, hdr->num_parts * hdr->part_size) ^ ~0U;

  hdr->crc = 0;
  hdr->crc = disk_crc32(~0U, hdr, hdr->size) ^ ~0U;

  memset(&pmbr, 0, sizeof(pmbr));
  pmbr.parts[0].type      = 0xEE;
  pmbr.parts[0].start_lba = 1;
  limit                   = dev->size / dev->sec_size - 1;
  pmbr.parts[0].size      = limit > 0xFFFFFFFFU ? 0xFFFFFFFFU : (uint32_t)limit;
  pmbr.sig[0]             = 0x55;
  pmbr.sig[1]             = 0xAA;
  if (blockdev_write(dev, 0, &pmbr, 1) < 0)
    return -1;

  memset(sector, 0, sizeof(sector));
  memcpy(sector, hdr, sizeof(*hdr));
  if (blockdev_write(dev, 1, sector, 1) < 0)
    return -1;
  if (blockdev_write(dev, hdr->partition_lba, parts, part_sectors) < 0)
    return -1;

  backup_hdr               = *hdr;
  backup_hdr.current_lba   = hdr->backup_lba;
  backup_hdr.backup_lba    = hdr->current_lba;
  backup_hdr.partition_lba = backup_hdr.current_lba - part_sectors;

  backup_hdr.crc = 0;
  backup_hdr.crc = disk_crc32(~0U, &backup_hdr, backup_hdr.size) ^ ~0U;

  if (blockdev_write(dev, backup_hdr.partition_lba, parts, part_sectors) < 0)
    return -1;

  memset(sector, 0, sizeof(sector));
  memcpy(sector, &backup_hdr, sizeof(backup_hdr));
  if (blockdev_write(dev, backup_hdr.current_lba, sector, 1) < 0)
    return -1;

  return 0;
}

