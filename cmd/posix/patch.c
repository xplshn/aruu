/* See LICENSE file for copyright and license details. */
#include "wexec.h"
#include "paths.h"
#include "util.h"

#include <sys/file.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <libgen.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEBUGGING

#define MAXHUNKSIZE 100000
#define INITHUNKMAX 125
#define INITLINELEN 8192
#define BUFFERSIZE 1024
#define LINENUM_MAX LONG_MAX

#define SCCSPREFIX "s."
#define GET "get -e %s"
#define SCCSDIFF "get -p %s | diff - %s >/dev/null"

#define RCSSUFFIX ",v"
#define CHECKOUT "co"
#define RCSDIFF "rcsdiff"

#define ORIGEXT ".orig"
#define REJEXT ".rej"

#define strNE(s1,s2) (strcmp(s1, s2))
#define strEQ(s1,s2) (!strcmp(s1, s2))
#define strnNE(s1,s2,l) (strncmp(s1, s2, l))
#define strnEQ(s1,s2,l) (!strncmp(s1, s2, l))

#define OLD_FILE 0
#define NEW_FILE 1
#define INDEX_FILE 2
#define MAX_FILE 3

#define CONTEXT_DIFF 1
#define NORMAL_DIFF 2
#define ED_DIFF 3
#define NEW_CONTEXT_DIFF 4
#define UNI_DIFF 5

#define _PATH_ED "ed"

#define ISDIGIT(c) (isascii((unsigned char)c) && isdigit((unsigned char)c))

#ifndef __UNCONST
#define __UNCONST(a) ((void *)(unsigned long)(const void *)(a))
#endif

#ifndef __dead
#define __dead __attribute__((__noreturn__))
#endif

typedef long LINENUM;

enum BackupType {
        BACKUP_UNDEFINED,
        BACKUP_NONE,
        BACKUP_SIMPLE,
        BACKUP_NUMBERED_EXISTING,
        BACKUP_NUMBERED
};

struct FileName {
        char *path;
        int exists;
};

mode_t filemode = 0644;
char *buf;
size_t bufsz;
int using_plan_a = 1;
int out_of_mem = 0;

#define MAXFILEC 2
char *filearg[MAXFILEC];
int ok_to_create_file = 0;
char *outname = NULL;
char *origprae = NULL;
char *TMPOUTNAME;
char *TMPINNAME;
char *TMPREJNAME;
char *TMPPATNAME;
int toutkeep = 0;
int trejkeep = 0;
int warn_on_invalid_line;
int last_line_missing_eol;

#ifdef DEBUGGING
int debug = 0;
#endif

int force = 0;
int batch = 0;
int verbose = 1;
int reverse = 0;
int noreverse = 0;
int skip_rest_of_patch = 0;
int strippath = 957;
int canonicalize = 0;
int check_only = 0;
int diff_type = 0;
char *revision = NULL;
LINENUM input_lines = 0;
int posix = 0;
int backup_if_mismatch = -1;

enum BackupType backup_type = BACKUP_UNDEFINED;
const char *simple_backup_suffix = "~";

static int remove_empty_files = 0;
static int reverse_flag_specified = 0;
static char rejname[PATH_MAX];
static char serrbuf[BUFSIZ];
static LINENUM last_frozen_line = 0;
static int Argc;
static char **Argv;
static int Argc_last;
static char **Argv_last;
static FILE *ofp = NULL;
static FILE *rejfp = NULL;
static int filec = 0;
static LINENUM last_offset = 0;
static LINENUM maxfuzz = 2;

static int do_defines = 0;
static char if_defined[128];
static char not_defined[128];
static const char else_defined[] = "#else\n";
static char end_defined[128];

/* function declarations */
static void reinitialize_almost_everything(void);
static void get_some_switches(void);
static void reset_getopt_state(void);
static LINENUM locate_hunk(LINENUM);
static void rej_line(int, LINENUM);
static void abort_hunk(void);
static void apply_hunk(LINENUM);
static void init_output(const char *);
static void init_reject(const char *);
static void copy_till(LINENUM, int);
static int spew_output(void);
static void dump_input_line(LINENUM, int);
static int patch_match(LINENUM, LINENUM, LINENUM);
static int similar(const char *, const char *, ssize_t);
static void usage(void);

void re_patch(void);
void open_patch_file(const char *);
void set_hunkmax(void);
int there_is_another_patch(void);
int another_hunk(void);
int pch_swap(void);
char *pfetch(LINENUM);
ssize_t pch_line_len(LINENUM);
LINENUM pch_first(void);
LINENUM pch_ptrn_lines(void);
LINENUM pch_newfirst(void);
LINENUM pch_repl_lines(void);
LINENUM pch_end(void);
LINENUM pch_context(void);
LINENUM pch_hunk_beg(void);
char pch_char(LINENUM);
void do_ed_script(void);

void re_input(void);
void scan_input(const char *);
char *ifetch(LINENUM, int);

char *fetchname(const char *, int *, int);
char *checked_in(char *);
LINENUM strtolinenum(char *, char **);
int backup_file(const char *);
int move_file(const char *, const char *);
int copy_file(const char *, const char *);
void say(const char *, ...);
__dead void fatal(const char *, ...);
__dead void pfatal(const char *, ...);
void ask(const char *, ...);
char *savestr(const char *);
void set_signals(int);
void ignore_signals(void);
void makedirs(const char *, int);
__dead void version(void);
__dead void my_exit(int);
static void *pch_realloc(void *, size_t, size_t);
int mkpath(char *);
char *find_backup_file_name(const char *);
enum BackupType get_version(const char *);

static off_t i_size;
static char *i_womp;
static char **i_ptr;
static char empty_line[] = { '\0' };

static int tifd = -1;
static char *tibuf[2];
static LINENUM tiline[2] = {-1, -1};
static LINENUM lines_per_buf;
static int tireclen;

static int rev_in_string(const char *);
static int reallocate_lines(size_t *);
static int plan_a(const char *);
static void plan_b(const char *);

static long p_filesize;
static LINENUM p_first;
static LINENUM p_newfirst;
static LINENUM p_ptrn_lines;
static LINENUM p_repl_lines;
static LINENUM p_end = -1;
static LINENUM p_max;
static LINENUM p_context = 3;
static LINENUM p_input_line = 0;
static char **p_line = NULL;
static ssize_t *p_len = NULL;
static char *p_char = NULL;
static int hunkmax = INITHUNKMAX;
static int p_indent;
static LINENUM p_base;
static LINENUM p_bline;
static LINENUM p_start;
static LINENUM p_sline;
static LINENUM p_hunk_beg;
static LINENUM p_efake = -1;
static LINENUM p_bfake = -1;
static FILE *pfp = NULL;
static char *bestguess = NULL;

static void grow_hunkmax(void);
static int intuit_diff_type(void);
static void next_intuit_at(LINENUM, LINENUM);
static void skip_to(LINENUM, LINENUM);
static int pgetline(char **, size_t *, FILE *);
static char *best_name(const struct FileName *, int);
static char *posix_name(const struct FileName *, int);
static size_t num_components(const char *);

static char *patch_concat(const char *, const char *);
static char *make_version_name(const char *, int);
static int max_backup_version(const char *, const char *);
static int version_number(const char *, const char *, size_t);
static int argmatch(const char *, const char **);
static void invalid_arg(const char *, const char *, int);

static const char *backup_args[] = {
        "none", "never", "simple", "nil", "existing", "t", "numbered", 0
};
static enum BackupType backup_types[] = {
        BACKUP_NONE, BACKUP_SIMPLE, BACKUP_SIMPLE, BACKUP_NUMBERED_EXISTING,
        BACKUP_NUMBERED_EXISTING, BACKUP_NUMBERED, BACKUP_NUMBERED
};

/* implementation */

