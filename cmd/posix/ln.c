/* see LICENSE file for copyright and license details */

#include <sys/stat.h>

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"

#if FEATURE_LN_RELATIVE
#define MAXPATHPARTS (PATH_MAX / 2)

/* splits an absolute, realpath()-canonicalized path in place on '/',
 * returning pointers to each component; safe only against realpath()
 * output, which has no trailing or doubled slashes to worry about */
static int
split_path(char *path, char **parts, int max)
{
  char *p = path;
  int   n = 0;

  if (*p == '/')
    p++;
  while (*p && n < max) {
    parts[n++] = p;
    p = strchr(p, '/');
    if (!p)
      break;
    *p++ = '\0';
  }
  return n;
}

/* the text a relative symlink at from_dir/name needs to store to reach
 * to_path: the common prefix of both canonicalized paths, a ".." per
 * remaining from_dir component, then whatever is left of to_path */
static void
relpath(const char *from_dir, const char *to_path, char *out, size_t outsz)
{
  char  from_real[PATH_MAX], to_real[PATH_MAX], to_dir_buf[PATH_MAX], to_base_buf[PATH_MAX];
  char *from_parts[MAXPATHPARTS], *to_parts[MAXPATHPARTS];
  int   nfrom, nto, ncommon, i;

  if (!realpath(from_dir, from_real))
    eprintf("realpath %s:", from_dir);

  strlcpy(to_dir_buf, to_path, sizeof(to_dir_buf));
  strlcpy(to_base_buf, to_path, sizeof(to_base_buf));
  if (!realpath(dirname(to_dir_buf), to_real))
    eprintf("realpath %s:", to_path);

  nfrom = split_path(from_real, from_parts, MAXPATHPARTS);
  nto   = split_path(to_real, to_parts, MAXPATHPARTS);

  ncommon = 0;
  while (ncommon < nfrom && ncommon < nto &&
         strcmp(from_parts[ncommon], to_parts[ncommon]) == 0)
    ncommon++;

  out[0] = '\0';
  for (i = ncommon; i < nfrom; i++)
    strlcat(out, "../", outsz);
  for (i = ncommon; i < nto; i++) {
    strlcat(out, to_parts[i], outsz);
    strlcat(out, "/", outsz);
  }
  strlcat(out, basename(to_base_buf), outsz);
}
#endif

static void
usage(void)
{
#if FEATURE_LN_RELATIVE
#if FEATURE_LN_NO_TARGET_DIR
  eprintf(
      "usage: %s [-f] [-L | -P | -r | -s] [-T] target [name]\n"
      "       %s [-f] [-L | -P | -r | -s] target ... dir\n",
      argv0,
      argv0
  );
#else
  eprintf(
      "usage: %s [-f] [-L | -P | -r | -s] target [name]\n"
      "       %s [-f] [-L | -P | -r | -s] target ... dir\n",
      argv0,
      argv0
  );
#endif
#else
#if FEATURE_LN_NO_TARGET_DIR
  eprintf(
      "usage: %s [-f] [-L | -P | -s] [-T] target [name]\n"
      "       %s [-f] [-L | -P | -s] target ... dir\n",
      argv0,
      argv0
  );
#else
  eprintf(
      "usage: %s [-f] [-L | -P | -s] target [name]\n"
      "       %s [-f] [-L | -P | -s] target ... dir\n",
      argv0,
      argv0
  );
#endif
#endif
}

