/* See LICENSE file for copyright and license details. */
#include "config.h"
#include "diffutil.h"
#include "util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct region {
        size_t from;
        size_t to;
};

struct pair_hunk {
        struct region old;
        struct region new;
};

enum hunk_kind {
        HUNK_NONE,
        HUNK_TYPE1,
        HUNK_TYPE2,
        HUNK_TYPE3,
};

struct three_hunk {
        enum hunk_kind kind;
        struct region r1;
        struct region r2;
        struct region r3;
        int dup;
};

static int aflag;
static int Tflag;
static int eflag;
static int Eflag;
static int xflag;
static int Xflag;
static int Aflag;
static int mflag;
static int iflag;
static int strip_cr;
static char *labels[3];
static int nlabels;

static void
usage(void)
{
        eprintf("usage: %s [-3aAeEimTxX] [-L label] file1 file2 file3\n", argv0);
}

static void
convert_hunks(const struct diffhunks *in, struct pair_hunk **out, size_t *outn)
{
        size_t i;
        struct pair_hunk *p;

        p = ecalloc(in->n + 1, sizeof(*p));
        for (i = 0; i < in->n; i++) {
                p[i].old.from = in->v[i].old.from;
                p[i].old.to = in->v[i].old.to;
                p[i].new.from = in->v[i].new.from;
                p[i].new.to = in->v[i].new.to;
        }
        *out = p;
        *outn = in->n;
}

static int
region_same(const struct difffile *a, struct region *ra,
            const struct difffile *b, struct region *rb)
{
        size_t i;
        if (ra->to - ra->from != rb->to - rb->from)
                return 0;
        for (i = 0; i < ra->to - ra->from; i++) {
                if (a->lines[ra->from + i].len != b->lines[rb->from + i].len)
                        return 0;
                if (memcmp(a->lines[ra->from + i].data,
                           b->lines[rb->from + i].data,
                           a->lines[ra->from + i].len) != 0)
                        return 0;
        }
        return 1;
}

static int
merge_hunks(const struct difffile *f1, const struct difffile *f2,
            const struct difffile *f3,
            const struct pair_hunk *d13, size_t n13,
            const struct pair_hunk *d23, size_t n23,
            struct three_hunk **out, size_t *outn)
{
        struct three_hunk *hs;
        size_t i, j, k, cap;
        int have1, have2;
        size_t a1, a2, b1, b2;

        cap = n13 + n23 + 4;
        hs = ecalloc(cap, sizeof(*hs));
        k = 0;
        i = j = 0;

