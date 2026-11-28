/*
 * Copyright (c) 2015 Grzegorz Kostka (kostka.grzegorz@gmail.com)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * - The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* Adapted from lwext4 src/ext4_mkfs.c, b2d8fe025940cf4091fc40334aedd64d56034cdc.
 * On-disk fields below use byte offsets from the ext4 and JBD2 specifications. */
#include "diskutil.h"
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

enum {
  BLOCK = MKFS_EXT4_BLOCK_SIZE, INODE = 256, DESC = 32, JOURNAL = 1024,
  GROUP_BLOCKS = BLOCK * 8, ROOT = 2, LOG_INO = 8, LOST = 11
};

struct Volume {
  const char *path;
  int fd;
  uint32_t blocks, groups, gdt_blocks, inodes_per_group;
  uint32_t table_blocks, now;
};

static void
usage(void)
{
  eprintf("usage: %s [-L label] [-U uuid] device|image\n", argv0);
}

static void
le16(unsigned char *p, uint16_t v)
{
  p[0] = v;
  p[1] = v >> 8;
}

static void
le32(unsigned char *p, uint32_t v)
{
  le16(p, v);
  le16(p + 2, v >> 16);
}

static void
be32(unsigned char *p, uint32_t v)
{
  p[0] = v >> 24;
  p[1] = v >> 16;
  p[2] = v >> 8;
  p[3] = v;
}

static void
volume_uuid(unsigned char uuid[16], const char *s)
{
  int fd, hi, lo;
  size_t i = 0;
  ssize_t n;

  if (s) {
    if (strlen(s) != 36)
      usage();
    for (; i < 16; i++) {
      if ((i == 4 || i == 6 || i == 8 || i == 10) && *s++ != '-')
        usage();
      hi = hexval(*s++);
      lo = hexval(*s++);
      if (hi < 0 || lo < 0)
        usage();
      uuid[i] = (hi << 4) | lo;
    }
    if (!memcmp(uuid, (unsigned char[16]){0}, 16))
      eprintf("UUID must be nonzero\n");
    return;
  }
  if ((fd = open(ARUU_PATH_DEVURANDOM, O_RDONLY)) < 0)
    eprintf("%s:", ARUU_PATH_DEVURANDOM);
  while (i < 16) {
    n = read(fd, uuid + i, 16 - i);
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0) {
      if (!n)
        errno = EIO;
      eprintf("%s:", ARUU_PATH_DEVURANDOM);
    }
    i += n;
  }
  if (close(fd) < 0)
    eprintf("%s:", ARUU_PATH_DEVURANDOM);
  uuid[6] = (uuid[6] & 15) | 0x40;
  uuid[8] = (uuid[8] & 63) | 0x80;
}

/* With sparse_super, groups 0, 1 and powers of 3, 5 and 7 have backups. */
static int
has_super(uint32_t group)
{
  const unsigned bases[] = {3, 5, 7};
  size_t i;

  if (group <= 1)
    return 1;
  for (i = 0; i < LEN(bases); i++) {
    uint32_t n = group;

    while (n % bases[i] == 0)
      n /= bases[i];
    if (n == 1)
      return 1;
  }
  return 0;
}

