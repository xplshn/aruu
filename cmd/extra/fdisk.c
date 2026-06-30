/* See LICENSE file for copyright and license details. */
#include "util.h"
#include "diskutil.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct MbrPartition {
	uint8_t boot;
	uint8_t start_chs[3];
	uint8_t type;
	uint8_t end_chs[3];
	uint32_t start_lba;
	uint32_t size;
} __attribute__((packed));

struct MbrHeader {
	uint8_t code[446];
	struct MbrPartition parts[4];
	uint8_t sig[2];
} __attribute__((packed));

struct GptHeader {
	uint8_t sig[8];
	uint32_t rev;
	uint32_t size;
	uint32_t crc;
	uint32_t reserved;
	uint64_t current_lba;
	uint64_t backup_lba;
	uint64_t first_usable;
	uint64_t last_usable;
	uint8_t disk_guid[16];
	uint64_t partition_lba;
	uint32_t num_parts;
	uint32_t part_size;
	uint32_t parts_crc;
} __attribute__((packed));

struct GptPartition {
	uint8_t type_guid[16];
	uint8_t part_guid[16];
	uint64_t start_lba;
	uint64_t end_lba;
	uint64_t flags;
	uint16_t name[36];
} __attribute__((packed));

struct BsdPartition {
	uint32_t size;
	uint32_t offset;
	uint16_t offset_h;
	uint16_t size_h;
	uint8_t fstype;
	uint8_t fragblock;
	uint16_t cpg;
} __attribute__((packed));

struct BsdDisklabel {
	uint32_t d_magic;
	uint16_t d_type;
	uint16_t d_subtype;
	char     d_typename[16];
	char     d_packname[16];
	uint32_t d_secsize;
	uint32_t d_nsectors;
	uint32_t d_ntracks;
	uint32_t d_ncylinders;
	uint32_t d_secpercyl;
	uint32_t d_secperunit;
	uint8_t  d_uid[8];
	uint32_t d_acylinders;
	uint16_t d_bstarth;
	uint16_t d_bendh;
	uint32_t d_bstart;
	uint32_t d_bend;
	uint32_t d_flags;
	uint32_t d_spare4[5];
	uint16_t d_secperunith;
	uint16_t d_version;
	uint32_t d_spare[4];
	uint32_t d_magic2;
	uint16_t d_checksum;
	uint16_t d_npartitions;
	uint32_t d_spare2;
	uint32_t d_spare3;
	struct BsdPartition d_partitions[16];
} __attribute__((packed));

static uint32_t crc32_table[256];
static int crc32_table_initialized = 0;

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

static uint32_t
crc32(uint32_t crc, const void *buf, size_t len)
{
	const uint8_t *p = buf;
	if (!crc32_table_initialized)
		init_crc32_table();
	while (len--)
		crc = (crc >> 8) ^ crc32_table[(crc ^ *p++) & 0xFF];
	return crc;
}

static int opt_list = 0;
static int opt_print = 0;
static int opt_init_gpt = 0;
static int opt_init_mbr = 0;
static int opt_init_bsd = 0;
static int opt_add = 0;
static int opt_del = 0;
static int opt_part_idx = -1;
static int opt_part_is_letter = 0;
static uint64_t opt_start = 0;
static uint64_t opt_end = 0;
static const char *opt_type = NULL;

static void
usage(void)
{
	eprintf("usage: %s [-l] [-p] [-g] [-m] [-s] [-a] [-d] [-n index] [-b start] [-e end] [-t type] [device]\n", argv0);
}

static void
mbr_print(struct MbrHeader *mbr, const char *path)
{
	int i;
	printf("Disk: %s\n", path);
	printf("Partition table (MBR/dos):\n");
	printf("%-5s %-4s %-12s %-12s %-4s\n", "Index", "Boot", "Start", "Size", "Type");
	for (i = 0; i < 4; i++) {
		struct MbrPartition *p = &mbr->parts[i];
		if (p->size == 0)
			continue;
		printf("%-5d %-4s %-12u %-12u 0x%02x\n",
		       i + 1,
		       p->boot == 0x80 ? "Yes" : "No",
		       p->start_lba,
		       p->size,
		       p->type);
	}
}

