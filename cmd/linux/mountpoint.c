/* See LICENSE file for copyright and license details. */


#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>

#include <mntent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "paths.h"
#include "util.h"

static void
usage(void)
{
	eprintf("usage: %s [-dqx] target\n", argv0);
}

// ?man mountpoint: check if a directory is a mountpoint
// ?man arguments: target
// ?man determine if a directory is a mountpoint
int
main(int argc, char *argv[])
{
	int dflag = 0, qflag = 0, xflag = 0;
	int ret = 0;
	struct mntent *me = NULL;
	FILE *fp;
	struct stat st1, st2;

	ARGBEGIN {
	// ?man -d: specify directory
	case 'd':
		dflag = 1;
		break;
	// ?man -q: quiet mode; suppress output
	case 'q':
		qflag = 1;
		break;
	// ?man -x: hex format or match whole lines
	case 'x':
		xflag = 1;
		break;
	default:
		usage();
	} ARGEND;

	if (argc < 1)
		usage();

	if (stat(argv[0], &st1) < 0) {
		if (qflag)
			return 1;
		eprintf("stat %s:", argv[0]);
	}

	if (xflag) {
		if (!S_ISBLK(st1.st_mode)) {
			if (qflag)
				return 1;
			eprintf("stat: %s: not a block device\n",
				argv[0]);
		}
		printf("%u:%u\n", major(st1.st_rdev),
		       minor(st1.st_rdev));
		return 0;
	}

	if (!S_ISDIR(st1.st_mode)) {
		if (qflag)
			return 1;
		eprintf("stat %s: not a directory\n", argv[0]);
	}

	if (dflag) {
		printf("%u:%u\n", major(st1.st_dev),
		       minor(st1.st_dev));
		return 0;
	}

	fp = setmntent(ARUU_LINUX_PATH_PROC_MOUNTS, "r");
	if (!fp) {
		if (qflag)
			return 1;
		eprintf("setmntent %s:", ARUU_LINUX_PATH_PROC_MOUNTS);
	}
	while ((me = getmntent(fp)) != NULL) {
		if (stat(me->mnt_dir, &st2) < 0) {
			if (qflag)
				return 1;
			eprintf("stat %s:", me->mnt_dir);
		}
		if (st1.st_dev == st2.st_dev &&
		    st1.st_ino == st2.st_ino)
			break;
	}
	endmntent(fp);

	if (me == NULL)
		ret = 1;

	if (!qflag)
		printf("%s %s a mountpoint\n", argv[0],
		       !ret ? "is" : "is not");

	return ret;
}