static void
open_volume(struct Volume *v)
{
  struct stat st;
  struct flock lock = {.l_type = F_WRLCK, .l_whence = SEEK_SET};
  int flags = O_RDWR | O_NONBLOCK | O_NOCTTY;
  uint64_t bytes;
  time_t now;

#if defined(__linux__) && !defined(ARUU_FORCE_POSIX)
  flags |= O_EXCL; /* kernel claim on a block device */
#endif
  if ((v->fd = open(v->path, flags)) < 0 || fstat(v->fd, &st) < 0)
    eprintf("%s:", v->path);
  if (S_ISREG(st.st_mode)) {
    if (fcntl(v->fd, F_SETLK, &lock) < 0 || fstat(v->fd, &st) < 0)
      eprintf("%s:", v->path);
#if defined(__linux__) && !defined(ARUU_FORCE_POSIX)
    blockdev_check_loop(&st, v->path);
#endif
    bytes = st.st_size;
#if defined(__linux__) && !defined(ARUU_FORCE_POSIX)
  } else if (S_ISBLK(st.st_mode)) {
    struct BlockDev dev = {.fd = v->fd};

    blockdev_sys_size(&dev);
    if (!dev.sec_size || BLOCK % dev.sec_size)
      eprintf("%s: incompatible sector size\n", v->path);
    if (dev.size % dev.sec_size)
      eprintf("%s: incomplete device sector\n", v->path);
    /* A partition can end between filesystem blocks. Leave its tail alone. */
    bytes = dev.size - dev.size % BLOCK;
#endif
  } else {
    eprintf("%s: unsupported target type\n", v->path);
  }
  if (bytes % BLOCK || !mkfs_ext4_size_ok(bytes) ||
      (off_t)bytes <= 0 || (uint64_t)(off_t)bytes != bytes)
    eprintf("%s: expected a 4 KiB aligned size from 8 MiB to below 16 TiB\n", v->path);

  v->blocks = bytes / BLOCK;
  v->groups = 1 + (v->blocks - 1) / GROUP_BLOCKS;
  v->gdt_blocks = 1 + (v->groups - 1) / (BLOCK / DESC);
  v->inodes_per_group = (v->blocks / 4 + v->groups - 1) / v->groups;
  v->table_blocks = (v->inodes_per_group + BLOCK / INODE - 1) / (BLOCK / INODE);
  v->inodes_per_group = v->table_blocks * (BLOCK / INODE);
  /* As in lwext4, omit a partial last group too small for its own metadata. */
  if (v->groups > 1 && v->blocks % GROUP_BLOCKS &&
      v->blocks % GROUP_BLOCKS < 2 + v->table_blocks +
          (has_super(v->groups - 1) ? 1 + v->gdt_blocks : 0)) {
    v->blocks -= v->blocks % GROUP_BLOCKS;
    v->groups--;
    v->gdt_blocks = 1 + (v->groups - 1) / (BLOCK / DESC);
  }
  now = time(NULL);
  if (now == (time_t)-1 || (uintmax_t)now > UINT32_MAX)
    eprintf("filesystem timestamp out of range\n");
  v->now = (uint32_t)now;
}

static void
write_blocks(const struct Volume *v, uint32_t block, const unsigned char *buf, unsigned count)
{
  off_t off = (off_t)((uint64_t)block * BLOCK);
  size_t done = 0, len = (size_t)count * BLOCK;
  ssize_t n;

  if (block >= v->blocks || count > v->blocks - block)
    eprintf("%s: block outside volume\n", v->path);
  while (done < len) {
    n = pwrite(v->fd, buf + done, len - done, off + (off_t)done);
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0) {
      if (!n)
        errno = EIO;
      eprintf("%s: writing block %u:", v->path, (unsigned)block);
    }
    done += n;
  }
}

static void
write_block(const struct Volume *v, uint32_t block, const unsigned char *buf)
{
  write_blocks(v, block, buf, 1);
}

static void
sync_volume(const struct Volume *v)
{
  while (fsync(v->fd) < 0)
    if (errno != EINTR)
      eprintf("%s: fsync:", v->path);
}

/* A used prefix, a free interval, then padding through the end of the bitmap. */
static void
bitmap(const struct Volume *v, uint32_t block, uint32_t used, uint32_t count)
{
  unsigned char map[BLOCK];

  memset(map, 0xff, sizeof map);
  for (; used < count; used++)
    map[used / 8] &= ~(1U << (used % 8));
  write_block(v, block, map);
}

static void
inode(unsigned char *p, uint32_t now, unsigned mode, unsigned links,
      uint32_t bytes, uint32_t blocks)
{
  le16(p, mode);
  le32(p + 4, bytes);
  le32(p + 8, now);  /* atime, ctime, mtime */
  le32(p + 12, now);
  le32(p + 16, now);
  le16(p + 26, links);
  le32(p + 28, blocks * (BLOCK / 512));
  le16(p + 128, 32); /* i_extra_isize */
  le32(p + 132, now >> 31);
  le32(p + 136, now >> 31);
  le32(p + 140, now >> 31);
  le32(p + 144, now); /* crtime */
  le32(p + 148, now >> 31);
}

