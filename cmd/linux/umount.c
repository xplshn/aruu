/* See LICENSE file for copyright and license details. */


#include <sys/mount.h>

#include <mntent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "paths.h"
#include "util.h"

#if FEATURE_UMOUNT_OPTIONS
static int
fsopt_matches(const char *opts_list, const char *opt, size_t optlen)
{
	int match = 1;

	if (optlen >= 2 && opt[0] == 'n' && opt[1] == 'o') {
		match--;
		opt += 2;
		optlen -= 2;
	}

	if (optlen == 0)
		return 0;

	if (match && optlen > 1 && *opt == '+') {
		opt++;
		optlen--;
	}

	while (1) {
		if (strncmp(opts_list, opt, optlen) == 0) {
			const char *after_opt = opts_list + optlen;
			if (*after_opt == '\0' || *after_opt == ',')
				return match;
		}

		opts_list = strchr(opts_list, ',');
		if (!opts_list)
			break;
		opts_list++;
	}

	return !match;
}

static int
fsopts_matches(const char *opts_list, const char *reqopts_list)
{
	const char *comma;
	size_t len;

	if (!reqopts_list)
		return 1;

	while (1) {
		comma = strchr(reqopts_list, ',');
		if (!comma)
			len = strlen(reqopts_list);
		else
			len = comma - reqopts_list;

		if (len && !fsopt_matches(opts_list, reqopts_list, len))
			return 0;

		if (!comma)
			break;
		reqopts_list = ++comma;
	}

	return 1;
}
#endif

static int
umountall(int flags
#if FEATURE_UMOUNT_OPTIONS
          , const char *oflag
#endif
)
{
	FILE *fp;
	struct mntent *me;
	int ret = 0;
	char **mntdirs = NULL;
	int len = 0;

	fp = setmntent(ARUU_LINUX_PATH_PROC_MOUNTS, "r");
	if (!fp)
		eprintf("setmntent %s:", ARUU_LINUX_PATH_PROC_MOUNTS);
	while ((me = getmntent(fp))) {
		if (strcmp(me->mnt_type, "proc") == 0)
			continue;
#if FEATURE_UMOUNT_OPTIONS
		if (oflag && !fsopts_matches(me->mnt_opts, oflag))
			continue;
#endif
		mntdirs = erealloc(mntdirs, ++len * sizeof(*mntdirs));
		mntdirs[len - 1] = estrdup(me->mnt_dir);
	}
	endmntent(fp);
	while (--len >= 0) {
		if (umount2(mntdirs[len], flags) < 0) {
			weprintf("umount2 %s:", mntdirs[len]);
			ret = 1;
		}
		free(mntdirs[len]);
	}
	free(mntdirs);
	return ret;
}

static void
usage(void)
{
#if FEATURE_UMOUNT_OPTIONS
	weprintf("usage: %s [-lfn] [-O options] target...\n", argv0);
	weprintf("usage: %s -a [-lfn] [-O options]\n", argv0);
#else
	weprintf("usage: %s [-lfn] target...\n", argv0);
	weprintf("usage: %s -a [-lfn]\n", argv0);
#endif
	exit(1);
}

// ?man umount: unmount filesystems
// ?man arguments: target...
// ?man unmount a filesystem from the directory tree
int
main(int argc, char *argv[])
{
	int i;
	int aflag = 0;
	int flags = 0;
	int ret = 0;
#if FEATURE_UMOUNT_OPTIONS
	char *oflag = NULL;
#endif

	ARGBEGIN {
	// ?man -a: print or show all entries
	case 'a':
		aflag = 1;
		break;
	// ?man -f: force the operation
	case 'f':
		flags |= MNT_FORCE;
		break;
	// ?man -l: list in long format
	case 'l':
		flags |= MNT_DETACH;
		break;
	// ?man -n: print line numbers or counts
	case 'n':
		break;
#if FEATURE_UMOUNT_OPTIONS
	// ?man -O:str: specify option flag
	case 'O':
		oflag = EARGF(usage());
		break;
#endif
	default:
		usage();
	} ARGEND;

	if (argc < 1 && aflag == 0)
		usage();

#if FEATURE_UMOUNT_OPTIONS
	if (oflag && aflag == 0)
		usage();
#endif

	if (aflag == 1)
		return umountall(flags
#if FEATURE_UMOUNT_OPTIONS
		                 , oflag
#endif
		);

	for (i = 0; i < argc; i++) {
		if (umount2(argv[i], flags) < 0) {
			weprintf("umount2 %s:", argv[i]);
			ret = 1;
		}
	}
	return ret;
}
