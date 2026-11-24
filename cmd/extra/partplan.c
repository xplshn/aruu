/* see LICENSE file for copyright and license details */
#include "libdisk/plan.h"
#include "arg.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define SECTORS_ZEROED 16384 /* 8 MiB at 512 byte sectors: old signatures that would be seen again */

static void
usage(void)
{
  eprintf("usage: %s guided [-s swap_mib] [-f root_fs] disk | disks | layout | show plan | check [-rb] plan | write plan |\n"
          "       mkfs plan | mounts plan | fstab plan | get plan esp|root|swap|bios|rootfs\n",
          argv0);
}

static int
load(struct plan *p, const char *path)
{
  char  why[200];
  FILE *f = path && strcmp(path, "-") ? fopen(path, "r") : stdin;

  if (!f)
    eprintf("%s: %s:", argv0, path);
  if (plan_load(p, f, why, sizeof why) < 0)
    eprintf("%s: %s: %s\n", argv0, path, why);
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
    char   path[96], model[64] = "";
    int    busy = d->mountpoint[0] != '\0', skipped = 0;
    size_t i;
    FILE  *f;
    struct BlockDevInfo *part;

    for (i = 0; i < sizeof skip / sizeof skip[0]; i++)
      if (strncmp(d->name, skip[i], strlen(skip[i])) == 0)
        skipped = 1;
    if (skipped || !d->size)
      continue;
    for (part = d->parts; part; part = part->next)
      if (part->mountpoint[0])
        busy = 1;
    snprintf(path, sizeof path, "/sys/block/%s/device/model", d->name);
    if ((f = fopen(path, "r"))) {
      if (fgets(model, sizeof model, f)) {
        size_t n;

        model[strcspn(model, "\n")] = '\0';
        for (n = strlen(model); n > 0 && model[n - 1] == ' '; n--)
          model[n - 1] = '\0';
      }
      fclose(f);
    }
    printf("%s\t%llu\t%s\t%d\n", d->name, (unsigned long long)d->size, model, busy);
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
    eprintf("%s: %s cannot hold the guided layout\n", argv0, argv[0]);
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
    eprintf("%s: %s:", argv0, p->disk);
  /* what was there is read again as a signature of something: a filesystem, an old table */
  rc = blockdev_write(&dev, 0, zero, SECTORS_ZEROED * 512 / dev.sec_size);
  free(zero);
  if (rc < 0)
    weprintf("%s: could not clear the start of %s\n", argv0, p->disk);
  rc = label_write(&dev, &p->label);
  blockdev_close(&dev);
  if (rc < 0)
    eprintf("%s: could not write the partition table of %s\n", argv0, p->disk);
  /* a disk that is an image has no devices to wait for */
  if (access(p->disk, F_OK) == 0 && plan_wait_nodes(p, 100) < 0)
    eprintf("%s: the partition devices of %s did not appear\n", argv0, p->disk);
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
      eprintf("%s: no way to make a %s filesystem\n", argv0, p->attr[i].fs);
    /* swap is a warning when its maker is missing: the system works without it */
    switch ((pid = fork())) {
      case -1:
        eprintf("%s: fork:", argv0);
        break;
      case 0:
        freopen("/dev/null", "w", stdout);
        execvp(argv[0], argv);
        _exit(127);
      default:
        if (waitpid(pid, &st, 0) < 0 || !WIFEXITED(st) || WEXITSTATUS(st) != 0) {
          if (strcmp(p->attr[i].fs, "swap") == 0) {
            weprintf("%s: %s did not make swap on %s, going on without it\n", argv0, argv[0], dev);
          } else {
            weprintf("%s: %s failed on %s\n", argv0, argv[0], dev);
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

/* the lines of the installed system's fstab: what is mounted, and swap */
static int
do_fstab(struct plan *p)
{
  int slots[LABEL_SLOTS], n = plan_mount_order(p, slots, LABEL_SLOTS), i;

  for (i = 0; i < n; i++) {
    const struct plan_fs *f = plan_filesystem(p->attr[slots[i]].fs);
    const char           *mp = p->attr[slots[i]].mount;
    char                  dev[PLAN_PATH + 8];

    if (!f || !f->mount_as)
      continue;
    plan_part_path(p->disk, slots[i], dev, sizeof dev);
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

// ?man partplan: a disk's layout as a plan, made, checked and applied
// ?man arguments: guided [-s swap_mib] [-f root_fs] disk | disks | layout | show plan | check [-rb] plan | write plan | mkfs plan | mounts plan | fstab plan | get plan role
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
// ?man or the root filesystem. a plan is read from the file named, or from the
// ?man standard input when it is -
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
  else if (strcmp(argv[1], "write") == 0)
    rc = do_write(p);
  else if (strcmp(argv[1], "mkfs") == 0)
    rc = do_mkfs(p);
  else if (strcmp(argv[1], "mounts") == 0)
    rc = do_mounts(p);
  else if (strcmp(argv[1], "fstab") == 0)
    rc = do_fstab(p);
  else if (strcmp(argv[1], "get") == 0 && argc == 4) {
    char value[PLAN_PATH + 8];

    rc = plan_role(p, argv[3], value, sizeof value) ? (puts(value), 0) : 1;
  } else
    usage();
  free(p);
  return rc;
}
