/* See LICENSE file for copyright and license details. */


#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utf.h"
#include "util.h"

#define is_odigit(c) ('0' <= (c) && (c) <= '7')

static size_t
unescape_pct(char *s, char *is_pct)
{
	static const char escapes[256] = {
		['"'] = '"',
		['\''] = '\'',
		['\\'] = '\\',
		['a'] = '\a',
		['b'] = '\b',
		['E'] = 033,
		['e'] = 033,
		['f'] = '\f',
		['n'] = '\n',
		['r'] = '\r',
		['t'] = '\t',
		['v'] = '\v'
	};
	size_t m, q;
	char *r, *w;

	for (r = w = s; *r;) {
		if (*r != '\\') {
			is_pct[w - s] = (*r == '%');
			*w++ = *r++;
			continue;
		}
		r++;
		if (!*r) {
			eprintf("null escape sequence\n");
		} else if (escapes[(unsigned char)*r]) {
			is_pct[w - s] = 0;
			*w++ = escapes[(unsigned char)*r++];
		} else if (is_odigit(*r)) {
			for (q = 0, m = 3; m && is_odigit(*r); m--, r++)
				q = q * 8 + (*r - '0');
			is_pct[w - s] = 0;
			*w++ = MIN(q, (size_t)255);
		} else if (*r == 'x' && isxdigit(r[1])) {
			r++;
			for (q = 0, m = 2; m && isxdigit(*r); m--, r++)
				if (isdigit(*r))
					q = q * 16 + (*r - '0');
				else
					q = q * 16 + (tolower(*r) - 'a' + 10);
			is_pct[w - s] = 0;
			*w++ = q;
		} else {
			eprintf("invalid escape sequence '\\%c'\n", *r);
		}
	}
	*w = '\0';

	return w - s;
}

static void
usage(void)
{
	eprintf("usage: %s format [arg ...]\n", argv0);
}