// ?man patch: apply a diff file to an original
int
main(int argc, char *argv[])
{
        int error, hunk, failed, i, fd;
        LINENUM where, newwhere, fuzz, mymaxfuzz;
        const char *tmpdir;
        char *v;

        error = 0;
        where = 0;
        bufsz = INITLINELEN;
        if ((buf = malloc(bufsz)) == NULL)
                pfatal("allocating input buffer");
        buf[0] = '\0';

        setbuf(stderr, serrbuf);
        for (i = 0; i < MAXFILEC; i++)
                filearg[i] = NULL;

        if ((tmpdir = getenv("TMPDIR")) == NULL || *tmpdir == '\0')
                tmpdir = ARUU_PATH_TMP;
        for (i = strlen(tmpdir) - 1; i > 0 && tmpdir[i] == '/'; i--)
                ;
        i++;
        if (asprintf(&TMPOUTNAME, "%.*s/patchoXXXXXXXXXX", i, tmpdir) == -1)
                fatal("cannot allocate memory");
        if ((fd = mkstemp(TMPOUTNAME)) < 0)
                pfatal("cant create %s", TMPOUTNAME);
        close(fd);

        if (asprintf(&TMPINNAME, "%.*s/patchiXXXXXXXXXX", i, tmpdir) == -1)
                fatal("cannot allocate memory");
        if ((fd = mkstemp(TMPINNAME)) < 0)
                pfatal("cant create %s", TMPINNAME);
        close(fd);

        if (asprintf(&TMPREJNAME, "%.*s/patchrXXXXXXXXXX", i, tmpdir) == -1)
                fatal("cannot allocate memory");
        if ((fd = mkstemp(TMPREJNAME)) < 0)
                pfatal("cant create %s", TMPREJNAME);
        close(fd);

        if (asprintf(&TMPPATNAME, "%.*s/patchpXXXXXXXXXX", i, tmpdir) == -1)
                fatal("cannot allocate memory");
        if ((fd = mkstemp(TMPPATNAME)) < 0)
                pfatal("cant create %s", TMPPATNAME);
        close(fd);

        v = getenv("SIMPLE_BACKUP_SUFFIX");
        if (v)
                simple_backup_suffix = v;
        else
                simple_backup_suffix = ORIGEXT;

        if ((v = getenv("PATCH_VERSION_CONTROL")) == NULL)
                v = getenv("VERSION_CONTROL");
        if (v != NULL)
                backup_type = get_version(v);

        Argc = argc;
        Argv = argv;
        get_some_switches();

        if (backup_type == BACKUP_UNDEFINED)
                backup_type = posix ? BACKUP_NONE : BACKUP_NUMBERED_EXISTING;

        set_signals(0);

        for (open_patch_file(filearg[1]); there_is_another_patch(); reinitialize_almost_everything()) {
                warn_on_invalid_line = 1;
                if (outname == NULL)
                        outname = savestr(filearg[0]);

                if (diff_type == ED_DIFF) {
                        do_ed_script();
                        continue;
                }
                if (!skip_rest_of_patch)
                        init_output(TMPOUTNAME);

                init_reject(TMPREJNAME);

                if (!skip_rest_of_patch)
                        scan_input(filearg[0]);

                hunk = 0;
                failed = 0;
                out_of_mem = 0;
                while (another_hunk()) {
                        hunk++;
                        fuzz = 0;
                        mymaxfuzz = pch_context();
                        if (maxfuzz < mymaxfuzz)
                                mymaxfuzz = maxfuzz;
                        if (!skip_rest_of_patch) {
                                do {
                                        where = locate_hunk(fuzz);
                                        if (hunk == 1 && where == 0 && !force) {
                                                if (!pch_swap()) {
                                                        if (fuzz == 0)
                                                                say("Not enough memory to try swapped hunk! Assuming unswapped.\n");
                                                        continue;
                                                }
                                                reverse = !reverse;
                                                where = locate_hunk(fuzz);
                                                if (where == 0) {
                                                        if (!pch_swap())
                                                                fatal("lost hunk on alloc error!\n");
                                                        reverse = !reverse;
                                                } else if (noreverse) {
                                                        if (!pch_swap())
                                                                fatal("lost hunk on alloc error!\n");
                                                        reverse = !reverse;
                                                        say("Ignoring previously applied (or reversed) patch.\n");
                                                        skip_rest_of_patch = 1;
                                                } else if (batch) {
                                                        if (verbose)
                                                                say("%seversed (or %spreviously applied) patch detected! %s -R.",
                                                                    reverse ? "R" : "Unr",
                                                                    reverse ? "" : "not ",
                                                                    reverse ? "Assuming" : "Ignoring");
                                                } else {
                                                        ask("%seversed (or %spreviously applied) patch detected! %s -R? [y] ",
                                                            reverse ? "R" : "Unr",
                                                            reverse ? "" : "not ",
                                                            reverse ? "Assume" : "Ignore");
                                                        if (*buf == 'n') {
                                                                ask("Apply anyway? [n] ");
                                                                if (*buf != 'y')
                                                                        skip_rest_of_patch = 1;
                                                                where = 0;
                                                                reverse = !reverse;
                                                                if (!pch_swap())
                                                                        fatal("lost hunk on alloc error!\n");
                                                        }
                                                }
                                        }
                                } while (!skip_rest_of_patch && where == 0 && ++fuzz <= mymaxfuzz);

                                if (skip_rest_of_patch) {
                                        if (ferror(ofp) || fclose(ofp)) {
                                                say("Error writing %s\n", TMPOUTNAME);
                                                error = 1;
                                        }
                                        ofp = NULL;
                                }
                        }
                        newwhere = pch_newfirst() + last_offset;
                        if (skip_rest_of_patch) {
                                abort_hunk();
                                failed++;
                                if (verbose)
                                        say("Hunk #%d ignored at %ld.\n", hunk, newwhere);
                        } else if (where == 0) {
                                abort_hunk();
                                failed++;
                                if (verbose)
                                        say("Hunk #%d failed at %ld.\n", hunk, newwhere);
                        } else {
                                apply_hunk(where);
                                if (verbose) {
                                        say("Hunk #%d succeeded at %ld", hunk, newwhere);
                                        if (fuzz != 0)
                                                say(" with fuzz %ld", fuzz);
                                        if (last_offset)
                                                say(" (offset %ld line%s)", last_offset, last_offset == 1L ? "" : "s");
                                        say(".\n");
                                }
                        }
                }

                if (out_of_mem && using_plan_a) {
                        Argc = Argc_last;
                        Argv = Argv_last;
                        say("\n\nRan out of memory using Plan A--trying again...\n\n");
                        if (ofp)
                                fclose(ofp);
                        ofp = NULL;
                        if (rejfp)
                                fclose(rejfp);
                        rejfp = NULL;
                        continue;
                }
                if (hunk == 0)
                        fatal("internal error: hunk should not be 0\n");

                if (!skip_rest_of_patch && !spew_output()) {
                        say("cant write %s\n", TMPOUTNAME);
                        error = 1;
                }

                ignore_signals();
                if (!skip_rest_of_patch) {
                        struct stat statbuf;
                        char *realout = outname;

                        if (!check_only) {
                                enum BackupType saved = backup_type;
                                if (failed > 0 && backup_if_mismatch > 0 && backup_type == BACKUP_NONE)
                                        backup_type = BACKUP_SIMPLE;
                                if (move_file(TMPOUTNAME, outname) < 0) {
                                        toutkeep = 1;
                                        realout = TMPOUTNAME;
                                        chmod(TMPOUTNAME, filemode);
                                } else
                                        chmod(outname, filemode);
                                backup_type = saved;

                                if (remove_empty_files && stat(realout, &statbuf) == 0 && statbuf.st_size == 0) {
                                        if (verbose)
                                                say("Removing %s (empty after patching).\n", realout);
                                        unlink(realout);
                                }
                        }
                }
                if (ferror(rejfp) || fclose(rejfp)) {
                        say("Error writing %s\n", rejname);
                        error = 1;
                }
                rejfp = NULL;
                if (failed) {
                        error = 1;
                        if (*rejname == '\0') {
                                if (strlcpy(rejname, outname, sizeof(rejname)) >= sizeof(rejname))
                                        fatal("filename %s is too long\n", outname);
                                if (strlcat(rejname, REJEXT, sizeof(rejname)) >= sizeof(rejname))
                                        fatal("filename %s is too long\n", outname);
                        }
                        if (skip_rest_of_patch) {
                                say("%d out of %d hunks ignored--saving rejects to %s\n", failed, hunk, rejname);
                        } else {
                                say("%d out of %d hunks FAILED -- saving rejects to %s\n", failed, hunk, rejname);
                        }
                        if (!check_only && move_file(TMPREJNAME, rejname) < 0)
                                trejkeep = 1;
                }
                set_signals(1);
        }
        my_exit(error);
}

static void
reset_getopt_state(void)
{
#if defined(__GLIBC__) || defined(__linux__)
        optind = 0;
#else
        optreset = 1;
        optind = 1;
#endif
}

static void
reinitialize_almost_everything(void)
{
        re_patch();
        re_input();

        input_lines = 0;
        last_frozen_line = 0;
        filec = 0;
        if (!out_of_mem) {
                free(filearg[0]);
                filearg[0] = NULL;
        }
        free(outname);
        outname = NULL;
        last_offset = 0;
        diff_type = 0;
        free(revision);
        revision = NULL;
        reverse = reverse_flag_specified;
        skip_rest_of_patch = 0;
        get_some_switches();
}

static void
get_some_switches(void)
{
        const char *options = "b::B:cCd:D:eEfF:i:lnNo:p:r:RstuvV:x:z:";
        static struct option longopts[] = {
                {"backup",              no_argument,            0,      'b'},
                {"backup-if-mismatch",  no_argument,            &backup_if_mismatch, 1},
                {"batch",               no_argument,            0,      't'},
                {"check",               no_argument,            0,      'C'},
                {"context",             no_argument,            0,      'c'},
                {"debug",               required_argument,      0,      'x'},
                {"directory",           required_argument,      0,      'd'},
                {"ed",                  no_argument,            0,      'e'},
                {"force",               no_argument,            0,      'f'},
                {"forward",             no_argument,            0,      'N'},
                {"fuzz",                required_argument,      0,      'F'},
                {"ifdef",               required_argument,      0,      'D'},
                {"input",               required_argument,      0,      'i'},
                {"ignore-whitespace",   no_argument,            0,      'l'},
                {"no-backup-if-mismatch",       no_argument,    &backup_if_mismatch, 0},
                {"normal",              no_argument,            0,      'n'},
                {"output",              required_argument,      0,      'o'},
                {"prefix",              required_argument,      0,      'B'},
                {"quiet",               no_argument,            0,      's'},
                {"reject-file",         required_argument,      0,      'r'},
                {"remove-empty-files",  no_argument,            0,      'E'},
                {"reverse",             no_argument,            0,      'R'},
                {"silent",              no_argument,            0,      's'},
                {"strip",               required_argument,      0,      'p'},
                {"suffix",              required_argument,      0,      'z'},
                {"unified",             no_argument,            0,      'u'},
                {"version",             no_argument,            0,      'v'},
                {"version-control",     required_argument,      0,      'V'},
                {"posix",               no_argument,            &posix, 1},
                {NULL,                  0,                      0,      0}
        };
        int ch;

        rejname[0] = '\0';
        Argc_last = Argc;
        Argv_last = Argv;
        if (!Argc)
                return;
        reset_getopt_state();
        while ((ch = getopt_long(Argc, Argv, options, longopts, NULL)) != -1) {
                switch (ch) {
                case 'b':
                        if (backup_type == BACKUP_UNDEFINED)
                                backup_type = BACKUP_NUMBERED_EXISTING;
                        if (optarg == NULL)
                                break;
                        if (verbose)
                                say("Warning, the -b suffix option has been obsoleted by the -z option\n");
                        /* FALLTHROUGH */
                case 'z':
                        simple_backup_suffix = savestr(optarg);
                        break;
                case 'B':
                        origprae = savestr(optarg);
                        break;
                case 'c':
                        diff_type = CONTEXT_DIFF;
                        break;
                case 'C':
                        check_only = 1;
                        break;
                case 'd':
                        if (chdir(optarg) < 0)
                                pfatal("cant cd to %s", optarg);
                        break;
                case 'D':
                        do_defines = 1;
                        if (!isalpha((unsigned char)*optarg) && *optarg != '_')
                                fatal("argument to -D is not an identifier\n");
                        snprintf(if_defined, sizeof if_defined, "#ifdef %s\n", optarg);
                        snprintf(not_defined, sizeof not_defined, "#ifndef %s\n", optarg);
                        snprintf(end_defined, sizeof end_defined, "#endif /* %s */\n", optarg);
                        break;
                case 'e':
                        diff_type = ED_DIFF;
                        break;
                case 'E':
                        remove_empty_files = 1;
                        break;
                case 'f':
                        force = 1;
                        break;
                case 'F':
                        maxfuzz = strtolinenum(optarg, NULL);
                        break;
                case 'i':
                        free(filearg[1]);
                        filearg[1] = fetchname(optarg, &warn_on_invalid_line, 0);
                        break;
                case 'l':
                        canonicalize = 1;
                        break;
                case 'n':
                        diff_type = NORMAL_DIFF;
                        break;
                case 'N':
                        noreverse = 1;
                        break;
                case 'o':
                        outname = savestr(optarg);
                        break;
                case 'p':
                        strippath = (int)strtolinenum(optarg, NULL);
                        break;
                case 'r':
                        if (strlcpy(rejname, optarg, sizeof(rejname)) >= sizeof(rejname))
                                fatal("reject filename %s too long\n", optarg);
                        break;
                case 'R':
                        reverse = 1;
                        reverse_flag_specified = 1;
                        break;
                case 's':
                        verbose = 0;
                        break;
                case 't':
                        batch = 1;
                        break;
                case 'u':
                        diff_type = UNI_DIFF;
                        break;
                case 'v':
                        version();
                        break;
                case 'V':
                        backup_type = get_version(optarg);
                        break;
                case 'x':
#ifdef DEBUGGING
                        debug = (int)strtolinenum(optarg, NULL);
#endif
                        break;
                default:
                        if (ch == 0)
                                break;
                        usage();
                }
        }
        Argc -= optind;
        Argv += optind;

        for (ch = 0; ch < Argc; ch++) {
                if (filec >= MAXFILEC)
                        usage();
                filearg[filec++] = savestr(Argv[ch]);
        }
}

static void
usage(void)
{
        fprintf(stderr, "usage: patch [-bCcEeflNnRstuv] [-B backup-prefix] [-D symbol] "
            "[-d directory] [-F max-fuzz] [-i patchfile] [-o out-file] "
            "[-p strip-count] [-r rej-name] [-V method] [-x number] "
            "[-z backup-ext] [origfile [patchfile]]\n");
        my_exit(2);
}

static LINENUM
locate_hunk(LINENUM fuzz)
{
        LINENUM first_hunk_line = pch_first();
        LINENUM count = pch_ptrn_lines();
        LINENUM max_line = input_lines - count + 1;
        LINENUM line;
        LINENUM offset;

        if (!count)
                return first_hunk_line + last_offset;

        if (first_hunk_line + last_offset <= 0)
                offset = 1 - first_hunk_line;
        else if (first_hunk_line + last_offset > max_line)
                offset = max_line - first_hunk_line;
        else
                offset = last_offset;

        for (line = first_hunk_line + offset; line <= max_line; line++) {
                if (patch_match(line, count, fuzz)) {
                        last_offset = line - first_hunk_line;
                        return line;
                }
        }
        for (line = first_hunk_line + offset - 1; line >= 1; line--) {
                if (patch_match(line, count, fuzz)) {
                        last_offset = line - first_hunk_line;
                        return line;
                }
        }
        return 0;
}

