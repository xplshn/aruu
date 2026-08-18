/*
 * copyright (c) 1998 todd c. miller <todd.miller@courtesan.com>
 *
 * permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE
 */

#include <string.h>
#include <sys/types.h>

#include "../util.h"

/*
 * copy src to string dst of size siz. at most siz-1 characters
 * will be copied. always NUL terminates (unless siz == 0)
 * returns strlen(src); if retval >= siz, truncation occurred
 */
size_t
strlcpy(char *dst, const char *src, size_t siz)
{
  char       *d = dst;
  const char *s = src;
  size_t      n = siz;
  /* copy as many bytes as will fit */
  if (n != 0) {
    while (--n != 0) {
      if ((*d++ = *s++) == '\0')
        break;
    }
  }
  /* not enough room in dst, add NUL and traverse rest of src */
  if (n == 0) {
    if (siz != 0)
      *d = '\0'; /* nul-terminate dst */
    while (*s++)
      ;
  }
  return (s - src - 1); /* count does not include NUL */
}

size_t
estrlcpy(char *dst, const char *src, size_t siz)
{
  size_t ret;

  if ((ret = strlcpy(dst, src, siz)) >= siz)
    eprintf("strlcpy: input string too long\n");

  return ret;
}
