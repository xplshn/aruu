/* see LICENSE file for copyright and license details */

#if FEATURE_LINUX_MOUNT
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#endif
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <errno.h>
#include <limits.h>
#include <mntent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "diskutil.h"
#include "paths.h"
#include "text.h"
#include "util.h"
#include "wexec.h"

#define FSOPTS_MAXLEN 512

struct {
  const char   *opt;
  const char   *notopt;
  unsigned long v;
} optnames[] = {
    {"defaults", NULL, 0},
    {"remount", NULL, MS_REMOUNT},
    {"ro", "rw", MS_RDONLY},
    {"sync", "async", MS_SYNCHRONOUS},
    {"dirsync", NULL, MS_DIRSYNC},
    {"nodev", "dev", MS_NODEV},
    {"noatime", "atime", MS_NOATIME},
    {"noauto", "auto", 0},
    {"nofail", NULL, 0},
    {"nodiratime", "diratime", MS_NODIRATIME},
    {"noexec", "exec", MS_NOEXEC},
    {"nosuid", "suid", MS_NOSUID},
    {"mand", "nomand", MS_MANDLOCK},
    {"relatime", "norelatime", MS_RELATIME},
    {"bind", NULL, MS_BIND},
    {NULL, NULL, 0}
};

static unsigned long argflags              = 0;
static char          fsopts[FSOPTS_MAXLEN] = "";
static int           nofail_opt            = 0;

static char *
findtype(const char *types, const char *t)
{
  const char *p;
  size_t      len;

  for (len = strlen(t); (p = strstr(types, t)); types = p + len) {
    if (!strncmp(p, t, len) && (p[len] == '\0' || p[len] == ','))
      return (char *)p;
  }
  return NULL;
}

static void
parseopts(const char *popts, unsigned long *flags, char *data, size_t datasiz)
{
  unsigned int i, validopt;
  size_t       optlen, dlen = 0;
  const char  *name, *e;

  name    = popts;
  data[0] = '\0';
  do {
    if ((e = strstr(name, ",")))
      optlen = e - name;
    else
      optlen = strlen(name);

    validopt = 0;
    if (optlen == 6 && !strncmp(name, "nofail", 6)) {
      nofail_opt = 1;
      validopt   = 1;
    }
    for (i = 0; !validopt && optnames[i].opt; i++) {
      if (optnames[i].opt && !strncmp(name, optnames[i].opt, optlen)) {
        *flags |= optnames[i].v;
        validopt = 1;
        break;
      }
      if (optnames[i].notopt && !strncmp(name, optnames[i].notopt, optlen)) {
        *flags &= ~optnames[i].v;
        validopt = 1;
        break;
      }
    }

    if (!validopt && optlen > 0) {
      /* unknown option, pass as data option to mount() */
      if (dlen + optlen + 2 >= datasiz)
        return; /* prevent overflow */
      if (dlen)
        data[dlen++] = ',';
      memcpy(&data[dlen], name, optlen);
      dlen += optlen;
      data[dlen] = '\0';
    }
    name = e + 1;
  } while (e);
}

static int
mounthelper(const char *fsname, const char *dir, const char *fstype)
{
  pid_t       pid;
  char        eprog[PATH_MAX];
  char const *eargv[10];
  char       *found;
  int         status, i;

  if (!fstype || strcmp(fstype, "auto") == 0)
    return 1;

  snprintf(eprog, sizeof(eprog), "mount.%s", fstype);
  if (strcmp(argv0, eprog) == 0)
    return 1;

  /* no helper for this type: mount(2) does it, and nothing is printed */
  if (!wexec_is_builtin(eprog)) {
    if (!(found = wwhich(eprog)))
      return 1;
    free(found);
  }

  pid = fork();
  switch (pid) {
    case -1:
      break;
    case 0:
      i          = 0;
      eargv[i++] = eprog;
      if (argflags & MS_BIND)
        eargv[i++] = "-B";
      if (argflags & MS_MOVE)
        eargv[i++] = "-M";
      if (argflags & MS_REC)
        eargv[i++] = "-R";

      if (fsopts[0]) {
        eargv[i++] = "-o";
        eargv[i++] = fsopts;
      }
      eargv[i++] = fsname;
      eargv[i++] = dir;
      eargv[i]   = NULL;

      wexecvp_self(eprog, (char *const *)eargv);
    default:
      if (waitpid(pid, &status, 0) < 0) {
        weprintf("waitpid:");
        return -1;
      }
      if (WIFEXITED(status))
        return WEXITSTATUS(status);
      else if (WIFSIGNALED(status))
        return 1;
      break;
  }
  return 0;
}