static int
patch_match(LINENUM line, LINENUM count, LINENUM fuzz)
{
        LINENUM i;
        LINENUM pat_line;
        char *str;
        char *pat;
        ssize_t len;

        for (i = 1; i <= count; i++) {
                pat_line = i;
                if (i <= fuzz)
                        continue;
                if (i > count - fuzz)
                        continue;
                str = ifetch(line + i - 1 - fuzz, 0);
                if (str == NULL)
                        return 0;
                pat = pfetch(pat_line);
                len = pch_line_len(pat_line);
                if (canonicalize) {
                        if (!similar(str, pat, len))
                                return 0;
                } else if (strncmp(str, pat, len) != 0)
                        return 0;
        }
        return 1;
}

static int
similar(const char *a, const char *b, ssize_t len)
{
        while (len > 0) {
                if (isspace((unsigned char)*b)) {
                        if (!isspace((unsigned char)*a))
                                return 0;
                        while (len > 0 && isspace((unsigned char)*b)) {
                                b++;
                                len--;
                        }
                        while (isspace((unsigned char)*a) && *a != '\n')
                                a++;
                } else {
                        if (*a != *b)
                                return 0;
                        a++;
                        b++;
                        len--;
                }
        }
        if (*a == '\n' || *a == '\r') {
                while (*a == '\n' || *a == '\r')
                        a++;
                return *a == '\0';
        }
        return *a == '\0' || isspace((unsigned char)*a);
}

static void
abort_hunk(void)
{
        LINENUM i;

        for (i = 1; i <= pch_end(); i++)
                rej_line(i, i);
}

static void
rej_line(int pat_line, LINENUM i)
{
        char *str = pfetch(pat_line);
        char ch = pch_char(pat_line);
        (void)i;
        if (rejfp != NULL) {
                if (ch == ' ')
                        fprintf(rejfp, " %s", str);
                else
                        fprintf(rejfp, "%c%s", ch, str);
        }
}

static void
apply_hunk(LINENUM where)
{
        LINENUM old = 1;
        const LINENUM lastline = pch_ptrn_lines();
        LINENUM new = lastline + 1;
#define OUTSIDE 0
#define IN_IFNDEF 1
#define IN_IFDEF 2
#define IN_ELSE 3
        int def_state = OUTSIDE;
        const LINENUM pat_end = pch_end();

        where--;
        while (pch_char(new) == '=' || pch_char(new) == '\n')
                new++;

        while (old <= lastline) {
                if (pch_char(old) == '-') {
                        copy_till(where + old - 1, 0);
                        if (do_defines) {
                                if (def_state == OUTSIDE) {
                                        fputs(not_defined, ofp);
                                        def_state = IN_IFNDEF;
                                } else if (def_state == IN_IFDEF) {
                                        fputs(else_defined, ofp);
                                        def_state = IN_ELSE;
                                }
                                fputs(pfetch(old), ofp);
                        }
                        last_frozen_line++;
                        old++;
                } else if (new > pat_end) {
                        break;
                } else if (pch_char(new) == '+') {
                        copy_till(where + old - 1, 0);
                        if (do_defines) {
                                if (def_state == IN_IFNDEF) {
                                        fputs(else_defined, ofp);
                                        def_state = IN_ELSE;
                                } else if (def_state == OUTSIDE) {
                                        fputs(if_defined, ofp);
                                        def_state = IN_IFDEF;
                                }
                        }
                        fputs(pfetch(new), ofp);
                        new++;
                } else if (pch_char(new) != pch_char(old)) {
                        say("Out-of-sync patch, lines %ld,%ld--mangled text or line numbers, maybe?\n",
                            pch_hunk_beg() + old,
                            pch_hunk_beg() + new);
                        my_exit(2);
                } else if (pch_char(new) == '!') {
                        copy_till(where + old - 1, 0);
                        if (do_defines) {
                                fputs(not_defined, ofp);
                                def_state = IN_IFNDEF;
                        }
                        while (pch_char(old) == '!') {
                                if (do_defines) {
                                        fputs(pfetch(old), ofp);
                                }
                                last_frozen_line++;
                                old++;
                        }
                        if (do_defines) {
                                fputs(else_defined, ofp);
                                def_state = IN_ELSE;
                        }
                        while (pch_char(new) == '!') {
                                fputs(pfetch(new), ofp);
                                new++;
                        }
                } else {
                        if (pch_char(new) != ' ')
                                fatal("Internal error: expected ' '\n");
                        old++;
                        new++;
                        if (do_defines && def_state != OUTSIDE) {
                                fputs(end_defined, ofp);
                                def_state = OUTSIDE;
                        }
                }
        }
        if (new <= pat_end && pch_char(new) == '+') {
                copy_till(where + old - 1, 0);
                if (do_defines) {
                        if (def_state == OUTSIDE) {
                                fputs(if_defined, ofp);
                                def_state = IN_IFDEF;
                        } else if (def_state == IN_IFNDEF) {
                                fputs(else_defined, ofp);
                                def_state = IN_ELSE;
                        }
                }
                while (new <= pat_end && pch_char(new) == '+') {
                        fputs(pfetch(new), ofp);
                        new++;
                }
        }
        if (do_defines && def_state != OUTSIDE) {
                fputs(end_defined, ofp);
        }
#undef OUTSIDE
#undef IN_IFNDEF
#undef IN_IFDEF
#undef IN_ELSE
}

static void
dump_input_line(LINENUM line, int write_newline)
{
        char *s = ifetch(line, 0);
        if (s == NULL)
                return;
        for (; *s != '\n' && *s != '\0'; s++)
                putc(*s, ofp);
        if (write_newline)
                putc('\n', ofp);
}

static void
copy_till(LINENUM line, int end)
{
        if (last_frozen_line > line)
                fatal("misordered hunks! output would be garbled\n");
        while (last_frozen_line < line) {
                if (++last_frozen_line == line && end)
                        dump_input_line(last_frozen_line, !last_line_missing_eol);
                else
                        dump_input_line(last_frozen_line, 1);
        }
}

static int
spew_output(void)
{
        if (input_lines)
                copy_till(input_lines, 1);
        if (ferror(ofp) || fclose(ofp))
                return 0;
        ofp = NULL;
        return 1;
}

static void
init_output(const char *name)
{
        ofp = fopen(name, "w");
        if (ofp == NULL)
                pfatal("cant create %s", name);
}

static void
init_reject(const char *name)
{
        rejfp = fopen(name, "w");
        if (rejfp == NULL)
                pfatal("cant create %s", name);
}

void
re_input(void)
{
        if (using_plan_a) {
                i_size = 0;
                free(i_ptr);
                i_ptr = NULL;
                if (i_womp != NULL) {
                        munmap(i_womp, i_size);
                        i_womp = NULL;
                }
        } else {
                using_plan_a = 1;
                close(tifd);
                tifd = -1;
                free(tibuf[0]);
                free(tibuf[1]);
                tibuf[0] = tibuf[1] = NULL;
                tiline[0] = tiline[1] = -1;
                tireclen = 0;
        }
}

void
scan_input(const char *filename)
{
        if (!plan_a(filename))
                plan_b(filename);
        if (verbose) {
                say("Patching file %s using Plan %s...\n", filename, (using_plan_a ? "A" : "B"));
        }
}

static int
reallocate_lines(size_t *lines_allocated)
{
        char **p;
        size_t new_size;

        new_size = *lines_allocated * 3 / 2;
        p = pch_realloc(i_ptr, new_size + 2, sizeof(char *));
        if (p == NULL) {
                munmap(i_womp, i_size);
                i_womp = NULL;
                free(i_ptr);
                i_ptr = NULL;
                *lines_allocated = 0;
                return 0;
        }
        *lines_allocated = new_size;
        i_ptr = p;
        return 1;
}

static int
plan_a(const char *filename)
{
        int ifd, statfailed;
        char *p, *s, *lbuf;
        struct stat filestat;
        off_t i;
        ptrdiff_t sz;
        size_t iline, lines_allocated, lbufsz;

#ifdef DEBUGGING
        if (debug & 8)
                return 0;
#endif

        if (filename == NULL || *filename == '\0')
                return 0;

        statfailed = stat(filename, &filestat);
        if (statfailed && ok_to_create_file) {
                if (verbose)
                        say("(Creating file %s...)\n", filename);
                if (check_only)
                        return 1;
                makedirs(filename, 1);
                close(creat(filename, 0666));
                statfailed = stat(filename, &filestat);
        }
        if (statfailed && check_only)
                fatal("%s not found, -C mode, cant probe further\n", filename);

        if (statfailed || (filestat.st_mode & 0222) == 0 ||
            ((filestat.st_mode & 0022) == 0 && filestat.st_uid != getuid())) {
                char *filebase, *filedir;
                struct stat cstat;
                char *tmp_filename1, *tmp_filename2;

                tmp_filename1 = strdup(filename);
                tmp_filename2 = strdup(filename);
                if (tmp_filename1 == NULL || tmp_filename2 == NULL)
                        fatal("strdupping filename");
                filebase = basename(tmp_filename1);
                filedir = dirname(tmp_filename2);

                lbufsz = INITLINELEN;
                if ((lbuf = malloc(bufsz)) == NULL)
                        pfatal("allocating line buffer");
                lbuf[0] = '\0';

#define try(f, a1, a2, a3) \
        (snprintf(lbuf, lbufsz, f, a1, a2, a3), stat(lbuf, &cstat) == 0)

                if (try("%s/RCS/%s%s", filedir, filebase, RCSSUFFIX) ||
                    try("%s/RCS/%s%s", filedir, filebase, "") ||
                    try("%s/%s%s", filedir, filebase, RCSSUFFIX)) {
                        if (!statfailed) {
                                if ((filestat.st_mode & 0222) != 0)
                                        fatal("file %s seems to be locked by somebody else under RCS\n", filename);
                                if (verbose)
                                        say("Comparing file %s to default RCS version...\n", filename);
                                {
                                        char *rcsdiff_argv[3];
                                        rcsdiff_argv[0] = __UNCONST(RCSDIFF);
                                        rcsdiff_argv[1] = __UNCONST(filename);
                                        rcsdiff_argv[2] = NULL;
                                        if (wexecvp(RCSDIFF, rcsdiff_argv) != 0)
                                                fatal("cant check out file %s: differs from default RCS version\n", filename);
                                }
                        }
                        if (verbose)
                                say("Checking out file %s from RCS...\n", filename);
                        {
                                char *co_argv[4];
                                co_argv[0] = __UNCONST(CHECKOUT);
                                co_argv[1] = __UNCONST("-l");
                                co_argv[2] = __UNCONST(filename);
                                co_argv[3] = NULL;
                                if (wexecvp(CHECKOUT, co_argv) != 0 || stat(filename, &filestat))
                                        fatal("cant check out file %s from RCS\n", filename);
                        }
                } else if (statfailed) {
                        fatal("cant find %s\n", filename);
                }
#undef try
                free(lbuf);
                free(tmp_filename1);
                free(tmp_filename2);
        }

        filemode = filestat.st_mode;
        if (!S_ISREG(filemode))
                fatal("%s is not a normal file--cant patch\n", filename);
        i_size = filestat.st_size;
        if (out_of_mem) {
                set_hunkmax();
                out_of_mem = 0;
                return 0;
        }
        if ((uintmax_t)i_size > (uintmax_t)SIZE_MAX) {
                say("block too large to mmap\n");
                return 0;
        }
        if ((ifd = open(filename, O_RDONLY)) < 0)
                pfatal("cant open file %s", filename);

        if (i_size) {
                i_womp = mmap(NULL, i_size, PROT_READ, MAP_PRIVATE, ifd, 0);
                if (i_womp == MAP_FAILED) {
                        perror("mmap failed");
                        i_womp = NULL;
                        close(ifd);
                        return 0;
                }
        } else
                i_womp = NULL;

        close(ifd);
        if (i_size)
                madvise(i_womp, i_size, MADV_SEQUENTIAL);

        lines_allocated = i_size / 25;
        if (lines_allocated < 100)
                lines_allocated = 100;

        if (!reallocate_lines(&lines_allocated))
                return 0;

        iline = 1;
        i_ptr[iline] = i_womp;
        for (s = i_womp, i = 0; i < i_size && *s != '\0'; s++, i++) {
                if (*s == '\n') {
                        if (iline == lines_allocated) {
                                if (!reallocate_lines(&lines_allocated))
                                        return 0;
                        }
                        i_ptr[++iline] = s + 1;
                }
        }
        if (i_size > 0 && i_womp[i_size - 1] != '\n') {
                last_line_missing_eol = 1;
                sz = s - i_ptr[iline];
                p = malloc(sz + 1);
                if (p == NULL) {
                        free(i_ptr);
                        i_ptr = NULL;
                        munmap(i_womp, i_size);
                        i_womp = NULL;
                        return 0;
                }
                memcpy(p, i_ptr[iline], sz);
                p[sz] = '\n';
                i_ptr[iline] = p;
                i_ptr[++iline] = empty_line;
        } else
                last_line_missing_eol = 0;

        input_lines = iline - 1;

        if (revision != NULL) {
                if (!rev_in_string(i_womp)) {
                        if (force) {
                                if (verbose)
                                        say("Warning: this file doesnt appear to be the %s version--patching anyway\n", revision);
                        } else if (batch) {
                                fatal("this file doesnt appear to be the %s version--aborting\n", revision);
                        } else {
                                ask("This file doesnt appear to be the %s version--patch anyway? [n] ", revision);
                                if (*buf != 'y')
                                        fatal("aborted\n");
                        }
                } else if (verbose)
                        say("Good. This file appears to be the %s version.\n", revision);
        }
        return 1;
}

