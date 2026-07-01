/* See LICENSE file for copyright and license details. */
#include "paths.h"
#include "util.h"
#include "wexec.h"

#include <sys/stat.h>
#include <sys/wait.h>

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <getopt.h>
#include <limits.h>
#include <math.h>
#include <poll.h>
#include <regex.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

#ifndef __dead
#define __dead __attribute__((__noreturn__))
#endif

#define d_status d_type

/* output format options */
#define D_NORMAL     0
#define D_EDIT       -1
#define D_REVERSE    1
#define D_CONTEXT    2
#define D_UNIFIED    3
#define D_IFDEF      4
#define D_NREVERSE   5
#define D_BRIEF      6
#define D_GFORMAT    7
#define D_SIDEBYSIDE 8

#define D_UNSET      -2

/* algorithms */
#define D_DIFFNONE     0
#define D_DIFFSTONE    1
#define D_DIFFMYERS    2
#define D_DIFFPATIENCE 3

/* output flags */
#define D_HEADER 0x001
#define D_EMPTY1 0x002
#define D_EMPTY2 0x004

/* command line flags */
#define D_FORCEASCII     0x008
#define D_FOLDBLANKS     0x010
#define D_MINIMAL        0x020
#define D_IGNORECASE     0x040
#define D_PROTOTYPE      0x080
#define D_EXPANDTABS     0x100
#define D_IGNOREBLANKS   0x200
#define D_STRIPCR        0x400
#define D_SKIPBLANKLINES 0x800
#define D_MATCHLAST      0x1000

/* features supported by new algorithms */
#define D_NEWALGO_FLAGS (D_FORCEASCII | D_PROTOTYPE | D_IGNOREBLANKS)

/* status values for print_status and diffreg return values */
#define D_SAME     0
#define D_DIFFER   1
#define D_BINARY   2
#define D_MISMATCH1 3
#define D_MISMATCH2 4
#define D_SKIPPED1 5
#define D_SKIPPED2 6
#define D_ERROR    7

/* color options */
#define COLORFLAG_NEVER  0
#define COLORFLAG_AUTO   1
#define COLORFLAG_ALWAYS 2

#define FUNCTION_CONTEXT_SIZE 55

#ifndef roundup
#define roundup(x, y) ((((x) + ((y) - 1)) / (y)) * (y))
#endif

struct Excludes {
        char *pattern;
        struct Excludes *next;
};

struct Cand {
        int x;
        int y;
        int pred;
};

struct Line {
        int serial;
        int value;
};

struct ContextVec {
        int a;
        int b;
        int c;
        int d;
};

struct Pr {
        int ostdout;
        pid_t cpid;
};

struct Algorithm {
        const char *name;
        int id;
};

static struct Algorithm algorithms[] = {
        {"stone", D_DIFFSTONE},
        {"myers", D_DIFFMYERS},
        {"patience", D_DIFFPATIENCE},
        {NULL, D_DIFFNONE}
};

/* options */
#define OPTIONS "0123456789A:aBbC:cdD:efF:HhI:iL:lnNPpqrS:sTtU:uwW:X:x:y"
enum {
        OPT_TSIZE = CHAR_MAX + 1,
        OPT_STRIPCR,
        OPT_IGN_FN_CASE,
        OPT_NO_IGN_FN_CASE,
        OPT_NORMAL,
        OPT_HELP,
        OPT_HORIZON_LINES,
        OPT_CHANGED_GROUP_FORMAT,
        OPT_SUPPRESS_COMMON,
        OPT_COLOR,
        OPT_NO_DEREFERENCE,
        OPT_VERSION,
};

static struct option longopts[] = {
        { "algorithm",                  required_argument,      0,      'A' },
        { "text",                       no_argument,            0,      'a' },
        { "ignore-space-change",        no_argument,            0,      'b' },
        { "context",                    optional_argument,      0,      'C' },
        { "ifdef",                      required_argument,      0,      'D' },
        { "minimal",                    no_argument,            0,      'd' },
        { "ed",                         no_argument,            0,      'e' },
        { "forward-ed",                 no_argument,            0,      'f' },
        { "show-function-line",         required_argument,      0,      'F' },
        { "speed-large-files",          no_argument,            NULL,   'H' },
        { "ignore-blank-lines",         no_argument,            0,      'B' },
        { "ignore-matching-lines",      required_argument,      0,      'I' },
        { "ignore-case",                no_argument,            0,      'i' },
        { "paginate",                   no_argument,            NULL,   'l' },
        { "label",                      required_argument,      0,      'L' },
        { "new-file",                   no_argument,            0,      'N' },
        { "rcs",                        no_argument,            0,      'n' },
        { "unidirectional-new-file",    no_argument,            0,      'P' },
        { "show-c-function",            no_argument,            0,      'p' },
        { "brief",                      no_argument,            0,      'q' },
        { "recursive",                  no_argument,            0,      'r' },
        { "report-identical-files",     no_argument,            0,      's' },
        { "starting-file",              required_argument,      0,      'S' },
        { "expand-tabs",                no_argument,            0,      't' },
        { "initial-tab",                no_argument,            0,      'T' },
        { "unified",                    optional_argument,      0,      'U' },
        { "ignore-all-space",           no_argument,            0,      'w' },
        { "width",                      required_argument,      0,      'W' },
        { "exclude",                    required_argument,      0,      'x' },
        { "exclude-from",               required_argument,      0,      'X' },
        { "side-by-side",               no_argument,            NULL,   'y' },
        { "ignore-file-name-case",      no_argument,            NULL,   OPT_IGN_FN_CASE },
        { "help",                       no_argument,            NULL,   OPT_HELP},
        { "horizon-lines",              required_argument,      NULL,   OPT_HORIZON_LINES },
        { "no-dereference",             no_argument,            NULL,   OPT_NO_DEREFERENCE},
        { "no-ignore-file-name-case",   no_argument,            NULL,   OPT_NO_IGN_FN_CASE },
        { "normal",                     no_argument,            NULL,   OPT_NORMAL },
        { "strip-trailing-cr",          no_argument,            NULL,   OPT_STRIPCR },
        { "tabsize",                    required_argument,      NULL,   OPT_TSIZE },
        { "changed-group-format",       required_argument,      NULL,   OPT_CHANGED_GROUP_FORMAT},
        { "suppress-common-lines",      no_argument,            NULL,   OPT_SUPPRESS_COMMON },
        { "color",                      optional_argument,      NULL,   OPT_COLOR },
        { "version",                    no_argument,            NULL,   OPT_VERSION},
        { NULL,                         0,                      0,      '\0'}
};

static const char diff_version[] = "FreeBSD diff 20240307";
int lflag, Nflag, Pflag, rflag, sflag, Tflag, cflag;
int ignore_file_case, suppress_common, color, noderef;
static int help = 0;
int diff_format, diff_context, diff_algorithm, status;
int diff_algorithm_set;
int tabsize = 8, width = 130;
static int colorflag = COLORFLAG_NEVER;
char *start, *ifdefname, *diffargs, *label[2];
char *ignore_pats, *most_recent_pat;
char *group_format = NULL;
const char *add_code, *del_code;
struct stat stb1, stb2;
struct Excludes *excludes_list;
regex_t ignore_re, most_recent_re;

enum Readhash {
        RH_BINARY,
        RH_OK,
        RH_EOF
};

static struct Line *file[2];

static int *J;
static int *class;
static int *klist;
static int *member;
static int clen;
static int inifdef;
static size_t len[2];
static size_t pref, suff;
static size_t slen[2];
static int anychange;
static int hw, lpad, rpad;
static int edoffset;
static long *ixnew;
static long *ixold;
static struct Cand *clist;
static int clistlen;
static struct Line *sfile[2];
static int (*chrtran)(int);
static struct ContextVec *context_vec_start;
static struct ContextVec *context_vec_end;
static struct ContextVec *context_vec_ptr;
static char lastbuf[FUNCTION_CONTEXT_SIZE];
static int lastline;
static int lastmatchline;

static int sigpipe[2] = {-1, -1};
static struct pollfd poll_fd;

static void checked_regcomp(char const *, regex_t *);
static void usage(void);
static void conflicting_format(void);
static void push_excludes(char *);
static void push_ignore_pats(char *);
static void read_excludes_file(char *);
static void set_argstr(char **, char **);
static char *diff_splice(char *, char *);
static int do_color(void);
static int cup2low(int);
static int clow2low(int);
static void xasprintf(char **, const char *, ...);

int diffreg(char *, char *, int, int);
void diffdir(char *, char *, int);
void print_status(int, char *, char *, const char *);

static int selectfile(const struct dirent *);
static void diffit(struct dirent *, char *, size_t, struct dirent *, char *, size_t, int);
static void print_only(const char *, size_t, const char *);

static FILE *opentemp(const char *);
static void output(char *, FILE *, char *, FILE *, int);
static void check(FILE *, FILE *, int);
static void range(int, int, const char *);
static void uni_range(int, int);
static void dump_context_vec(FILE *, FILE *, int);
static void dump_unified_vec(FILE *, FILE *, int);
static int prepare(int, FILE *, size_t, int);
static void prune(void);
static void equiv(struct Line *, int, struct Line *, int, int *);
static void unravel(int);
static void unsort(struct Line *, int, int *);
static void change(char *, FILE *, char *, FILE *, int, int, int, int, int *);
static void sort(struct Line *, int);
static void print_header(const char *, const char *);
static void print_space(int, int, int);
static int ignoreline_pattern(char *);
static int ignoreline(char *, int);
static int asciifile(FILE *);
static int fetch(long *, int, int, FILE *, int, int, int);
static int newcand(int, int, int);
static int search(int *, int, int);
static int skipline(FILE *);
static int stone(int *, int, int *, int *, int);
static enum Readhash readhash(FILE *, int, unsigned *);
static int files_differ(FILE *, FILE *, int);
static char *match_function(const long *, int, FILE *);
static char *preadline(int, size_t, off_t);