static const char *
resolve_source(const char *src, char *buf, size_t bufsz)
{
  struct BlockDevInfo *list, *cur;
  struct BlockDev      dev;
  char                 dev_type[32], dev_label[128], dev_uuid[128];
  const char          *target_val;
  int                  is_label;

  if (!src)
    return NULL;

  if (strncmp(src, "LABEL=", 6) == 0) {
    target_val = src + 6;
    is_label   = 1;
  } else if (strncmp(src, "UUID=", 5) == 0) {
    target_val = src + 5;
    is_label   = 0;
  } else {
    return src;
  }

  list = blockdev_get_list();
  if (!list)
    return NULL;

  for (cur = list; cur; cur = cur->next) {
    if (blockdev_open(&dev, cur->path, 0) == 0) {
      if (blockdev_detect_fs(&dev, dev_type, sizeof(dev_type), dev_label,
                             sizeof(dev_label), dev_uuid, sizeof(dev_uuid)) == 0) {
        if (is_label && strcmp(dev_label, target_val) == 0) {
          blockdev_close(&dev);
          estrlcpy(buf, cur->path, bufsz);
          blockdev_free_list(list);
          return buf;
        }
        if (!is_label && strcasecmp(dev_uuid, target_val) == 0) {
          blockdev_close(&dev);
          estrlcpy(buf, cur->path, bufsz);
          blockdev_free_list(list);
          return buf;
        }
      }
      blockdev_close(&dev);
    }
  }
  blockdev_free_list(list);
  return NULL;
}

static const char *
detect_fstype(const char *devpath, char *buf, size_t bufsz)
{
  struct BlockDev dev;
  char            label[128], uuid[128];

  if (!devpath)
    return NULL;
  if (blockdev_open(&dev, devpath, 0) == 0) {
    if (blockdev_detect_fs(&dev, buf, bufsz, label, sizeof(label), uuid, sizeof(uuid)) == 0) {
      blockdev_close(&dev);
      return buf;
    }
    blockdev_close(&dev);
  }
  return NULL;
}

static int
mounted(const char *dir)
{
  FILE          *fp;
  struct mntent *me, mebuf;
  struct stat    st1, st2;
  char           linebuf[256];

  if (stat(dir, &st1) < 0) {
    weprintf("stat %s:", dir);
    return 0;
  }
  if (!(fp = setmntent(ARUU_LINUX_PATH_PROC_MOUNTS, "r")))
    eprintf("setmntent %s:", ARUU_LINUX_PATH_PROC_MOUNTS);

  while ((me = getmntent_r(fp, &mebuf, linebuf, sizeof(linebuf)))) {
    if (stat(me->mnt_dir, &st2) < 0) {
      weprintf("stat %s:", me->mnt_dir);
      continue;
    }
    if (st1.st_dev == st2.st_dev && st1.st_ino == st2.st_ino)
      return 1;
  }
  endmntent(fp);

  return 0;
}

static void
usage(void)
{
  eprintf(
      "usage: %s [-BMRan] [-t fstype] [-o options] [source] "
      "[target]\n",
      argv0
  );
}