        while (i < n13 || j < n23) {
                have1 = i < n13;
                have2 = j < n23;
                a1 = have1 ? d13[i].new.from : 0;
                a2 = have1 ? d13[i].new.to : 0;
                b1 = have2 ? d23[j].old.from : 0;
                b2 = have2 ? d23[j].old.to : 0;

                if (have1 && (!have2 || a2 <= b1)) {
                        if (k + 1 >= cap) {
                                cap *= 2;
                                hs = erealloc(hs, cap * sizeof(*hs));
                        }
                        hs[k].kind = HUNK_TYPE1;
                        hs[k].r1.from = d13[i].old.from;
                        hs[k].r1.to = d13[i].old.to;
                        hs[k].r2.from = d13[i].new.from;
                        hs[k].r2.to = d13[i].new.to;
                        hs[k].r3.from = d13[i].new.from;
                        hs[k].r3.to = d13[i].new.to;
                        hs[k].dup = 0;
                        k++;
                        i++;
                        continue;
                }
                if (have2 && (!have1 || b2 < a1)) {
                        if (k + 1 >= cap) {
                                cap *= 2;
                                hs = erealloc(hs, cap * sizeof(*hs));
                        }
                        hs[k].kind = HUNK_TYPE2;
                        hs[k].r1.from = d23[j].old.from;
                        hs[k].r1.to = d23[j].old.to;
                        hs[k].r2.from = d23[j].old.from;
                        hs[k].r2.to = d23[j].old.to;
                        hs[k].r3.from = d23[j].new.from;
                        hs[k].r3.to = d23[j].new.to;
                        hs[k].dup = 0;
                        k++;
                        j++;
                        continue;
                }
                {
                        struct region r2;
                        size_t extra_pre, extra_post;

                        r2.from = a1 < b1 ? a1 : b1;
                        r2.to = a2 > b2 ? a2 : b2;
                        extra_pre = (a1 > b1) ? (a1 - b1) : (b1 - a1);
                        extra_post = (a2 > b2) ? (a2 - b2) : (b2 - a2);
                        if (k + 1 >= cap) {
                                cap *= 2;
                                hs = erealloc(hs, cap * sizeof(*hs));
                        }
                        hs[k].kind = HUNK_TYPE3;
                        hs[k].r2 = r2;
                        if (a1 >= b1) {
                                hs[k].r1.from = d13[i].old.from - extra_pre;
                                hs[k].r1.to = d13[i].old.to + extra_post;
                        } else {
                                hs[k].r1.from = d13[i].old.from;
                                hs[k].r1.to = d13[i].old.to + (extra_pre + extra_post);
                        }
                        if (b1 >= a1) {
                                hs[k].r3.from = d23[j].new.from - extra_pre;
                                hs[k].r3.to = d23[j].new.to + extra_post;
                        } else {
                                hs[k].r3.from = d23[j].new.from;
                                hs[k].r3.to = d23[j].new.to + (extra_pre + extra_post);
                        }
                        hs[k].dup = region_same(f1, &hs[k].r1, f3, &hs[k].r3);
                        k++;
                        i++;
                        j++;
                }
        }
        *out = hs;
        *outn = k;
        (void)f1; (void)f2; (void)f3;
        return 0;
}

static void
print_lines(const struct difffile *f, size_t from, size_t to)
{
        size_t i;
        for (i = from; i < to; i++) {
                fwrite(f->lines[i].data, 1, f->lines[i].len, stdout);
                fputc('\n', stdout);
        }
}

static void
print_range_cmd(FILE *out, size_t from, size_t to, char op)
{
        if (to <= from) {
                fprintf(out, "%zua\n", from == 0 ? 0 : from);
        } else {
                fprintf(out, "%zu", from + 1);
                if (to - from > 1)
                        fprintf(out, ",%zu", to);
                fprintf(out, "%c\n", op);
        }
}

static int
format_normal(const struct three_hunk *hs, size_t n,
              const struct difffile *f1, const struct difffile *f2,
              const struct difffile *f3)
{
        size_t i;
        for (i = 0; i < n; i++) {
                const struct three_hunk *h = &hs[i];
                const char *tag = "";
                if (h->kind == HUNK_TYPE1) tag = "1";
                else if (h->kind == HUNK_TYPE2) tag = "2";
                else if (h->kind == HUNK_TYPE3) tag = h->dup ? "3" : "";
                printf("====%s\n", tag);
                if (h->kind == HUNK_TYPE1) {
                        printf("1:%zu", h->r1.from + 1);
                        if (h->r1.to - h->r1.from > 1)
                                printf(",%zu", h->r1.to);
                        printf("a\n");
                        printf("3:%zu", h->r3.from + 1);
                        if (h->r3.to - h->r3.from > 1)
                                printf(",%zu", h->r3.to);
                        printf("a\n");
                        print_lines(f1, h->r1.from, h->r1.to);
                } else if (h->kind == HUNK_TYPE2) {
                        printf("2:%zu", h->r2.from + 1);
                        if (h->r2.to - h->r2.from > 1)
                                printf(",%zu", h->r2.to);
                        printf("a\n");
                        printf("3:%zu", h->r3.from + 1);
                        if (h->r3.to - h->r3.from > 1)
                                printf(",%zu", h->r3.to);
                        printf("a\n");
                        print_lines(f3, h->r3.from, h->r3.to);
                } else {
                        printf("1:%zu", h->r1.from + 1);
                        if (h->r1.to - h->r1.from > 1)
                                printf(",%zu", h->r1.to);
                        printf("c\n");
                        printf("2:%zu", h->r2.from + 1);
                        if (h->r2.to - h->r2.from > 1)
                                printf(",%zu", h->r2.to);
                        printf("c\n");
                        printf("3:%zu", h->r3.from + 1);
                        if (h->r3.to - h->r3.from > 1)
                                printf(",%zu", h->r3.to);
                        printf("c\n");
                        print_lines(f1, h->r1.from, h->r1.to);
                        print_lines(f3, h->r3.from, h->r3.to);
                }
        }
        (void)f2;
        return 0;
}