// ?man ln: make links between files
// ?man arguments: target [name]
// ?man create hard or symbolic links between files
int
main(int argc, char *argv[])
{
  char *targetdir = ".", *target = NULL;
  int   ret = 0, sflag = 0, fflag = 0, dirfd = AT_FDCWD, hastarget = 0, flags = AT_SYMLINK_FOLLOW;
  struct stat st, tst;
#if FEATURE_LN_RELATIVE
  int  rflag = 0;
  char relbuf[PATH_MAX];
#endif
#if FEATURE_LN_NO_TARGET_DIR
  int Tflag = 0;
#endif

  ARGBEGIN
  {
    // ?man -f: force creation of links by removing existing destination
    // files
    case 'f':
      fflag = 1;
      break;
    // ?man -L: specify option flag
    case 'L':
      flags |= AT_SYMLINK_FOLLOW;
      break;
    // ?man -P: specify option flag
    case 'P':
      flags &= ~AT_SYMLINK_FOLLOW;
      break;
#if FEATURE_LN_RELATIVE
    // ?man -r: with -s, store the target as a path relative to name's
    // own directory instead of copying it as given
    case 'r':
      rflag = 1;
      sflag = 1;
      break;
#endif
    // ?man -s: make symbolic links instead of hard links
    case 's':
      sflag = 1;
      break;
#if FEATURE_LN_NO_TARGET_DIR
    // ?man -T: treat name as a regular file, never as a directory to
    // place the link into, even if a file of that name is currently
    // a directory
    case 'T':
      Tflag = 1;
      break;
#endif
    default:
      usage();
  }
  ARGEND

  if (!argc)
    usage();

  if (argc > 1) {
#if FEATURE_LN_NO_TARGET_DIR
    if (Tflag && argc != 2)
      eprintf("%s: extra operand\n", argv[2]);
    if (!Tflag && !stat(argv[argc - 1], &st) && S_ISDIR(st.st_mode)) {
#else
    if (!stat(argv[argc - 1], &st) && S_ISDIR(st.st_mode)) {
#endif
      if ((dirfd = open(argv[argc - 1], O_RDONLY)) < 0)
        eprintf("open %s:", argv[argc - 1]);
      targetdir = argv[argc - 1];
      if (targetdir[strlen(targetdir) - 1] == '/')
        targetdir[strlen(targetdir) - 1] = '\0';
    } else if (argc == 2) {
      hastarget = 1;
      target    = argv[argc - 1];
    } else {
      eprintf("%s: not a directory\n", argv[argc - 1]);
    }
    argv[argc - 1] = NULL;
    argc--;
  }

  for (; *argv; argc--, argv++) {
    if (!hastarget)
      target = basename(*argv);

    if (!sflag) {
      if (stat(*argv, &st) < 0) {
        weprintf("stat %s:", *argv);
        ret = 1;
        continue;
      } else if (fstatat(dirfd, target, &tst, AT_SYMLINK_NOFOLLOW) < 0) {
        if (errno != ENOENT) {
          weprintf("fstatat %s %s:", targetdir, target);
          ret = 1;
          continue;
        }
      } else if (st.st_dev == tst.st_dev && st.st_ino == tst.st_ino) {
        if (!fflag) {
          weprintf(
              "%s and %s/%s are the same "
              "file\n",
              *argv,
              targetdir,
              target
          );
          ret = 1;
        }
        continue;
      }
    }

    if (fflag && unlinkat(dirfd, target, 0) < 0 && errno != ENOENT) {
      weprintf("unlinkat %s %s:", targetdir, target);
      ret = 1;
      continue;
    }

#if FEATURE_LN_RELATIVE
    if (rflag) {
      /* targetdir only names the real destination directory when name
       * was defaulted from target's own basename (the dir-argument
       * form); an explicit name may itself carry a directory prefix
       * (ln -r target dir/name), which then names it instead */
      char        dirbuf[PATH_MAX];
      const char *fromdir = targetdir;

      if (hastarget) {
        strlcpy(dirbuf, target, sizeof(dirbuf));
        fromdir = dirname(dirbuf);
      }
      relpath(fromdir, *argv, relbuf, sizeof(relbuf));
    }
#endif

    if ((sflag ?
#if FEATURE_LN_RELATIVE
                symlinkat(rflag ? relbuf : *argv, dirfd, target)
#else
                symlinkat(*argv, dirfd, target)
#endif
                : linkat(AT_FDCWD, *argv, dirfd, target, flags))
        < 0) {
      weprintf("%s %s <- %s/%s:", sflag ? "symlinkat" : "linkat", *argv, targetdir, target);
      ret = 1;
    }
  }

  return ret;
}
