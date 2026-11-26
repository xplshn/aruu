/* see LICENSE file for copyright and license details */
#include "libdisk/gpt.h"
#include "libdisk/plan.h"
#include "arg.h"
#include "util.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define SECTORS_ZEROED 16384 /* 8 MiB at 512 byte sectors: old signatures that would be seen again */

static void __attribute__((noreturn))
usage(void)
{
  eprintf("usage: %s guided [-s swap_mib] [-f root_fs] disk | disks | layout | read disk | new [-m] disk |\n"
          "       show plan | gaps plan | changes plan | check [-rb] plan | write plan | mkfs plan | mounts plan |\n"
          "       fstab plan | get plan esp|root|swap|bios|rootfs|rootuuid |\n"
          "       add [-t type] [-l label] plan size fs [mount] | delete plan n | set [-k] [-l label] plan n fs [mount]\n",
          argv0);
}

static int
load(struct plan *p, const char *path)
{
  char  why[200];
  FILE *f = path && strcmp(path, "-") ? fopen(path, "r") : stdin;

  if (!f)
    eprintf("%s:", path);
  if (plan_load(p, f, why, sizeof why) < 0)
    eprintf("%s: %s\n", path, why);
  if (f != stdin)
    fclose(f);
  return 0;
}

/* the disks a system can be put on, a line each: name, bytes, model, and 1 when a
 * partition of it is mounted (the media the system came from). not the devices that are
 * no disk: loop, ram, optical, floppy and the mappers */
static int
do_disks(void)
{
  static const char *const skip[] = {"loop", "ram", "zram", "sr", "fd", "dm-", "md", "nbd"};
  struct BlockDevInfo     *list = blockdev_get_list(), *d;

  for (d = list; d; d = d->next) {
    int                  busy = d->mountpoint[0] != '\0', skipped = 0;
    size_t               i;
    struct BlockDevInfo *part;

    for (i = 0; i < sizeof skip / sizeof skip[0]; i++)
      if (strncmp(d->name, skip[i], strlen(skip[i])) == 0)
        skipped = 1;
    if (skipped || !d->size)
      continue;
    for (part = d->parts; part; part = part->next)
      if (part->mountpoint[0])
        busy = 1;
    printf("%s\t%llu\t%s\t%d\n", d->name, (unsigned long long)d->size, d->model, busy);
  }
  blockdev_free_list(list);
  return 0;
}

static int
do_guided(int argc, char *argv[])
{
  struct plan p;
  uint64_t    swap = 0;
  const char *fs   = "ext4";

  ARGBEGIN
  {
    case 's':
      swap = (uint64_t)estrtol(EARGF(usage()), 10);
      break;
    case 'f':
      fs = EARGF(usage());
      break;
    default:
      usage();
  }
  ARGEND
  if (argc != 1)
    usage();
  if (plan_guided(&p, argv[0], swap, fs) < 0)
    eprintf("%s cannot hold the guided layout\n", argv[0]);
  return plan_save(&p, stdout);
}

static void
show_line(const char *line, void *arg)
{
  (void)arg;
  puts(line);
}

static int
do_show(struct plan *p)
{
  int i;

  printf("%s, %s\n", p->disk, p->label.kind == LABEL_GPT ? "GPT" : p->label.kind == LABEL_MBR ? "MBR" : "no table");
  for (i = 0; i < p->label.slots; i++) {
    const struct label_part *l = &p->label.part[i];
    const struct plan_part  *a = &p->attr[i];
    char                     dev[PLAN_PATH + 8], size[32];

    if (!l->used)
      continue;
    plan_part_path(p->disk, i, dev, sizeof dev);
    plan_size((l->end - l->start + 1) * p->label.sec_size, size, sizeof size);
    printf("%-14s %9s  %-18s %-6s %-12s %s\n", dev, size, label_type_name(l), a->fs, a->mount, a->format ? "format" : "");
  }
  (void)show_line;
  return 0;
}

static int
do_check(int argc, char *argv[])
{
  struct plan p;
  unsigned    need = 0;
  char        why[200];

  ARGBEGIN
  {
    case 'r':
      need |= PLAN_NEED_ROOT;
      break;
    case 'b':
      need |= PLAN_NEED_BOOT;
      break;
    default:
      usage();
  }
  ARGEND
  if (argc != 1)
    usage();
  load(&p, argv[0]);
  if (plan_check(&p, need, why, sizeof why) < 0) {
    weprintf("%s\n", why);
    return 1;
  }
  return 0;
}