static void
plan_b(const char *filename)
{
        FILE *ifp;
        size_t i = 0, j, maxlen = 1;
        char *p;
        int found_revision = (revision == NULL);

        using_plan_a = 0;
        if ((ifp = fopen(filename, "r")) == NULL)
                pfatal("cant open file %s", filename);
        unlink(TMPINNAME);
        if ((tifd = open(TMPINNAME, O_EXCL | O_CREAT | O_WRONLY, 0666)) < 0)
                pfatal("cant open file %s", TMPINNAME);
        while (getline(&buf, &bufsz, ifp) != -1) {
                if (revision != NULL && !found_revision && rev_in_string(buf))
                        found_revision = 1;
                if ((i = strlen(buf)) > maxlen)
                        maxlen = i;
        }
        last_line_missing_eol = i > 0 && buf[i - 1] != '\n';
        if (last_line_missing_eol && maxlen == i)
                maxlen++;

        if (revision != NULL) {
                if (!found_revision) {
                        if (force) {
                                if (verbose)
                                        say("Warning: this file doesnt appear to be the %s version--patching anyway\n", revision);
                        } else if (batch) {
                                fatal("this file doesnt appear to be the %s version--aborting\n", revision);
                        } else {
                                ask("This file doesnt appear to be the %s version--patch anyway? [n] ", revision);
                                if (*buf != 'y')
                                        fatal("aborted\n");
                        }
                } else if (verbose)
                        say("Good. This file appears to be the %s version.\n", revision);
        }
        fseek(ifp, 0L, SEEK_SET);
        lines_per_buf = BUFFERSIZE / maxlen;
        tireclen = maxlen;
        tibuf[0] = malloc(BUFFERSIZE + 1);
        if (tibuf[0] == NULL)
                fatal("out of memory\n");
        tibuf[1] = malloc(BUFFERSIZE + 1);
        if (tibuf[1] == NULL)
                fatal("out of memory\n");
        for (i = 1;; i++) {
                p = tibuf[0] + maxlen * (i % lines_per_buf);
                if (i % lines_per_buf == 0) {
                        if (write(tifd, tibuf[0], BUFFERSIZE) < BUFFERSIZE)
                                pfatal("cant write temp file");
                }
                if (fgets(p, maxlen + 1, ifp) == NULL) {
                        input_lines = i - 1;
                        if (i % lines_per_buf != 0) {
                                if (write(tifd, tibuf[0], BUFFERSIZE) < BUFFERSIZE)
                                        pfatal("cant write temp file");
                        }
                        break;
                }
                j = strlen(p);
                if (j == 0 || p[j - 1] != '\n')
                        p[j] = '\n';
        }
        fclose(ifp);
        close(tifd);
        if ((tifd = open(TMPINNAME, O_RDONLY)) < 0)
                pfatal("cant reopen file %s", TMPINNAME);
}

char *
ifetch(LINENUM line, int whichbuf)
{
        if (line < 1 || line > input_lines) {
                if (warn_on_invalid_line) {
                        say("No such line %ld in input file, ignoring\n", line);
                        warn_on_invalid_line = 0;
                }
                return NULL;
        }
        if (using_plan_a)
                return i_ptr[line];
        else {
                LINENUM offline = line % lines_per_buf;
                LINENUM baseline = line - offline;

                if (tiline[0] == baseline)
                        whichbuf = 0;
                else if (tiline[1] == baseline)
                        whichbuf = 1;
                else {
                        tiline[whichbuf] = baseline;
                        if (lseek(tifd, (off_t)(baseline / lines_per_buf * BUFFERSIZE), SEEK_SET) < 0)
                                pfatal("cannot seek in the temporary input file");
                        if (read(tifd, tibuf[whichbuf], BUFFERSIZE) < 0)
                                pfatal("error reading tmp file %s", TMPINNAME);
                }
                return tibuf[whichbuf] + (tireclen * offline);
        }
}

static int
rev_in_string(const char *string)
{
        const char *s;
        size_t patlen;

        if (revision == NULL)
                return 1;
        patlen = strlen(revision);
        if (strnEQ(string, revision, patlen) && isspace((unsigned char)string[patlen]))
                return 1;
        for (s = string; *s; s++) {
                if (isspace((unsigned char)*s) && strnEQ(s + 1, revision, patlen) &&
                    isspace((unsigned char)s[patlen + 1]))
                        return 1;
        }
        return 0;
}

void
re_patch(void)
{
        p_first = 0;
        p_newfirst = 0;
        p_ptrn_lines = 0;
        p_repl_lines = 0;
        p_end = (LINENUM)-1;
        p_max = 0;
        p_indent = 0;
}

void
open_patch_file(const char *filename)
{
        struct stat filestat;

        if (filename == NULL || *filename == '\0' || strEQ(filename, "-")) {
                pfp = fopen(TMPPATNAME, "w");
                if (pfp == NULL)
                        pfatal("cant create %s", TMPPATNAME);
                while (getline(&buf, &bufsz, stdin) != -1)
                        fprintf(pfp, "%s", buf);
                if (ferror(pfp) || fclose(pfp))
                        pfatal("cant write %s", TMPPATNAME);
                filename = TMPPATNAME;
        }
        pfp = fopen(filename, "r");
        if (pfp == NULL)
                pfatal("patch file %s not found", filename);
        fstat(fileno(pfp), &filestat);
        p_filesize = filestat.st_size;
        next_intuit_at(0L, 1L);
        set_hunkmax();
}

void
set_hunkmax(void)
{
        if (p_line == NULL)
                p_line = calloc((size_t)hunkmax, sizeof(char *));
        if (p_len == NULL)
                p_len = calloc((size_t)hunkmax, sizeof(ssize_t));
        if (p_char == NULL)
                p_char = calloc((size_t)hunkmax, sizeof(char));
}

static void
grow_hunkmax(void)
{
        int new_hunkmax;
        char **new_p_line;
        ssize_t *new_p_len;
        char *new_p_char;

        new_hunkmax = hunkmax * 2;
        if (p_line == NULL || p_len == NULL || p_char == NULL)
                fatal("Internal memory allocation error\n");

        new_p_line = pch_realloc(p_line, new_hunkmax, sizeof(char *));
        if (new_p_line == NULL)
                free(p_line);
        new_p_len = pch_realloc(p_len, new_hunkmax, sizeof(ssize_t));
        if (new_p_len == NULL)
                free(p_len);
        new_p_char = pch_realloc(p_char, new_hunkmax, sizeof(char));
        if (new_p_char == NULL)
                free(p_char);

        p_char = new_p_char;
        p_len = new_p_len;
        p_line = new_p_line;

        if (p_line != NULL && p_len != NULL && p_char != NULL) {
                hunkmax = new_hunkmax;
                return;
        }

        if (!using_plan_a)
                fatal("out of memory\n");
        out_of_mem = 1;
}

int
there_is_another_patch(void)
{
        int exists = 0;

        if (p_base != 0L && p_base >= p_filesize) {
                if (verbose)
                        say("done\n");
                return 0;
        }
        if (verbose)
                say("Hmm...");
        diff_type = intuit_diff_type();
        if (!diff_type) {
                if (p_base != 0L) {
                        if (verbose)
                                say(" Ignoring the trailing garbage.\ndone\n");
                } else
                        say(" I cant seem to find a patch in there anywhere\n");
                return 0;
        }
        if (verbose) {
                say(" %sooks like %s to me...\n",
                    (p_base == 0L ? "L" : "The next patch l"),
                    diff_type == UNI_DIFF ? "a unified diff" :
                    diff_type == CONTEXT_DIFF ? "a context diff" :
                    diff_type == NEW_CONTEXT_DIFF ? "a new-style context diff" :
                    diff_type == NORMAL_DIFF ? "a normal diff" : "an ed script");
        }
        if (p_indent && verbose)
                say("(Patch is indented %d space%s.)\n", p_indent, p_indent == 1 ? "" : "s");
        skip_to(p_start, p_sline);
        while (filearg[0] == NULL) {
                if (force || batch) {
                        say("No file to patch. Skipping...\n");
                        filearg[0] = savestr(bestguess);
                        skip_rest_of_patch = 1;
                        return 1;
                }
                ask("File to patch: ");
                if (*buf != '\n') {
                        free(bestguess);
                        bestguess = savestr(buf);
                        filearg[0] = fetchname(buf, &exists, 0);
                }
                if (!exists) {
                        ask("No file found--skip this patch? [n] ");
                        if (*buf != 'y')
                                continue;
                        if (verbose)
                                say("Skipping patch...\n");
                        free(filearg[0]);
                        filearg[0] = fetchname(bestguess, &exists, 0);
                        skip_rest_of_patch = 1;
                        return 1;
                }
        }
        return 1;
}