static void handle_sig(int);
struct Pr *start_pr(char *, char *);
void stop_pr(struct Pr *);

static void
handle_sig(int signo)
{
        write(sigpipe[1], &signo, sizeof(signo));
}

struct Pr *
start_pr(char *file1, char *file2)
{
        int pfd[2];
        pid_t pid;
        char *header;
        struct Pr *pr;

        pr = ecalloc(1, sizeof(*pr));
        xasprintf(&header, "%s %s %s", diffargs, file1, file2);
        signal(SIGPIPE, SIG_IGN);
        fflush(stdout);
        if (pipe(pfd) == -1)
                enprintf(2, "pipe");
        if (sigpipe[0] < 0) {
                if (pipe(sigpipe) == -1)
                        enprintf(2, "pipe");
                if (fcntl(sigpipe[0], F_SETFD, FD_CLOEXEC) == -1)
                        enprintf(2, "fcntl");
                if (fcntl(sigpipe[1], F_SETFD, FD_CLOEXEC) == -1)
                        enprintf(2, "fcntl");
                if (signal(SIGCHLD, handle_sig) == SIG_ERR)
                        enprintf(2, "signal");
                poll_fd.fd = sigpipe[0];
                poll_fd.events = POLLIN;
        }
        poll_fd.revents = 0;
        switch ((pid = fork())) {
        case -1:
                status |= 2;
                free(header);
                enprintf(2, "no more processes");
                /* fallthrough */
        case 0:
                if (pfd[0] != STDIN_FILENO) {
                        dup2(pfd[0], STDIN_FILENO);
                        close(pfd[0]);
                }
                close(pfd[1]);
                {
                        char *pr_argv[4];
                        pr_argv[0] = "pr";
                        pr_argv[1] = "-h";
                        pr_argv[2] = header;
                        pr_argv[3] = NULL;
                        wexecvp_self("pr", pr_argv);
                }
                _exit(127);
        default:
                if (pfd[1] != STDOUT_FILENO) {
                        pr->ostdout = dup(STDOUT_FILENO);
                        dup2(pfd[1], STDOUT_FILENO);
                        close(pfd[1]);
                }
                close(pfd[0]);
                free(header);
                pr->cpid = pid;
        }
        return pr;
}

void
stop_pr(struct Pr *pr)
{
        int wstatus;
        int done = 0;

        if (!pr)
                return;

        fflush(stdout);
        if (pr->ostdout != STDOUT_FILENO) {
                close(STDOUT_FILENO);
                dup2(pr->ostdout, STDOUT_FILENO);
                close(pr->ostdout);
        }
        while (!done) {
                pid_t wpid;
                int npe = poll(&poll_fd, 1, -1);
                if (npe == -1) {
                        if (errno == EINTR)
                                continue;
                        enprintf(2, "poll");
                }
                if (poll_fd.revents != POLLIN)
                        continue;
                if (read(poll_fd.fd, &npe, sizeof(npe)) < 0)
                        enprintf(2, "read");
                while ((wpid = waitpid(-1, &wstatus, WNOHANG)) > 0) {
                        if (wpid != pr->cpid)
                                continue;
                        if (WIFEXITED(wstatus) && WEXITSTATUS(wstatus) != 0)
                                enprintf(2, "pr exited abnormally");
                        else if (WIFSIGNALED(wstatus))
                                enprintf(2, "pr killed by signal %d", WTERMSIG(wstatus));
                        done = 1;
                        break;
                }
        }
        free(pr);
}

