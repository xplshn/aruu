/* see LICENSE file for copyright and license details */
#include "plan.h"
#include "../util.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const struct plan_fs filesystems[] = {
    {"ext4", "ext4",         "ext4", 1, 16},
    {"f2fs", "f2fs",         "f2fs", 1, 512},
    {"vfat", "FAT32 (vfat)", "vfat", 0, 11},
    {"swap", "swap",         NULL,   0, 15},
};
#define NFS ((int)(sizeof filesystems / sizeof filesystems[0]))

const struct plan_fs *
plan_filesystems(int *n)
{
  *n = NFS;
  return filesystems;
}

const struct plan_fs *
plan_filesystem(const char *name)
{
  int i;

  for (i = 0; i < NFS; i++)
    if (name && strcmp(name, filesystems[i].name) == 0)
      return &filesystems[i];
  return NULL;
}

void
plan_part_path(const char *disk, int slot, char *buf, size_t n)
{
  size_t len = strlen(disk);

  /* a name that ends in a digit takes a p before the number: nvme0n1p1, mmcblk0p1 */
  snprintf(buf, n, "%s%s%d", disk, len && isdigit((unsigned char)disk[len - 1]) ? "p" : "", slot + 1);
}

void
plan_size(uint64_t bytes, char *buf, size_t n)
{
  static const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
  double             v      = (double)bytes;
  int                u      = 0;

  while (v >= 1024 && u < 4) {
    v /= 1024;
    u++;
  }
  if (u == 0 || v >= 100)
    snprintf(buf, n, "%.0f %s", v, units[u]);
  else
    snprintf(buf, n, "%.1f %s", v, units[u]);
}

int
plan_read_disk(struct plan *p, const char *disk)
{
  struct BlockDev dev;
  int             i;

  memset(p, 0, sizeof *p);
  snprintf(p->disk, sizeof p->disk, "%s", disk);
  if (blockdev_open(&dev, disk, 0) < 0)
    return -1;
  if (label_read(&dev, &p->label) < 0) {
    blockdev_close(&dev);
    return -1;
  }
  if (p->label.kind == LABEL_NONE)
    label_init(&p->label, &dev, LABEL_NONE);
  blockdev_close(&dev);
  for (i = 0; i < p->label.slots; i++) {
    struct BlockDev part;
    char            path[PLAN_PATH + 8], type[16] = "", label[64] = "", uuid[64] = "";

    if (!p->label.part[i].used)
      continue;
    if (strcmp(p->label.part[i].type, "8200") == 0)
      snprintf(p->attr[i].found, PLAN_FS, "swap");
    plan_part_path(disk, i, path, sizeof path);
    if (blockdev_open(&part, path, 0) < 0)
      continue;
    if (blockdev_detect_fs(&part, type, sizeof type, label, sizeof label, uuid, sizeof uuid) == 0) {
      /* the detector calls ext2, 3 and 4 alike */
      snprintf(p->attr[i].found, PLAN_FS, "%s", strcmp(type, "ext") == 0 ? "ext4" : type);
      snprintf(p->attr[i].label, PLAN_LABEL, "%.*s", PLAN_LABEL - 1, label);
    }
    blockdev_close(&part);
  }
  return 0;
}

int
plan_set(struct plan *p, int slot, const char *fs, const char *mount, const char *label, int format)
{
  struct plan_part *a;
  const struct plan_fs *f;

  if (slot < 0 || slot >= p->label.slots || !p->label.part[slot].used)
    return -EINVAL;
  f = fs && fs[0] ? plan_filesystem(fs) : NULL;
  if (fs && fs[0] && !f)
    return -EINVAL;
  if (label && f && strlen(label) > (size_t)f->max_label)
    return -EINVAL;
  a = &p->attr[slot];
  snprintf(a->fs, sizeof a->fs, "%s", fs ? fs : "");
  snprintf(a->mount, sizeof a->mount, "%s", mount ? mount : "");
  snprintf(a->label, sizeof a->label, "%s", label ? label : "");
  a->format = f ? format : 0;
  return 0;
}