static void
dirent(unsigned char *p, uint32_t ino, unsigned len, const char *name)
{
  le32(p, ino);
  le16(p + 4, len);
  p[6] = strlen(name);
  p[7] = 2; /* EXT4_FT_DIR */
  memcpy(p + 8, name, p[6]);
}

/* The first table block holds all reserved inodes and lost+found. The two
 * directory blocks, journal indirect block and journal data follow the table. */
static void
write_files(const struct Volume *v, uint32_t root, unsigned char *sb)
{
  unsigned char table[BLOCK] = {0}, buf[BLOCK] = {0};
  unsigned char *log = table + (LOG_INO - 1) * INODE;
  uint32_t first = root + 3, i;

  inode(table + (ROOT - 1) * INODE, v->now, 0040755, 3, BLOCK, 1);
  le32(table + (ROOT - 1) * INODE + 40, root);
  inode(table + (LOST - 1) * INODE, v->now, 0040700, 2, BLOCK, 1);
  le32(table + (LOST - 1) * INODE + 40, root + 1);
  inode(log, v->now, 0100600, 1, JOURNAL * BLOCK, JOURNAL + 1);
  for (i = 0; i < 12; i++)
    le32(log + 40 + 4 * i, first + i);
  le32(log + 40 + 12 * 4, root + 2);
  memcpy(sb + 268, log + 40, 60); /* journal i_block backup */
  le32(sb + 332, JOURNAL * BLOCK);
  write_block(v, root - v->table_blocks, table);

  dirent(buf, ROOT, 12, ".");
  dirent(buf + 12, ROOT, 12, "..");
  dirent(buf + 24, LOST, BLOCK - 24, "lost+found");
  write_block(v, root, buf);
  memset(buf, 0, BLOCK);
  dirent(buf, LOST, 12, ".");
  dirent(buf + 12, ROOT, BLOCK - 12, "..");
  write_block(v, root + 1, buf);

  memset(buf, 0, BLOCK);
  for (i = 12; i < JOURNAL; i++)
    le32(buf + (i - 12) * 4, first + i);
  write_block(v, root + 2, buf);

  /* lwext4 create_journal_inode: JBD2 superblock v2, empty log. */
  memset(buf, 0, BLOCK);
  be32(buf, 0xc03b3998);
  be32(buf + 4, 4);
  be32(buf + 12, BLOCK);
  be32(buf + 16, JOURNAL);
  be32(buf + 20, 1); /* first log block */
  be32(buf + 24, 1); /* next sequence */
  be32(buf + 40, 1); /* revoke */
  memcpy(buf + 48, sb + 104, 16);
  be32(buf + 64, 1); /* internal journal, one user */
  write_block(v, first, buf);
  memset(buf, 0, BLOCK);
  for (i = 1; i < JOURNAL; i++)
    write_block(v, first + i, buf);
}