// ?man printf: format and print data
// ?man arguments: format [arg ...]
// ?man format and print arguments to standard output
int
main(int argc, char *argv[])
{
	Rune *rarg;
	size_t i, j, f, formatlen, blen, nflags;
	long long num;
	double dou;
	int cooldown = 0, width, precision, ret = 0, argi, lastargi;
	int has_width, has_precision;
	char *format, *tmp, *arg, *fmt, *fmt_ptr, *is_pct;

	argv0 = argv[0];
	if (argc < 2)
		usage();

	format = argv[1];
	if ((tmp = strstr(format, "\\c"))) {
		*tmp = 0;
		cooldown = 1;
	}
	is_pct = ecalloc(strlen(format) + 1, sizeof(*is_pct));
	formatlen = unescape_pct(format, is_pct);
	if (formatlen == 0) {
		free(is_pct);
		return 0;
	}
	lastargi = 0;
	for (i = 0, argi = 2; !cooldown || i < formatlen; i++, i = cooldown ? i : (i % formatlen)) {
		if (i == 0) {
			if (lastargi == argi)
				break;
			lastargi = argi;
		}

		if (format[i] != '%' || !is_pct[i]) {
			putchar(format[i]);
			continue;
		}

		/* flag */
		f = ++i;
		nflags = strspn(&format[f], "#-+ 0");
		i += nflags;

		if (nflags > INT_MAX)
			eprintf("Too many flags in format\n");

		/* field width */
		has_width = 0;
		width = 0;
		if (format[i] == '*') {
			has_width = 1;
			if (argi < argc)
				width = estrtonum(argv[argi++], INT_MIN, INT_MAX);
			else
				cooldown = 1;
			i++;
		} else {
			j = i;
			i += strspn(&format[i], "+-0123456789");
			if (j != i) {
				has_width = 1;
				tmp = estrndup(format + j, i - j);
				width = estrtonum(tmp, INT_MIN, INT_MAX);
				free(tmp);
			}
		}

		/* field precision */
		has_precision = 0;
		precision = 0;
		if (format[i] == '.') {
			has_precision = 1;
			if (format[++i] == '*') {
				if (argi < argc)
					precision = estrtonum(argv[argi++], INT_MIN, INT_MAX);
				else
					cooldown = 1;
				i++;
			} else {
				j = i;
				i += strspn(&format[i], "+-0123456789");
				if (j != i) {
					tmp = estrndup(format + j, i - j);
					precision = estrtonum(tmp, INT_MIN, INT_MAX);
					free(tmp);
				}
			}
		}

		if (format[i] != '%' || !is_pct[i]) {
			if (argi < argc)
				arg = argv[argi++];
			else {
				arg = "";
				cooldown = 1;
			}
		} else {
			putchar('%');
			continue;
		}

		switch (format[i]) {
		case 'b':
			if ((tmp = strstr(arg, "\\c"))) {
				*tmp = 0;
				blen = unescape(arg);
				fwrite(arg, sizeof(*arg), blen, stdout);
				free(is_pct);
				return 0;
			}
			blen = unescape(arg);
			fwrite(arg, sizeof(*arg), blen, stdout);
			break;
		case 'c':
			unescape(arg);
			rarg = ereallocarray(NULL, utflen(arg) + 1, sizeof(*rarg));
			utftorunestr(arg, rarg);
			efputrune(rarg, stdout, "<stdout>");
			free(rarg);
			break;
		case 's':
			fmt = emalloc(nflags + 10);
			fmt_ptr = fmt;
			*fmt_ptr++ = '%';
			memcpy(fmt_ptr, &format[f], nflags);
			fmt_ptr += nflags;
			if (has_width)
				*fmt_ptr++ = '*';
			if (has_precision) {
				*fmt_ptr++ = '.';
				*fmt_ptr++ = '*';
			}
			*fmt_ptr++ = 's';
			*fmt_ptr = '\0';

			if (has_width && has_precision)
				printf(fmt, width, precision, arg);
			else if (has_width)
				printf(fmt, width, arg);
			else if (has_precision)
				printf(fmt, precision, arg);
			else
				printf(fmt, arg);
			free(fmt);
			break;
		case 'd': case 'i': case 'o': case 'u': case 'x': case 'X':
			for (j = 0; isspace(arg[j]); j++);
			if (arg[j] == '\'' || arg[j] == '\"') {
				arg += j + 1;
				unescape(arg);
				rarg = ereallocarray(NULL, utflen(arg) + 1, sizeof(*rarg));
				utftorunestr(arg, rarg);
				num = rarg[0];
			} else if (arg[0]) {
				errno = 0;
				if (format[i] == 'd' || format[i] == 'i')
					num = strtol(arg, &tmp, 0);
				else
					num = strtoul(arg, &tmp, 0);

				if (tmp == arg || *tmp != '\0') {
					ret = 1;
					weprintf("%%%c %s: conversion error\n",
					    format[i], arg);
				}
				if (errno == ERANGE) {
					ret = 1;
					weprintf("%%%c %s: out of range\n",
					    format[i], arg);
				}
			} else {
					num = 0;
			}
			fmt = emalloc(nflags + 15);
			fmt_ptr = fmt;
			*fmt_ptr++ = '%';
			memcpy(fmt_ptr, &format[f], nflags);
			fmt_ptr += nflags;
			if (has_width)
				*fmt_ptr++ = '*';
			if (has_precision) {
				*fmt_ptr++ = '.';
				*fmt_ptr++ = '*';
			}
			*fmt_ptr++ = 'l';
			*fmt_ptr++ = 'l';
			*fmt_ptr++ = format[i];
			*fmt_ptr = '\0';

			if (has_width && has_precision)
				printf(fmt, width, precision, num);
			else if (has_width)
				printf(fmt, width, num);
			else if (has_precision)
				printf(fmt, precision, num);
			else
				printf(fmt, num);
			free(fmt);
			break;
		case 'a': case 'A': case 'e': case 'E': case 'f': case 'F': case 'g': case 'G':
			fmt = emalloc(nflags + 15);
			fmt_ptr = fmt;
			*fmt_ptr++ = '%';
			memcpy(fmt_ptr, &format[f], nflags);
			fmt_ptr += nflags;
			if (has_width)
				*fmt_ptr++ = '*';
			if (has_precision) {
				*fmt_ptr++ = '.';
				*fmt_ptr++ = '*';
			}
			*fmt_ptr++ = format[i];
			*fmt_ptr = '\0';

			dou = (strlen(arg) > 0) ? estrtod(arg) : 0;
			if (has_width && has_precision)
				printf(fmt, width, precision, dou);
			else if (has_width)
				printf(fmt, width, dou);
			else if (has_precision)
				printf(fmt, precision, dou);
			else
				printf(fmt, dou);
			free(fmt);
			break;
		case '\0':
			eprintf("Missing format specifier.\n");
			break;
		default:
			eprintf("Invalid format specifier '%c'.\n", format[i]);
		}
		if (argi >= argc)
			cooldown = 1;
	}

	free(is_pct);
	return fshut(stdout, "<stdout>") | ret;
}
