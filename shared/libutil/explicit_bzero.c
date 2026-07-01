/* See LICENSE file for copyright and license details. */
#include <string.h>

#include "../util.h"

#ifndef HAVE_EXPLICIT_BZERO
static void *(*volatile explicit_memset)(void *, int, size_t) = memset;

void
explicit_bzero(void *b, size_t len)
{
  (*explicit_memset)(b, 0, len);
}
#endif
