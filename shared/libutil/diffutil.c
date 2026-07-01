/* See LICENSE file for copyright and license details. */
#include "../diffutil.h"
#include "../text.h"
#include "../util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
read_all(FILE *fp, char **out, size_t *outlen, const char *path)
{
	char buf[8192];
	char *data;
	size_t cap, len;
	ssize_t n;

	data = NULL;
	cap = len = 0;
	while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
		if (len + (size_t)n > cap) {
			cap = (len + (size_t)n) * 2 + 64;
			data = erealloc(data, cap);
		}
		memcpy(data + len, buf, (size_t)n);
		len += (size_t)n;
	}
	if (ferror(fp)) {
		weprintf("read %s:", path);
		free(data);
		return -1;
	}
	if (!data) {
		data = emalloc(1);
		data[0] = '\0';
	} else if (len == cap) {
		data = erealloc(data, cap + 1);
		data[len] = '\0';
	}
	*out = data;
	*outlen = len;
	return 0;
}

static void
split_lines(struct diffline **out, size_t *outn, char *data, size_t len)
{
	struct diffline *lines;
	size_t i, n, start;

	n = 0;
	for (i = 0; i < len; i++)
		if (data[i] == '\n')
			n++;
	if (len > 0 && (len == 0 || data[len - 1] != '\n'))
		n++;
	if (n == 0) {
		*out = NULL;
		*outn = 0;
		return;
	}
	lines = ecalloc(n, sizeof(*lines));
	i = 0;
	start = 0;
	for (n = 0; n < (size_t)len; n++) {
		if (data[n] == '\n') {
			lines[i].data = data + start;
			lines[i].len = n - start;
			i++;
			start = n + 1;
		}
	}
	if (start < len) {
		lines[i].data = data + start;
		lines[i].len = len - start;
		i++;
	}
	*out = lines;
	*outn = i;
}

int
diff_load(struct difffile *df, const char *path)
{
	FILE *fp;
	char *data;
	size_t len;

	if (!strcmp(path, "-")) {
		fp = stdin;
	} else if (!(fp = fopen(path, "r"))) {
		weprintf("open %s:", path);
		return -1;
	}
	if (read_all(fp, &data, &len, path) < 0) {
		if (fp != stdin)
			fclose(fp);
		return -1;
	}
	if (fp != stdin)
		fclose(fp);
	df->lines = NULL;
	df->nlines = 0;
	if (len == 0) {
		df->lines = ecalloc(1, sizeof(*df->lines));
		df->lines[0].data = data;
		df->lines[0].len = 0;
		df->nlines = 0;
		return 0;
	}
	split_lines(&df->lines, &df->nlines, data, len);
	{
		struct diffline *grown;
		grown = erealloc(df->lines, (df->nlines + 1) * sizeof(*grown));
		grown[df->nlines].data = data;
		grown[df->nlines].len = 0;
		df->lines = grown;
	}
	return 0;
}

void
diff_free(struct difffile *df)
{
	char *base;

	if (!df->lines)
		return;
	if (df->nlines == 0) {
		free(df->lines[0].data);
		free(df->lines);
		df->lines = NULL;
		df->nlines = 0;
		return;
	}
	base = df->lines[df->nlines].data;
	free(base);
	free(df->lines);
	df->lines = NULL;
	df->nlines = 0;
}

static int
lines_equal(const struct diffline *a, const struct diffline *b,
            const struct diffopts *opts)
{
	size_t i, j;

	if (opts && opts->ignore_blanks) {
		i = j = 0;
		while (i < a->len && j < b->len) {
			while (i < a->len && isspace((unsigned char)a->data[i]))
				i++;
			while (j < b->len && isspace((unsigned char)b->data[j]))
				j++;
			if (i >= a->len || j >= b->len)
				break;
			if (opts->ignore_case) {
				if (tolower((unsigned char)a->data[i]) !=
				    tolower((unsigned char)b->data[j]))
					return 0;
			} else if (a->data[i] != b->data[j]) {
				return 0;
			}
			i++;
			j++;
		}
		while (i < a->len && isspace((unsigned char)a->data[i]))
			i++;
		while (j < b->len && isspace((unsigned char)b->data[j]))
			j++;
		return i == a->len && j == b->len;
	}

	if (opts && opts->ignore_case) {
		if (a->len != b->len)
			return 0;
		for (i = 0; i < a->len; i++) {
			if (tolower((unsigned char)a->data[i]) !=
			    tolower((unsigned char)b->data[i]))
				return 0;
		}
		return 1;
	}

	if (a->len != b->len)
		return 0;
	return memcmp(a->data, b->data, a->len) == 0;
}