/* the first sectors of the disk zeroed, the table written, and the devices of it waited for */
static int
do_write(struct plan *p)
{
  struct BlockDev dev;
  char           *zero = ecalloc(SECTORS_ZEROED, 512);
  int             rc;

  if (blockdev_open(&dev, p->disk, 1) < 0)
    eprintf("%s:", p->disk);
  /* what was there is read again as a signature of something: a filesystem, an old table */
  rc = blockdev_write(&dev, 0, zero, SECTORS_ZEROED * 512 / dev.sec_size);
  free(zero);
  if (rc < 0)
    weprintf("could not clear the start of %s\n", p->disk);
  rc = label_write(&dev, &p->label);
  blockdev_close(&dev);
  if (rc < 0)
    eprintf("could not write the partition table of %s\n", p->disk);
  /* a disk that is an image has no devices to wait for */
  if (access(p->disk, F_OK) == 0 && plan_wait_nodes(p, 100) < 0)
    eprintf("the partition devices of %s did not appear\n", p->disk);
  return 0;
}

static int
do_mkfs(struct plan *p)
{
  char *argv[16], store[512];
  int   i, status = 0;

  for (i = 0; i < p->label.slots; i++) {
    char dev[PLAN_PATH + 8];
    pid_t pid;
    int   st;

    if (!p->label.part[i].used || !p->attr[i].format || !p->attr[i].fs[0])
      continue;
    plan_part_path(p->disk, i, dev, sizeof dev);
    printf("formatting %s as %s...\n", dev, p->attr[i].fs);
    fflush(stdout);
    if (plan_mkfs_argv(p, i, argv, 16, store, sizeof store) < 0)
      eprintf("no way to make a %s filesystem\n", p->attr[i].fs);
    /* swap is a warning when its maker is missing: the system works without it */
    switch ((pid = fork())) {
      case -1:
        eprintf("fork:");
        break;
      case 0:
        freopen("/dev/null", "w", stdout);
        execvp(argv[0], argv);
        _exit(127);
      default:
        if (waitpid(pid, &st, 0) < 0 || !WIFEXITED(st) || WEXITSTATUS(st) != 0) {
          if (strcmp(p->attr[i].fs, "swap") == 0) {
            weprintf("%s did not make swap on %s, going on without it\n", argv[0], dev);
          } else {
            weprintf("%s failed on %s\n", argv[0], dev);
            status = 1;
          }
        }
    }
  }
  return status;
}

/* "device mountpoint type", in the order to mount them */
static int
do_mounts(struct plan *p)
{
  int slots[LABEL_SLOTS], n = plan_mount_order(p, slots, LABEL_SLOTS), i;

  for (i = 0; i < n; i++) {
    const struct plan_fs *f = plan_filesystem(p->attr[slots[i]].fs);
    char                  dev[PLAN_PATH + 8];

    if (!f || !f->mount_as)
      continue;
    plan_part_path(p->disk, slots[i], dev, sizeof dev);
    printf("%s %s %s\n", dev, p->attr[slots[i]].mount, f->mount_as);
  }
  return 0;
}

/* how fstab names a partition: by its filesystem's UUID, which stays when
 * another disk is plugged in and the names move, or its device when the
 * filesystem has none to read */
static void
fstab_source(const char *dev, char *out, size_t n)
{
  struct BlockDev bd;
  char            type[32] = "", label[128] = "", uuid[128] = "";

  snprintf(out, n, "%s", dev);
  if (blockdev_open(&bd, dev, 0) < 0)
    return;
  if (blockdev_detect_fs(&bd, type, sizeof type, label, sizeof label, uuid, sizeof uuid) == 0 && uuid[0])
    snprintf(out, n, "UUID=%s", uuid);
  blockdev_close(&bd);
}

