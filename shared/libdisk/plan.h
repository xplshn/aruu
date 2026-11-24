/* see LICENSE file for copyright and license details */
#ifndef ARUU_LIBDISK_PLAN_H
#define ARUU_LIBDISK_PLAN_H

/* a disk as someone means it to be: its partition table, and for each
 * partition the filesystem it is to hold, where that is mounted, its label
 * and whether it is made fresh. what a partitioner edits, an installer is
 * handed, and a file says (plan_save, plan_load), so each of them is the
 * same thing in the same words.
 *
 * the file is text, a line each:
 *
 *   disk /dev/vda
 *   label gpt
 *   part 1 start=2048 sectors=2048 type=ef02 name=BIOS
 *   part 2 start=4096 sectors=1048576 type=ef00 name=ESP fs=vfat mount=/boot/efi format=yes
 *   part 3 start=1052672 sectors=0 type=8300 name=ROOT fs=ext4 mount=/ format=yes
 *
 * sectors=0 is what is left of the disk. nothing here writes a disk but
 * plan_write_table, and what it makes of the filesystems is the argv
 * plan_mkfs_argv gives, for the caller to run */

#include "label.h"

#include <stdio.h>

#define PLAN_PATH  128
#define PLAN_FS    16
#define PLAN_MOUNT 64
#define PLAN_LABEL 32

struct plan_part {
  char fs[PLAN_FS];       /* what it is to hold: a name of plan_filesystems, "" for none */
  char mount[PLAN_MOUNT]; /* where it is mounted in the installed system, "" for nowhere */
  char label[PLAN_LABEL]; /* the filesystem's label */
  int  format;            /* the filesystem is made, else what is there is kept */
  char found[PLAN_FS];    /* what is on it now, "" when nothing is known */
};

struct plan {
  char             disk[PLAN_PATH];
  struct label     label;
  struct plan_part attr[LABEL_SLOTS];
};

/* the filesystems a partition can hold */
struct plan_fs {
  const char *name;
  const char *title;     /* for a person */
  const char *mount_as;  /* the type the system mounts it as, NULL for none (swap) */
  int         bootable;  /* a root filesystem the system can start from */
  int         max_label;
};
const struct plan_fs *plan_filesystems(int *n);
const struct plan_fs *plan_filesystem(const char *name);

/* the device of partition slot (0 based) of disk: /dev/vda1, /dev/nvme0n1p1 */
void plan_part_path(const char *disk, int slot, char *buf, size_t n);

/* disk read: its table, and what is on each partition. a disk with none is an
 * empty plan of no table. -1 when it cannot be read */
int plan_read_disk(struct plan *p, const char *disk);

/* the partition's attributes set. false for a filesystem not known or a label too long */
int plan_set(struct plan *p, int slot, const char *fs, const char *mount, const char *label, int format);
/* label_add, with what it is to hold: the slot or a negative errno */
int plan_add(struct plan *p, uint64_t start, uint64_t sectors, const char *type, const char *name, const char *fs,
             const char *mount, const char *label);
int plan_delete(struct plan *p, int slot);

/* what the plan needs to be installed to: the checks asked for */
#define PLAN_NEED_ROOT 1 /* a partition mounted at /, of a filesystem to start from */
#define PLAN_NEED_BOOT 2 /* a way for the machine to start: an EFI system partition at /boot/efi or a BIOS boot one */
/* 0 when the plan holds, else -1 and why, in words for a person */
int plan_check(const struct plan *p, unsigned need, char *why, size_t n);

/* what applying now would do over before, a line each (fn may be NULL to count), to read before it is done
 * ("delete /dev/vda2", "create 512 MiB EFI system", "format /dev/vda3 as ext4"). how many */
int plan_changes(const struct plan *before, const struct plan *now, void (*fn)(const char *line, void *arg), void *arg);

/* the table written to the disk, the filesystems left for the caller */
int plan_write_table(const struct plan *p);
/* the command that makes the filesystem of slot: its argv into argv (at most max words,
 * NULL ended) whose words are held in store. 0, -1 when the slot has no filesystem to make */
int plan_mkfs_argv(const struct plan *p, int slot, char **argv, int max, char *store, size_t store_n);

int plan_save(const struct plan *p, FILE *f);
/* a plan read: -1 with why for a line that is not one */
int plan_load(struct plan *p, FILE *f, char *why, size_t n);

/* a size for a person, "512 MiB" */
void plan_size(uint64_t bytes, char *buf, size_t n);

#endif