static int
format_ed(const struct three_hunk *hs, size_t n, int overlap_only,
          int show_all, const struct difffile *f2, const struct difffile *f3)
{
        size_t i;
        for (i = n; i > 0; i--) {
                const struct three_hunk *h = &hs[i - 1];

                if (h->kind == HUNK_TYPE1) {
                        if (h->r1.to == h->r1.from) {
                                print_range_cmd(stdout, h->r2.from, h->r2.from, 'a');
                                print_lines(f2, h->r2.from, h->r2.from);
                                printf(".\n");
                        } else {
                                print_range_cmd(stdout, h->r2.from, h->r2.to, 'c');
                                print_lines(f2, h->r1.from, h->r1.to);
                                printf(".\n");
                        }
                        continue;
                }
                if (h->kind == HUNK_TYPE2) {
                        print_range_cmd(stdout, h->r2.from, h->r2.to, 'c');
                        print_lines(f3, h->r3.from, h->r3.to);
                        printf(".\n");
                        continue;
                }
                if (h->dup) {
                        print_range_cmd(stdout, h->r2.from, h->r2.to, 'c');
                        print_lines(f2, h->r1.from, h->r1.to);
                        printf(".\n");
                        continue;
                }
                if (overlap_only)
                        continue;
                if (show_all) {
                        print_range_cmd(stdout, h->r2.from, h->r2.to, 'c');
                        print_lines(f3, h->r3.from, h->r3.to);
                        printf(".\n");
                }
        }
        return 0;
}

#if FEATURE_DIFF3_MERGE
static int
format_merge(const struct three_hunk *hs, size_t n,
             const struct difffile *f1, const struct difffile *f2,
             const struct difffile *f3)
{
        size_t i, j;
        size_t cursor;
        const char *l1, *l2, *l3;

        l1 = nlabels >= 1 ? labels[0] : "file1";
        l2 = nlabels >= 2 ? labels[1] : "file2";
        l3 = nlabels >= 3 ? labels[2] : "file3";
        cursor = 0;
        for (i = 0; i < n; i++) {
                const struct three_hunk *h = &hs[i];
                for (j = cursor; j < h->r2.from; j++) {
                        fwrite(f2->lines[j].data, 1, f2->lines[j].len, stdout);
                        fputc('\n', stdout);
                }
                cursor = h->r2.to;
                if (h->kind == HUNK_TYPE1) {
                        print_lines(f1, h->r1.from, h->r1.to);
                } else if (h->kind == HUNK_TYPE2) {
                        print_lines(f3, h->r3.from, h->r3.to);
                } else if (h->dup) {
                        print_lines(f1, h->r1.from, h->r1.to);
                } else {
                        printf("<<<<<<< %s\n", l1);
                        print_lines(f1, h->r1.from, h->r1.to);
                        printf("||||||| %s\n", l2);
                        print_lines(f2, h->r2.from, h->r2.to);
                        printf("=======\n");
                        print_lines(f3, h->r3.from, h->r3.to);
                        printf(">>>>>>> %s\n", l3);
                }
        }
        for (j = cursor; j < f2->nlines; j++) {
                fwrite(f2->lines[j].data, 1, f2->lines[j].len, stdout);
                fputc('\n', stdout);
        }
        return 0;
}
#endif