static int
intuit_diff_type(void)
{
        long this_line = 0, previous_line;
        long first_command_line = -1;
        LINENUM fcl_line = -1;
        int last_line_was_command = 0, this_is_a_command = 0;
        int stars_last_line = 0, stars_this_line = 0;
        char *s, *t;
        int indent, retval;
        struct FileName names[MAX_FILE];

        memset(names, 0, sizeof(names));
        ok_to_create_file = 0;
        fseek(pfp, p_base, SEEK_SET);
        p_input_line = p_bline - 1;
        for (;;) {
                previous_line = this_line;
                last_line_was_command = this_is_a_command;
                stars_last_line = stars_this_line;
                this_line = ftell(pfp);
                indent = 0;
                p_input_line++;
                if (getline(&buf, &bufsz, pfp) == -1) {
                        if (first_command_line >= 0L) {
                                p_start = first_command_line;
                                p_sline = fcl_line;
                                retval = ED_DIFF;
                                goto scan_exit;
                        } else {
                                p_start = this_line;
                                p_sline = p_input_line;
                                retval = 0;
                                goto scan_exit;
                        }
                }
                for (s = buf; *s == ' ' || *s == '\t' || *s == 'X'; s++) {
                        if (*s == '\t')
                                indent += 8 - (indent % 8);
                        else
                                indent++;
                }
                for (t = s; isdigit((unsigned char)*t) || *t == ','; t++)
                        ;
                this_is_a_command = (isdigit((unsigned char)*s) && (*t == 'd' || *t == 'c' || *t == 'a'));
                if (first_command_line < 0L && this_is_a_command) {
                        first_command_line = this_line;
                        fcl_line = p_input_line;
                        p_indent = indent;
                }
                if (!stars_last_line && strnEQ(s, "*** ", 4)) {
                        names[OLD_FILE].path = fetchname(s + 4, &names[OLD_FILE].exists, strippath);
                } else if (strnEQ(s, "--- ", 4)) {
                        names[NEW_FILE].path = fetchname(s + 4, &names[NEW_FILE].exists, strippath);
                } else if (strnEQ(s, "+++ ", 4)) {
                        names[OLD_FILE].path = fetchname(s + 4, &names[OLD_FILE].exists, strippath);
                } else if (strnEQ(s, "Index:", 6)) {
                        names[INDEX_FILE].path = fetchname(s + 6, &names[INDEX_FILE].exists, strippath);
                } else if (strnEQ(s, "Prereq:", 7)) {
                        for (t = s + 7; isspace((unsigned char)*t); t++)
                                ;
                        revision = savestr(t);
                        for (t = revision; *t && !isspace((unsigned char)*t); t++)
                                ;
                        *t = '\0';
                        if (*revision == '\0') {
                                free(revision);
                                revision = NULL;
                        }
                }
                if ((!diff_type || diff_type == ED_DIFF) && first_command_line >= 0L && strEQ(s, ".\n")) {
                        p_indent = indent;
                        p_start = first_command_line;
                        p_sline = fcl_line;
                        retval = ED_DIFF;
                        goto scan_exit;
                }
                if ((!diff_type || diff_type == UNI_DIFF) && strnEQ(s, "@@ -", 4)) {
                        if (strnEQ(s + 4, "0,0", 3))
                                ok_to_create_file = 1;
                        p_indent = indent;
                        p_start = this_line;
                        p_sline = p_input_line;
                        retval = UNI_DIFF;
                        goto scan_exit;
                }
                stars_this_line = strnEQ(s, "********", 8);
                if ((!diff_type || diff_type == CONTEXT_DIFF) && stars_last_line && strnEQ(s, "*** ", 4)) {
                        if (atol(s + 4) == 0)
                                ok_to_create_file = 1;
                        while (*s != '\n')
                                s++;
                        p_indent = indent;
                        p_start = previous_line;
                        p_sline = p_input_line - 1;
                        retval = (*(s - 1) == '*' ? NEW_CONTEXT_DIFF : CONTEXT_DIFF);
                        goto scan_exit;
                }
                if ((!diff_type || diff_type == NORMAL_DIFF) && last_line_was_command &&
                    (strnEQ(s, "< ", 2) || strnEQ(s, "> ", 2))) {
                        p_start = previous_line;
                        p_sline = p_input_line - 1;
                        p_indent = indent;
                        retval = NORMAL_DIFF;
                        goto scan_exit;
                }
        }
scan_exit:
        if (retval == UNI_DIFF) {
                struct FileName tmp = names[OLD_FILE];
                names[OLD_FILE] = names[NEW_FILE];
                names[NEW_FILE] = tmp;
        }
        if (filearg[0] == NULL) {
                if (posix)
                        filearg[0] = posix_name(names, ok_to_create_file);
                else {
                        if (names[OLD_FILE].path != NULL || names[NEW_FILE].path != NULL) {
                                free(names[INDEX_FILE].path);
                                names[INDEX_FILE].path = NULL;
                        }
                        filearg[0] = best_name(names, ok_to_create_file);
                }
        }
        free(bestguess);
        bestguess = NULL;
        if (filearg[0] != NULL)
                bestguess = savestr(filearg[0]);
        else if (!ok_to_create_file) {
                if (posix)
                        bestguess = posix_name(names, 1);
                else
                        bestguess = best_name(names, 1);
        }
        free(names[OLD_FILE].path);
        free(names[NEW_FILE].path);
        free(names[INDEX_FILE].path);
        return retval;
}

static void
next_intuit_at(LINENUM file_pos, LINENUM file_line)
{
        p_base = file_pos;
        p_bline = file_line;
}

static void
skip_to(LINENUM file_pos, LINENUM file_line)
{
        int ret;

        if (p_base > file_pos)
                fatal("Internal error: seek %ld>%ld\n", p_base, file_pos);
        if (verbose && p_base < file_pos) {
                fseek(pfp, p_base, SEEK_SET);
                say("The text leading up to this was:\n--------------------------\n");
                while (ftell(pfp) < file_pos) {
                        ret = getline(&buf, &bufsz, pfp);
                        if (ret == -1)
                                fatal("Unexpected end of file\n");
                        say("|%s", buf);
                }
                say("--------------------------\n");
        } else
                fseek(pfp, file_pos, SEEK_SET);
        p_input_line = file_line - 1;
}

__dead static void
malformed(void)
{
        fatal("malformed patch at line %ld: %s", p_input_line, buf);
}

static LINENUM
getlinenum(const char *s)
{
        LINENUM l = (LINENUM)atol(s);
        if (l < 0) {
                l = 0;
                malformed();
        }
        return l;
}

static LINENUM
getskiplinenum(char **p)
{
        char *s = *p;
        LINENUM l = getlinenum(s);
        while (isdigit((unsigned char)*s))
                s++;
        *p = s;
        return l;
}

static int
remove_special_line(void)
{
        int c;

        c = fgetc(pfp);
        if (c == '\\') {
                do {
                        c = fgetc(pfp);
                } while (c != EOF && c != '\n');
                return 1;
        }
        if (c != EOF)
                fseek(pfp, -1L, SEEK_CUR);
        return 0;
}