// ?man diff: differential file and directory comparator
// ?man arguments: file1 file2
// ?man synopsis: [-aBbdipTtw] [-c|-e|-f|-n|-q|-u|-y] [-A algo] [--brief] [--color=when] [--changed-group-format GFMT] [--ed] [--expand-tabs] [--forward-ed] [--ignore-all-space] [--ignore-case] [--ignore-space-change] [--initial-tab] [--minimal] [--no-dereference] [--no-ignore-file-name-case] [--normal] [--rcs] [--show-c-function] [--starting-file] [--speed-large-files] [--strip-trailing-cr] [--tabsize number] [--text] [-I pattern] [-F pattern] [-L label] file1 file2
int
main(int argc, char **argv)
{
        const char *errstr = NULL;
        char *ep, **oargv;
        long l;
        int ch, dflags, lastch, gotstdin, prevoptind, newarg;

        oargv = argv;
        gotstdin = 0;
        dflags = 0;
        lastch = '\0';
        prevoptind = 1;
        newarg = 1;
        diff_context = 3;
        diff_format = D_UNSET;
        diff_algorithm = D_DIFFMYERS;
        diff_algorithm_set = 0;
#define FORMAT_MISMATCHED(type) \
        (diff_format != D_UNSET && diff_format != (type))
        while ((ch = getopt_long(argc, argv, OPTIONS, longopts, NULL)) != -1) {
                // ?man -0: context length
                // ?man -1: context length
                // ?man -2: context length
                // ?man -3: context length
                // ?man -4: context length
                // ?man -5: context length
                // ?man -6: context length
                // ?man -7: context length
                // ?man -8: context length
                // ?man -9: context length
                switch (ch) {
                case '0': case '1': case '2': case '3': case '4':
                case '5': case '6': case '7': case '8': case '9':
                        if (newarg)
                                usage();
                        else if (lastch == 'c' || lastch == 'u')
                                diff_context = 0;
                        else if (!isdigit(lastch) || diff_context > INT_MAX / 10)
                                usage();
                        diff_context = (diff_context * 10) + (ch - '0');
                        break;
                // ?man -A:algo: algorithm (stone, myers, patience)
                case 'A':
                        diff_algorithm = D_DIFFNONE;
                        for (struct Algorithm *a = algorithms; a->name; a++) {
                                if (strcasecmp(optarg, a->name) == 0) {
                                        diff_algorithm = a->id;
                                        diff_algorithm_set = 1;
                                        break;
                                }
                        }
                        if (diff_algorithm == D_DIFFNONE) {
                                printf("unknown algorithm: %s\n", optarg);
                                usage();
                        }
                        break;
                // ?man -a: force text mode
                case 'a':
                        dflags |= D_FORCEASCII;
                        break;
                // ?man -b: ignore space changes
                case 'b':
                        dflags |= D_FOLDBLANKS;
                        break;
                // ?man -C:lines: context format
                case 'C':
                // ?man -c: context format
                case 'c':
                        if (FORMAT_MISMATCHED(D_CONTEXT))
                                conflicting_format();
                        cflag = 1;
                        diff_format = D_CONTEXT;
                        if (optarg != NULL) {
                                l = strtol(optarg, &ep, 10);
                                if (*ep != '\0' || l < 0 || l >= INT_MAX)
                                        usage();
                                diff_context = (int)l;
                        }
                        break;
                // ?man -d: minimal diff search
                case 'd':
                        dflags |= D_MINIMAL;
                        break;
                // ?man -D:name: ifdef format
                case 'D':
                        if (FORMAT_MISMATCHED(D_IFDEF))
                                conflicting_format();
                        diff_format = D_IFDEF;
                        ifdefname = optarg;
                        break;
                // ?man -e: ed script format
                case 'e':
                        if (FORMAT_MISMATCHED(D_EDIT))
                                conflicting_format();
                        diff_format = D_EDIT;
                        break;
                // ?man -f: forward ed script format
                case 'f':
                        if (FORMAT_MISMATCHED(D_REVERSE))
                                conflicting_format();
                        diff_format = D_REVERSE;
                        break;
                // ?man -H: speed up large files (stub)
                case 'H':
                        break;
                // ?man -h: backward compatibility (stub)
                case 'h':
                        break;
                // ?man -B: ignore blank lines
                case 'B':
                        dflags |= D_SKIPBLANKLINES;
                        break;
                // ?man -F:pat: show function matching pattern
                case 'F':
                        if (dflags & D_PROTOTYPE)
                                conflicting_format();
                        dflags |= D_MATCHLAST;
                        most_recent_pat = estrdup(optarg);
                        break;
                // ?man -I:pat: ignore pattern matching lines
                case 'I':
                        push_ignore_pats(optarg);
                        break;
                // ?man -i: ignore case
                case 'i':
                        dflags |= D_IGNORECASE;
                        break;
                // ?man -L:label: custom label
                case 'L':
                        if (label[0] == NULL)
                                label[0] = optarg;
                        else if (label[1] == NULL)
                                label[1] = optarg;
                        else
                                usage();
                        break;
                // ?man -l: paginate output using pr
                case 'l':
                        lflag = 1;
                        break;
                // ?man -N: treat missing files as empty
                case 'N':
                        Nflag = 1;
                        break;
                // ?man -n: rcs format
                case 'n':
                        if (FORMAT_MISMATCHED(D_NREVERSE))
                                conflicting_format();
                        diff_format = D_NREVERSE;
                        break;
                // ?man -p: show C prototype context
                case 'p':
                        if (dflags & D_MATCHLAST)
                                conflicting_format();
                        dflags |= D_PROTOTYPE;
                        break;
                // ?man -P: treat missing destination files as empty
                case 'P':
                        Pflag = 1;
                        break;
                // ?man -r: recursive directory comparison
                case 'r':
                        rflag = 1;
                        break;
                // ?man -q: brief output (differ / same only)
                case 'q':
                        if (FORMAT_MISMATCHED(D_BRIEF))
                                conflicting_format();
                        diff_format = D_BRIEF;
                        break;
                // ?man -S:name: starting file in directory comparison
                case 'S':
                        start = optarg;
                        break;
                // ?man -s: report identical files
                case 's':
                        sflag = 1;
                        break;
                // ?man -T: initial tab alignment
                case 'T':
                        Tflag = 1;
                        break;
                // ?man -t: expand tabs in output
                case 't':
                        dflags |= D_EXPANDTABS;
                        break;
                // ?man -U:lines: unified context format
                case 'U':
                // ?man -u: unified context format
                case 'u':
                        if (FORMAT_MISMATCHED(D_UNIFIED))
                                conflicting_format();
                        diff_format = D_UNIFIED;
                        if (optarg != NULL) {
                                l = strtol(optarg, &ep, 10);
                                if (*ep != '\0' || l < 0 || l >= INT_MAX)
                                        usage();
                                diff_context = (int)l;
                        }
                        break;
                // ?man -w: ignore all whitespace
                case 'w':
                        dflags |= D_IGNOREBLANKS;
                        break;
                // ?man -W:cols: column width for side-by-side
                case 'W':
                        width = (int)strtonum(optarg, 1, INT_MAX, &errstr);
                        if (errstr) {
                                weprintf("invalid width argument");
                                usage();
                        }
                        break;
                // ?man -X:file: exclude patterns file
                case 'X':
                        read_excludes_file(optarg);
                        break;
                // ?man -x:pat: exclude pattern
                case 'x':
                        push_excludes(optarg);
                        break;
                // ?man -y: side by side format
                case 'y':
                        if (FORMAT_MISMATCHED(D_SIDEBYSIDE))
                                conflicting_format();
                        diff_format = D_SIDEBYSIDE;
                        break;
                case OPT_CHANGED_GROUP_FORMAT:
                        if (FORMAT_MISMATCHED(D_GFORMAT))
                                conflicting_format();
                        diff_format = D_GFORMAT;
                        group_format = optarg;
                        break;
                case OPT_HELP:
                        help = 1;
                        usage();
                        break;
                case OPT_HORIZON_LINES:
                        break;
                case OPT_IGN_FN_CASE:
                        ignore_file_case = 1;
                        break;
                case OPT_NO_IGN_FN_CASE:
                        ignore_file_case = 0;
                        break;
                case OPT_NORMAL:
                        if (FORMAT_MISMATCHED(D_NORMAL))
                                conflicting_format();
                        diff_format = D_NORMAL;
                        break;
                case OPT_TSIZE:
                        tabsize = (int)strtonum(optarg, 1, INT_MAX, &errstr);
                        if (errstr) {
                                weprintf("invalid tabsize argument");
                                usage();
                        }
                        break;
                case OPT_STRIPCR:
                        dflags |= D_STRIPCR;
                        break;
                case OPT_SUPPRESS_COMMON:
                        suppress_common = 1;
                        break;
                case OPT_COLOR:
                        if (optarg == NULL || strncmp(optarg, "auto", 4) == 0)
                                colorflag = COLORFLAG_AUTO;
                        else if (strncmp(optarg, "always", 6) == 0)
                                colorflag = COLORFLAG_ALWAYS;
                        else if (strncmp(optarg, "never", 5) == 0)
                                colorflag = COLORFLAG_NEVER;
                        else
                                enprintf(2, "unsupported color option %s", optarg);
                        break;
                case OPT_NO_DEREFERENCE:
                        noderef = 1;
                        break;
                case OPT_VERSION:
                        printf("%s\n", diff_version);
                        exit(0);
                default:
                        usage();
                        break;
                }
                lastch = ch;
                newarg = optind != prevoptind;
                prevoptind = optind;
        }
        if (diff_format == D_UNSET && (dflags & D_PROTOTYPE) != 0)
                diff_format = D_CONTEXT;
        if (diff_format == D_UNSET)
                diff_format = D_NORMAL;
        argc -= optind;
        argv += optind;

        if (do_color()) {
                char *p;
                const char *env;

                color = 1;
                add_code = "32";
                del_code = "31";
                env = getenv("DIFFCOLORS");
                if (env != NULL && *env != '\0' && (p = estrdup(env))) {
                        add_code = p;
                        strsep(&p, ":");
                        if (p != NULL)
                                del_code = p;
                }
        }

        if (argc != 2)
                usage();
        checked_regcomp(ignore_pats, &ignore_re);
        checked_regcomp(most_recent_pat, &most_recent_re);
        if (strcmp(argv[0], "-") == 0) {
                fstat(STDIN_FILENO, &stb1);
                gotstdin = 1;
        } else if (stat(argv[0], &stb1) != 0) {
                if (!Nflag || errno != ENOENT)
                        enprintf(2, "%s", argv[0]);
                dflags |= D_EMPTY1;
                memset(&stb1, 0, sizeof(struct stat));
        }

        if (strcmp(argv[1], "-") == 0) {
                fstat(STDIN_FILENO, &stb2);
                gotstdin = 1;
        } else if (stat(argv[1], &stb2) != 0) {
                if (!Nflag || errno != ENOENT)
                        enprintf(2, "%s", argv[1]);
                dflags |= D_EMPTY2;
                memset(&stb2, 0, sizeof(stb2));
                stb2.st_mode = stb1.st_mode;
        }

        if (dflags & D_EMPTY1 && dflags & D_EMPTY2){
                weprintf("%s", argv[0]);
                weprintf("%s", argv[1]);
                exit(2);
        }

        if (stb1.st_mode == 0)
                stb1.st_mode = stb2.st_mode;

        if (gotstdin && (S_ISDIR(stb1.st_mode) || S_ISDIR(stb2.st_mode)))
                enprintf(2, "cant compare - to directory");
        set_argstr(oargv, argv);
        if (S_ISDIR(stb1.st_mode) && S_ISDIR(stb2.st_mode)) {
                if (diff_format == D_IFDEF)
                        enprintf(2, "-D option not supported with directories");
                diffdir(argv[0], argv[1], dflags);
        } else {
                if (S_ISDIR(stb1.st_mode)) {
                        argv[0] = diff_splice(argv[0], argv[1]);
                        if (stat(argv[0], &stb1) == -1)
                                enprintf(2, "%s", argv[0]);
                }
                if (S_ISDIR(stb2.st_mode)) {
                        argv[1] = diff_splice(argv[1], argv[0]);
                        if (stat(argv[1], &stb2) == -1)
                                enprintf(2, "%s", argv[1]);
                }
                print_status(diffreg(argv[0], argv[1], dflags, 1), argv[0], argv[1], "");
        }
        if (fflush(stdout) != 0)
                enprintf(2, "stdout");
        exit(status);
}

static void
checked_regcomp(char const *pattern, regex_t *comp)
{
        char buf[BUFSIZ];
        int error;

        if (pattern == NULL)
                return;

        error = regcomp(comp, pattern, REG_NEWLINE | REG_EXTENDED);
        if (error != 0) {
                regerror(error, comp, buf, sizeof(buf));
                if (*pattern != '\0')
                        enprintf(2, "%s: %s", pattern, buf);
                else
                        enprintf(2, "%s", buf);
        }
}

static void
set_argstr(char **av, char **ave)
{
        size_t argsize;
        char **ap;

        argsize = strlen("diff") + 1;
        for (ap = av + 1; ap < ave; ap++) {
                if (strcmp(*ap, "--") != 0)
                        argsize += 1 + strlen(*ap);
        }
        diffargs = emalloc(argsize);
        strlcpy(diffargs, "diff", argsize);
        for (ap = av + 1; ap < ave; ap++) {
                if (strcmp(*ap, "--") != 0) {
                        strlcat(diffargs, " ", argsize);
                        strlcat(diffargs, *ap, argsize);
                }
        }
}

static void
read_excludes_file(char *file)
{
        FILE *fp;
        char *pattern = NULL;
        size_t blen = 0;
        ssize_t len;

        if (strcmp(file, "-") == 0)
                fp = stdin;
        else if ((fp = fopen(file, "r")) == NULL)
                enprintf(2, "%s", file);
        while ((len = getline(&pattern, &blen, fp)) >= 0) {
                if ((len > 0) && (pattern[len - 1] == '\n'))
                        pattern[len - 1] = '\0';
                push_excludes(pattern);
                pattern = NULL;
                blen = 0;
        }
        free(pattern);
        if (strcmp(file, "-") != 0)
                fclose(fp);
}

static void
push_excludes(char *pattern)
{
        struct Excludes *entry;

        entry = emalloc(sizeof(*entry));
        entry->pattern = pattern;
        entry->next = excludes_list;
        excludes_list = entry;
}

static void
push_ignore_pats(char *pattern)
{
        size_t len;

        if (ignore_pats == NULL)
                ignore_pats = estrdup(pattern);
        else {
                len = strlen(ignore_pats) + strlen(pattern) + 2;
                ignore_pats = ereallocarray(ignore_pats, 1, len);
                strlcat(ignore_pats, "|", len);
                strlcat(ignore_pats, pattern, len);
        }
}

void
print_status(int val, char *path1, char *path2, const char *entry)
{
        if (label[0] != NULL)
                path1 = label[0];
        if (label[1] != NULL)
                path2 = label[1];

        switch (val) {
        case D_BINARY:
                printf("Binary files %s%s and %s%s differ\n", path1, entry, path2, entry);
                break;
        case D_DIFFER:
                if (diff_format == D_BRIEF)
                        printf("Files %s%s and %s%s differ\n", path1, entry, path2, entry);
                break;
        case D_SAME:
                if (sflag)
                        printf("Files %s%s and %s%s are identical\n", path1, entry, path2, entry);
                break;
        case D_MISMATCH1:
                printf("File %s%s is directory, %s%s is file\n", path1, entry, path2, entry);
                break;
        case D_MISMATCH2:
                printf("File %s%s is file, %s%s is directory\n", path1, entry, path2, entry);
                break;
        case D_SKIPPED1:
                printf("File %s%s is not regular file or directory\n", path1, entry);
                break;
        case D_SKIPPED2:
                printf("File %s%s is not regular file or directory\n", path2, entry);
                break;
        }
}

