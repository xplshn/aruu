/* See LICENSE file for copyright and license details. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "crypt.h"
#include "md5.h"
#include "util.h"

static struct md5 s;
struct crypt_ops  md5_ops = {
    md5_init,
    md5_update,
    md5_sum,
    &s,
};

static void
usage(void)
{
  eprintf("usage: %s [-c] [file ...]\n", argv0);
}

// ?man md5sum: compute md5 checksums
// ?man arguments: file ...
// ?man compute and check md5 message digests
int
main(int argc, char *argv[])
{
  int     ret = 0, (*cryptfunc)(int, char **, struct crypt_ops *, uint8_t *, size_t) = cryptmain;
  uint8_t md[MD5_DIGEST_LENGTH];

  ARGBEGIN
  {
    // ?man -b: specify block size or base directory
    case 'b':
    // ?man -t: sort or specify timestamp
    case 't':
      /* ignore */
      break;
    // ?man -c: print count or perform stdout action
    case 'c':
      cryptfunc = cryptcheck;
      break;
    default:
      usage();
  }
  ARGEND

  ret |= cryptfunc(argc, argv, &md5_ops, md, sizeof(md));
  ret |= fshut(stdin, "<stdin>") | fshut(stdout, "<stdout>");

  return ret;
}