/* the lines of the installed system's fstab: what is mounted, and swap */
static int
do_fstab(struct plan *p)
{
  int slots[LABEL_SLOTS], n = plan_mount_order(p, slots, LABEL_SLOTS), i;

  for (i = 0; i < n; i++) {
    const struct plan_fs *f = plan_filesystem(p->attr[slots[i]].fs);
    const char           *mp = p->attr[slots[i]].mount;
    char                  path[PLAN_PATH + 8], dev[PLAN_PATH + 140];

    if (!f || !f->mount_as)
      continue;
    plan_part_path(p->disk, slots[i], path, sizeof path);
    fstab_source(path, dev, sizeof dev);
    if (strcmp(mp, "/") == 0)
      printf("%s  /  %s  defaults,noatime  0  1\n", dev, f->mount_as);
    else
      printf("%s  %s  %s  defaults  0  2\n", dev, mp, f->mount_as);
  }
  for (i = 0; i < p->label.slots; i++)
    if (p->label.part[i].used && strcmp(p->attr[i].fs, "swap") == 0) {
      char dev[PLAN_PATH + 8];

      plan_part_path(p->disk, i, dev, sizeof dev);
      printf("%s  none  swap  sw  0  0\n", dev);
    }
  return 0;
}

/* the plan written back over its file, whole or not at all */
static void
save(const struct plan *p, const char *path)
{
  char  tmp[PATH_MAX];
  FILE *f;

  if (snprintf(tmp, sizeof tmp, "%s.new", path) >= (int)sizeof tmp)
    eprintf("%s: name too long\n", path);
  if (!(f = fopen(tmp, "w")))
    eprintf("%s:", tmp);
  if (plan_save(p, f) < 0 || fclose(f) == EOF)
    eprintf("%s: could not write\n", tmp);
  if (rename(tmp, path) < 0)
    eprintf("%s:", path);
}

/* a partition named as a person does, by the number its device ends in */
static int
slot_of(const struct plan *p, const char *n)
{
  long slot = estrtol(n, 10) - 1;

  if (slot < 0 || slot >= p->label.slots || !p->label.part[slot].used)
    eprintf("%s: no such partition\n", n);
  return (int)slot;
}

/* "512M", "20G", "1T", plain MiB, or "rest": sectors, 0 for what is left */
static uint64_t
sectors_of(const struct plan *p, const char *size)
{
  char    *end;
  uint64_t n, mul = 1048576;

  if (strcmp(size, "rest") == 0 || strcmp(size, "0") == 0)
    return 0;
  n = strtoull(size, &end, 10);
  switch (*end) {
    case 'k': case 'K': mul = 1024; end++; break;
    case 'm': case 'M': end++; break;
    case 'g': case 'G': mul = 1048576ULL * 1024; end++; break;
    case 't': case 'T': mul = 1048576ULL * 1024 * 1024; end++; break;
  }
  if (*end == 'i' || *end == 'I')
    end++;
  if (*end == 'b' || *end == 'B')
    end++;
  if (end == size || *end || !n)
    eprintf("%s: not a size (512M, 20G, rest)\n", size);
  return (n * mul + p->label.sec_size - 1) / p->label.sec_size;
}

/* the type a filesystem is given when none is said: what a firmware, limine and a reader of the table expect */
static const char *
type_of(const char *fs, const char *mount)
{
  if (strcmp(fs, "swap") == 0)
    return "8200";
  if (strcmp(fs, "vfat") == 0 && mount && strcmp(mount, "/boot/efi") == 0)
    return "ef00";
  if (strcmp(fs, "bios") == 0)
    return "ef02";
  if (strcmp(fs, "vfat") == 0)
    return "0700";
  return "8300";
}

static int
do_read(int argc, char *argv[])
{
  struct plan p;

  if (argc != 2)
    usage();
  if (plan_read_disk(&p, argv[1]) < 0)
    eprintf("%s: could not read its partitions\n", argv[1]);
  return plan_save(&p, stdout);
}

static int
do_new(int argc, char *argv[])
{
  struct plan     p;
  struct BlockDev dev;
  enum label_kind kind = LABEL_GPT;

  ARGBEGIN
  {
    case 'm':
      kind = LABEL_MBR;
      break;
    default:
      usage();
  }
  ARGEND
  if (argc != 1)
    usage();
  memset(&p, 0, sizeof p);
  snprintf(p.disk, sizeof p.disk, "%s", argv[0]);
  if (blockdev_open(&dev, argv[0], 0) < 0)
    eprintf("%s:", argv[0]);
  if (label_init(&p.label, &dev, kind) < 0)
    eprintf("%s is too small for a partition table\n", argv[0]);
  blockdev_close(&dev);
  return plan_save(&p, stdout);
}

static int
do_gaps(struct plan *p)
{
  struct label_gap gaps[LABEL_SLOTS + 1];
  int              n = label_gaps(&p->label, gaps, LABEL_SLOTS + 1), i;
  char             size[32];

  for (i = 0; i < n; i++) {
    plan_size((gaps[i].end - gaps[i].start + 1) * p->label.sec_size, size, sizeof size);
    printf("free %9s  sectors %llu-%llu\n", size, (unsigned long long)gaps[i].start, (unsigned long long)gaps[i].end);
  }
  return 0;
}