// ?man diff3: three-way file comparison
// ?man arguments: file1 file2 file3
// ?man compare three files line by line and show the differences
int
main(int argc, char *argv[])
{
        struct difffile f1, f2, f3;
        struct pair_hunk *d13, *d23;
        size_t n13, n23;
        struct three_hunk *hs;
        size_t nhunks;
        struct diffhunks raw13, raw23;
        struct diffopts opts;
        int ret, conflicts;

        ARGBEGIN {
        case '3':
                // ?man -3: like -A but skip overlapping changes
                Aflag = 1;
                break;
        case 'a':
                // ?man -a: treat all files as text
                aflag = 1;
                break;
        case 'A':
                // ?man -A: emit ed script with all changes including overlaps
                Aflag = 1;
                break;
        case 'e':
                // ?man -e: emit ed script to apply non-overlapping changes
                eflag = 1;
                break;
        case 'E':
                // ?man -E: like -e but include overlap markers
                Eflag = 1;
                break;
#if FEATURE_DIFF3_MERGE
        case 'i':
                // ?man -i: ignore case when comparing lines
                iflag = 1;
                break;
        case 'L':
                // ?man -L:label: use label in place of file name in merge output
                if (nlabels >= 3)
                        usage();
                labels[nlabels++] = EARGF(usage());
                break;
        case 'm':
                // ?man -m: emit merged output with conflict markers
                mflag = 1;
                break;
        case 'T':
                // ?man -T: prefix output lines with a tab
                Tflag = 1;
                break;
#endif
        case 'x':
                // ?man -x: emit ed script for overlapping changes only
                xflag = 1;
                break;
        case 'X':
                // ?man -X: emit ed script for overlapping changes with markers
                Xflag = 1;
                break;
        default:
                usage();
        } ARGEND

        if (argc != 3)
                usage();

        if (diff_load(&f1, argv[0]) < 0)
                return 2;
        if (diff_load(&f2, argv[1]) < 0) {
                diff_free(&f1);
                return 2;
        }
        if (diff_load(&f3, argv[2]) < 0) {
                diff_free(&f1);
                diff_free(&f2);
                return 2;
        }

        memset(&opts, 0, sizeof(opts));
        opts.format = DIFF_NORMAL;
        opts.ignore_case = iflag;
        opts.strip_cr = strip_cr;
        opts.context = 3;

        if (diff_compute(&raw13, &f1, &f2, &opts) < 0 ||
            diff_compute(&raw23, &f2, &f3, &opts) < 0) {
                diff_free(&f1); diff_free(&f2); diff_free(&f3);
                return 2;
        }
        convert_hunks(&raw13, &d13, &n13);
        convert_hunks(&raw23, &d23, &n23);

        if (merge_hunks(&f1, &f2, &f3, d13, n13, d23, n23, &hs, &nhunks) < 0) {
                diff_hunks_free(&raw13);
                diff_hunks_free(&raw23);
                free(d13); free(d23);
                diff_free(&f1); diff_free(&f2); diff_free(&f3);
                return 2;
        }

        ret = 0;
        conflicts = 0;
        if (nhunks == 0) {
                ret = 0;
        } else {
#if FEATURE_DIFF3_MERGE
                if (mflag) {
                        size_t i;
                        for (i = 0; i < nhunks; i++)
                                if (hs[i].kind == HUNK_TYPE3 && !hs[i].dup)
                                        conflicts++;
                        format_merge(hs, nhunks, &f1, &f2, &f3);
                        ret = conflicts ? 1 : 0;
                } else
#endif
                if (eflag || Eflag) {
                        format_ed(hs, nhunks, Eflag && !eflag, 0, &f2, &f3);
                        ret = 1;
                } else if (xflag) {
                        format_ed(hs, nhunks, 1, 0, &f2, &f3);
                        ret = 1;
                } else if (Xflag) {
                        format_ed(hs, nhunks, 0, 1, &f2, &f3);
                        ret = 1;
                } else if (Aflag) {
                        format_ed(hs, nhunks, 0, 1, &f2, &f3);
                        ret = 1;
                } else {
                        format_normal(hs, nhunks, &f1, &f2, &f3);
                        ret = 1;
                }
        }

        if (fshut(stdout, "<stdout>"))
                ret = 2;

        diff_hunks_free(&raw13);
        diff_hunks_free(&raw23);
        free(d13); free(d23);
        free(hs);
        diff_free(&f1); diff_free(&f2); diff_free(&f3);
        return ret;
}