struct lcs_cell {
	int diag;
	int up;
	int left;
};

int
diff_compute(struct diffhunks *hunks, const struct difffile *old,
             const struct difffile *new, const struct diffopts *opts)
{
	struct lcs_cell *c;
	size_t n, m, i, j, k, cap;
	struct diffhunk *h;
	int *xs, *ys;
	size_t hfrom, hto, vfrom, vto;
	struct diffopts default_opts;

	if (!opts) {
		memset(&default_opts, 0, sizeof(default_opts));
		default_opts.context = 3;
		opts = &default_opts;
	}

	n = old->nlines;
	m = new->nlines;

	if (n == 0 && m == 0) {
		hunks->v = NULL;
		hunks->n = 0;
		return 0;
	}

	c = ecalloc((n + 1) * (m + 1), sizeof(*c));
	for (i = 0; i <= n; i++) {
		for (j = 0; j <= m; j++) {
			if (i == 0 && j == 0) {
				c[i * (m + 1) + j].diag = 0;
				continue;
			}
			if (i > 0 && j > 0 &&
			    lines_equal(&old->lines[i - 1], &new->lines[j - 1], opts)) {
				c[i * (m + 1) + j].diag = c[(i - 1) * (m + 1) + (j - 1)].diag + 1;
				c[i * (m + 1) + j].up = 0;
				c[i * (m + 1) + j].left = 0;
				continue;
			}
			if (i > 0 && (j == 0 || c[(i - 1) * (m + 1) + j].diag >=
				      c[i * (m + 1) + (j - 1)].diag)) {
				c[i * (m + 1) + j].diag = c[(i - 1) * (m + 1) + j].diag;
				c[i * (m + 1) + j].up = 1;
				c[i * (m + 1) + j].left = 0;
			} else {
				c[i * (m + 1) + j].diag = c[i * (m + 1) + (j - 1)].diag;
				c[i * (m + 1) + j].up = 0;
				c[i * (m + 1) + j].left = 1;
			}
		}
	}

	cap = n + m + 16;
	xs = ecalloc(cap, sizeof(*xs));
	ys = ecalloc(cap, sizeof(*ys));
	k = 0;
	i = n;
	j = m;
	while (i > 0 || j > 0) {
		if (i > 0 && j > 0 &&
		    lines_equal(&old->lines[i - 1], &new->lines[j - 1], opts)) {
			xs[k] = 0;
			ys[k] = 0;
			i--;
			j--;
		} else if (i > 0 && c[i * (m + 1) + j].up) {
			xs[k] = 1;
			ys[k] = 0;
			i--;
		} else {
			xs[k] = 0;
			ys[k] = 2;
			j--;
		}
		k++;
	}
	hunks->v = NULL;
	hunks->n = 0;
	h = NULL;
	i = 0;
	j = 0;
	{
		size_t step, idx;
		int op;
		step = k;
		while (step > 0) {
			idx = step - 1;
			op = xs[idx] | ys[idx];
			if (op == 0) {
				step--;
				i++;
				j++;
				continue;
			}
			hfrom = i;
			vfrom = j;
			hto = i;
			vto = j;
			while (step > 0) {
				idx = step - 1;
				op = xs[idx] | ys[idx];
				if (op == 0)
					break;
				if (xs[idx] == 1)
					hto++;
				if (ys[idx] == 2)
					vto++;
				step--;
			}
			i = hto;
			j = vto;
			hunks->v = erealloc(hunks->v, (hunks->n + 1) * sizeof(*h));
			h = &hunks->v[hunks->n];
			h->old.from = hfrom;
			h->old.to = hto;
			h->new.from = vfrom;
			h->new.to = vto;
			hunks->n++;
		}
	}
	free(c);
	free(xs);
	free(ys);
	return 0;
}

void
diff_hunks_free(struct diffhunks *hunks)
{
	if (!hunks)
		return;
	free(hunks->v);
	hunks->v = NULL;
	hunks->n = 0;
}

