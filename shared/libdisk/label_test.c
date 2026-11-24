/* see LICENSE file for copyright and license details */
/* a table made, written to an image, read back and changed:
 *   cc -Ishared shared/libdisk/label_test.c shared/libdisk/label.c shared/libdisk/gpt.c \
 *      shared/libutil/diskutil.c shared/libutil/libutil.a -o label_test && ./label_test */
#include "label.h"

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define IMAGE_MIB 64

static void
roundtrip(const char *path, enum label_kind kind)
{
  struct BlockDev dev;
  struct label    a, b;
  struct label_gap gaps[8];
  int             esp, root, n;

  assert(blockdev_open(&dev, path, 1) == 0);
  assert(label_init(&a, &dev, kind) == 0);
  assert(label_gaps(&a, gaps, 8) == 1);
  esp  = label_add(&a, 0, 16 * 2048, "ef00", "ESP");
  root = label_add(&a, 0, 0, "8300", "ROOT");
  assert(esp == 0 && root == 1);
  assert(a.part[esp].start % LABEL_ALIGN == 0 && a.part[root].start == a.part[esp].end + 1);
  assert(label_gaps(&a, gaps, 8) == 0);
  /* a gap again where one is deleted, and the next one fills it */
  assert(label_delete(&a, esp) == 0);
  n = label_gaps(&a, gaps, 8);
  assert(n == 1 && gaps[0].start == a.first + (LABEL_ALIGN - a.first % LABEL_ALIGN) % LABEL_ALIGN);
  assert(label_add(&a, 0, 8 * 2048, "8200", "SWAP") == 0);
  assert(label_set_type(&a, root, "8302") == 0);
  assert(label_write(&dev, &a) == 0);
  blockdev_close(&dev);

  assert(blockdev_open(&dev, path, 0) == 0);
  assert(label_read(&dev, &b) == 0);
  assert(b.kind == kind);
  assert(b.part[0].used && b.part[1].used && !b.part[2].used);
  assert(b.part[0].start == a.part[0].start && b.part[0].end == a.part[0].end);
  assert(!strcmp(b.part[0].type, "8200") && !strcmp(b.part[1].type, kind == LABEL_GPT ? "8302" : "8300"));
  if (kind == LABEL_GPT)
    assert(!strcmp(b.part[0].name, "SWAP") && !strcmp(b.part[1].name, "ROOT"));
  blockdev_close(&dev);
}

int
main(void)
{
  char path[] = "/tmp/label_test.XXXXXX";
  int  fd = mkstemp(path);

  assert(fd >= 0 && ftruncate(fd, (off_t)IMAGE_MIB << 20) == 0);
  close(fd);
  roundtrip(path, LABEL_GPT);
  roundtrip(path, LABEL_MBR);
  /* going back to GPT is read as GPT, the MBR one being gone with its table */
  roundtrip(path, LABEL_GPT);
  unlink(path);
  puts("label_test: ok");
  return 0;
}