int
another_hunk(void)
{
        long line_beginning;
        LINENUM repl_beginning;
        LINENUM fillcnt;
        LINENUM fillsrc;
        LINENUM filldst;
        int ptrn_spaces_eaten;
        int repl_could_be_missing;
        int repl_missing;
        long repl_backtrack_position;
        LINENUM repl_patch_line;
        LINENUM ptrn_copiable;
        char *s;
        int context = 0;
        int ret;

        while (p_end >= 0) {
                if (p_end == p_efake)
                        p_end = p_bfake;
                else
                        free(p_line[p_end]);
                p_end--;
        }
        p_efake = -1;
        p_max = hunkmax;

        if (diff_type == CONTEXT_DIFF || diff_type == NEW_CONTEXT_DIFF) {
                line_beginning = ftell(pfp);
                repl_beginning = 0;
                fillcnt = 0;
                fillsrc = 0;
                filldst = 0;
                ptrn_spaces_eaten = 0;
                repl_could_be_missing = 1;
                repl_missing = 0;
                repl_backtrack_position = 0;
                repl_patch_line = 0;
                ptrn_copiable = 0;

                ret = pgetline(&buf, &bufsz, pfp);
                p_input_line++;
                if (ret == -1 || strnNE(buf, "********", 8)) {
                        next_intuit_at(line_beginning, p_input_line);
                        return 0;
                }
                p_context = 100;
                p_hunk_beg = p_input_line + 1;
                while (p_end < p_max) {
                        ret = pgetline(&buf, &bufsz, pfp);
                        p_input_line++;
                        if (ret == -1) {
                                if (repl_beginning && repl_could_be_missing) {
                                        repl_missing = 1;
                                        goto hunk_done;
                                }
                                fatal("unexpected end of file in patch\n");
                        }
                        p_end++;
                        if (p_end >= hunkmax)
                                fatal("Internal error: hunk larger than hunk buffer size");
                        p_char[p_end] = *buf;
                        p_line[p_end] = NULL;
                        switch (*buf) {
                        case '*':
                                if (strnEQ(buf, "********", 8)) {
                                        if (repl_beginning && repl_could_be_missing) {
                                                repl_missing = 1;
                                                goto hunk_done;
                                        } else
                                                fatal("unexpected end of hunk at line %ld\n", p_input_line);
                                }
                                if (p_end != 0) {
                                        if (repl_beginning && repl_could_be_missing) {
                                                repl_missing = 1;
                                                goto hunk_done;
                                        }
                                        fatal("unexpected *** at line %ld: %s", p_input_line, buf);
                                }
                                context = 0;
                                p_line[p_end] = savestr(buf);
                                if (out_of_mem) {
                                        p_end--;
                                        return 0;
                                }
                                for (s = buf; *s && !isdigit((unsigned char)*s); s++)
                                        ;
                                if (!*s)
                                        malformed();
                                if (strnEQ(s, "0,0", 3))
                                        memmove(s, s + 2, strlen(s + 2) + 1);
                                p_first = getskiplinenum(&s);
                                if (*s == ',') {
                                        for (; *s && !isdigit((unsigned char)*s); s++)
                                                ;
                                        if (!*s)
                                                malformed();
                                        p_ptrn_lines = (getlinenum(s)) - p_first + 1;
                                        if (p_ptrn_lines < 0)
                                                malformed();
                                } else if (p_first)
                                        p_ptrn_lines = 1;
                                else {
                                        p_ptrn_lines = 0;
                                        p_first = 1;
                                }
                                if (p_first >= LINENUM_MAX - p_ptrn_lines || p_ptrn_lines >= LINENUM_MAX - 6)
                                        malformed();
                                p_max = p_ptrn_lines + 6;
                                while (p_max >= hunkmax)
                                        grow_hunkmax();
                                p_max = hunkmax;
                                break;
                        case '-':
                                if (buf[1] == '-') {
                                        if (repl_beginning || (p_end != p_ptrn_lines + 1 + (p_char[p_end - 1] == '\n'))) {
                                                if (p_end == 1) {
                                                        p_end = p_ptrn_lines + 1;
                                                        fillsrc = p_end + 1;
                                                        filldst = 1;
                                                        fillcnt = p_ptrn_lines;
                                                } else {
                                                        if (repl_beginning) {
                                                                if (repl_could_be_missing) {
                                                                        repl_missing = 1;
                                                                        goto hunk_done;
                                                                }
                                                                fatal("duplicate \"---\" at line %ld\n", p_input_line);
                                                        } else {
                                                                fatal("premature/overdue \"---\" at line %ld\n", p_input_line);
                                                        }
                                                }
                                        }
                                        repl_beginning = p_end;
                                        repl_backtrack_position = ftell(pfp);
                                        repl_patch_line = p_input_line;
                                        p_line[p_end] = savestr(buf);
                                        if (out_of_mem) {
                                                p_end--;
                                                return 0;
                                        }
                                        p_char[p_end] = '=';
                                        for (s = buf; *s && !isdigit((unsigned char)*s); s++)
                                                ;
                                        if (!*s)
                                                malformed();
                                        p_newfirst = getskiplinenum(&s);
                                        if (*s == ',') {
                                                for (; *s && !isdigit((unsigned char)*s); s++)
                                                        ;
                                                if (!*s)
                                                        malformed();
                                                p_repl_lines = (getlinenum(s)) - p_newfirst + 1;
                                                if (p_repl_lines < 0)
                                                        malformed();
                                        } else if (p_newfirst)
                                                p_repl_lines = 1;
                                        else {
                                                p_repl_lines = 0;
                                                p_newfirst = 1;
                                        }
                                        if (p_newfirst >= LINENUM_MAX - p_repl_lines || p_repl_lines >= LINENUM_MAX - p_end)
                                                malformed();
                                        p_max = p_repl_lines + p_end;
                                        if (p_max > MAXHUNKSIZE)
                                                fatal("hunk too large at line %ld\n", p_input_line);
                                        while (p_max >= hunkmax)
                                                grow_hunkmax();
                                        if (p_repl_lines != ptrn_copiable && (p_context != 0 || p_repl_lines != 1))
                                                repl_could_be_missing = 0;
                                        break;
                                }
                                goto change_line;
                        case '+':
                        case '!':
                                repl_could_be_missing = 0;
                        change_line:
                                if (buf[1] == '\n' && canonicalize)
                                        strlcpy(buf + 1, " \n", bufsz - 1);
                                if (!isspace((unsigned char)buf[1]) && buf[1] != '>' && buf[1] != '<' &&
                                    repl_beginning && repl_could_be_missing) {
                                        repl_missing = 1;
                                        goto hunk_done;
                                }
                                if (context >= 0) {
                                        if (context < p_context)
                                                p_context = context;
                                        context = -1000;
                                }
                                p_line[p_end] = savestr(buf + 2);
                                if (out_of_mem) {
                                        p_end--;
                                        return 0;
                                }
                                if (p_end == p_ptrn_lines) {
                                        if (remove_special_line()) {
                                                int length = strlen(p_line[p_end]) - 1;
                                                (p_line[p_end])[length] = 0;
                                        }
                                }
                                break;
                        case '\t':
                        case '\n':
                                if (repl_beginning && repl_could_be_missing &&
                                    (!ptrn_spaces_eaten || diff_type == NEW_CONTEXT_DIFF)) {
                                        repl_missing = 1;
                                        goto hunk_done;
                                }
                                p_line[p_end] = savestr(buf);
                                if (out_of_mem) {
                                        p_end--;
                                        return 0;
                                }
                                if (p_end != p_ptrn_lines + 1) {
                                        ptrn_spaces_eaten |= (repl_beginning != 0);
                                        context++;
                                        if (!repl_beginning)
                                                ptrn_copiable++;
                                        p_char[p_end] = ' ';
                                }
                                break;
                        case ' ':
                                if (!isspace((unsigned char)buf[1]) && repl_beginning && repl_could_be_missing) {
                                        repl_missing = 1;
                                        goto hunk_done;
                                }
                                context++;
                                if (!repl_beginning)
                                        ptrn_copiable++;
                                p_line[p_end] = savestr(buf + 2);
                                if (out_of_mem) {
                                        p_end--;
                                        return 0;
                                }
                                break;
                        default:
                                if (repl_beginning && repl_could_be_missing) {
                                        repl_missing = 1;
                                        goto hunk_done;
                                }
                                malformed();
                        }
                        if (p_line[p_end])
                                p_len[p_end] = strlen(p_line[p_end]);
                        else
                                p_len[p_end] = 0;
                }

hunk_done:
                if (p_end >= 0 && !repl_beginning)
                        fatal("no --- found in patch at line %ld\n", pch_hunk_beg());

                if (repl_missing) {
                        p_input_line = repl_patch_line;
                        for (p_end--; p_end > repl_beginning; p_end--)
                                free(p_line[p_end]);
                        fseek(pfp, repl_backtrack_position, SEEK_SET);

                        if (!p_context && p_repl_lines == 1) {
                                p_repl_lines = 0;
                                p_max--;
                        }
                        fillsrc = 1;
                        filldst = repl_beginning + 1;
                        fillcnt = p_repl_lines;
                        p_end = p_max;
                } else if (!p_context && fillcnt == 1) {
                        while (filldst < p_end) {
                                p_line[filldst] = p_line[filldst + 1];
                                p_char[filldst] = p_char[filldst + 1];
                                p_len[filldst] = p_len[filldst + 1];
                                filldst++;
                        }
                        p_end--;
                        p_first++;
                        fillcnt = 0;
                        p_ptrn_lines = 0;
                }
                if (diff_type == CONTEXT_DIFF && (fillcnt || (p_first > 1 && ptrn_copiable > 2 * p_context))) {
                        diff_type = NEW_CONTEXT_DIFF;
                }
                if (fillcnt) {
                        p_bfake = filldst;
                        p_efake = filldst + fillcnt - 1;
                        while (fillcnt-- > 0) {
                                while (fillsrc <= p_end && p_char[fillsrc] != ' ')
                                        fillsrc++;
                                if (fillsrc > p_end)
                                        fatal("replacement text mangled in hunk at line %ld\n", p_hunk_beg);
                                p_line[filldst] = p_line[fillsrc];
                                p_char[filldst] = p_char[fillsrc];
                                p_len[filldst] = p_len[fillsrc];
                                fillsrc++;
                                filldst++;
                        }
                        while (fillsrc <= p_end && fillsrc != repl_beginning && p_char[fillsrc] != ' ')
                                fillsrc++;
                        if (fillsrc != p_end + 1 && fillsrc != repl_beginning)
                                malformed();
                        if (filldst != p_end + 1 && filldst != repl_beginning)
                                malformed();
                }
                if (p_line[p_end] != NULL) {
                        if (remove_special_line()) {
                                p_len[p_end] -= 1;
                                (p_line[p_end])[p_len[p_end]] = 0;
                        }
                }
        } else if (diff_type == UNI_DIFF) {
                LINENUM fillold;
                LINENUM fillnew;
                char ch;

                line_beginning = ftell(pfp);
                ret = pgetline(&buf, &bufsz, pfp);
                p_input_line++;
                if (ret == -1 || strnNE(buf, "@@ -", 4)) {
                        next_intuit_at(line_beginning, p_input_line);
                        return 0;
                }
                s = buf + 4;
                if (!*s)
                        malformed();
                p_first = getskiplinenum(&s);
                if (*s == ',') {
                        s++;
                        p_ptrn_lines = getskiplinenum(&s);
                } else
                        p_ptrn_lines = 1;
                if (p_first >= LINENUM_MAX - p_ptrn_lines)
                        malformed();
                if (*s == ' ')
                        s++;
                if (*s != '+' || !*++s)
                        malformed();
                p_newfirst = getskiplinenum(&s);
                if (*s == ',') {
                        s++;
                        p_repl_lines = getskiplinenum(&s);
                } else
                        p_repl_lines = 1;
                if (*s == ' ')
                        s++;
                if (*s != '@')
                        malformed();
                if (p_first >= LINENUM_MAX - p_ptrn_lines ||
                    p_newfirst > LINENUM_MAX - p_repl_lines ||
                    p_ptrn_lines >= LINENUM_MAX - p_repl_lines - 1)
                        malformed();
                if (!p_ptrn_lines)
                        p_first++;
                p_max = p_ptrn_lines + p_repl_lines + 1;
                while (p_max >= hunkmax)
                        grow_hunkmax();
                fillold = 1;
                fillnew = fillold + p_ptrn_lines;
                p_end = fillnew + p_repl_lines;
                snprintf(buf, bufsz, "*** %ld,%ld ****\n", p_first, p_first + p_ptrn_lines - 1);
                p_line[0] = savestr(buf);
                if (out_of_mem) {
                        p_end = -1;
                        return 0;
                }
                p_char[0] = '*';
                snprintf(buf, bufsz, "--- %ld,%ld ----\n", p_newfirst, p_newfirst + p_repl_lines - 1);
                p_line[fillnew] = savestr(buf);
                if (out_of_mem) {
                        p_end = 0;
                        return 0;
                }
                p_char[fillnew++] = '=';
                p_context = 100;
                context = 0;
                p_hunk_beg = p_input_line + 1;
                while (fillold <= p_ptrn_lines || fillnew <= p_end) {
                        ret = pgetline(&buf, &bufsz, pfp);
                        p_input_line++;
                        if (ret == -1) {
                                if (p_max - fillnew < 3)
                                        strlcpy(buf, " \n", bufsz);
                                else
                                        fatal("unexpected end of file in patch\n");
                        }
                        if (*buf == '\t' || *buf == '\n') {
                                ch = ' ';
                                s = savestr(buf);
                        } else {
                                ch = *buf;
                                s = savestr(buf + 1);
                        }
                        if (out_of_mem) {
                                while (--fillnew > p_ptrn_lines)
                                        free(p_line[fillnew]);
                                p_end = fillold - 1;
                                return 0;
                        }
                        switch (ch) {
                        case '-':
                                if (fillold > p_ptrn_lines) {
                                        free(s);
                                        p_end = fillnew - 1;
                                        malformed();
                                }
                                p_char[fillold] = ch;
                                p_line[fillold] = s;
                                p_len[fillold++] = strlen(s);
                                if (fillold > p_ptrn_lines) {
                                        if (remove_special_line()) {
                                                p_len[fillold - 1] -= 1;
                                                s[p_len[fillold - 1]] = 0;
                                        }
                                }
                                break;
                        case '=':
                                ch = ' ';
                                /* FALL THROUGH */
                        case ' ':
                                if (fillold > p_ptrn_lines) {
                                        free(s);
                                        while (--fillnew > p_ptrn_lines)
                                                free(p_line[fillnew]);
                                        p_end = fillold - 1;
                                        malformed();
                                }
                                context++;
                                p_char[fillold] = ch;
                                p_line[fillold] = s;
                                p_len[fillold++] = strlen(s);
                                s = savestr(s);
                                if (out_of_mem) {
                                        while (--fillnew > p_ptrn_lines)
                                                free(p_line[fillnew]);
                                        p_end = fillold - 1;
                                        return 0;
                                }
                                if (fillold > p_ptrn_lines) {
                                        if (remove_special_line()) {
                                                p_len[fillold - 1] -= 1;
                                                s[p_len[fillold - 1]] = 0;
                                        }
                                }
                                /* FALL THROUGH */
                        case '+':
                                if (fillnew > p_end) {
                                        free(s);
                                        while (--fillnew > p_ptrn_lines)
                                                free(p_line[fillnew]);
                                        p_end = fillold - 1;
                                        malformed();
                                }
                                p_char[fillnew] = ch;
                                p_line[fillnew] = s;
                                p_len[fillnew++] = strlen(s);
                                if (fillold > p_ptrn_lines) {
                                        if (remove_special_line()) {
                                                p_len[fillnew - 1] -= 1;
                                                s[p_len[fillnew - 1]] = 0;
                                        }
                                }
                                break;
                        default:
                                p_end = fillnew;
                                malformed();
                        }
                        if (ch != ' ' && context > 0) {
                                if (context < p_context)
                                        p_context = context;
                                context = -1000;
                        }
                }
        } else {
                char hunk_type;
                int i;
                LINENUM min, max;

                line_beginning = ftell(pfp);
                p_context = 0;
                ret = pgetline(&buf, &bufsz, pfp);
                p_input_line++;
                if (ret == -1 || !isdigit((unsigned char)*buf)) {
                        next_intuit_at(line_beginning, p_input_line);
                        return 0;
                }
                s = buf;
                p_first = getskiplinenum(&s);
                if (*s == ',') {
                        s++;
                        p_ptrn_lines = getskiplinenum(&s) - p_first + 1;
                } else
                        p_ptrn_lines = (*s != 'a');
                if (p_first >= LINENUM_MAX - p_ptrn_lines)
                        malformed();
                hunk_type = *s++;
                if (hunk_type == 'a')
                        p_first++;
                min = getskiplinenum(&s);
                if (*s == ',')
                        max = getlinenum(++s);
                else
                        max = min;
                if (min < 0 || min > max || max - min == LINENUM_MAX)
                        malformed();
                if (hunk_type == 'd')
                        min++;
                p_end = p_ptrn_lines + p_repl_lines + 1;
                p_newfirst = min;
                p_repl_lines = max - min + 1;
                if (p_newfirst > LINENUM_MAX - p_repl_lines || p_ptrn_lines >= LINENUM_MAX - p_repl_lines - 1)
                        malformed();
                p_end = p_ptrn_lines + p_repl_lines + 1;
                if (p_end > MAXHUNKSIZE)
                        fatal("hunk too large at line %ld\n", p_input_line);
                while (p_end >= hunkmax)
                        grow_hunkmax();
                snprintf(buf, bufsz, "*** %ld,%ld\n", p_first, p_first + p_ptrn_lines - 1);
                p_line[0] = savestr(buf);
                if (out_of_mem) {
                        p_end = -1;
                        return 0;
                }
                p_char[0] = '*';
                for (i = 1; i <= p_ptrn_lines; i++) {
                        ret = pgetline(&buf, &bufsz, pfp);
                        p_input_line++;
                        if (ret == -1)
                                fatal("unexpected end of file in patch at line %ld\n", p_input_line);
                        if (*buf != '<')
                                fatal("< expected at line %ld of patch\n", p_input_line);
                        p_line[i] = savestr(buf + 2);
                        if (out_of_mem) {
                                p_end = i - 1;
                                return 0;
                        }
                        p_len[i] = strlen(p_line[i]);
                        p_char[i] = '-';
                }
                if (remove_special_line()) {
                        p_len[i - 1] -= 1;
                        (p_line[i - 1])[p_len[i - 1]] = 0;
                }
                if (hunk_type == 'c') {
                        ret = pgetline(&buf, &bufsz, pfp);
                        p_input_line++;
                        if (ret == -1)
                                fatal("unexpected end of file in patch at line %ld\n", p_input_line);
                        if (*buf != '-')
                                fatal("--- expected at line %ld of patch\n", p_input_line);
                }
                snprintf(buf, bufsz, "--- %ld,%ld\n", min, max);
                p_line[i] = savestr(buf);
                if (out_of_mem) {
                        p_end = i - 1;
                        return 0;
                }
                p_char[i] = '=';
                for (i++; i <= p_end; i++) {
                        ret = pgetline(&buf, &bufsz, pfp);
                        p_input_line++;
                        if (ret == -1)
                                fatal("unexpected end of file in patch at line %ld\n", p_input_line);
                        if (*buf != '>')
                                fatal("> expected at line %ld of patch\n", p_input_line);
                        p_line[i] = savestr(buf + 2);
                        if (out_of_mem) {
                                p_end = i - 1;
                                return 0;
                        }
                        p_len[i] = strlen(p_line[i]);
                        p_char[i] = '+';
                }
                if (remove_special_line()) {
                        p_len[i - 1] -= 1;
                        (p_line[i - 1])[p_len[i - 1]] = 0;
                }
        }
        if (reverse) {
                if (!pch_swap())
                        say("Not enough memory to swap next hunk!\n");
        }
        if (p_end + 1 < hunkmax)
                p_char[p_end + 1] = '^';
        return 1;
}

