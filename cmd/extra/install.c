/* see LICENSE file for copyright and license details */
#include <sys/stat.h>
#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <libgen.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fs.h"
#include "util.h"
#include "wexec.h"

static int    dflag, Dflag, pflag, sflag;
static mode_t mode = 0755;
static uid_t  uid  = (uid_t)-1;
static gid_t  gid  = (gid_t)-1;
static int    status;

static void
usage(void)
{
  eprintf(
      "usage: %s [-Dps] [-o owner] [-g group] [-m mode] [-t dir] source ... dest\n"
      "       %s -d [-Dp] [-o owner] [-g group] [-m mode] dir ...\n",
      argv0,
      argv0
  );
}

static void
lookup_owner(const char *owner, const char *group)
{
  struct passwd *pw;
  struct group  *gr;

  if (owner) {
    errno = 0;
    pw    = getpwnam(owner);
    if (pw) {
      uid = pw->pw_uid;
    } else {
      if (errno)
        eprintf("getpwnam %s:", owner);
      uid = (uid_t)estrtonum(owner, 0, UINT_MAX);
    }
  }
  if (group) {
    errno = 0;
    gr    = getgrnam(group);
    if (gr) {
      gid = gr->gr_gid;
    } else {
      if (errno)
        eprintf("getgrnam %s:", group);
      gid = (gid_t)estrtonum(group, 0, UINT_MAX);
    }
  }
}

static void
chown_if_requested(const char *path)
{
  if (uid == (uid_t)-1 && gid == (gid_t)-1)
    return;
  if (chown(path, uid, gid) < 0) {
    weprintf("chown %s:", path);
    status = 1;
  }
}

static void
preserve_time(const char *src, const char *dst)
{
  struct stat     st;
  struct timespec times[2];

  if (stat(src, &st) < 0) {
    weprintf("stat %s:", src);
    status = 1;
    return;
  }
  times[0] = st.st_atim;
  times[1] = st.st_mtim;
  if (utimensat(AT_FDCWD, dst, times, 0) < 0) {
    weprintf("utimensat %s:", dst);
    status = 1;
  }
}

static void
strip_binary(const char *path)
{
  char *args[] = {"strip", "-p", (char *)path, NULL};

  if (wexecvp("strip", args) != 0) {
    weprintf("strip %s:", path);
    status = 1;
  }
}

// leading path components of dst, not dst itself
static void
make_leading_dirs(const char *dst)
{
  char  buf[PATH_MAX];
  char *dir;

  estrlcpy(buf, dst, sizeof(buf));
  dir = dirname(buf);
  if (mkdirp(dir, 0755, 0755) < 0 && errno != EEXIST) {
    weprintf("mkdir %s:", dir);
    status = 1;
  }
}

static void
install_dirs(char **argv)
{
  for (; *argv; argv++) {
    if (mkdirp(*argv, mode, 0755) < 0 && errno != EEXIST) {
      weprintf("mkdir %s:", *argv);
      status = 1;
      continue;
    }
    if (chmod(*argv, mode) < 0) {
      weprintf("chmod %s:", *argv);
      status = 1;
    }
    chown_if_requested(*argv);
  }
}

static void
install_one(const char *src, const char *dst)
{
  if (Dflag)
    make_leading_dirs(dst);

  cp_fflag  = 1;
  cp_pflag  = 0;
  cp_rflag  = 0;
  cp_follow = 'L';
  cp(src, dst, 0);
  if (cp_status) {
    status = 1;
    return;
  }

  if (sflag)
    strip_binary(dst);

  if (chmod(dst, mode) < 0) {
    weprintf("chmod %s:", dst);
    status = 1;
  }
  chown_if_requested(dst);
  if (pflag)
    preserve_time(src, dst);
}

// ?man install: copy files and set attributes
// ?man arguments: source ... dest
// ?man install copies each source to dest, or into dest when dest is a
// ?man directory or -t is given, then applies mode/owner/group; with
// ?man -d, every argument is a directory to create instead
int
main(int argc, char *argv[])
{
  const char *owner = NULL, *group = NULL, *modestr = NULL, *targetdir = NULL;
  const char *dest;
  struct stat st;
  int         isdir;
  int         i;

  ARGBEGIN
  {
    // ?man -c: ignored; accepted for compatibility with older install callers
    case 'c':
      break;
    // ?man -d: create directories instead of installing files
    case 'd':
      dflag = 1;
      break;
    // ?man -D: create dest's leading directories before installing
    case 'D':
      Dflag = 1;
      break;
    // ?man -p: preserve source modification and access times
    case 'p':
      pflag = 1;
      break;
    // ?man -s: strip the installed file's symbol table
    case 's':
      sflag = 1;
      break;
    // ?man -o:owner set the installed file's owner (name or uid)
    case 'o':
      owner = EARGF(usage());
      break;
    // ?man -g:group set the installed file's group (name or gid)
    case 'g':
      group = EARGF(usage());
      break;
    // ?man -m:mode set the installed file's permissions
    case 'm':
      modestr = EARGF(usage());
      break;
    // ?man -t:dir install every source into dir
    case 't':
      targetdir = EARGF(usage());
      break;
    default:
      usage();
  }
  ARGEND;

  if (argc < 1 || (dflag && (targetdir || sflag)))
    usage();
  if (modestr)
    mode = parsemode(modestr, 0, 0);
  lookup_owner(owner, group);

  if (dflag) {
    install_dirs(argv);
    goto shut;
  }

  if (targetdir) {
    isdir = 1;
    dest  = targetdir;
    if (Dflag && mkdirp(targetdir, 0755, 0755) < 0 && errno != EEXIST) {
      weprintf("mkdir %s:", targetdir);
      status = 1;
    }
  } else {
    if (argc < 2)
      usage();
    dest  = argv[--argc];
    isdir = stat(dest, &st) == 0 && S_ISDIR(st.st_mode);
    if (!isdir && argc > 1)
      eprintf("%s: not a directory\n", dest);
  }

  for (i = 0; i < argc; i++) {
    if (isdir) {
      char        buf[PATH_MAX], nb[PATH_MAX];
      const char *base;

      estrlcpy(nb, argv[i], sizeof(nb));
      base = basename(nb);
      estrlcpy(buf, dest, sizeof(buf));
      estrlcat(buf, "/", sizeof(buf));
      estrlcat(buf, base, sizeof(buf));
      install_one(argv[i], buf);
    } else {
      install_one(argv[i], dest);
    }
  }

shut:
  return fshut(stdout, "<stdout>") || status;
}
