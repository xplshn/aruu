/* See LICENSE file for copyright and license details. */
#ifndef ARUU_DIFFUTIL_H
#define ARUU_DIFFUTIL_H

#include <stdio.h>
#include <sys/types.h>

struct diffline {
	char *data;
	size_t len;
};

struct difffile {
	struct diffline *lines;
	size_t nlines;
};

struct diffrange {
	size_t from;
	size_t to;
};

struct diffhunk {
	struct diffrange old;
	struct diffrange new;
};

struct diffhunks {
	struct diffhunk *v;
	size_t n;
};

enum diffformat {
	DIFF_NORMAL,
	DIFF_UNIFIED,
	DIFF_CONTEXT,
	DIFF_ED,
	DIFF_RCSED,
	DIFF_BRIEF,
};

struct diffopts {
	enum diffformat format;
	size_t context;
	int ignore_case;
	int ignore_blanks;
	int strip_cr;
	int treat_binary;
};

#define DIFF_SAME   0
#define DIFF_DIFFER 1
#define DIFF_ERROR  2

int diff_load(struct difffile *df, const char *path);
void diff_free(struct difffile *df);
int diff_compute(struct diffhunks *hunks, const struct difffile *old,
                 const struct difffile *new, const struct diffopts *opts);
void diff_hunks_free(struct diffhunks *hunks);
int diff_format(FILE *out, const struct diffhunks *hunks,
                const struct difffile *old, const struct difffile *new,
                const char *oldlabel, const char *newlabel,
                const struct diffopts *opts);
int diff_looks_binary(const struct difffile *df);

#endif