static void
usage(void)
{
        fprintf(stderr, "usage: diff [-aBbdipTtw] [-c|-e|-f|-n|-q|-u|-y] [-A algo] "
            "[-I pattern] [-F pattern] [-L label] file1 file2\n");
        exit(2);
}

static void
conflicting_format(void)
{
        enprintf(2, "conflicting output format options");
}

static char *
diff_splice(char *dir, char *file)
{
        char *p, *res;

        p = strrchr(file, '/');
        p = p ? p + 1 : file;
        xasprintf(&res, "%s/%s", dir, p);
        return res;
}

static void
xasprintf(char **strp, const char *fmt, ...)
{
        va_list ap;
        int r;

        va_start(ap, fmt);
        r = vasprintf(strp, fmt, ap);
        va_end(ap);
        if (r == -1)
                enprintf(2, "asprintf");
}

static int
cup2low(int c)
{
        return tolower(c);
}

static int
clow2low(int c)
{
        return c;
}

static int
do_color(void)
{
        if (colorflag == COLORFLAG_ALWAYS)
                return 1;
        if (colorflag == COLORFLAG_NEVER)
                return 0;
        if (isatty(STDOUT_FILENO)) {
                char *term = getenv("COLORTERM");
                if (term && *term != '\0')
                        return 1;
        }
        return 0;
}

void
diffdir(char *p1, char *p2, int flags)
{
        struct dirent *dent1, **dp1, **edp1, **dirp1 = NULL;
        struct dirent *dent2, **dp2, **edp2, **dirp2 = NULL;
        size_t dirlen1, dirlen2;
        char path1[PATH_MAX], path2[PATH_MAX];
        int pos;

        edp1 = edp2 = NULL;
        dirlen1 = strlcpy(path1, *p1 ? p1 : ".", sizeof(path1));
        if (dirlen1 >= sizeof(path1) - 1) {
                errno = ENAMETOOLONG;
                weprintf("%s", p1);
                status |= 2;
                return;
        }
        if (path1[dirlen1 - 1] != '/') {
                path1[dirlen1++] = '/';
                path1[dirlen1] = '\0';
        }
        dirlen2 = strlcpy(path2, *p2 ? p2 : ".", sizeof(path2));
        if (dirlen2 >= sizeof(path2) - 1) {
                errno = ENAMETOOLONG;
                weprintf("%s", p2);
                status |= 2;
                return;
        }
        if (path2[dirlen2 - 1] != '/') {
                path2[dirlen2++] = '/';
                path2[dirlen2] = '\0';
        }

        pos = scandir(path1, &dirp1, selectfile, alphasort);
        if (pos == -1) {
                if (errno == ENOENT && (Nflag || Pflag))
                        pos = 0;
                else {
                        weprintf("%s", path1);
                        goto closem;
                }
        }
        dp1 = dirp1;
        edp1 = dirp1 + pos;

        pos = scandir(path2, &dirp2, selectfile, alphasort);
        if (pos == -1) {
                if (errno == ENOENT && Nflag)
                        pos = 0;
                else {
                        weprintf("%s", path2);
                        goto closem;
                }
        }
        dp2 = dirp2;
        edp2 = dirp2 + pos;

        if (start != NULL) {
                while (dp1 != edp1 && strcmp((*dp1)->d_name, start) < 0)
                        dp1++;
                while (dp2 != edp2 && strcmp((*dp2)->d_name, start) < 0)
                        dp2++;
        }

        while (dp1 != edp1 || dp2 != edp2) {
                dent1 = dp1 != edp1 ? *dp1 : NULL;
                dent2 = dp2 != edp2 ? *dp2 : NULL;

                pos = dent1 == NULL ? 1 : dent2 == NULL ? -1 :
                    ignore_file_case ? strcasecmp(dent1->d_name, dent2->d_name) :
                    strcmp(dent1->d_name, dent2->d_name);
                if (pos == 0) {
                        diffit(dent1, path1, dirlen1, dent2, path2, dirlen2, flags);
                        dp1++;
                        dp2++;
                } else if (pos < 0) {
                        if (Nflag)
                                diffit(dent1, path1, dirlen1, dent2, path2, dirlen2, flags);
                        else {
                                print_only(path1, dirlen1, dent1->d_name);
                                status |= 1;
                        }
                        dp1++;
                } else {
                        if (Nflag || Pflag)
                                diffit(dent2, path1, dirlen1, dent1, path2, dirlen2, flags);
                        else {
                                print_only(path2, dirlen2, dent2->d_name);
                                status |= 1;
                        }
                        dp2++;
                }
        }

closem:
        if (dirp1 != NULL) {
                for (dp1 = dirp1; dp1 < edp1; dp1++)
                        free(*dp1);
                free(dirp1);
        }
        if (dirp2 != NULL) {
                for (dp2 = dirp2; dp2 < edp2; dp2++)
                        free(*dp2);
                free(dirp2);
        }
}

static void
diffit(struct dirent *dp, char *path1, size_t plen1, struct dirent *dp2,
    char *path2, size_t plen2, int flags)
{
        flags |= D_HEADER;
        strlcpy(path1 + plen1, dp->d_name, PATH_MAX - plen1);

        if (ignore_file_case && strcasecmp(dp->d_name, dp2->d_name) == 0)
                strlcpy(path2 + plen2, dp2->d_name, PATH_MAX - plen2);
        else
                strlcpy(path2 + plen2, dp->d_name, PATH_MAX - plen2);

        if (noderef) {
                if (lstat(path1, &stb1) != 0) {
                        if (!(Nflag || Pflag) || errno != ENOENT) {
                                weprintf("%s", path1);
                                return;
                        }
                        flags |= D_EMPTY1;
                        memset(&stb1, 0, sizeof(stb1));
                }

                if (lstat(path2, &stb2) != 0) {
                        if (!Nflag || errno != ENOENT) {
                                weprintf("%s", path2);
                                return;
                        }
                        flags |= D_EMPTY2;
                        memset(&stb2, 0, sizeof(stb2));
                        stb2.st_mode = stb1.st_mode;
                }
                if (stb1.st_mode == 0)
                        stb1.st_mode = stb2.st_mode;
                if (S_ISLNK(stb1.st_mode) || S_ISLNK(stb2.st_mode)) {
                        if (S_ISLNK(stb1.st_mode) && S_ISLNK(stb2.st_mode)) {
                                char buf1[PATH_MAX];
                                char buf2[PATH_MAX];
                                ssize_t len1;
                                ssize_t len2;

                                len1 = readlink(path1, buf1, sizeof(buf1));
                                len2 = readlink(path2, buf2, sizeof(buf2));
                                if (len1 < 0 || len2 < 0) {
                                        perror("reading links");
                                        return;
                                }
                                buf1[len1] = '\0';
                                buf2[len2] = '\0';
                                if (len1 != len2 || strncmp(buf1, buf2, len1) != 0) {
                                        printf("Symbolic links %s and %s differ\n", path1, path2);
                                        status |= 1;
                                }
                                return;
                        }
                        printf("File %s is a %s while file %s is a %s\n",
                            path1, S_ISLNK(stb1.st_mode) ? "symbolic link" :
                                S_ISDIR(stb1.st_mode) ? "directory" : "file",
                            path2, S_ISLNK(stb2.st_mode) ? "symbolic link" :
                                S_ISDIR(stb2.st_mode) ? "directory" : "file");
                        status |= 1;
                        return;
                }
        } else {
                if (stat(path1, &stb1) != 0) {
                        if (!(Nflag || Pflag) || errno != ENOENT) {
                                weprintf("%s", path1);
                                return;
                        }
                        flags |= D_EMPTY1;
                        memset(&stb1, 0, sizeof(stb1));
                }

                if (stat(path2, &stb2) != 0) {
                        if (!Nflag || errno != ENOENT) {
                                weprintf("%s", path2);
                                return;
                        }
                        flags |= D_EMPTY2;
                        memset(&stb2, 0, sizeof(stb2));
                        stb2.st_mode = stb1.st_mode;
                }
                if (stb1.st_mode == 0)
                        stb1.st_mode = stb2.st_mode;
        }
        if (S_ISDIR(stb1.st_mode) && S_ISDIR(stb2.st_mode)) {
                if (rflag)
                        diffdir(path1, path2, flags);
                else
                        printf("Common subdirectories: %s and %s\n", path1, path2);
                return;
        }
        if (!S_ISREG(stb1.st_mode) && !S_ISDIR(stb1.st_mode))
                dp->d_status = D_SKIPPED1;
        else if (!S_ISREG(stb2.st_mode) && !S_ISDIR(stb2.st_mode))
                dp->d_status = D_SKIPPED2;
        else
                dp->d_status = diffreg(path1, path2, flags, 0);
        print_status(dp->d_status, path1, path2, "");
}

static int
selectfile(const struct dirent *dp)
{
        struct Excludes *excl;

        if (dp->d_fileno == 0)
                return 0;

        if (dp->d_name[0] == '.' && (dp->d_name[1] == '\0' ||
            (dp->d_name[1] == '.' && dp->d_name[2] == '\0')))
                return 0;

        for (excl = excludes_list; excl != NULL; excl = excl->next)
                if (fnmatch(excl->pattern, dp->d_name, FNM_PATHNAME) == 0)
                        return 0;

        return 1;
}

void
print_only(const char *path, size_t dirlen, const char *entry)
{
        if (dirlen > 1)
                dirlen--;
        printf("Only in %.*s: %s\n", (int)dirlen, path, entry);
}

