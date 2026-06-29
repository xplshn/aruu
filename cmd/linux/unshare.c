/* See LICENSE file for copyright and license details. */


#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"

static void
usage(void)
{
	eprintf("usage: %s [-muinpU] cmd [args...]\n", argv0);
}

// ?man unshare: run program in new namespaces
// ?man arguments: cmd [args ...]
// ?man run a program with some namespaces unshared from the parent
int
main(int argc, char *argv[])
{
	int flags = 0;

	ARGBEGIN {
	// ?man -m: specify mode or limit
	case 'm':
		flags |= CLONE_NEWNS;
		break;
	// ?man -u: unbuffered output
	case 'u':
		flags |= CLONE_NEWUTS;
		break;
	// ?man -i: interactive mode or prompt for confirmation
	case 'i':
		flags |= CLONE_NEWIPC;
		break;
	// ?man -n: print line numbers or counts
	case 'n':
		flags |= CLONE_NEWNET;
		break;
	// ?man -p: preserve file attributes
	case 'p':
		flags |= CLONE_NEWPID;
		break;
	// ?man -U: specify option flag
	case 'U':
		flags |= CLONE_NEWUSER;
		break;
	default:
		usage();
	} ARGEND;

	if (argc < 1)
		usage();

	if (unshare(flags) < 0)
		eprintf("unshare:");

	if (execvp(argv[0], argv) < 0)
		eprintf("execvp:");

	return 0;
}