static int
do_changes(struct plan *p)
{
  struct plan before;

  if (plan_read_disk(&before, p->disk) < 0)
    memset(&before, 0, sizeof before);
  plan_changes(&before, p, show_line, NULL);
  return 0;
}

static int
do_add(int argc, char *argv[])
{
  struct plan *p;
  const char  *type = NULL, *label = "", *fs, *mount;
  char         name[16];
  int          slot;

  ARGBEGIN
  {
    case 't':
      type = EARGF(usage());
      break;
    case 'l':
      label = EARGF(usage());
      break;
    default:
      usage();
  }
  ARGEND
  if (argc < 3 || argc > 4)
    usage();
  p = ecalloc(1, sizeof *p);
  load(p, argv[0]);
  if (p->label.kind == LABEL_NONE)
    eprintf("%s has no partition table: start from partplan new\n", p->disk);
  fs    = argv[2];
  mount = argc == 4 ? argv[3] : "";
  if (!type)
    type = type_of(fs, mount);
  /* the BIOS boot partition holds no filesystem, it is only its type */
  if (strcmp(fs, "bios") == 0 || strcmp(fs, "none") == 0)
    fs = "";
  snprintf(name, sizeof name, "%s", strcmp(type, "ef00") == 0 ? "ESP" : strcmp(type, "ef02") == 0 ? "BIOS"
                                    : strcmp(type, "8200") == 0 ? "SWAP" : strcmp(mount, "/") == 0 ? "ROOT" : "DATA");
  slot = plan_add(p, 0, sectors_of(p, argv[1]), type, name, fs, mount, label);
  if (slot < 0)
    eprintf("no room for %s, or %s %s is not a partition (gaps shows the room)\n", argv[1], argv[2], mount);
  save(p, argv[0]);
  printf("%d\n", slot + 1);
  free(p);
  return 0;
}

static int
do_delete(int argc, char *argv[])
{
  struct plan *p;

  if (argc != 3)
    usage();
  p = ecalloc(1, sizeof *p);
  load(p, argv[1]);
  if (plan_delete(p, slot_of(p, argv[2])) < 0)
    eprintf("could not delete %s\n", argv[2]);
  save(p, argv[1]);
  free(p);
  return 0;
}

/* a partition's filesystem and place: -k keeps what is on it instead of making it anew */
static int
do_set(int argc, char *argv[])
{
  struct plan *p;
  const char  *label = NULL;
  int          keep  = 0, slot;

  ARGBEGIN
  {
    case 'k':
      keep = 1;
      break;
    case 'l':
      label = EARGF(usage());
      break;
    default:
      usage();
  }
  ARGEND
  if (argc < 3 || argc > 4)
    usage();
  p = ecalloc(1, sizeof *p);
  load(p, argv[0]);
  slot = slot_of(p, argv[1]);
  if (strcmp(argv[2], "none") == 0)
    argv[2] = "";
  if (plan_set(p, slot, argv[2], argc == 4 ? argv[3] : "", label ? label : p->attr[slot].label, !keep) < 0)
    eprintf("%s is not a filesystem partplan makes, or its label is too long\n", argv[2]);
  save(p, argv[0]);
  free(p);
  return 0;
}

/* the root partition's PARTUUID, which the kernel finds it by whatever the disk
 * is called this boot: a GPT partition's own GUID, or the MBR's disk signature
 * and the partition's number. it is read from the disk, as the table written
 * made it, not from the plan, which carries none */
static int
do_rootuuid(struct plan *p)
{
  struct plan disk;
  char        guid[37];
  int         i;

  if (plan_read_disk(&disk, p->disk) < 0)
    eprintf("%s: could not read its partitions\n", p->disk);
  for (i = 0; i < p->label.slots; i++) {
    if (!p->label.part[i].used || strcmp(p->attr[i].mount, "/") != 0)
      continue;
    if (!disk.label.part[i].used || disk.label.part[i].start != p->label.part[i].start)
      eprintf("%s: the root partition is not on the disk: write the plan first\n", p->disk);
    if (disk.label.kind == LABEL_GPT) {
      format_guid(disk.label.part[i].guid, guid);
      for (char *c = guid; *c; c++)
        if (*c >= 'A' && *c <= 'F')
          *c += 'a' - 'A';
      puts(guid);
    } else {
      printf("%08x-%02x\n", (unsigned)disk.label.mbr_id, (unsigned)(i + 1));
    }
    return 0;
  }
  return 1;
}