int
diffreg(char *file1, char *file2, int flags, int capsicum)
{
        FILE *f1, *f2;
        int i, rval;
        struct Pr *pr = NULL;

        f1 = f2 = NULL;
        rval = D_SAME;
        anychange = 0;
        lastline = 0;
        lastmatchline = 0;

        if (diff_format == D_SIDEBYSIDE) {
                if (flags & D_EXPANDTABS) {
                        if (width > 3)
                                hw = (width - 3) / 2;
                        else
                                hw = 0;
                } else if (width <= 3 || width <= tabsize) {
                        hw = 0;
                } else {
                        hw = (width - 3) / 2;
                        while (hw > 0 && roundup(hw + 3, tabsize) + hw > width)
                                hw--;
                        if (width - (roundup(hw + 3, tabsize) + hw) < tabsize)
                                width = roundup(hw + 3, tabsize) + hw;
                }
                lpad = (width - hw * 2 - 1) / 2;
                rpad = (width - hw * 2 - 1) - lpad;
        }

        if (flags & D_IGNORECASE)
                chrtran = cup2low;
        else
                chrtran = clow2low;
        if (S_ISDIR(stb1.st_mode) != S_ISDIR(stb2.st_mode))
                return S_ISDIR(stb1.st_mode) ? D_MISMATCH1 : D_MISMATCH2;
        if (strcmp(file1, "-") == 0 && strcmp(file2, "-") == 0)
                goto closem;

        if (flags & D_EMPTY1)
                f1 = fopen(ARUU_PATH_DEVNULL, "r");
        else {
                if (!S_ISREG(stb1.st_mode)) {
                        if ((f1 = opentemp(file1)) == NULL || fstat(fileno(f1), &stb1) == -1) {
                                weprintf("%s", file1);
                                rval = D_ERROR;
                                status |= 2;
                                goto closem;
                        }
                } else if (strcmp(file1, "-") == 0)
                        f1 = stdin;
                else
                        f1 = fopen(file1, "r");
        }
        if (f1 == NULL) {
                weprintf("%s", file1);
                rval = D_ERROR;
                status |= 2;
                goto closem;
        }

        if (flags & D_EMPTY2)
                f2 = fopen(ARUU_PATH_DEVNULL, "r");
        else {
                if (!S_ISREG(stb2.st_mode)) {
                        if ((f2 = opentemp(file2)) == NULL || fstat(fileno(f2), &stb2) == -1) {
                                weprintf("%s", file2);
                                rval = D_ERROR;
                                status |= 2;
                                goto closem;
                        }
                } else if (strcmp(file2, "-") == 0)
                        f2 = stdin;
                else
                        f2 = fopen(file2, "r");
        }
        if (f2 == NULL) {
                weprintf("%s", file2);
                rval = D_ERROR;
                status |= 2;
                goto closem;
        }

        if (lflag)
                pr = start_pr(file1, file2);

        (void)capsicum;

        switch (files_differ(f1, f2, flags)) {
        case 0:
                goto closem;
        case 1:
                break;
        default:
                rval = D_ERROR;
                status |= 2;
                goto closem;
        }

        if (diff_format == D_BRIEF && ignore_pats == NULL &&
            (flags & (D_FOLDBLANKS|D_IGNOREBLANKS|D_IGNORECASE|D_SKIPBLANKLINES|D_STRIPCR)) == 0)
        {
                rval = D_DIFFER;
                status |= 1;
                goto closem;
        }
        if ((flags & D_FORCEASCII) != 0) {
                prepare(0, f1, stb1.st_size, flags);
                prepare(1, f2, stb2.st_size, flags);
        } else if (!asciifile(f1) || !asciifile(f2) ||
                   !prepare(0, f1, stb1.st_size, flags) ||
                   !prepare(1, f2, stb2.st_size, flags)) {
                rval = D_BINARY;
                status |= 1;
                goto closem;
        }
        if (len[0] > INT_MAX - 2)
                errno = EFBIG, enprintf(1, "%s", file1);
        if (len[1] > INT_MAX - 2)
                errno = EFBIG, enprintf(1, "%s", file2);

        prune();
        sort(sfile[0], slen[0]);
        sort(sfile[1], slen[1]);

        member = (int *)file[1];
        equiv(sfile[0], slen[0], sfile[1], slen[1], member);
        member = ereallocarray(member, slen[1] + 2, sizeof(*member));

        class = (int *)file[0];
        unsort(sfile[0], slen[0], class);
        class = ereallocarray(class, slen[0] + 2, sizeof(*class));

        klist = ecalloc(slen[0] + 2, sizeof(*klist));
        clen = 0;
        clistlen = 100;
        clist = ecalloc(clistlen, sizeof(*clist));
        i = stone(class, slen[0], member, klist, flags);
        free(member);
        free(class);

        J = ereallocarray(J, len[0] + 2, sizeof(*J));
        unravel(klist[i]);
        free(clist);
        free(klist);

        ixold = ereallocarray(ixold, len[0] + 2, sizeof(*ixold));
        ixnew = ereallocarray(ixnew, len[1] + 2, sizeof(*ixnew));
        check(f1, f2, flags);
        output(file1, f1, file2, f2, flags);

closem:
        if (pr != NULL)
                stop_pr(pr);
        if (anychange) {
                status |= 1;
                if (rval == D_SAME)
                        rval = D_DIFFER;
        }
        if (f1 != NULL && f1 != stdin)
                fclose(f1);
        if (f2 != NULL && f2 != stdin)
                fclose(f2);

        return rval;
}

static int
files_differ(FILE *f1, FILE *f2, int flags)
{
        char buf1[BUFSIZ], buf2[BUFSIZ];
        size_t i, j;

        if ((flags & (D_EMPTY1|D_EMPTY2)) || stb1.st_size != stb2.st_size ||
            (stb1.st_mode & S_IFMT) != (stb2.st_mode & S_IFMT))
                return 1;

        if (stb1.st_dev == stb2.st_dev && stb1.st_ino == stb2.st_ino)
                return 0;

        for (;;) {
                i = fread(buf1, 1, sizeof(buf1), f1);
                j = fread(buf2, 1, sizeof(buf2), f2);
                if ((!i && ferror(f1)) || (!j && ferror(f2)))
                        return -1;
                if (i != j)
                        return 1;
                if (i == 0)
                        return 0;
                if (memcmp(buf1, buf2, i) != 0)
                        return 1;
        }
}

static FILE *
opentemp(const char *f)
{
        char buf[BUFSIZ], tempfile[PATH_MAX];
        ssize_t nread;
        int ifd, ofd;

        if (strcmp(f, "-") == 0)
                ifd = STDIN_FILENO;
        else if ((ifd = open(f, O_RDONLY, 0644)) == -1)
                return NULL;

        strlcpy(tempfile, ARUU_PATH_TMP "/diff.XXXXXXXX", sizeof(tempfile));
        if ((ofd = mkstemp(tempfile)) == -1) {
                close(ifd);
                return NULL;
        }
        unlink(tempfile);
        while ((nread = read(ifd, buf, BUFSIZ)) > 0) {
                if (write(ofd, buf, nread) != nread) {
                        close(ifd);
                        close(ofd);
                        return NULL;
                }
        }
        close(ifd);
        lseek(ofd, (off_t)0, SEEK_SET);
        return fdopen(ofd, "r");
}

static int
prepare(int i, FILE *fd, size_t filesize, int flags)
{
        struct Line *p;
        unsigned h;
        size_t sz, j = 0;
        enum Readhash r;

        rewind(fd);
        sz = filesize / 25;
        if (sz < 100)
                sz = 100;

        p = ecalloc(sz + 3, sizeof(*p));
        while ((r = readhash(fd, flags, &h)) != RH_EOF) {
                if (r == RH_BINARY)
                        return 0;
                if (j == SIZE_MAX)
                        break;
                if (j == sz) {
                        sz = sz * 3 / 2;
                        p = ereallocarray(p, sz + 3, sizeof(*p));
                }
                p[++j].value = h;
        }
        len[i] = j;
        file[i] = p;
        return 1;
}

static void
prune(void)
{
        size_t i, j;

        for (pref = 0; pref < len[0] && pref < len[1] &&
            file[0][pref + 1].value == file[1][pref + 1].value;
            pref++)
                ;
        for (suff = 0; suff < len[0] - pref && suff < len[1] - pref &&
            file[0][len[0] - suff].value == file[1][len[1] - suff].value;
            suff++)
                ;
        for (j = 0; j < 2; j++) {
                sfile[j] = file[j] + pref;
                slen[j] = len[j] - pref - suff;
                for (i = 0; i <= slen[j]; i++)
                        sfile[j][i].serial = i;
        }
}

static void
equiv(struct Line *a, int n, struct Line *b, int m, int *c)
{
        int i, j;

        i = j = 1;
        while (i <= n && j <= m) {
                if (a[i].value < b[j].value)
                        a[i++].value = 0;
                else if (a[i].value == b[j].value)
                        a[i++].value = j;
                else
                        j++;
        }
        while (i <= n)
                a[i++].value = 0;
        b[m + 1].value = 0;
        j = 0;
        while (++j <= m) {
                c[j] = -b[j].serial;
                while (b[j + 1].value == b[j].value) {
                        j++;
                        c[j] = b[j].serial;
                }
        }
        c[j] = -1;
}

static int
stone(int *a, int n, int *b, int *c, int flags)
{
        int i, k, y, j, l;
        int oldc, tc, oldl, sq;
        unsigned numtries, bound;

        if (flags & D_MINIMAL)
                bound = UINT_MAX;
        else {
                sq = sqrt(n);
                bound = MAX(256, sq);
        }

        k = 0;
        c[0] = newcand(0, 0, 0);
        for (i = 1; i <= n; i++) {
                j = a[i];
                if (j == 0)
                        continue;
                y = -b[j];
                oldl = 0;
                oldc = c[0];
                numtries = 0;
                do {
                        if (y <= clist[oldc].y)
                                continue;
                        l = search(c, k, y);
                        if (l != oldl + 1)
                                oldc = c[l - 1];
                        if (l <= k) {
                                if (clist[c[l]].y <= y)
                                        continue;
                                tc = c[l];
                                c[l] = newcand(i, y, oldc);
                                oldc = tc;
                                oldl = l;
                                numtries++;
                        } else {
                                c[l] = newcand(i, y, oldc);
                                k++;
                                break;
                        }
                } while ((y = b[++j]) > 0 && numtries < bound);
        }
        return k;
}