int
plan_add(struct plan *p, uint64_t start, uint64_t sectors, const char *type, const char *name, const char *fs,
         const char *mount, const char *label)
{
  int slot = label_add(&p->label, start, sectors, type, name);

  if (slot < 0)
    return slot;
  memset(&p->attr[slot], 0, sizeof p->attr[slot]);
  /* a new partition's filesystem is made: nothing is on it */
  if (plan_set(p, slot, fs, mount, label, 1) < 0) {
    label_delete(&p->label, slot);
    return -EINVAL;
  }
  return slot;
}

int
plan_delete(struct plan *p, int slot)
{
  int rc = label_delete(&p->label, slot);

  if (rc == 0)
    memset(&p->attr[slot], 0, sizeof p->attr[slot]);
  return rc;
}

static int
fail(char *why, size_t n, const char *fmt, const char *a)
{
  snprintf(why, n, fmt, a ? a : "");
  return -1;
}

int
plan_check(const struct plan *p, unsigned need, char *why, size_t n)
{
  int i, j, root = 0, esp = 0, bios = 0;

  for (i = 0; i < p->label.slots; i++) {
    const struct plan_part *a = &p->attr[i];
    const struct label_part *l = &p->label.part[i];
    const struct plan_fs *f   = plan_filesystem(a->fs);

    if (!l->used)
      continue;
    if (a->mount[0] && a->mount[0] != '/')
      return fail(why, n, "%s is not a place to mount: it starts with a /", a->mount);
    if (a->mount[0] && !a->fs[0])
      return fail(why, n, "%s needs a filesystem", a->mount);
    if (a->mount[0] && f && !f->mount_as)
      return fail(why, n, "%s cannot be mounted: it is swap", a->mount);
    for (j = i + 1; j < p->label.slots; j++)
      if (p->label.part[j].used && a->mount[0] && strcmp(a->mount, p->attr[j].mount) == 0)
        return fail(why, n, "two partitions are mounted at %s", a->mount);
    if (strcmp(a->mount, "/") == 0) {
      if (!f || !f->bootable)
        return fail(why, n, "%s", "the system cannot start from the filesystem at /");
      root = 1;
    }
    if (strcmp(a->mount, "/boot/efi") == 0 && strcmp(l->type, "ef00") == 0 && strcmp(a->fs, "vfat") == 0)
      esp = 1;
    if (strcmp(l->type, "ef02") == 0)
      bios = 1;
  }
  if ((need & PLAN_NEED_ROOT) && !root)
    return fail(why, n, "%s", "no partition is mounted at /");
  if ((need & PLAN_NEED_BOOT) && !esp && !bios)
    return fail(why, n, "%s", "the machine needs a way to start: an EFI system partition mounted at /boot/efi, or a BIOS boot partition");
  return 0;
}

int
plan_changes(const struct plan *before, const struct plan *now, void (*fn)(const char *line, void *arg), void *arg)
{
  char line[256], dev[PLAN_PATH + 8], size[32];
  int  i, n = 0;

  struct plan *none = NULL;

  /* a table of another kind replaces the one there: all it held is gone, all the new one holds is made */
  if (before->label.kind != now->label.kind) {
    if (before->label.kind != LABEL_NONE) {
      snprintf(line, sizeof line, "write a new %s partition table on %s, every partition on it is lost",
               now->label.kind == LABEL_GPT ? "GPT" : "MBR", now->disk);
      if (fn)
        fn(line, arg);
      n++;
    }
    if (!(none = calloc(1, sizeof *none)))
      return n;
    none->label.slots = now->label.slots;
    before = none;
  }
  for (i = 0; i < now->label.slots; i++) {
    const struct label_part *a = &before->label.part[i], *b = &now->label.part[i];
    const struct plan_part  *x = &before->attr[i], *y = &now->attr[i];
    int                      same = a->used && b->used && a->start == b->start && a->end == b->end &&
               strcmp(a->type, b->type) == 0;

    plan_part_path(now->disk, i, dev, sizeof dev);
    if (a->used && !same) {
      snprintf(line, sizeof line, "delete %s (%s)", dev, label_type_name(a));
      if (fn)
        fn(line, arg);
      n++;
    }
    if (b->used && !same) {
      plan_size((b->end - b->start + 1) * now->label.sec_size, size, sizeof size);
      snprintf(line, sizeof line, "create %s, %s", dev, size);
      if (fn)
        fn(line, arg);
      n++;
    }
    if (b->used && y->format && y->fs[0]) {
      snprintf(line, sizeof line, "format %s as %s%s%s", dev, y->fs, y->label[0] ? " labelled " : "", y->label);
      if (fn)
        fn(line, arg);
      n++;
    } else if (b->used && same && strcmp(x->mount, y->mount) != 0 && y->mount[0]) {
      snprintf(line, sizeof line, "use %s at %s", dev, y->mount);
      if (fn)
        fn(line, arg);
      n++;
    }
  }
  free(none);
  return n;
}