// ?man partplan: a disk's layout as a plan, made, checked and applied
// ?man arguments: guided [-s swap_mib] [-f root_fs] disk | disks | layout | read disk | new [-m] disk | show plan | gaps plan | changes plan | check [-rb] plan | write plan | mkfs plan | mounts plan | fstab plan | get plan role | add [-t type] [-l label] plan size fs [mount] | delete plan n | set [-k] [-l label] plan n fs [mount]
// ?man partplan works on a plan, the text a partitioner and an installer
// ?man share: the disk, its partition table, and for each partition the
// ?man filesystem it holds, where it is mounted and whether it is made
// ?man anew. disks lists the disks a system can go on, a line each: name, bytes,
// ?man model and 1 for one in use. guided prints the layout for a whole disk, layout
// ?man the partitions it puts ahead of the root, show reads one,
// ?man check says whether it can be installed to (-r a root, -b a way to start),
// ?man write clears the start of the disk and writes the table, mkfs makes
// ?man the filesystems, mounts and fstab say where each goes, and get tells
// ?man the device of the esp, root or swap, the number of the bios boot partition,
// ?man the root filesystem, or rootuuid, the root's PARTUUID read from the disk.
// ?man read makes a plan of a disk as it is, new one of an empty GPT table
// ?man (-m an MBR one), and add, delete and set edit a plan file in place:
// ?man add puts a partition of size (512M, 20G, rest) holding fs (ext4, f2fs,
// ?man vfat, swap, bios, none) in the first room that fits it, set
// ?man gives partition n its filesystem and mount point, made anew unless -k
// ?man keeps what is on it. gaps lists the free room, changes what writing the
// ?man plan would do to the disk. a plan is read from the file named, or from
// ?man the standard input when it is -
int
main(int argc, char *argv[])
{
  struct plan *p;
  int          rc;

  argv0 = argv[0];
  if (argc < 2)
    usage();
  if (strcmp(argv[1], "guided") == 0)
    return do_guided(argc - 1, argv + 1);
  if (strcmp(argv[1], "check") == 0)
    return do_check(argc - 1, argv + 1);
  if (strcmp(argv[1], "disks") == 0)
    return do_disks();
  if (strcmp(argv[1], "read") == 0)
    return do_read(argc - 1, argv + 1);
  if (strcmp(argv[1], "new") == 0)
    return do_new(argc - 1, argv + 1);
  if (strcmp(argv[1], "add") == 0)
    return do_add(argc - 1, argv + 1);
  if (strcmp(argv[1], "delete") == 0)
    return do_delete(argc - 1, argv + 1);
  if (strcmp(argv[1], "set") == 0)
    return do_set(argc - 1, argv + 1);
  /* what the guided layout puts ahead of the root, for a question to show */
  if (strcmp(argv[1], "layout") == 0) {
    printf("BIOS boot|%d MiB\nEFI system|%d MiB\n", PLAN_BIOS_MIB, PLAN_ESP_MIB);
    return 0;
  }
  if (argc < 3)
    usage();
  p = ecalloc(1, sizeof *p);
  load(p, argv[2]);
  if (strcmp(argv[1], "show") == 0)
    rc = do_show(p);
  else if (strcmp(argv[1], "gaps") == 0)
    rc = do_gaps(p);
  else if (strcmp(argv[1], "changes") == 0)
    rc = do_changes(p);
  else if (strcmp(argv[1], "write") == 0)
    rc = do_write(p);
  else if (strcmp(argv[1], "mkfs") == 0)
    rc = do_mkfs(p);
  else if (strcmp(argv[1], "mounts") == 0)
    rc = do_mounts(p);
  else if (strcmp(argv[1], "fstab") == 0)
    rc = do_fstab(p);
  else if (strcmp(argv[1], "get") == 0 && argc == 4 && strcmp(argv[3], "rootuuid") == 0)
    rc = do_rootuuid(p);
  else if (strcmp(argv[1], "get") == 0 && argc == 4) {
    char value[PLAN_PATH + 8];

    rc = plan_role(p, argv[3], value, sizeof value) ? (puts(value), 0) : 1;
  } else
    usage();
  free(p);
  return rc;
}