static int
newcand(int x, int y, int pred)
{
        struct Cand *q;

        if (clen == clistlen) {
                clistlen = clistlen * 11 / 10;
                clist = ereallocarray(clist, clistlen, sizeof(*clist));
        }
        q = clist + clen;
        q->x = x;
        q->y = y;
        q->pred = pred;
        return clen++;
}

static int
search(int *c, int k, int y)
{
        int i, j, l, t;

        if (clist[c[k]].y < y)
                return k + 1;
        i = 0;
        j = k + 1;
        for (;;) {
                l = (i + j) / 2;
                if (l <= i)
                        break;
                t = clist[c[l]].y;
                if (t > y)
                        j = l;
                else if (t < y)
                        i = l;
                else
                        return l;
        }
        return l + 1;
}

static void
unravel(int p)
{
        struct Cand *q;
        size_t i;

        for (i = 0; i <= len[0]; i++)
                J[i] = i <= pref ? i :
                    i > len[0] - suff ? i + len[1] - len[0] : 0;
        for (q = clist + p; q->y != 0; q = clist + q->pred)
                J[q->x + pref] = q->y + pref;
}

static void
check(FILE *f1, FILE *f2, int flags)
{
        int i, j, c, d;
        long ctold, ctnew;

        rewind(f1);
        rewind(f2);
        j = 1;
        ixold[0] = ixnew[0] = 0;
        ctold = ctnew = 0;
        for (i = 1; i <= (int)len[0]; i++) {
                if (J[i] == 0) {
                        ixold[i] = ctold += skipline(f1);
                        continue;
                }
                while (j < J[i]) {
                        ixnew[j] = ctnew += skipline(f2);
                        j++;
                }
                if (flags & (D_FOLDBLANKS | D_IGNOREBLANKS | D_IGNORECASE | D_STRIPCR)) {
                        for (;;) {
                                c = getc(f1);
                                d = getc(f2);
                                if (flags & (D_FOLDBLANKS | D_IGNOREBLANKS)) {
                                        if (c == EOF && isspace(d)) {
                                                ctnew++;
                                                break;
                                        } else if (isspace(c) && d == EOF) {
                                                ctold++;
                                                break;
                                        }
                                }
                                ctold++;
                                ctnew++;
                                if (flags & D_STRIPCR && (c == '\r' || d == '\r')) {
                                        if (c == '\r') {
                                                if ((c = getc(f1)) == '\n')
                                                        ctold++;
                                                else
                                                        ungetc(c, f1);
                                        }
                                        if (d == '\r') {
                                                if ((d = getc(f2)) == '\n')
                                                        ctnew++;
                                                else
                                                        ungetc(d, f2);
                                        }
                                        break;
                                }
                                if ((flags & D_FOLDBLANKS) && isspace(c) && isspace(d)) {
                                        do {
                                                if (c == '\n')
                                                        break;
                                                ctold++;
                                        } while (isspace(c = getc(f1)));
                                        do {
                                                if (d == '\n')
                                                        break;
                                                ctnew++;
                                        } while (isspace(d = getc(f2)));
                                } else if (flags & D_IGNOREBLANKS) {
                                        while (isspace(c) && c != '\n') {
                                                c = getc(f1);
                                                ctold++;
                                        }
                                        while (isspace(d) && d != '\n') {
                                                d = getc(f2);
                                                ctnew++;
                                        }
                                }
                                if (chrtran(c) != chrtran(d)) {
                                        J[i] = 0;
                                        if (c != '\n' && c != EOF)
                                                ctold += skipline(f1);
                                        if (d != '\n' && c != EOF)
                                                ctnew += skipline(f2);
                                        break;
                                }
                                if (c == '\n' || c == EOF)
                                        break;
                        }
                } else {
                        for (;;) {
                                ctold++;
                                ctnew++;
                                if ((c = getc(f1)) != (d = getc(f2))) {
                                        J[i] = 0;
                                        if (c != '\n' && c != EOF)
                                                ctold += skipline(f1);
                                        if (d != '\n' && c != EOF)
                                                ctnew += skipline(f2);
                                        break;
                                }
                                if (c == '\n' || c == EOF)
                                        break;
                        }
                }
                ixold[i] = ctold;
                ixnew[j] = ctnew;
                j++;
        }
        for (; j <= (int)len[1]; j++) {
                ixnew[j] = ctnew += skipline(f2);
        }
}

static void
sort(struct Line *a, int n)
{
        struct Line *ai, *aim, w;
        int j, m = 0, k;

        if (n == 0)
                return;
        for (j = 1; j <= n; j *= 2)
                m = 2 * j - 1;
        for (m /= 2; m != 0; m /= 2) {
                k = n - m;
                for (j = 1; j <= k; j++) {
                        for (ai = &a[j]; ai > a; ai -= m) {
                                aim = &ai[m];
                                if (aim < ai)
                                        break;
                                if (aim->value > ai[0].value ||
                                    (aim->value == ai[0].value && aim->serial > ai[0].serial))
                                        break;
                                w.value = ai[0].value;
                                ai[0].value = aim->value;
                                aim->value = w.value;
                                w.serial = ai[0].serial;
                                ai[0].serial = aim->serial;
                                aim->serial = w.serial;
                        }
                }
        }
}

static void
unsort(struct Line *f, int l, int *b)
{
        int *a, i;

        a = ecalloc(l + 1, sizeof(*a));
        for (i = 1; i <= l; i++)
                a[f[i].serial] = f[i].value;
        for (i = 1; i <= l; i++)
                b[i] = a[i];
        free(a);
}

static int
skipline(FILE *f)
{
        int i, c;

        for (i = 1; (c = getc(f)) != '\n' && c != EOF; i++)
                continue;
        return i;
}

static void
output(char *file1, FILE *f1, char *file2, FILE *f2, int flags)
{
        int i, j, m, i0, i1, j0, j1, nc;

        rewind(f1);
        rewind(f2);
        m = len[0];
        J[0] = 0;
        J[m + 1] = len[1] + 1;
        if (diff_format != D_EDIT) {
                for (i0 = 1; i0 <= m; i0 = i1 + 1) {
                        while (i0 <= m && J[i0] == J[i0 - 1] + 1) {
                                if (diff_format == D_SIDEBYSIDE && suppress_common != 1) {
                                        nc = fetch(ixold, i0, i0, f1, '\0', 1, flags);
                                        print_space(nc, hw - nc + lpad + 1 + rpad, flags);
                                        fetch(ixnew, J[i0], J[i0], f2, '\0', 0, flags);
                                        printf("\n");
                                }
                                i0++;
                        }
                        j0 = J[i0 - 1] + 1;
                        i1 = i0 - 1;
                        while (i1 < m && J[i1 + 1] == 0)
                                i1++;
                        j1 = J[i1 + 1] - 1;
                        J[i1] = j1;

                        if (diff_format == D_SIDEBYSIDE) {
                                for (i = i0, j = j0; i <= i1 && j <= j1; i++, j++)
                                        change(file1, f1, file2, f2, i, i, j, j, &flags);
                                while (i <= i1) {
                                        change(file1, f1, file2, f2, i, i, j + 1, j, &flags);
                                        i++;
                                }
                                while (j <= j1) {
                                        change(file1, f1, file2, f2, i + 1, i, j, j, &flags);
                                        j++;
                                }
                        } else
                                change(file1, f1, file2, f2, i0, i1, j0, j1, &flags);
                }
        } else {
                for (i0 = m; i0 >= 1; i0 = i1 - 1) {
                        while (i0 >= 1 && J[i0] == J[i0 + 1] - 1 && J[i0] != 0)
                                i0--;
                        j0 = J[i0 + 1] - 1;
                        i1 = i0 + 1;
                        while (i1 > 1 && J[i1 - 1] == 0)
                                i1--;
                        j1 = J[i1 - 1] + 1;
                        J[i1] = j1;
                        change(file1, f1, file2, f2, i1, i0, j1, j0, &flags);
                }
        }
        if (m == 0)
                change(file1, f1, file2, f2, 1, 0, 1, len[1], &flags);
        if (diff_format == D_IFDEF || diff_format == D_GFORMAT) {
                for (;;) {
#define c i0
                        if ((c = getc(f1)) == EOF)
                                return;
                        printf("%c", c);
                }
#undef c
        }
        if (anychange != 0) {
                if (diff_format == D_CONTEXT)
                        dump_context_vec(f1, f2, flags);
                else if (diff_format == D_UNIFIED)
                        dump_unified_vec(f1, f2, flags);
        }
}

static void
range(int a, int b, const char *separator)
{
        printf("%d", a > b ? b : a);
        if (a < b)
                printf("%s%d", separator, b);
}

static void
uni_range(int a, int b)
{
        if (a < b)
                printf("%d,%d", a, b - a + 1);
        else if (a == b)
                printf("%d", b);
        else
                printf("%d,0", b);
}

static char *
preadline(int fd, size_t rlen, off_t off)
{
        char *line;
        ssize_t nr;

        line = emalloc(rlen + 1);
        if ((nr = pread(fd, line, rlen, off)) == -1)
                enprintf(2, "preadline");
        if (nr > 0 && line[nr-1] == '\n')
                nr--;
        line[nr] = '\0';
        return line;
}

static int
ignoreline_pattern(char *line)
{
        int ret;

        ret = regexec(&ignore_re, line, 0, NULL, 0);
        return ret == 0;
}