// ?man mount: mount a filesystem
// ?man arguments: source [target]
// ?man mount a filesystem to the directory tree
int
main(int argc, char *argv[])
{
  char          *types = NULL, data[FSOPTS_MAXLEN] = "", *resolvpath = NULL;
  char          *files[] = {ARUU_LINUX_PATH_PROC_MOUNTS, "/etc/fstab", NULL};
  const char    *source, *target;
  struct mntent *me    = NULL;
  int            aflag = 0, status = 0, i, r;
  unsigned long  flags = 0;
  FILE          *fp;

  ARGBEGIN
  {
    // ?man -B: specify option flag
    case 'B':
      argflags |= MS_BIND;
      break;
    // ?man -M: specify option flag
    case 'M':
      argflags |= MS_MOVE;
      break;
    // ?man -R: operate recursively on directories
    case 'R':
      argflags |= MS_REC;
      break;
    // ?man -a: print or show all entries
    case 'a':
      aflag = 1;
      break;
    // ?man -o:str: specify output file
    case 'o':
      estrlcat(fsopts, EARGF(usage()), sizeof(fsopts));
      parseopts(fsopts, &flags, data, sizeof(data));
      break;
    // ?man -t:str: sort or specify timestamp
    case 't':
      types = EARGF(usage());
      break;
    // ?man -n: print line numbers or counts
    case 'n':
      break;
    default:
      usage();
  }
  ARGEND;

  if (!types && strncmp(argv0, "mount.", 6) == 0)
    types = argv0 + 6;

  if (argc < 1 && aflag == 0) {
    if (!(fp = fopen(files[0], "r")))
      eprintf("fopen %s:", files[0]);
    fconcat(fp, files[0], stdout, "<stdout>");
    fclose(fp);
    return 0;
  }

  if (aflag == 1)
    goto mountall;

  source = argv[0];
  target = argv[1];

  if (!target) {
    target = argv[0];
    source = NULL;
    if (strcmp(target, "/") != 0) {
      if (!(resolvpath = realpath(target, NULL)))
        eprintf("realpath %s:", target);
      target = resolvpath;
    }
  }

  for (i = 0; files[i]; i++) {
    if (!(fp = setmntent(files[i], "r"))) {
      if (strcmp(files[i], ARUU_LINUX_PATH_PROC_MOUNTS) != 0)
        weprintf("setmntent %s:", files[i]);
      continue;
    }
    while ((me = getmntent(fp))) {
      if (strcmp(me->mnt_dir, target) == 0 || strcmp(me->mnt_fsname, target) == 0
          || (source && strcmp(me->mnt_dir, source) == 0)
          || (source && strcmp(me->mnt_fsname, source) == 0)) {
        if (!source) {
          target = me->mnt_dir;
          source = me->mnt_fsname;
        }
        if (!fsopts[0]) {
          estrlcat(fsopts, me->mnt_opts, sizeof(fsopts));
          parseopts(fsopts, &flags, data, sizeof(data));
        }
        if (!types)
          types = me->mnt_type;
        goto mountsingle;
      }
    }
    endmntent(fp);
    fp = NULL;
  }
  if (!source)
    eprintf("can't find %s in /etc/fstab\n", target);

mountsingle:
  {
    char        resbuf[PATH_MAX];
    char        typebuf[32];
    const char *realsrc;

    realsrc = resolve_source(source, resbuf, sizeof(resbuf));
    if (!realsrc) {
      if (nofail_opt) {
        if (fp)
          endmntent(fp);
        free(resolvpath);
        return 0;
      }
      eprintf("mount: unable to resolve %s\n", source);
    }
    source = realsrc;

    if (!types || strcmp(types, "auto") == 0) {
      const char *det = detect_fstype(source, typebuf, sizeof(typebuf));
      if (det)
        types = (char *)det;
    }

    r = mounthelper(source, target, types);
    if (r == -1)
      status = 1;
    if (r > 0 && mount(source, target, types, argflags | flags, data) < 0) {
      if (!nofail_opt) {
        weprintf("mount: %s:", source);
        status = 1;
      }
    }
    if (fp)
      endmntent(fp);
    free(resolvpath);
    return status;
  }

mountall:
  if (!(fp = setmntent("/etc/fstab", "r")))
    eprintf("setmntent %s:", "/etc/fstab");
  while ((me = getmntent(fp))) {
    char        resbuf[PATH_MAX];
    char        typebuf[32];
    const char *realsrc;
    const char *mtype = me->mnt_type;

    /* has "noauto" option or already mounted: skip */
    if (hasmntopt(me, MNTOPT_NOAUTO) || mounted(me->mnt_dir))
      continue;
    flags      = 0;
    fsopts[0]  = '\0';
    nofail_opt = 0;
    if (strlcat(fsopts, me->mnt_opts, sizeof(fsopts)) >= sizeof(fsopts)) {
      weprintf("%s: option string too long\n", me->mnt_dir);
      status = 1;
      continue;
    }
    parseopts(fsopts, &flags, data, sizeof(data));
    /* if -t types specified:
 * if non-match, skip
 * if match and prefixed with "no", skip */
    if (types
        && ((types[0] == 'n' && types[1] == 'o' && findtype(types + 2, me->mnt_type))
            || (!findtype(types, me->mnt_type))))
      continue;

    realsrc = resolve_source(me->mnt_fsname, resbuf, sizeof(resbuf));
    if (!realsrc) {
      if (nofail_opt)
        continue;
      weprintf("mount: unable to resolve %s\n", me->mnt_fsname);
      status = 1;
      continue;
    }

    if (!mtype || strcmp(mtype, "auto") == 0) {
      const char *det = detect_fstype(realsrc, typebuf, sizeof(typebuf));
      if (det)
        mtype = det;
    }

    r = mounthelper(realsrc, me->mnt_dir, mtype);
    if (r > 0 && mount(realsrc, me->mnt_dir, mtype, argflags | flags, data) < 0) {
      if (!nofail_opt) {
        weprintf("mount: %s:", realsrc);
        status = 1;
      }
    }
  }
  endmntent(fp);

  return status;
}