int
diff_looks_binary(const struct difffile *df)
{
	size_t i, scan;

	scan = 4096;
	for (i = 0; i < df->nlines && scan > 0; i++) {
		size_t take = df->lines[i].len < scan ? df->lines[i].len : scan;
		if (memchr(df->lines[i].data, '\0', take))
			return 1;
		scan -= take;
	}
	return 0;
}

static void
print_range(FILE *out, size_t from, size_t to)
{
	if (to - from <= 1)
		fprintf(out, "%zu", from + 1);
	else
		fprintf(out, "%zu,%zu", from + 1, to);
}

static void
print_lines(FILE *out, const struct diffline *lines, size_t from, size_t to,
            char prefix)
{
	size_t i;
	for (i = from; i < to; i++) {
		fputc(prefix, out);
		fwrite(lines[i].data, 1, lines[i].len, out);
		fputc('\n', out);
	}
}

static int
format_normal(FILE *out, const struct diffhunks *hunks,
              const struct difffile *old, const struct difffile *new)
{
	size_t i;
	for (i = 0; i < hunks->n; i++) {
		struct diffhunk *h = &hunks->v[i];
		size_t oldn = h->old.to - h->old.from;
		size_t newn = h->new.to - h->new.from;
		char oc;

		oc = oldn == 0 ? 'a' : (newn == 0 ? 'd' : 'c');
		print_range(out, h->old.from, h->old.to);
		fputc(oc, out);
		print_range(out, h->new.from, h->new.to);
		fputc('\n', out);
		if (oldn && newn) {
			print_lines(out, old->lines, h->old.from, h->old.to, '<');
			fprintf(out, "---\n");
			print_lines(out, new->lines, h->new.from, h->new.to, '>');
		} else if (oldn) {
			print_lines(out, old->lines, h->old.from, h->old.to, '<');
		} else {
			print_lines(out, new->lines, h->new.from, h->new.to, '>');
		}
	}
	return 0;
}

static void
emit_unified_hunk(FILE *out, const struct diffhunk *h,
                  const struct difffile *old, const struct difffile *new,
                  size_t ctx)
{
	size_t oldfrom, oldto, newfrom, newto;
	size_t i;

	oldfrom = h->old.from < ctx ? 0 : h->old.from - ctx;
	oldto = h->old.to + ctx;
	if (oldto > old->nlines)
		oldto = old->nlines;
	newfrom = h->new.from < ctx ? 0 : h->new.from - ctx;
	newto = h->new.to + ctx;
	if (newto > new->nlines)
		newto = new->nlines;

	if (oldto - oldfrom <= 1)
		fprintf(out, "@@ -%zu +%zu,%zu @@\n", oldfrom + 1, newfrom + 1,
		        newto - newfrom);
	else if (newto - newfrom <= 1)
		fprintf(out, "@@ -%zu,%zu +%zu @@\n", oldfrom + 1, oldto - oldfrom,
		        newfrom + 1);
	else
		fprintf(out, "@@ -%zu,%zu +%zu,%zu @@\n", oldfrom + 1,
		        oldto - oldfrom, newfrom + 1, newto - newfrom);

	for (i = oldfrom; i < h->old.from; i++) {
		fputc(' ', out);
		fwrite(old->lines[i].data, 1, old->lines[i].len, out);
		fputc('\n', out);
	}
	print_lines(out, old->lines, h->old.from, h->old.to, '-');
	print_lines(out, new->lines, h->new.from, h->new.to, '+');
	for (i = h->old.to; i < oldto; i++) {
		fputc(' ', out);
		fwrite(old->lines[i].data, 1, old->lines[i].len, out);
		fputc('\n', out);
	}
}

static int
format_unified(FILE *out, const struct diffhunks *hunks,
               const struct difffile *old, const struct difffile *new,
               const char *oldlabel, const char *newlabel, size_t ctx)
{
	size_t i;
	if (oldlabel)
		fprintf(out, "--- %s\n", oldlabel);
	if (newlabel)
		fprintf(out, "+++ %s\n", newlabel);
	for (i = 0; i < hunks->n; i++)
		emit_unified_hunk(out, &hunks->v[i], old, new, ctx);
	return 0;
}