static int
ignoreline(char *line, int skip_blanks)
{
        if (skip_blanks && *line == '\0')
                return 1;
        if (ignore_pats != NULL && ignoreline_pattern(line))
                return 1;
        return 0;
}

static void
change(char *file1, FILE *f1, char *file2, FILE *f2, int a, int b, int c, int d,
    int *pflags)
{
        static size_t max_context = 64;
        long curpos;
        int i, nc;
        const char *walk;
        int skip_blanks, ignore;

        skip_blanks = (*pflags & D_SKIPBLANKLINES);
restart:
        if ((diff_format != D_IFDEF || diff_format == D_GFORMAT) && a > b && c > d)
                return;
        if (ignore_pats != NULL || skip_blanks) {
                char *line;
                if (a <= b) {
                        for (i = a; i <= b; i++) {
                                line = preadline(fileno(f1), ixold[i] - ixold[i - 1], ixold[i - 1]);
                                ignore = ignoreline(line, skip_blanks);
                                free(line);
                                if (!ignore)
                                        goto proceed;
                        }
                }
                if (a > b || c <= d) {
                        for (i = c; i <= d; i++) {
                                line = preadline(fileno(f2), ixnew[i] - ixnew[i - 1], ixnew[i - 1]);
                                ignore = ignoreline(line, skip_blanks);
                                free(line);
                                if (!ignore)
                                        goto proceed;
                        }
                }
                return;
        }
proceed:
        if (*pflags & D_HEADER && diff_format != D_BRIEF) {
                printf("%s %s %s\n", diffargs, file1, file2);
                *pflags &= ~D_HEADER;
        }
        if (diff_format == D_CONTEXT || diff_format == D_UNIFIED) {
                if (context_vec_start == NULL || context_vec_ptr == context_vec_end - 1) {
                        ptrdiff_t offset = -1;

                        if (context_vec_start != NULL)
                                offset = context_vec_ptr - context_vec_start;
                        max_context <<= 1;
                        context_vec_start = ereallocarray(context_vec_start, max_context, sizeof(*context_vec_start));
                        context_vec_end = context_vec_start + max_context;
                        context_vec_ptr = context_vec_start + offset;
                }
                if (anychange == 0) {
                        print_header(file1, file2);
                        anychange = 1;
                } else if (a > context_vec_ptr->b + (2 * diff_context) + 1 &&
                    c > context_vec_ptr->d + (2 * diff_context) + 1) {
                        if (diff_format == D_CONTEXT)
                                dump_context_vec(f1, f2, *pflags);
                        else
                                dump_unified_vec(f1, f2, *pflags);
                }
                context_vec_ptr++;
                context_vec_ptr->a = a;
                context_vec_ptr->b = b;
                context_vec_ptr->c = c;
                context_vec_ptr->d = d;
                return;
        }
        if (anychange == 0)
                anychange = 1;
        switch (diff_format) {
        case D_BRIEF:
                return;
        case D_NORMAL:
        case D_EDIT:
                range(a, b, ",");
                printf("%c", a > b ? 'a' : c > d ? 'd' : 'c');
                if (diff_format == D_NORMAL)
                        range(c, d, ",");
                printf("\n");
                break;
        case D_REVERSE:
                printf("%c", a > b ? 'a' : c > d ? 'd' : 'c');
                range(a, b, " ");
                printf("\n");
                break;
        case D_NREVERSE:
                if (a > b)
                        printf("a%d %d\n", b, d - c + 1);
                else {
                        printf("d%d %d\n", a, b - a + 1);
                        if (!(c > d))
                                printf("a%d %d\n", b, d - c + 1);
                }
                break;
        }
        if (diff_format == D_GFORMAT) {
                curpos = ftell(f1);
                nc = ixold[a > b ? b : a - 1] - curpos;
                for (i = 0; i < nc; i++)
                        printf("%c", getc(f1));
                for (walk = group_format; *walk != '\0'; walk++) {
                        if (*walk == '%') {
                                walk++;
                                switch (*walk) {
                                case '<':
                                        fetch(ixold, a, b, f1, '<', 1, *pflags);
                                        break;
                                case '>':
                                        fetch(ixnew, c, d, f2, '>', 0, *pflags);
                                        break;
                                default:
                                        printf("%%%c", *walk);
                                        break;
                                }
                                continue;
                        }
                        printf("%c", *walk);
                }
        }
        if (diff_format == D_SIDEBYSIDE) {
                if (color && a > b)
                        printf("\033[%sm", add_code);
                else if (color && c > d)
                        printf("\033[%sm", del_code);
                if (a > b) {
                        print_space(0, hw + lpad, *pflags);
                } else {
                        nc = fetch(ixold, a, b, f1, '\0', 1, *pflags);
                        print_space(nc, hw - nc + lpad, *pflags);
                }
                if (color && a > b)
                        printf("\033[%sm", add_code);
                else if (color && c > d)
                        printf("\033[%sm", del_code);
                printf("%c", (a > b) ? '>' : ((c > d) ? '<' : '|'));
                if (color && c > d)
                        printf("\033[m");
                print_space(hw + lpad + 1, rpad, *pflags);
                fetch(ixnew, c, d, f2, '\0', 0, *pflags);
                printf("\n");
        }
        if (diff_format == D_NORMAL || diff_format == D_IFDEF) {
                fetch(ixold, a, b, f1, '<', 1, *pflags);
                if (a <= b && c <= d && diff_format == D_NORMAL)
                        printf("---\n");
        }
        if (diff_format != D_GFORMAT && diff_format != D_SIDEBYSIDE)
                fetch(ixnew, c, d, f2, diff_format == D_NORMAL ? '>' : '\0', 0, *pflags);
        if (edoffset != 0 && diff_format == D_EDIT) {
                printf(".\n");
                printf("%ds/.//\n", a + edoffset - 1);
                b = a + edoffset - 1;
                a = b + 1;
                c += edoffset;
                goto restart;
        }
        if ((diff_format == D_EDIT || diff_format == D_REVERSE) && c <= d)
                printf(".\n");
        if (inifdef) {
                printf("#endif /* %s */\n", ifdefname);
                inifdef = 0;
        }
}

static int
fetch(long *f, int a, int b, FILE *lb, int ch, int oldfile, int flags)
{
        int i, j, c, lastc, col, nc, newcol;

        edoffset = 0;
        nc = 0;
        col = 0;
        if ((diff_format == D_IFDEF) && oldfile) {
                long curpos = ftell(lb);
                nc = f[a > b ? b : a - 1] - curpos;
                for (i = 0; i < nc; i++)
                        printf("%c", getc(lb));
        }
        if (a > b)
                return 0;
        if (diff_format == D_IFDEF) {
                if (inifdef) {
                        printf("#else /* %s%s */\n",
                            oldfile == 1 ? "!" : "", ifdefname);
                } else {
                        if (oldfile)
                                printf("#ifndef %s\n", ifdefname);
                        else
                                printf("#ifdef %s\n", ifdefname);
                }
                inifdef = 1 + oldfile;
        }
        for (i = a; i <= b; i++) {
                fseek(lb, f[i - 1], SEEK_SET);
                nc = f[i] - f[i - 1];
                if (diff_format == D_SIDEBYSIDE && hw < nc)
                        nc = hw;
                if (diff_format != D_IFDEF && diff_format != D_GFORMAT && ch != '\0') {
                        if (color && (ch == '>' || ch == '+'))
                                printf("\033[%sm", add_code);
                        else if (color && (ch == '<' || ch == '-'))
                                printf("\033[%sm", del_code);
                        printf("%c", ch);
                        if (Tflag && (diff_format == D_NORMAL ||
                            diff_format == D_CONTEXT ||
                            diff_format == D_UNIFIED))
                                printf("\t");
                        else if (diff_format != D_UNIFIED)
                                printf(" ");
                }
                col = j = 0;
                lastc = '\0';
                while (j < nc && (hw == 0 || col < hw)) {
                        c = getc(lb);
                        if (flags & D_STRIPCR && c == '\r') {
                                if ((c = getc(lb)) == '\n')
                                        j++;
                                else {
                                        ungetc(c, lb);
                                        c = '\r';
                                }
                        }
                        if (c == EOF) {
                                if (diff_format == D_EDIT ||
                                    diff_format == D_REVERSE ||
                                    diff_format == D_NREVERSE)
                                        weprintf("No newline at end of file");
                                else
                                        printf("\n\\ No newline at end of file\n");
                                return col;
                        }
                        if (c == '\t') {
                                newcol = roundup(col + 1, tabsize);
                                if ((flags & D_EXPANDTABS) == 0) {
                                        if (hw > 0 && newcol >= hw)
                                                return col;
                                        printf("\t");
                                } else {
                                        if (hw > 0 && newcol > hw)
                                                newcol = hw;
                                        printf("%*s", newcol - col, "");
                                }
                                col = newcol;
                        } else {
                                if (diff_format == D_EDIT && j == 1 && c == '\n' && lastc == '.') {
                                        printf(".\n");
                                        edoffset = i - a + 1;
                                        return edoffset;
                                }
                                if (diff_format != D_SIDEBYSIDE || c != '\n') {
                                        if (color && c == '\n')
                                                printf("\033[m%c", c);
                                        else
                                                printf("%c", c);
                                        col++;
                                }
                        }
                        j++;
                        lastc = c;
                }
        }
        if (color && diff_format == D_SIDEBYSIDE)
                printf("\033[m");
        return col;
}