int
pgetline(char **bf, size_t *sz, FILE *fp)
{
        char *s;
        int indent = 0;
        int ret;

        ret = getline(bf, sz, fp);
        if (p_indent && ret != -1) {
                for (s = buf; indent < p_indent && (*s == ' ' || *s == '\t' || *s == 'X'); s++) {
                        if (*s == '\t')
                                indent += 8 - (indent % 7);
                        else
                                indent++;
                }
                if (buf != s && strlcpy(buf, s, bufsz) >= bufsz)
                        fatal("buffer too small in pgetline()\n");
        }
        return ret;
}

int
pch_swap(void)
{
        char **tp_line;
        ssize_t *tp_len;
        char *tp_char;
        LINENUM i;
        LINENUM n;
        int blankline = 0;
        char *s;

        i = p_first;
        p_first = p_newfirst;
        p_newfirst = i;

        tp_line = p_line;
        tp_len = p_len;
        tp_char = p_char;
        p_line = NULL;
        p_len = NULL;
        p_char = NULL;
        set_hunkmax();
        if (p_line == NULL || p_len == NULL || p_char == NULL) {
                free(p_line);
                p_line = tp_line;
                free(p_len);
                p_len = tp_len;
                free(p_char);
                p_char = tp_char;
                return 0;
        }

        i = p_ptrn_lines + 1;
        if (tp_char[i] == '\n') {
                blankline = 1;
                i++;
        }
        if (p_efake >= 0) {
                if (p_efake <= i)
                        n = p_end - i + 1;
                else
                        n = -i;
                p_efake += n;
                p_bfake += n;
        }
        for (n = 0; i <= p_end; i++, n++) {
                p_line[n] = tp_line[i];
                p_char[n] = tp_char[i];
                if (p_char[n] == '+')
                        p_char[n] = '-';
                p_len[n] = tp_len[i];
        }
        if (blankline) {
                i = p_ptrn_lines + 1;
                p_line[n] = tp_line[i];
                p_char[n] = tp_char[i];
                p_len[n] = tp_len[i];
                n++;
        }
        if (p_char[0] != '=')
                fatal("expected '=' found '%c'\n", p_char[0]);
        p_char[0] = '*';
        for (s = p_line[0]; *s; s++)
                if (*s == '-')
                        *s = '*';

        if (p_char[0] != '*')
                fatal("expected '*' found '%c'\n", p_char[0]);
        tp_char[0] = '=';
        for (s = tp_line[0]; *s; s++)
                if (*s == '*')
                        *s = '-';
        for (i = 0; n <= p_end; i++, n++) {
                p_line[n] = tp_line[i];
                p_char[n] = tp_char[i];
                if (p_char[n] == '-')
                        p_char[n] = '+';
                p_len[n] = tp_len[i];
        }
        if (i != p_ptrn_lines + 1)
                fatal("expected %ld lines, got %ld\n", p_ptrn_lines + 1, i);

        i = p_ptrn_lines;
        p_ptrn_lines = p_repl_lines;
        p_repl_lines = i;

        free(tp_line);
        free(tp_len);
        free(tp_char);
        return 1;
}

LINENUM
pch_first(void)
{
        return p_first;
}

LINENUM
pch_ptrn_lines(void)
{
        return p_ptrn_lines;
}

LINENUM
pch_newfirst(void)
{
        return p_newfirst;
}

LINENUM
pch_repl_lines(void)
{
        return p_repl_lines;
}

LINENUM
pch_end(void)
{
        return p_end;
}

LINENUM
pch_context(void)
{
        return p_context;
}

ssize_t
pch_line_len(LINENUM line)
{
        return p_len[line];
}

char
pch_char(LINENUM line)
{
        return p_char[line];
}

char *
pfetch(LINENUM line)
{
        return p_line[line];
}

LINENUM
pch_hunk_beg(void)
{
        return p_hunk_beg;
}

void
do_ed_script(void)
{
        char *t;
        long beginning_of_this_line;
        FILE *pipefp = NULL;
        int continuation;

        if (!skip_rest_of_patch) {
                if (copy_file(filearg[0], TMPOUTNAME) < 0) {
                        unlink(TMPOUTNAME);
                        fatal("cant create temp file %s", TMPOUTNAME);
                }
                snprintf(buf, bufsz, "%s -S%s %s", _PATH_ED, verbose ? "" : "s", TMPOUTNAME);
                pipefp = wpopen(buf, NULL, "w");
        }
        for (;;) {
                beginning_of_this_line = ftell(pfp);
                if (pgetline(&buf, &bufsz, pfp) == -1) {
                        next_intuit_at(beginning_of_this_line, p_input_line);
                        break;
                }
                p_input_line++;
                for (t = buf; isdigit((unsigned char)*t) || *t == ','; t++)
                        ;
                if (isdigit((unsigned char)*buf) && (*t == 'a' || *t == 'c' || *t == 'd' || *t == 'i' || *t == 's')) {
                        if (pipefp != NULL)
                                fprintf(pipefp, "%s", buf);
                        if (*t == 's') {
                                for (;;) {
                                        continuation = 0;
                                        t = strchr(buf, '\0') - 1;
                                        while (--t >= buf && *t == '\\')
                                                continuation = !continuation;
                                        if (!continuation || pgetline(&buf, &bufsz, pfp) == -1)
                                                break;
                                        if (pipefp != NULL)
                                                fprintf(pipefp, "%s", buf);
                                }
                        } else if (*t != 'd') {
                                while (pgetline(&buf, &bufsz, pfp) != -1) {
                                        p_input_line++;
                                        if (pipefp != NULL)
                                                fprintf(pipefp, "%s", buf);
                                        if (strEQ(buf, ".\n"))
                                                break;
                                }
                        }
                } else {
                        next_intuit_at(beginning_of_this_line, p_input_line);
                        break;
                }
        }
        if (pipefp == NULL)
                return;
        fprintf(pipefp, "w\n");
        fprintf(pipefp, "q\n");
        fflush(pipefp);
        wpclose(pipefp);
        ignore_signals();
        if (!check_only) {
                if (move_file(TMPOUTNAME, outname) < 0) {
                        toutkeep = 1;
                        chmod(TMPOUTNAME, filemode);
                } else
                        chmod(outname, filemode);
        }
        set_signals(1);
}

static char *
posix_name(const struct FileName *names, int assume_exists)
{
        char *path = NULL;
        int i;

        for (i = 0; i < MAX_FILE; i++) {
                if (names[i].path != NULL && names[i].exists) {
                        path = names[i].path;
                        break;
                }
        }
        if (path == NULL && !assume_exists) {
                for (i = 0; i < MAX_FILE; i++) {
                        if (names[i].path != NULL && (path = checked_in(names[i].path)) != NULL)
                                break;
                }
                if (path == NULL && ok_to_create_file && names[NEW_FILE].path != NULL)
                        path = names[NEW_FILE].path;
        }
        return path ? savestr(path) : NULL;
}

static char *
best_name(const struct FileName *names, int assume_exists)
{
        size_t min_components, min_baselen, min_len, tmp;
        char *best = NULL;
        int i;

        min_components = min_baselen = min_len = SIZE_MAX;
        for (i = INDEX_FILE; i >= OLD_FILE; i--) {
                if (names[i].path == NULL || (!names[i].exists && !assume_exists))
                        continue;
                if ((tmp = num_components(names[i].path)) > min_components)
                        continue;
                min_components = tmp;
                if ((tmp = strlen(basename(names[i].path))) > min_baselen)
                        continue;
                min_baselen = tmp;
                if ((tmp = strlen(names[i].path)) > min_len)
                        continue;
                min_len = tmp;
                best = names[i].path;
        }
        if (best == NULL) {
                min_components = min_baselen = min_len = SIZE_MAX;
                for (i = INDEX_FILE; i >= OLD_FILE; i--) {
                        if (names[i].path == NULL || checked_in(names[i].path) == NULL)
                                continue;
                        if ((tmp = num_components(names[i].path)) > min_components)
                                continue;
                        min_components = tmp;
                        if ((tmp = strlen(basename(names[i].path))) > min_baselen)
                                continue;
                        min_baselen = tmp;
                        if ((tmp = strlen(names[i].path)) > min_len)
                                continue;
                        min_len = tmp;
                        best = names[i].path;
                }
                if (best == NULL && ok_to_create_file && names[NEW_FILE].path != NULL)
                        best = names[NEW_FILE].path;
        }
        return best ? savestr(best) : NULL;
}

static size_t
num_components(const char *path)
{
        size_t n;
        const char *cp;

        for (n = 0, cp = path; (cp = strchr(cp, '/')) != NULL; n++, cp++) {
                while (*cp == '/')
                        cp++;
        }
        return n;
}

LINENUM
strtolinenum(char *nptr, char **endptr)
{
        LINENUM rv;
        char c;
        char *p;
        const char *errstr;

        for (p = nptr; isdigit((unsigned char)*p); p++)
                ;

        if (p == nptr)
                malformed();

        c = *p;
        *p = '\0';

        rv = strtonum(nptr, 0, LINENUM_MAX, &errstr);
        if (errstr != NULL)
                fatal("invalid line number at line %ld: `%s' is %s\n",
                    p_input_line, nptr, errstr);

        *p = c;
        *endptr = p;

        return rv;
}

int
move_file(const char *from, const char *to)
{
        int fromfd;
        ssize_t i;

        if (strEQ(to, "-")) {
                fromfd = open(from, O_RDONLY);
                if (fromfd < 0)
                        pfatal("internal error, cant reopen %s", from);
                while ((i = read(fromfd, buf, bufsz)) > 0) {
                        if (write(STDOUT_FILENO, buf, i) != i)
                                pfatal("write failed");
                }
                close(fromfd);
                return 0;
        }
        if (backup_file(to) < 0) {
                say("Cant backup %s, output is in %s: %s\n", to, from, strerror(errno));
                return -1;
        }
        if (rename(from, to) < 0) {
                if (errno != EXDEV || copy_file(from, to) < 0) {
                        say("Cant create %s, output is in %s: %s\n", to, from, strerror(errno));
                        return -1;
                }
        }
        return 0;
}

