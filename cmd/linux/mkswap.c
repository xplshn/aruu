/* see LICENSE file for copyright and license details */

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "diskutil.h"
#include "util.h"

#define SWAP_UUID_LENGTH  16
#define SWAP_LABEL_LENGTH 16
#define SWAP_MIN_PAGES    10

struct swap_hdr {
  char          bootbits[1024];
  unsigned int  version;
  unsigned int  last_page;
  unsigned int  nr_badpages;
  unsigned char uuid[SWAP_UUID_LENGTH];
  char          volume_name[SWAP_LABEL_LENGTH];
  unsigned int  padding[117];
  unsigned int  badpages[1];
};

static void
usage(void)
{
  eprintf("usage: %s [-L label] [-U uuid] device\n", argv0);
}

// ?man mkswap: set up a swap area
// ?man arguments: [-L label] [-U uuid] device
// ?man initialize a linux swap area on a device or file
int
main(int argc, char *argv[])
{
  int              fd;
  struct BlockDev  dev;
  unsigned int     pages;
  long             pagesize;
  char            *buf;
  struct swap_hdr *hdr;
  uint64_t         bytes = 0;
  char            *label = NULL;
  char            *uuid_str = NULL;
  char             uuid_out[37] = {0};

  ARGBEGIN
  {
    case 'L':
      label = EARGF(usage());
      break;
    case 'U':
      uuid_str = EARGF(usage());
      break;
    default:
      usage();
  }
  ARGEND;

  if (argc < 1)
    usage();

  pagesize = sysconf(_SC_PAGESIZE);
  if (pagesize <= 0) {
    pagesize = sysconf(_SC_PAGE_SIZE);
    if (pagesize <= 0)
      eprintf("can't determine pagesize\n");
  }

  if (blockdev_open(&dev, argv[0], 1) < 0)
    eprintf("open %s:", argv[0]);
  fd    = dev.fd;
  bytes = dev.size;
  if (bytes == 0) {
    off_t end = lseek(fd, 0, SEEK_END);
    if (end != (off_t)-1)
      bytes = (uint64_t)end;
    lseek(fd, 0, SEEK_SET);
  }

  if (bytes == 0)
    eprintf("unable to determine size of %s\n", argv[0]);

  pages = bytes / pagesize;
  if (pages < SWAP_MIN_PAGES)
    eprintf("swap space needs to be at least %ldKiB\n", SWAP_MIN_PAGES * pagesize / 1024);

  buf = ecalloc(1, pagesize);

  hdr            = (struct swap_hdr *)buf;
  hdr->version   = 1;
  hdr->last_page = pages - 1;

  if (uuid_str) {
    unsigned int u[16];
    if (sscanf(uuid_str,
               "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
               &u[0], &u[1], &u[2], &u[3], &u[4], &u[5], &u[6], &u[7],
               &u[8], &u[9], &u[10], &u[11], &u[12], &u[13], &u[14], &u[15]) == 16) {
      int i;
      for (i = 0; i < 16; i++)
        hdr->uuid[i] = (unsigned char)u[i];
    }
  } else {
    int rfd = open("/dev/urandom", O_RDONLY);
    if (rfd >= 0) {
      if (read(rfd, hdr->uuid, SWAP_UUID_LENGTH) == SWAP_UUID_LENGTH) {
        hdr->uuid[6] = (hdr->uuid[6] & 0x0f) | 0x40;
        hdr->uuid[8] = (hdr->uuid[8] & 0x3f) | 0x80;
      }
      close(rfd);
    }
  }

  if (label)
    estrlcpy(hdr->volume_name, label, sizeof(hdr->volume_name));

  memcpy(buf + pagesize - 10, "SWAPSPACE2", 10);

  snprintf(uuid_out, sizeof(uuid_out),
           "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
           hdr->uuid[0], hdr->uuid[1], hdr->uuid[2], hdr->uuid[3],
           hdr->uuid[4], hdr->uuid[5], hdr->uuid[6], hdr->uuid[7],
           hdr->uuid[8], hdr->uuid[9], hdr->uuid[10], hdr->uuid[11],
           hdr->uuid[12], hdr->uuid[13], hdr->uuid[14], hdr->uuid[15]);

  printf("Setting up swapspace version 1, size = %luKiB\n", (unsigned long)(pages - 1) * pagesize / 1024);
  if (hdr->volume_name[0])
    printf("LABEL=%s, ", hdr->volume_name);
  printf("UUID=%s\n", uuid_out);

  if (lseek(fd, 0, SEEK_SET) != 0 || write(fd, buf, pagesize) != pagesize)
    eprintf("unable to write signature page\n");

  fsync(fd);
  close(fd);
  free(buf);

  return 0;
}