static void
format(const struct Volume *v, const char *label, const unsigned char uuid[16])
{
  static const unsigned char zero[64 * 1024];
  unsigned char sb[1024] = {0}, first[BLOCK], buf[BLOCK] = {0};
  unsigned char *gdt = ecalloc(v->gdt_blocks, BLOCK);
  uint32_t g, i, free_blocks = 0;
  size_t done = 0;
  ssize_t n;

  /* Read the whole block for devices with 4 KiB logical sectors. Only the
   * superblock at offset 1024 is invalidated and later published. */
  while (done < sizeof first) {
    n = pread(v->fd, first + done, sizeof first - done, done);
    if (n < 0 && errno == EINTR)
      continue;
    if (n < 0)
      eprintf("%s: reading boot area:", v->path);
    if (!n)
      eprintf("%s: short read of boot area\n", v->path);
    done += n;
  }
  memset(first + 1024, 0, sizeof sb);
  write_block(v, 0, first);
  sync_volume(v);
  for (g = 0; g < v->groups; g++) {
    uint32_t start = g * GROUP_BLOCKS;
    uint32_t count = MIN(v->blocks - start, GROUP_BLOCKS);
    uint32_t bitmap_block = start + (has_super(g) ? 1 + v->gdt_blocks : 0);
    uint32_t table = bitmap_block + 2;
    uint32_t used = table - start + v->table_blocks + (g == 0 ? JOURNAL + 3 : 0);
    uint32_t inodes = g == 0 ? LOST : 0;
    unsigned char *d = gdt + g * DESC;

    le32(d, bitmap_block);
    le32(d + 4, bitmap_block + 1);
    le32(d + 8, table);
    le16(d + 12, count - used);
    le16(d + 14, v->inodes_per_group - inodes);
    le16(d + 16, g == 0 ? 2 : 0);
    free_blocks += count - used;
    bitmap(v, bitmap_block, used, count);
    bitmap(v, bitmap_block + 1, inodes, v->inodes_per_group);
    for (i = (g == 0); i < v->table_blocks;) {
      unsigned chunk = MIN(v->table_blocks - i, sizeof zero / BLOCK);
      write_blocks(v, table + i, zero, chunk);
      i += chunk;
    }
  }

  /* lwext4 fill_sb: dynamic revision, Linux inode layout, remount-ro on error. */
  le32(sb, v->inodes_per_group * v->groups);
  le32(sb + 4, v->blocks);
  le32(sb + 12, free_blocks);
  le32(sb + 16, v->inodes_per_group * v->groups - LOST);
  le32(sb + 24, 2); /* block and cluster size, log2(bytes / 1024) */
  le32(sb + 28, 2);
  le32(sb + 32, GROUP_BLOCKS);
  le32(sb + 36, GROUP_BLOCKS);
  le32(sb + 40, v->inodes_per_group);
  le32(sb + 48, v->now); /* last write */
  le16(sb + 54, 0xffff); /* no mount-count limit */
  le16(sb + 56, 0xef53);
  le16(sb + 58, 1); /* clean */
  le16(sb + 60, 2);
  le32(sb + 64, v->now); /* last check */
  le32(sb + 76, 1);
  le32(sb + 84, LOST); /* first non-reserved inode */
  le16(sb + 88, INODE);
  le32(sb + 92, 4);     /* has_journal */
  le32(sb + 96, 2);     /* filetype */
  le32(sb + 100, 0x43); /* sparse_super | large_file | extra_isize */
  memcpy(sb + 104, uuid, 16);
  memcpy(sb + 120, label, strlen(label));
  le32(sb + 224, LOG_INO);
  sb[253] = 1; /* journal backup type */
  le32(sb + 264, v->now); /* creation time */
  le16(sb + 348, 32); /* required and desired extra inode fields */
  le16(sb + 350, 32);
  write_files(v, 3 + v->gdt_blocks + v->table_blocks, sb);

  for (g = 0; g < v->groups; g++) {
    if (!has_super(g))
      continue;
    write_blocks(v, g * GROUP_BLOCKS + 1, gdt, v->gdt_blocks);
    if (g) {
      le16(sb + 90, g);
      memcpy(buf, sb, sizeof sb);
      write_block(v, g * GROUP_BLOCKS, buf);
    }
  }
  sync_volume(v);
  le16(sb + 90, 0);
  memcpy(first + 1024, sb, sizeof sb);
  write_block(v, 0, first);
  sync_volume(v);
  free(gdt);
}

// ?man mkfs.ext4: create a MICE journaled filesystem for the Linux ext4 driver
// ?man arguments: [-L label] [-U uuid] device|image
// ?man Formats an existing image or Linux block device, 8 MiB to below 16 TiB with 4 KiB blocks.
int
main(int argc, char **argv)
{
  struct Volume v = {0};
  const char *label = "", *id = NULL;
  unsigned char uuid[16];

  ARGBEGIN {
  case 'L':
    label = EARGF(usage());
    break;
  case 'U':
    id = EARGF(usage());
    break;
  default:
    usage();
  } ARGEND
  if (argc != 1)
    usage();
  if (strlen(label) > 16)
    eprintf("volume label exceeds 16 bytes\n");
  volume_uuid(uuid, id);
  v.path = argv[0];
  open_volume(&v);
  format(&v, label, uuid);
  if (close(v.fd) < 0)
    eprintf("%s:", v.path);
  return 0;
}