int
backup_file(const char *orig)
{
        struct stat filestat;
        char bakname[PATH_MAX], *s, *simplename;
        dev_t orig_device;
        ino_t orig_inode;

        if (backup_type == BACKUP_NONE || stat(orig, &filestat) != 0)
                return 0;
        if ((origprae && *origprae == 0) || *simple_backup_suffix == 0) {
                unlink(orig);
                return 0;
        }
        orig_device = filestat.st_dev;
        orig_inode = filestat.st_ino;

        if (origprae) {
                if (strlcpy(bakname, origprae, sizeof(bakname)) >= sizeof(bakname) ||
                    strlcat(bakname, orig, sizeof(bakname)) >= sizeof(bakname))
                        fatal("filename too long\n");
        } else {
                if ((s = find_backup_file_name(orig)) == NULL)
                        fatal("out of memory\n");
                if (strlcpy(bakname, s, sizeof(bakname)) >= sizeof(bakname))
                        fatal("filename too long\n");
                free(s);
        }

        if ((simplename = strrchr(bakname, '/')) != NULL)
                simplename = simplename + 1;
        else
                simplename = bakname;

        while (stat(bakname, &filestat) == 0 && orig_device == filestat.st_dev && orig_inode == filestat.st_ino) {
                for (s = simplename; *s && !islower((unsigned char)*s); s++)
                        ;
                if (*s)
                        *s = toupper((unsigned char)*s);
                else
                        memmove(simplename, simplename + 1, strlen(simplename + 1) + 1);
        }
        if (rename(orig, bakname) < 0) {
                if (errno != EXDEV || copy_file(orig, bakname) < 0)
                        return -1;
        }
        return 0;
}

int
copy_file(const char *from, const char *to)
{
        int tofd, fromfd;
        ssize_t i;

        tofd = open(to, O_CREAT|O_TRUNC|O_WRONLY, 0666);
        if (tofd < 0)
                return -1;
        fromfd = open(from, O_RDONLY, 0);
        if (fromfd < 0)
                pfatal("internal error, cant reopen %s", from);
        while ((i = read(fromfd, buf, bufsz)) > 0) {
                if (write(tofd, buf, i) != i)
                        pfatal("write to %s failed", to);
        }
        close(fromfd);
        close(tofd);
        return 0;
}

char *
savestr(const char *s)
{
        char *rv;

        if (!s)
                s = "Oops";
        rv = strdup(s);
        if (rv == NULL) {
                if (using_plan_a)
                        out_of_mem = 1;
                else
                        fatal("out of memory\n");
        }
        return rv;
}

void
say(const char *fmt, ...)
{
        va_list ap;

        va_start(ap, fmt);
        vfprintf(stderr, fmt, ap);
        va_end(ap);
        fflush(stderr);
}

__dead void
fatal(const char *fmt, ...)
{
        va_list ap;

        va_start(ap, fmt);
        fprintf(stderr, "patch: **** ");
        vfprintf(stderr, fmt, ap);
        va_end(ap);
        my_exit(2);
}

__dead void
pfatal(const char *fmt, ...)
{
        va_list ap;
        int errnum = errno;

        fprintf(stderr, "patch: **** ");
        va_start(ap, fmt);
        vfprintf(stderr, fmt, ap);
        va_end(ap);
        fprintf(stderr, ": %s\n", strerror(errnum));
        my_exit(2);
}

void
ask(const char *fmt, ...)
{
        va_list ap;
        ssize_t nr = 0;
        static int ttyfd = -1;

        va_start(ap, fmt);
        vfprintf(stdout, fmt, ap);
        va_end(ap);
        fflush(stdout);
        if (ttyfd < 0)
                ttyfd = open(ARUU_PATH_DEVTTY, O_RDONLY);
        if (ttyfd >= 0) {
                if ((nr = read(ttyfd, buf, bufsz)) > 0 && buf[nr - 1] == '\n')
                        buf[nr - 1] = '\0';
        }
        if (ttyfd < 0 || nr <= 0) {
                putchar('\n');
                buf[0] = '\0';
        }
}

void
set_signals(int reset)
{
        static sig_t hupval, intval;

        if (!reset) {
                hupval = signal(SIGHUP, SIG_IGN);
                if (hupval != SIG_IGN)
                        hupval = (sig_t)my_exit;
                intval = signal(SIGINT, SIG_IGN);
                if (intval != SIG_IGN)
                        intval = (sig_t)my_exit;
        }
        signal(SIGHUP, hupval);
        signal(SIGINT, intval);
}

void
ignore_signals(void)
{
        signal(SIGHUP, SIG_IGN);
        signal(SIGINT, SIG_IGN);
}

void
makedirs(const char *filename, int striplast)
{
        char *tmpbuf;

        if ((tmpbuf = strdup(filename)) == NULL)
                fatal("out of memory\n");

        if (striplast) {
                char *s = strrchr(tmpbuf, '/');
                if (s == NULL) {
                        free(tmpbuf);
                        return;
                }
                *s = '\0';
        }
        if (mkpath(tmpbuf) != 0)
                pfatal("creation of %s failed", tmpbuf);
        free(tmpbuf);
}

char *
fetchname(const char *at, int *exists, int strip_leading)
{
        char *fullname, *name, *t;
        int sleading, tab;
        struct stat filestat;

        if (at == NULL || *at == '\0')
                return NULL;
        while (isspace((unsigned char)*at))
                at++;
        if (strnEQ(at, ARUU_PATH_DEVNULL, sizeof(ARUU_PATH_DEVNULL) - 1))
                return NULL;
        name = fullname = t = savestr(at);
        tab = strchr(t, '\t') != NULL;
        for (sleading = strip_leading; *t != '\0' && ((tab && *t != '\t') || !isspace((unsigned char)*t)); t++) {
                if (t[0] == '/' && t[1] != '/' && t[1] != '\0') {
                        if (--sleading >= 0)
                                name = t + 1;
                }
        }
        *t = '\0';

        if (strip_leading == 957 && name != fullname && *fullname != '/') {
                name[-1] = '\0';
                if (stat(fullname, &filestat) == 0 && S_ISDIR(filestat.st_mode)) {
                        name[-1] = '/';
                        name = fullname;
                }
        }
        name = savestr(name);
        free(fullname);

        *exists = stat(name, &filestat) == 0;
        return name;
}

char *
checked_in(char *file)
{
        char *filebase, *filedir, tmpbuf[PATH_MAX];
        struct stat filestat;

        filebase = basename(file);
        filedir = dirname(file);

#define try(f, a1, a2, a3) \
(snprintf(tmpbuf, sizeof tmpbuf, f, a1, a2, a3), stat(tmpbuf, &filestat) == 0)

        if (try("%s/RCS/%s%s", filedir, filebase, RCSSUFFIX) ||
            try("%s/RCS/%s%s", filedir, filebase, "") ||
            try("%s/%s%s", filedir, filebase, RCSSUFFIX) ||
            try("%s/SCCS/%s%s", filedir, SCCSPREFIX, filebase) ||
            try("%s/%s%s", filedir, SCCSPREFIX, filebase))
                return file;

        return NULL;
}

__dead void
version(void)
{
        printf("Patch version 2.0-12u9-NetBSD\n");
        my_exit(EXIT_SUCCESS);
}

__dead void
my_exit(int status)
{
        unlink(TMPINNAME);
        if (!toutkeep)
                unlink(TMPOUTNAME);
        if (!trejkeep)
                unlink(TMPREJNAME);
        unlink(TMPPATNAME);
        exit(status);
}

static void *
pch_realloc(void *ptr, size_t number, size_t size)
{
        if (number > SIZE_MAX / size) {
                errno = EOVERFLOW;
                return NULL;
        }
        return realloc(ptr, number * size);
}

int
mkpath(char *path)
{
        struct stat sb;
        char *slash;
        int done = 0;

        slash = path;
        while (!done) {
                slash += strspn(slash, "/");
                slash += strcspn(slash, "/");
                done = (*slash == '\0');
                *slash = '\0';
                if (stat(path, &sb)) {
                        if (errno != ENOENT || (mkdir(path, 0777) && errno != EEXIST)) {
                                weprintf("%s", path);
                                return -1;
                        }
                } else if (!S_ISDIR(sb.st_mode)) {
                        weprintf("%s: %s", path, strerror(ENOTDIR));
                        return -1;
                }
                *slash = '/';
        }
        return 0;
}

char *
find_backup_file_name(const char *file)
{
        char *dir, *base_versions, *tmp_file;
        int highest_backup;

        if (backup_type == BACKUP_SIMPLE)
                return patch_concat(file, simple_backup_suffix);
        tmp_file = strdup(file);
        if (tmp_file == NULL)
                return NULL;
        base_versions = patch_concat(basename(tmp_file), ".~");
        free(tmp_file);
        if (base_versions == NULL)
                return NULL;
        tmp_file = strdup(file);
        if (tmp_file == NULL) {
                free(base_versions);
                return NULL;
        }
        dir = dirname(tmp_file);
        if (dir == NULL) {
                free(base_versions);
                free(tmp_file);
                return NULL;
        }
        highest_backup = max_backup_version(base_versions, dir);
        free(base_versions);
        free(tmp_file);
        if (backup_type == BACKUP_NUMBERED_EXISTING && highest_backup == 0)
                return patch_concat(file, simple_backup_suffix);
        return make_version_name(file, highest_backup + 1);
}

static int
max_backup_version(const char *file, const char *dir)
{
        DIR *dirp;
        struct dirent *dp;
        int highest_version, this_version;
        size_t file_name_length;

        dirp = opendir(dir);
        if (dirp == NULL)
                return 0;

        highest_version = 0;
        file_name_length = strlen(file);
        while ((dp = readdir(dirp)) != NULL) {
                if (strlen(dp->d_name) <= file_name_length)
                        continue;
                this_version = version_number(file, dp->d_name, file_name_length);
                if (this_version > highest_version)
                        highest_version = this_version;
        }
        closedir(dirp);
        return highest_version;
}

static char *
make_version_name(const char *file, int version_num)
{
        char *backup_name;

        if (asprintf(&backup_name, "%s.~%d~", file, version_num) == -1)
                return NULL;
        return backup_name;
}

static int
version_number(const char *base, const char *backup, size_t base_length)
{
        int version_num;
        const char *p;

        version_num = 0;
        if (!strncmp(base, backup, base_length) && ISDIGIT(backup[base_length])) {
                for (p = &backup[base_length]; ISDIGIT(*p); ++p)
                        version_num = version_num * 10 + *p - '0';
                if (p[0] != '~' || p[1])
                        version_num = 0;
        }
        return version_num;
}

static char *
patch_concat(const char *str1, const char *str2)
{
        char *newstr;

        if (asprintf(&newstr, "%s%s", str1, str2) == -1)
                return NULL;
        return newstr;
}

static int
argmatch(const char *arg, const char **optlist)
{
        int i;
        size_t arglen;
        int matchind = -1;
        int ambiguous = 0;

        arglen = strlen(arg);
        for (i = 0; optlist[i]; i++) {
                if (!strncmp(optlist[i], arg, arglen)) {
                        if (strlen(optlist[i]) == arglen)
                                return i;
                        else if (matchind == -1)
                                matchind = i;
                        else
                                ambiguous = 1;
                }
        }
        if (ambiguous)
                return -2;
        else
                return matchind;
}

static void
invalid_arg(const char *kind, const char *value, int problem)
{
        fprintf(stderr, "patch: ");
        if (problem == -1)
                fprintf(stderr, "invalid");
        else
                fprintf(stderr, "ambiguous");
        fprintf(stderr, " %s `%s'\n", kind, value);
}

enum BackupType
get_version(const char *version_str)
{
        int i;

        if (version_str == NULL || *version_str == '\0')
                return BACKUP_NUMBERED_EXISTING;
        i = argmatch(version_str, backup_args);
        if (i >= 0)
                return backup_types[i];
        invalid_arg("version control type", version_str, i);
        exit(2);
}