static enum Readhash
readhash(FILE *f, int flags, unsigned *hash)
{
        int i, t, space;
        unsigned sum;

        sum = 1;
        space = 0;
        for (i = 0;;) {
                switch (t = getc(f)) {
                case '\0':
                        if ((flags & D_FORCEASCII) == 0)
                                return RH_BINARY;
                        goto hashchar;
                case '\r':
                        if (flags & D_STRIPCR) {
                                t = getc(f);
                                if (t == '\n')
                                        break;
                                ungetc(t, f);
                        }
                        /* FALLTHROUGH */
                case '\t':
                case '\v':
                case '\f':
                case ' ':
                        if ((flags & (D_FOLDBLANKS|D_IGNOREBLANKS)) != 0) {
                                space++;
                                continue;
                        }
                        /* FALLTHROUGH */
                default:
                hashchar:
                        if (space && (flags & D_IGNOREBLANKS) == 0) {
                                i++;
                                space = 0;
                        }
                        sum = sum * 127 + chrtran(t);
                        i++;
                        continue;
                case EOF:
                        if (i == 0)
                                return RH_EOF;
                        /* FALLTHROUGH */
                case '\n':
                        break;
                }
                break;
        }
        *hash = sum;
        return RH_OK;
}

static int
asciifile(FILE *f)
{
        unsigned char buf[BUFSIZ];
        size_t cnt;

        if (f == NULL)
                return 1;

        rewind(f);
        cnt = fread(buf, 1, sizeof(buf), f);
        return memchr(buf, '\0', cnt) == NULL;
}

#define begins_with(s, pre) (strncmp(s, pre, sizeof(pre) - 1) == 0)

static char *
match_function(const long *f, int pos, FILE *fp)
{
        char buf[FUNCTION_CONTEXT_SIZE];
        size_t nc;
        int last = lastline;
        const char *state = NULL;

        lastline = pos;
        for (; pos > last; pos--) {
                fseek(fp, f[pos - 1], SEEK_SET);
                nc = f[pos] - f[pos - 1];
                if (nc >= sizeof(buf))
                        nc = sizeof(buf) - 1;
                nc = fread(buf, 1, nc, fp);
                if (nc == 0)
                        continue;
                buf[nc] = '\0';
                buf[strcspn(buf, "\n")] = '\0';
                if (most_recent_pat != NULL) {
                        int ret = regexec(&most_recent_re, buf, 0, NULL, 0);
                        if (ret != 0)
                                continue;
                        strlcpy(lastbuf, buf, sizeof(lastbuf));
                        lastmatchline = pos;
                        return lastbuf;
                } else if (isalpha(buf[0]) || buf[0] == '_' || buf[0] == '$' || buf[0] == '-' || buf[0] == '+') {
                        if (begins_with(buf, "private:")) {
                                if (!state)
                                        state = " (private)";
                        } else if (begins_with(buf, "protected:")) {
                                if (!state)
                                        state = " (protected)";
                        } else if (begins_with(buf, "public:")) {
                                if (!state)
                                        state = " (public)";
                        } else {
                                strlcpy(lastbuf, buf, sizeof(lastbuf));
                                if (state)
                                        strlcat(lastbuf, state, sizeof(lastbuf));
                                lastmatchline = pos;
                                return lastbuf;
                        }
                }
        }
        return lastmatchline > 0 ? lastbuf : NULL;
}

static void
dump_context_vec(FILE *f1, FILE *f2, int flags)
{
        struct ContextVec *cvp = context_vec_start;
        int lowa, upb, lowc, upd, do_output;
        int a, b, c, d;
        char ch, *f;

        if (context_vec_start > context_vec_ptr)
                return;

        b = d = 0;
        lowa = MAX(1, cvp->a - diff_context);
        upb = MIN((int)len[0], context_vec_ptr->b + diff_context);
        lowc = MAX(1, cvp->c - diff_context);
        upd = MIN((int)len[1], context_vec_ptr->d + diff_context);

        printf("***************");
        if (flags & (D_PROTOTYPE | D_MATCHLAST)) {
                f = match_function(ixold, cvp->a - 1, f1);
                if (f != NULL)
                        printf(" %s", f);
        }
        printf("\n*** ");
        range(lowa, upb, ",");
        printf(" ****\n");

        do_output = 0;
        for (; cvp <= context_vec_ptr; cvp++)
                if (cvp->a <= cvp->b) {
                        cvp = context_vec_start;
                        do_output++;
                        break;
                }
        if (do_output) {
                while (cvp <= context_vec_ptr) {
                        a = cvp->a;
                        b = cvp->b;
                        c = cvp->c;
                        d = cvp->d;

                        if (a <= b && c <= d)
                                ch = 'c';
                        else
                                ch = (a <= b) ? 'd' : 'a';

                        if (ch == 'a')
                                fetch(ixold, lowa, b, f1, ' ', 0, flags);
                        else {
                                fetch(ixold, lowa, a - 1, f1, ' ', 0, flags);
                                fetch(ixold, a, b, f1, ch == 'c' ? '!' : '-', 0, flags);
                        }
                        lowa = b + 1;
                        cvp++;
                }
                fetch(ixold, b + 1, upb, f1, ' ', 0, flags);
        }

        printf("--- ");
        range(lowc, upd, ",");
        printf(" ----\n");

        do_output = 0;
        for (cvp = context_vec_start; cvp <= context_vec_ptr; cvp++)
                if (cvp->c <= cvp->d) {
                        cvp = context_vec_start;
                        do_output++;
                        break;
                }
        if (do_output) {
                while (cvp <= context_vec_ptr) {
                        a = cvp->a;
                        b = cvp->b;
                        c = cvp->c;
                        d = cvp->d;

                        if (a <= b && c <= d)
                                ch = 'c';
                        else
                                ch = (a <= b) ? 'd' : 'a';

                        if (ch == 'd')
                                fetch(ixnew, lowc, d, f2, ' ', 0, flags);
                        else {
                                fetch(ixnew, lowc, c - 1, f2, ' ', 0, flags);
                                fetch(ixnew, c, d, f2, ch == 'c' ? '!' : '+', 0, flags);
                        }
                        lowc = d + 1;
                        cvp++;
                }
                fetch(ixnew, d + 1, upd, f2, ' ', 0, flags);
        }
        context_vec_ptr = context_vec_start - 1;
}

static void
dump_unified_vec(FILE *f1, FILE *f2, int flags)
{
        struct ContextVec *cvp = context_vec_start;
        int lowa, upb, lowc, upd;
        int a, b, c, d;
        char ch, *f;

        if (context_vec_start > context_vec_ptr)
                return;

        b = d = 0;
        lowa = MAX(1, cvp->a - diff_context);
        upb = MIN((int)len[0], context_vec_ptr->b + diff_context);
        lowc = MAX(1, cvp->c - diff_context);
        upd = MIN((int)len[1], context_vec_ptr->d + diff_context);

        printf("@@ -");
        uni_range(lowa, upb);
        printf(" +");
        uni_range(lowc, upd);
        printf(" @@");
        if (flags & (D_PROTOTYPE | D_MATCHLAST)) {
                f = match_function(ixold, cvp->a - 1, f1);
                if (f != NULL)
                        printf(" %s", f);
        }
        printf("\n");

        for (; cvp <= context_vec_ptr; cvp++) {
                a = cvp->a;
                b = cvp->b;
                c = cvp->c;
                d = cvp->d;

                if (a <= b && c <= d)
                        ch = 'c';
                else
                        ch = (a <= b) ? 'd' : 'a';

                switch (ch) {
                case 'c':
                        fetch(ixold, lowa, a - 1, f1, ' ', 0, flags);
                        fetch(ixold, a, b, f1, '-', 0, flags);
                        fetch(ixnew, c, d, f2, '+', 0, flags);
                        break;
                case 'd':
                        fetch(ixold, lowa, a - 1, f1, ' ', 0, flags);
                        fetch(ixold, a, b, f1, '-', 0, flags);
                        break;
                case 'a':
                        fetch(ixnew, lowc, c - 1, f2, ' ', 0, flags);
                        fetch(ixnew, c, d, f2, '+', 0, flags);
                        break;
                }
                lowa = b + 1;
                lowc = d + 1;
        }
        fetch(ixnew, d + 1, upd, f2, ' ', 0, flags);
        context_vec_ptr = context_vec_start - 1;
}

static void
print_header(const char *file1, const char *file2)
{
        const char *time_format;
        char buf[256];
        struct tm tm1, tm2, *tm_ptr1, *tm_ptr2;
        int nsec1 = stb1.st_mtim.tv_nsec;
        int nsec2 = stb2.st_mtim.tv_nsec;

        time_format = "%Y-%m-%d %H:%M:%S";
        if (cflag)
                time_format = "%c";
        tm_ptr1 = localtime_r(&stb1.st_mtime, &tm1);
        tm_ptr2 = localtime_r(&stb2.st_mtime, &tm2);
        if (label[0] != NULL)
                printf("%s %s\n", diff_format == D_CONTEXT ? "***" : "---", label[0]);
        else {
                strftime(buf, sizeof(buf), time_format, tm_ptr1);
                printf("%s %s\t%s", diff_format == D_CONTEXT ? "***" : "---", file1, buf);
                if (!cflag) {
                        strftime(buf, sizeof(buf), "%z", tm_ptr1);
                        printf(".%.9d %s", nsec1, buf);
                }
                printf("\n");
        }
        if (label[1] != NULL)
                printf("%s %s\n", diff_format == D_CONTEXT ? "---" : "+++", label[1]);
        else {
                strftime(buf, sizeof(buf), time_format, tm_ptr2);
                printf("%s %s\t%s", diff_format == D_CONTEXT ? "---" : "+++", file2, buf);
                if (!cflag) {
                        strftime(buf, sizeof(buf), "%z", tm_ptr2);
                        printf(".%.9d %s", nsec2, buf);
                }
                printf("\n");
        }
}

static void
print_space(int nc, int n, int flags)
{
        int col, newcol, tabstop;

        col = nc;
        newcol = nc + n;
        if ((flags & D_EXPANDTABS) == 0) {
                while ((tabstop = roundup(col + 1, tabsize)) <= newcol) {
                        printf("\t");
                        col = tabstop;
                }
        }
        printf("%*s", newcol - col, "");
}