static void
gpt_print(struct GptHeader *hdr, struct GptPartition *parts, const char *path)
{
	uint32_t i;
	char guid[37];
	printf("Disk: %s\n", path);
	printf("Partition table (GPT):\n");
	printf("%-5s %-12s %-12s %-36s\n", "Index", "Start", "End", "Type GUID");
	for (i = 0; i < hdr->num_parts; i++) {
		struct GptPartition *p = &parts[i];
		if (p->start_lba == 0 && p->end_lba == 0)
			continue;

		snprintf(guid, sizeof(guid),
		         "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
		         p->type_guid[3], p->type_guid[2], p->type_guid[1], p->type_guid[0],
		         p->type_guid[5], p->type_guid[4],
		         p->type_guid[7], p->type_guid[6],
		         p->type_guid[8], p->type_guid[9],
		         p->type_guid[10], p->type_guid[11], p->type_guid[12], p->type_guid[13], p->type_guid[14], p->type_guid[15]);

		printf("%-5d %-12llu %-12llu %-36s\n",
		       i + 1,
		       (unsigned long long)p->start_lba,
		       (unsigned long long)p->end_lba,
		       guid);
	}
}

static int
gpt_read(struct BlockDev *dev, struct GptHeader *hdr, struct GptPartition *parts)
{
	unsigned char sector[512];
	size_t part_sectors;

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

static int
gpt_write(struct BlockDev *dev, struct GptHeader *hdr, struct GptPartition *parts)
{
	unsigned char sector[512];
	struct MbrHeader pmbr;
	struct GptHeader backup_hdr;
	uint64_t limit;
	size_t part_sectors;

	part_sectors = (hdr->num_parts * hdr->part_size + dev->sec_size - 1) / dev->sec_size;

	hdr->parts_crc = crc32(~0U, parts, hdr->num_parts * hdr->part_size) ^ ~0U;

	hdr->crc = 0;
	hdr->crc = crc32(~0U, hdr, hdr->size) ^ ~0U;

	memset(&pmbr, 0, sizeof(pmbr));
	pmbr.parts[0].type = 0xEE;
	pmbr.parts[0].start_lba = 1;
	limit = dev->size / dev->sec_size - 1;
	pmbr.parts[0].size = limit > 0xFFFFFFFFU ? 0xFFFFFFFFU : (uint32_t)limit;
	pmbr.sig[0] = 0x55;
	pmbr.sig[1] = 0xAA;
	if (blockdev_write(dev, 0, &pmbr, 1) < 0)
		return -1;

	memset(sector, 0, sizeof(sector));
	memcpy(sector, hdr, sizeof(*hdr));
	if (blockdev_write(dev, 1, sector, 1) < 0)
		return -1;
	if (blockdev_write(dev, hdr->partition_lba, parts, part_sectors) < 0)
		return -1;

	backup_hdr = *hdr;
	backup_hdr.current_lba = hdr->backup_lba;
	backup_hdr.backup_lba = hdr->current_lba;
	backup_hdr.partition_lba = backup_hdr.current_lba - part_sectors;

	backup_hdr.crc = 0;
	backup_hdr.crc = crc32(~0U, &backup_hdr, backup_hdr.size) ^ ~0U;

	if (blockdev_write(dev, backup_hdr.partition_lba, parts, part_sectors) < 0)
		return -1;

	memset(sector, 0, sizeof(sector));
	memcpy(sector, &backup_hdr, sizeof(backup_hdr));
	if (blockdev_write(dev, backup_hdr.current_lba, sector, 1) < 0)
		return -1;

	return 0;
}

static uint16_t
bsd_dkcksum(void *lp, size_t nparts)
{
	uint16_t *start;
	uint16_t *end;
	uint16_t sum = 0;

	start = (uint16_t *)lp;
	end = (uint16_t *)((char *)lp + 148 + nparts * 16);
	while (start < end)
		sum ^= *start++;
	return sum;
}

static int
bsd_is_partition_type(uint8_t type)
{
	return type == 0xa5 || type == 0xa6 || type == 0xa9;
}

static int
bsd_read(struct BlockDev *dev, uint64_t base_sec, struct BsdDisklabel *lp)
{
	unsigned char sector[512];
	if (blockdev_read(dev, base_sec + 1, sector, 1) < 0)
		return -1;
	memcpy(lp, sector, sizeof(*lp));
	if (lp->d_magic != 0x82564557U || lp->d_magic2 != 0x82564557U)
		return -1;
	return 0;
}

static int
bsd_write(struct BlockDev *dev, uint64_t base_sec, struct BsdDisklabel *lp)
{
	unsigned char sector[512];
	memset(sector, 0, sizeof(sector));
	lp->d_checksum = 0;
	lp->d_checksum = bsd_dkcksum(lp, lp->d_npartitions);
	memcpy(sector, lp, sizeof(*lp));
	if (blockdev_write(dev, base_sec + 1, sector, 1) < 0)
		return -1;
	return 0;
}

static void
bsd_print(struct BsdDisklabel *lp, const char *path, uint64_t base_sec)
{
	uint64_t start, size;
	int i;

	printf("Disk: %s (at sector %llu)\n", path, (unsigned long long)base_sec);
	printf("Partition table (BSD disklabel):\n");
	printf("%-5s %-12s %-12s %-10s\n", "Index", "Start", "Size", "Type");
	for (i = 0; i < lp->d_npartitions; i++) {
		struct BsdPartition *p = &lp->d_partitions[i];
		start = ((uint64_t)p->offset_h << 32) | p->offset;
		size = ((uint64_t)p->size_h << 32) | p->size;
		if (size == 0)
			continue;
		printf("%-5c %-12llu %-12llu 0x%02x\n",
		       'a' + i,
		       (unsigned long long)start,
		       (unsigned long long)size,
		       p->fstype);
	}
}

static int
bsd_find_label(struct BlockDev *dev, uint64_t *base_sec, struct BsdDisklabel *lp)
{
	struct MbrHeader mbr;
	int i;

	if (bsd_read(dev, 0, lp) == 0) {
		*base_sec = 0;
		return 0;
	}

	if (blockdev_read(dev, 0, &mbr, 1) == 0 && mbr.sig[0] == 0x55 && mbr.sig[1] == 0xAA) {
		for (i = 0; i < 4; i++) {
			if (mbr.parts[i].size > 0 && bsd_is_partition_type(mbr.parts[i].type)) {
				if (bsd_read(dev, mbr.parts[i].start_lba, lp) == 0) {
					*base_sec = mbr.parts[i].start_lba;
					return 0;
				}
			}
		}
	}

	return -1;
}

static int
do_print(const char *path)
{
	struct BlockDev dev;
	struct MbrHeader mbr;
	struct GptHeader gpt_hdr;
	struct GptPartition gpt_parts[128];
	struct BsdDisklabel bsd_lp;
	int i;
	int found_bsd = 0;

	if (blockdev_open(&dev, path, 0) < 0) {
		weprintf("cannot open %s:", path);
		return -1;
	}

	if (gpt_read(&dev, &gpt_hdr, gpt_parts) == 0) {
		gpt_print(&gpt_hdr, gpt_parts, path);
	} else if (blockdev_read(&dev, 0, &mbr, 1) == 0 && mbr.sig[0] == 0x55 && mbr.sig[1] == 0xAA) {
		for (i = 0; i < 4; i++) {
			if (mbr.parts[i].size > 0 && bsd_is_partition_type(mbr.parts[i].type)) {
				if (bsd_read(&dev, mbr.parts[i].start_lba, &bsd_lp) == 0) {
					bsd_print(&bsd_lp, path, mbr.parts[i].start_lba);
					found_bsd = 1;
				}
			}
		}
		if (!found_bsd) {
			if (bsd_read(&dev, 0, &bsd_lp) == 0) {
				bsd_print(&bsd_lp, path, 0);
			} else {
				mbr_print(&mbr, path);
			}
		}
	} else {
		if (bsd_read(&dev, 0, &bsd_lp) == 0) {
			bsd_print(&bsd_lp, path, 0);
		} else {
			printf("Disk %s: unpartitioned or unknown format\n", path);
		}
	}

	blockdev_close(&dev);
	return 0;
}

static int
do_fdisk(const char *path)
{
	struct BlockDev dev;
	struct MbrHeader mbr;
	struct GptHeader gpt_hdr;
	struct GptPartition gpt_parts[128];
	struct BsdDisklabel lp;
	struct BsdDisklabel bsd_lp;
	struct GptPartition *gp;
	struct BsdPartition *bp;
	struct MbrPartition *mp;
	uint64_t base_sec;
	uint64_t limit;
	uint64_t bsd_base;
	uint64_t size;
	int idx;
	int has_gpt = 0;
	int has_bsd = 0;

	if (blockdev_open(&dev, path, 1) < 0) {
		weprintf("cannot open %s:", path);
		return -1;
	}

	has_gpt = (gpt_read(&dev, &gpt_hdr, gpt_parts) == 0);

	if (opt_init_mbr) {
		memset(&mbr, 0, sizeof(mbr));
		mbr.sig[0] = 0x55;
		mbr.sig[1] = 0xAA;
		if (blockdev_write(&dev, 0, &mbr, 1) < 0) {
			weprintf("cannot write MBR to %s:", path);
			blockdev_close(&dev);
			return -1;
		}
		printf("Initialized MBR partition table on %s\n", path);
		blockdev_close(&dev);
		return 0;
	}

	if (opt_init_gpt) {
		memset(&gpt_hdr, 0, sizeof(gpt_hdr));
		memcpy(gpt_hdr.sig, "EFI PART", 8);
		gpt_hdr.rev = 0x00010000;
		gpt_hdr.size = 92;
		gpt_hdr.current_lba = 1;
		gpt_hdr.backup_lba = dev.size / dev.sec_size - 1;
		gpt_hdr.first_usable = 34;
		gpt_hdr.last_usable = gpt_hdr.backup_lba - 34;
		gpt_hdr.partition_lba = 2;
		gpt_hdr.num_parts = 128;
		gpt_hdr.part_size = 128;
		memset(gpt_parts, 0, sizeof(gpt_parts));

		if (gpt_write(&dev, &gpt_hdr, gpt_parts) < 0) {
			weprintf("cannot write GPT to %s:", path);
			blockdev_close(&dev);
			return -1;
		}
		printf("Initialized GPT partition table on %s\n", path);
		blockdev_close(&dev);
		return 0;
	}

	if (opt_init_bsd) {
		base_sec = 0;
		limit = dev.size / dev.sec_size;

		if (blockdev_read(&dev, 0, &mbr, 1) == 0 && mbr.sig[0] == 0x55 && mbr.sig[1] == 0xAA) {
			if (!opt_part_is_letter && opt_part_idx >= 1 && opt_part_idx <= 4) {
				mp = &mbr.parts[opt_part_idx - 1];
				if (mp->size > 0) {
					base_sec = mp->start_lba;
					limit = base_sec + mp->size;
				}
			}
		}

		memset(&lp, 0, sizeof(lp));
		lp.d_magic = 0x82564557U;
		lp.d_magic2 = 0x82564557U;
		lp.d_secsize = dev.sec_size;
		lp.d_npartitions = 8;
		lp.d_partitions[2].offset = (uint32_t)base_sec;
		lp.d_partitions[2].offset_h = (uint16_t)(base_sec >> 32);
		size = limit - base_sec;
		lp.d_partitions[2].size = (uint32_t)size;
		lp.d_partitions[2].size_h = (uint16_t)(size >> 32);
		lp.d_partitions[2].fstype = 0;

		if (bsd_write(&dev, base_sec, &lp) < 0) {
			weprintf("cannot write BSD disklabel to %s:", path);
			blockdev_close(&dev);
			return -1;
		}
		printf("Initialized BSD disklabel on %s (at sector %llu)\n", path, (unsigned long long)base_sec);
		blockdev_close(&dev);
		return 0;
	}

	if (opt_add) {
		if (opt_part_idx < 1) {
			weprintf("add requires partition index (-n)\n");
			blockdev_close(&dev);
			return -1;
		}

		if (has_gpt) {
			if (opt_part_idx > 128) {
				weprintf("GPT partition index must be 1-128\n");
				blockdev_close(&dev);
				return -1;
			}
			gp = &gpt_parts[opt_part_idx - 1];
			gp->start_lba = opt_start;
			gp->end_lba = opt_end;
			gp->type_guid[0] = 0xAF; gp->type_guid[1] = 0x3D; gp->type_guid[2] = 0xC6; gp->type_guid[3] = 0x0F;
			gp->type_guid[4] = 0x83; gp->type_guid[5] = 0x84;
			gp->type_guid[6] = 0x72; gp->type_guid[7] = 0x47;
			gp->type_guid[8] = 0x8E; gp->type_guid[9] = 0x79;
			gp->type_guid[10] = 0x3D; gp->type_guid[11] = 0x69; gp->type_guid[12] = 0xD8; gp->type_guid[13] = 0x47; gp->type_guid[14] = 0x7D; gp->type_guid[15] = 0xE4;

			if (gpt_write(&dev, &gpt_hdr, gpt_parts) < 0) {
				weprintf("cannot update GPT on %s:", path);
				blockdev_close(&dev);
				return -1;
			}
			printf("Added GPT partition %d to %s\n", opt_part_idx, path);
		} else {
			has_bsd = (bsd_find_label(&dev, &bsd_base, &bsd_lp) == 0);
			if (opt_part_is_letter || has_bsd) {
				if (!has_bsd) {
					weprintf("BSD disklabel not found on %s\n", path);
					blockdev_close(&dev);
					return -1;
				}
				idx = opt_part_idx - 1;
				if (idx >= 16) {
					weprintf("BSD partition index must be a-p\n");
					blockdev_close(&dev);
					return -1;
				}
				bp = &bsd_lp.d_partitions[idx];
				bp->offset = (uint32_t)opt_start;
				bp->offset_h = (uint16_t)(opt_start >> 32);
				size = opt_end - opt_start + 1;
				bp->size = (uint32_t)size;
				bp->size_h = (uint16_t)(size >> 32);
				bp->fstype = opt_type ? (uint8_t)strtoul(opt_type, NULL, 0) : 7;
				if (idx >= bsd_lp.d_npartitions)
					bsd_lp.d_npartitions = idx + 1;

				if (bsd_write(&dev, bsd_base, &bsd_lp) < 0) {
					weprintf("cannot update BSD disklabel on %s:", path);
					blockdev_close(&dev);
					return -1;
				}
				printf("Added BSD partition %c to %s\n", 'a' + idx, path);
			} else {
				if (blockdev_read(&dev, 0, &mbr, 1) < 0) {
					weprintf("cannot read MBR from %s:", path);
					blockdev_close(&dev);
					return -1;
				}
				if (opt_part_idx > 4) {
					weprintf("MBR partition index must be 1-4\n");
					blockdev_close(&dev);
					return -1;
				}
				mp = &mbr.parts[opt_part_idx - 1];
				mp->start_lba = (uint32_t)opt_start;
				mp->size = (uint32_t)(opt_end - opt_start + 1);
				mp->type = opt_type ? (uint8_t)strtoul(opt_type, NULL, 0) : 0x83;
				mbr.sig[0] = 0x55;
				mbr.sig[1] = 0xAA;

				if (blockdev_write(&dev, 0, &mbr, 1) < 0) {
					weprintf("cannot update MBR on %s:", path);
					blockdev_close(&dev);
					return -1;
				}
				printf("Added MBR partition %d to %s\n", opt_part_idx, path);
			}
		}
	}

	if (opt_del) {
		if (opt_part_idx < 1) {
			weprintf("delete requires partition index (-n)\n");
			blockdev_close(&dev);
			return -1;
		}

		if (has_gpt) {
			if (opt_part_idx > 128) {
				weprintf("GPT partition index must be 1-128\n");
				blockdev_close(&dev);
				return -1;
			}
			memset(&gpt_parts[opt_part_idx - 1], 0, sizeof(struct GptPartition));
			if (gpt_write(&dev, &gpt_hdr, gpt_parts) < 0) {
				weprintf("cannot update GPT on %s:", path);
				blockdev_close(&dev);
				return -1;
			}
			printf("Deleted GPT partition %d from %s\n", opt_part_idx, path);
		} else {
			has_bsd = (bsd_find_label(&dev, &bsd_base, &bsd_lp) == 0);
			if (opt_part_is_letter || has_bsd) {
				if (!has_bsd) {
					weprintf("BSD disklabel not found on %s\n", path);
					blockdev_close(&dev);
					return -1;
				}
				idx = opt_part_idx - 1;
				if (idx >= 16) {
					weprintf("BSD partition index must be a-p\n");
					blockdev_close(&dev);
					return -1;
				}
				memset(&bsd_lp.d_partitions[idx], 0, sizeof(struct BsdPartition));
				if (bsd_write(&dev, bsd_base, &bsd_lp) < 0) {
					weprintf("cannot update BSD disklabel on %s:", path);
					blockdev_close(&dev);
					return -1;
				}
				printf("Deleted BSD partition %c from %s\n", 'a' + idx, path);
			} else {
				if (blockdev_read(&dev, 0, &mbr, 1) < 0) {
					weprintf("cannot read MBR from %s:", path);
					blockdev_close(&dev);
					return -1;
				}
				if (opt_part_idx > 4) {
					weprintf("MBR partition index must be 1-4\n");
					blockdev_close(&dev);
					return -1;
				}
				mp = &mbr.parts[opt_part_idx - 1];
				memset(mp, 0, sizeof(struct MbrPartition));
				if (blockdev_write(&dev, 0, &mbr, 1) < 0) {
					weprintf("cannot update MBR on %s:", path);
					blockdev_close(&dev);
					return -1;
				}
				printf("Deleted MBR partition %d from %s\n", opt_part_idx, path);
			}
		}
	}

	blockdev_close(&dev);
	return 0;
}

// ?man fdisk: partition table manipulator
// ?man arguments: [-l] [-p] [-g] [-m] [-s] [-a] [-d] [-n index] [-b start] [-e end] [-t type] [device]
// ?man fdisk performs partition table operations for MBR, GPT and BSD disklabels
int
main(int argc, char *argv[])
{
	int ret = 0;

	ARGBEGIN {
	// ?man -l:list partitions of all devices
	case 'l':
		opt_list = 1;
		break;
	// ?man -p:print partition table of specified device
	case 'p':
		opt_print = 1;
		break;
	// ?man -g:initialize disk with GPT
	case 'g':
		opt_init_gpt = 1;
		break;
	// ?man -m:initialize disk with MBR
	case 'm':
		opt_init_mbr = 1;
		break;
	// ?man -s:initialize disk with BSD disklabel
	case 's':
		opt_init_bsd = 1;
		break;
	// ?man -a:add partition
	case 'a':
		opt_add = 1;
		break;
	// ?man -d:delete partition
	case 'd':
		opt_del = 1;
		break;
	// ?man -n:partition index or letter
	case 'n': {
		char *arg = EARGF(usage());
		if (arg[0] >= 'a' && arg[0] <= 'p' && arg[1] == '\0') {
			opt_part_idx = arg[0] - 'a' + 1;
			opt_part_is_letter = 1;
		} else {
			opt_part_idx = (int)estrtol(arg, 10);
			opt_part_is_letter = 0;
		}
		break;
	}
	// ?man -b:starting sector LBA
	case 'b':
		opt_start = (uint64_t)estrtoul(EARGF(usage()), 10);
		break;
	// ?man -e:ending sector LBA
	case 'e':
		opt_end = (uint64_t)estrtoul(EARGF(usage()), 10);
		break;
	// ?man -t:partition type (MBR/BSD hex or GPT string)
	case 't':
		opt_type = EARGF(usage());
		break;
	default:
		usage();
	} ARGEND

	if (opt_list) {
		struct BlockDevInfo *list = blockdev_get_list();
		struct BlockDevInfo *curr;
		if (!list)
			return 1;
		for (curr = list; curr; curr = curr->next) {
			do_print(curr->path);
		}
		blockdev_free_list(list);
		if (fshut(stdin, "<stdin>") | fshut(stdout, "<stdout>"))
			ret = 2;
		return ret;
	}

	if (argc != 1)
		usage();

	if (opt_print) {
		ret = do_print(argv[0]) < 0 ? 1 : 0;
	} else {
		ret = do_fdisk(argv[0]) < 0 ? 1 : 0;
	}

	if (fshut(stdin, "<stdin>") | fshut(stdout, "<stdout>"))
		ret = 2;
	return ret;
}