int
plan_write_table(const struct plan *p)
{
  struct BlockDev dev;
  int             rc;

  if (blockdev_open(&dev, p->disk, 1) < 0)
    return -1;
  rc = label_write(&dev, &p->label);
  blockdev_close(&dev);
  return rc;
}

/* the words of the command that makes a filesystem, each with its place in store */
static int
put(char **argv, int *n, int max, char *store, size_t store_n, size_t *used, const char *word)
{
  size_t len = strlen(word) + 1;

  if (*n + 1 >= max || *used + len > store_n)
    return -1;
  memcpy(store + *used, word, len);
  argv[(*n)++] = store + *used;
  *used += len;
  argv[*n] = NULL;
  return 0;
}

int
plan_mkfs_argv(const struct plan *p, int slot, char **argv, int max, char *store, size_t store_n)
{
  const struct plan_part *a = &p->attr[slot];
  char                    dev[PLAN_PATH + 8];
  size_t                  used = 0;
  int                     n = 0, rc = 0;

  if (slot < 0 || slot >= p->label.slots || !p->label.part[slot].used || !a->fs[0])
    return -1;
  plan_part_path(p->disk, slot, dev, sizeof dev);
  if (max < 1)
    return -1;
  argv[0] = NULL;
  if (strcmp(a->fs, "ext4") == 0) {
    rc |= put(argv, &n, max, store, store_n, &used, "mkfs.ext4");
    rc |= put(argv, &n, max, store, store_n, &used, "-F");
    if (a->label[0]) {
      rc |= put(argv, &n, max, store, store_n, &used, "-L");
      rc |= put(argv, &n, max, store, store_n, &used, a->label);
    }
  } else if (strcmp(a->fs, "f2fs") == 0) {
    rc |= put(argv, &n, max, store, store_n, &used, "mkfs.f2fs");
    rc |= put(argv, &n, max, store, store_n, &used, "-f");
    if (a->label[0]) {
      rc |= put(argv, &n, max, store, store_n, &used, "-l");
      rc |= put(argv, &n, max, store, store_n, &used, a->label);
    }
  } else if (strcmp(a->fs, "vfat") == 0) {
    rc |= put(argv, &n, max, store, store_n, &used, "mkfs.fat");
    rc |= put(argv, &n, max, store, store_n, &used, "-F");
    rc |= put(argv, &n, max, store, store_n, &used, "32");
    if (a->label[0]) {
      rc |= put(argv, &n, max, store, store_n, &used, "-n");
      rc |= put(argv, &n, max, store, store_n, &used, a->label);
    }
  } else if (strcmp(a->fs, "swap") == 0) {
    rc |= put(argv, &n, max, store, store_n, &used, "mkswap");
    if (a->label[0]) {
      rc |= put(argv, &n, max, store, store_n, &used, "-L");
      rc |= put(argv, &n, max, store, store_n, &used, a->label);
    }
  } else
    return -1;
  rc |= put(argv, &n, max, store, store_n, &used, dev);
  return rc ? -1 : 0;
}

