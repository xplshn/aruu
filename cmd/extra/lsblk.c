/* see LICENSE file for copyright and license details */
#include "diskutil.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
usage(void)
{
  eprintf("usage: %s\n", argv0);
}

static void
print_line(
    int         name_pad,
    const char *name,
    int         major,
    int         minor,
    const char *size,
    const char *type,
    const char *fstype,
    const char *mountpoint
)
{
  char linebuf[512];
  int  len;

  len = snprintf(
      linebuf,
      sizeof(linebuf),
      "%-*s %3d:%-3d %6s %-4s %-8s %s",
      name_pad,
      name,
      major,
      minor,
      size,
      type,
      fstype,
      mountpoint
  );

  /* trim trailing spaces */
  while (len > 0 && linebuf[len - 1] == ' ') {
    linebuf[len - 1] = '\0';
    len--;
  }
  printf("%s\n", linebuf);
}

// ?man lsblk: list block devices
// ?man lsblk lists information about all available block devices in a tree-like
// layout
int
main(int argc, char *argv[])
{
  struct BlockDevInfo *list, *curr, *part;
  char                 namebuf[64];
  char                 hdrbuf[512];
  int                  max_len = 4;
  int                  hdrlen;

  ARGBEGIN
  {
    default:
      usage();
  }
  ARGEND

  if (argc > 0)
    usage();

  list = blockdev_get_list();
  if (!list)
    return 1;

  /* calculate maximum display width of the name column */
  for (curr = list; curr; curr = curr->next) {
    int len = strlen(curr->name);
    if (len > max_len)
      max_len = len;
    for (part = curr->parts; part; part = part->next) {
      len = 2 + (int)strlen(part->name);
      if (len > max_len)
        max_len = len;
    }
  }

  hdrlen = snprintf(
      hdrbuf,
      sizeof(hdrbuf),
      "%-*s %-7s %6s %-4s %-8s %s",
      max_len,
      "NAME",
      "MAJ:MIN",
      "SIZE",
      "TYPE",
      "FSTYPE",
      "MOUNTPOINT"
  );
  while (hdrlen > 0 && hdrbuf[hdrlen - 1] == ' ') {
    hdrbuf[hdrlen - 1] = '\0';
    hdrlen--;
  }
  printf("%s\n", hdrbuf);

  for (curr = list; curr; curr = curr->next) {
    if (strncmp(curr->name, "ram", 3) == 0)
      continue;

    print_line(
        max_len,
        curr->name,
        curr->major,
        curr->minor,
        humansize(curr->size),
        curr->type,
        curr->fstype[0] ? curr->fstype : "",
        curr->mountpoint[0] ? curr->mountpoint : ""
    );

    for (part = curr->parts; part; part = part->next) {
      int is_last = (part->next == NULL);
      snprintf(namebuf, sizeof(namebuf), "%s%s", is_last ? "└─" : "├─", part->name);
      print_line(
          max_len + 4,
          namebuf,
          part->major,
          part->minor,
          humansize(part->size),
          part->type,
          part->fstype[0] ? part->fstype : "",
          part->mountpoint[0] ? part->mountpoint : ""
      );
    }
  }

  blockdev_free_list(list);
  return 0;
}
