/* $OpenBSD: xmalloc.c,v 1.10 2019/06/28 05:44:09 deraadt Exp $ */
/*
 * Author: Tatu Ylonen <ylo@cs.hut.fi>
 * Copyright (c) 1995 Tatu Ylonen <ylo@cs.hut.fi>, Espoo, Finland
 *                    All rights reserved
 * Versions of malloc and friends that check their results, and never return
 * failure (they call fatal if they encounter an error).
 *
 * As far as I am concerned, the code I have written for this software
 * can be used freely for any purpose.  Any derived versions of this
 * software must be clearly marked as such, and if the derived work is
 * incompatible with the protocol description in the RFC file, it must be
 * called by a name other than "ssh" or "Secure Shell".
 */

#include <err.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

#include "xmalloc.h"

void *
xmalloc(size_t size)
{
	void *ptr;

	if (size == 0)
		errx(2, "xmalloc: zero size");
	ptr = malloc(size);
	if (ptr == NULL)
		err(2, "xmalloc: allocating %zu bytes", size);
	return ptr;
}

void *
xcalloc(size_t nmemb, size_t size)
{
	void *ptr;

	ptr = calloc(nmemb, size);
	if (ptr == NULL)
		err(2, "xcalloc: allocating %zu * %zu bytes", nmemb, size);
	return ptr;
}

void *
xreallocarray(void *ptr, size_t nmemb, size_t size)
{
	void *new_ptr;

	new_ptr = reallocarray(ptr, nmemb, size);
	if (new_ptr == NULL)
		err(2, "xreallocarray: allocating %zu * %zu bytes",
		    nmemb, size);
	return new_ptr;
}

char *
xstrdup(const char *str)
{
	char *cp;

	if ((cp = strdup(str)) == NULL)
		err(2, "xstrdup");
	return cp;
}

int
xasprintf(char **ret, const char *fmt, ...)
{
	va_list ap;
	va_list ap2;
	int i;
	size_t n;

	va_start(ap, fmt);
	va_copy(ap2, ap);
	i = vsnprintf(NULL, 0, fmt, ap);
	if (i < 0)
		err(2, "xasprintf");
	n = (size_t)i + 1;
	*ret = xmalloc(n);
	i = vsnprintf(*ret, n, fmt, ap2);
	va_end(ap2);
	va_end(ap);

	if (i == -1)
		err(2, "xasprintf");

	return i;
}