int
plan_save(const struct plan *p, FILE *f)
{
  int i;

  fprintf(f, "disk %s\nlabel %s\n", p->disk, p->label.kind == LABEL_GPT ? "gpt" : p->label.kind == LABEL_MBR ? "mbr" : "none");
  for (i = 0; i < p->label.slots; i++) {
    const struct label_part *l = &p->label.part[i];
    const struct plan_part  *a = &p->attr[i];

    if (!l->used)
      continue;
    fprintf(f, "part %d start=%llu sectors=%llu type=%s", i + 1, (unsigned long long)l->start,
            (unsigned long long)(l->end - l->start + 1), l->type[0] ? l->type : "8300");
    if (l->name[0])
      fprintf(f, " name=%s", l->name);
    if (a->fs[0])
      fprintf(f, " fs=%s", a->fs);
    if (a->mount[0])
      fprintf(f, " mount=%s", a->mount);
    if (a->label[0])
      fprintf(f, " label=%s", a->label);
    if (a->format)
      fprintf(f, " format=yes");
    fputc('\n', f);
  }
  return ferror(f) ? -1 : 0;
}

static int
bad(char *why, size_t n, const char *line)
{
  snprintf(why, n, "not a line of a plan: %.60s", line);
  return -1;
}

int
plan_load(struct plan *p, FILE *f, char *why, size_t n)
{
  char line[512];
  struct BlockDev dev;

  memset(p, 0, sizeof *p);
  p->label.slots = 0;
  while (fgets(line, sizeof line, f)) {
    char *nl = strchr(line, '\n'), *word, *save = NULL;

    if (nl)
      *nl = '\0';
    if (!line[0] || line[0] == '#')
      continue;
    if (strncmp(line, "disk ", 5) == 0) {
      snprintf(p->disk, sizeof p->disk, "%s", line + 5);
    } else if (strncmp(line, "label ", 6) == 0) {
      enum label_kind kind = strcmp(line + 6, "gpt") == 0 ? LABEL_GPT : strcmp(line + 6, "mbr") == 0 ? LABEL_MBR : LABEL_NONE;

      /* the table's geometry comes from the disk it is for */
      if (kind == LABEL_NONE || !p->disk[0] || blockdev_open(&dev, p->disk, 0) < 0 || label_init(&p->label, &dev, kind) < 0)
        return bad(why, n, line);
      blockdev_close(&dev);
    } else if (strncmp(line, "part ", 5) == 0) {
      uint64_t start = 0, sectors = 0;
      char     type[8] = "8300", name[40] = "", fs[PLAN_FS] = "", mount[PLAN_MOUNT] = "", label[PLAN_LABEL] = "";
      int      format = 0, index = (int)strtol(line + 5, &word, 10);

      if (p->label.kind == LABEL_NONE || index < 1 || index > p->label.slots)
        return bad(why, n, line);
      for (word = strtok_r(word, " ", &save); word; word = strtok_r(NULL, " ", &save)) {
        if (strncmp(word, "start=", 6) == 0)
          start = strtoull(word + 6, NULL, 10);
        else if (strncmp(word, "sectors=", 8) == 0)
          sectors = strtoull(word + 8, NULL, 10);
        else if (strncmp(word, "type=", 5) == 0)
          snprintf(type, sizeof type, "%s", word + 5);
        else if (strncmp(word, "name=", 5) == 0)
          snprintf(name, sizeof name, "%s", word + 5);
        else if (strncmp(word, "fs=", 3) == 0)
          snprintf(fs, sizeof fs, "%s", word + 3);
        else if (strncmp(word, "mount=", 6) == 0)
          snprintf(mount, sizeof mount, "%s", word + 6);
        else if (strncmp(word, "label=", 6) == 0)
          snprintf(label, sizeof label, "%s", word + 6);
        else if (strcmp(word, "format=yes") == 0)
          format = 1;
        else
          return bad(why, n, line);
      }
      /* a plan names the slots, so a partition goes where it says */
      if (start == 0 || p->label.part[index - 1].used)
        return bad(why, n, line);
      {
        int slot = label_add(&p->label, start, sectors, type, name);

        if (slot < 0)
          return bad(why, n, line);
        if (slot != index - 1) {
          p->label.part[index - 1] = p->label.part[slot];
          memset(&p->label.part[slot], 0, sizeof p->label.part[slot]);
          slot = index - 1;
        }
        if (plan_set(p, slot, fs, mount, label, format) < 0)
          return bad(why, n, line);
      }
    } else
      return bad(why, n, line);
  }
  return p->disk[0] ? 0 : (snprintf(why, n, "a plan names a disk"), -1);
}
