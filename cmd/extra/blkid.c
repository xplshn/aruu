/* See LICENSE file for copyright and license details. */
#include "util.h"
#include "diskutil.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

static char *oflag = "full";
static char *sflag = NULL;
static int Uflag = 0;
static int Lflag = 0;

static void
usage(void)
{
        eprintf("usage: %s [-o format] [-s tag] [-U] [-L] [device ...]\n", argv0);
}

static void
print_tag(const char *devname, const char *tag, const char *value)
{
        (void)devname;
        if (sflag && strcasecmp(sflag, tag) != 0)
                return;

        if (strcmp(oflag, "value") == 0) {
                printf("%s\n", value);
        } else if (strcmp(oflag, "export") == 0) {
                printf("%s=%s\n", tag, value);
        } else {
                printf(" %s=\"%s\"", tag, value);
        }
}

static int
do_blkid(const char *path)
{
        struct BlockDev dev;
        char type[64] = {0};
        char label[256] = {0};
        char uuid[256] = {0};
        int res;

        if (blockdev_open(&dev, path, 0) < 0)
                return -1;

        res = blockdev_detect_fs(&dev, type, sizeof(type), label, sizeof(label), uuid, sizeof(uuid));
        if (res == 0) {
                if (Uflag || Lflag) {
                        if (Uflag) {
                                printf("%s\n", uuid);
                        } else {
                                printf("%s\n", label);
                        }
                } else {
                        if (strcmp(oflag, "export") == 0) {
                                printf("DEVNAME=%s\n", path);
                        } else if (strcmp(oflag, "value") != 0) {
                                printf("%s:", path);
                        }

                        if (label[0])
                                print_tag(path, "LABEL", label);
                        if (uuid[0])
                                print_tag(path, "UUID", uuid);
                        print_tag(path, "TYPE", type);

                        if (strcmp(oflag, "full") == 0)
                                printf("\n");
                }
        }

        blockdev_close(&dev);
        return res == 0 ? 0 : -2;
}

// ?man blkid: print block device attributes
// ?man arguments: [device ...]
// ?man blkid locates and prints attributes (such as uuid, volume label, and filesystem type)
// ?man of block devices or partition images
int
main(int argc, char *argv[])
{
        int ret = 0;

        ARGBEGIN {
        // ?man -o:specify output format (full, value, export)
        case 'o':
                oflag = EARGF(usage());
                break;
        // ?man -s:only show specified tag (e.g. UUID, LABEL, TYPE)
        case 's':
                sflag = EARGF(usage());
                break;
        // ?man -U:print UUID only
        case 'U':
                Uflag = 1;
                break;
        // ?man -L:print volume label only
        case 'L':
                Lflag = 1;
                break;
        default:
                usage();
        } ARGEND

        if (argc > 0) {
                for (; *argv; argv++) {
                        if (do_blkid(*argv) < 0)
                                ret = 1;
                }
        } else {
                struct BlockDevInfo *list = blockdev_get_list();
                struct BlockDevInfo *curr;
                if (!list) {
                        return 1;
                }
                for (curr = list; curr; curr = curr->next) {
                        do_blkid(curr->path);
                        struct BlockDevInfo *part;
                        for (part = curr->parts; part; part = part->next) {
                                do_blkid(part->path);
                        }
                }
                blockdev_free_list(list);
        }

        if (fshut(stdin, "<stdin>") | fshut(stdout, "<stdout>"))
                ret = 2;

        return ret;
}
