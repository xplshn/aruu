/* See LICENSE file for copyright and license details. */

#include <stdint.h>
#include <stdio.h>

#include "crypt.h"
#include "sha512-224.h"
#include "util.h"

static struct sha512_224 s;
struct crypt_ops         sha512_224_ops = {
    sha512_224_init,
    sha512_224_update,
    sha512_224_sum,
    &s,
};

static void
usage(void)
{
  eprintf("usage: %s [-c] [file ...]\n", argv0);
}

// ?man sha512-224sum: compute sha512/224 checksums
// ?man arguments: file ...
// ?man compute and check sha512/224 message digests
int
main(int argc, char *argv[])
{
  int     ret = 0, (*cryptfunc)(int, char **, struct crypt_ops *, uint8_t *, size_t) = cryptmain;
  uint8_t md[SHA512_224_DIGEST_LENGTH];

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

  ret |= cryptfunc(argc, argv, &sha512_224_ops, md, sizeof(md));
  ret |= fshut(stdin, "<stdin>") | fshut(stdout, "<stdout>");

  return ret;
}
