/* See LICENSE file for copyright and license details. */
#include <unistd.h>

#include "arg.h"
#include "util.h"

// ?man sync: flush disk cache
// ?man synopsis:
// ?man The sync utility invokes sync(2) to flush all unwritten changes to disk. This is usually done before shutting down, rebooting or halting.
// ?man ## SEE ALSO
// ?man fsync(2), sync(2)

static void
usage(void)
{
	eprintf("usage: %s\n", argv0);
}

int
main(int argc, char *argv[])
{
	ARGBEGIN {
	default:
		usage();
	} ARGEND

	if (argc)
		usage();
	sync();

	return 0;
}