static void
emit_context_hunk(FILE *out, const struct diffhunk *h,
                  const struct difffile *old, const struct difffile *new,
                  size_t ctx)
{
	size_t oldfrom, oldto, newfrom, newto;
	size_t i;

	oldfrom = h->old.from < ctx ? 0 : h->old.from - ctx;
	oldto = h->old.to + ctx;
	if (oldto > old->nlines)
		oldto = old->nlines;
	newfrom = h->new.from < ctx ? 0 : h->new.from - ctx;
	newto = h->new.to + ctx;
	if (newto > new->nlines)
		newto = new->nlines;

	fprintf(out, "***************\n");
	if (oldto - oldfrom <= 1)
		fprintf(out, "*** %zu ****\n", oldfrom + 1);
	else
		fprintf(out, "*** %zu,%zu ****\n", oldfrom + 1, oldto - oldfrom);
	for (i = oldfrom; i < h->old.from; i++) {
		fputc(' ', out);
		fwrite(old->lines[i].data, 1, old->lines[i].len, out);
		fputc('\n', out);
	}
	print_lines(out, old->lines, h->old.from, h->old.to, '-');
	for (i = h->old.to; i < oldto; i++) {
		fputc(' ', out);
		fwrite(old->lines[i].data, 1, old->lines[i].len, out);
		fputc('\n', out);
	}
	if (newto - newfrom <= 1)
		fprintf(out, "--- %zu ----\n", newfrom + 1);
	else
		fprintf(out, "--- %zu,%zu ----\n", newfrom + 1, newto - newfrom);
	for (i = newfrom; i < h->new.from; i++) {
		fputc(' ', out);
		fwrite(new->lines[i].data, 1, new->lines[i].len, out);
		fputc('\n', out);
	}
	print_lines(out, new->lines, h->new.from, h->new.to, '+');
	for (i = h->new.to; i < newto; i++) {
		fputc(' ', out);
		fwrite(new->lines[i].data, 1, new->lines[i].len, out);
		fputc('\n', out);
	}
}

static int
format_context(FILE *out, const struct diffhunks *hunks,
               const struct difffile *old, const struct difffile *new,
               const char *oldlabel, const char *newlabel, size_t ctx)
{
	size_t i;
	if (oldlabel)
		fprintf(out, "*** %s\n", oldlabel);
	if (newlabel)
		fprintf(out, "--- %s\n", newlabel);
	for (i = 0; i < hunks->n; i++)
		emit_context_hunk(out, &hunks->v[i], old, new, ctx);
	return 0;
}

static int
format_ed(FILE *out, const struct diffhunks *hunks,
          const struct difffile *old, const struct difffile *new)
{
	size_t i;
	(void)old;
	for (i = 0; i < hunks->n; i++) {
		struct diffhunk *h = &hunks->v[i];
		size_t oldn = h->old.to - h->old.from;
		size_t newn = h->new.to - h->new.from;
		char op;

		op = oldn == 0 ? 'a' : (newn == 0 ? 'd' : 'c');
		print_range(out, h->old.from, h->old.to);
		fputc(op, out);
		fputc('\n', out);
		if (op == 'a' || op == 'c') {
			size_t j;
			for (j = h->new.from; j < h->new.to; j++) {
				if (new->lines[j].len == 1 && new->lines[j].data[0] == '.')
					fprintf(out, "..\n");
				else if (new->lines[j].len == 0)
					fprintf(out, ".\n");
				else {
					fwrite(new->lines[j].data, 1, new->lines[j].len, out);
					fputc('\n', out);
				}
			}
			fprintf(out, ".\n");
		}
	}
	return 0;
}

int
diff_format(FILE *out, const struct diffhunks *hunks,
            const struct difffile *old, const struct difffile *new,
            const char *oldlabel, const char *newlabel,
            const struct diffopts *opts)
{
	struct diffopts default_opts;
	size_t ctx;

	if (!opts) {
		memset(&default_opts, 0, sizeof(default_opts));
		default_opts.context = 3;
		opts = &default_opts;
	}
	ctx = opts->context ? opts->context : 3;
	switch (opts->format) {
	case DIFF_NORMAL:
		return format_normal(out, hunks, old, new);
	case DIFF_UNIFIED:
		return format_unified(out, hunks, old, new, oldlabel, newlabel, ctx);
	case DIFF_CONTEXT:
		return format_context(out, hunks, old, new, oldlabel, newlabel, ctx);
	case DIFF_ED:
	case DIFF_RCSED:
		return format_ed(out, hunks, old, new);
	case DIFF_BRIEF:
		if (hunks->n > 0)
			return 1;
		return 0;
	}
	return 0;
}
