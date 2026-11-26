/* see LICENSE file for copyright and license details */

#define _DEFAULT_SOURCE /* clearenv, putenv */

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>

#include <linux/netlink.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <regex.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "paths.h"
#include "util.h"

#define CONF      "/etc/mdev.conf"
#define EVENT_MAX 8192
#define ENV_MAX   64
#define RCVBUF    (4 << 20) /* the burst of a dock or a hub, or of the replay at start */

/* a line of mdev.conf:
 *
 *   [-][$VAR=]regex  user:group mode [=path | >path | !] [@cmd | $cmd | *cmd]
 *   [-]@maj,min[-min2] ...
 *
 * the regex is matched against the device's name under /dev, or with $VAR=
 * against the event's VAR. the first line that matches is taken, a line
 * starting with - is taken and the matching goes on */
struct rule {
  int     cont;
  char    env[64]; /* "" for the device's name */
  int     bymaj, maj, min0, min1;
  regex_t re;
  uid_t   uid;
  gid_t   gid;
  mode_t  mode;
  char    place;   /* 0, '=' to move the node, '>' to move it and link it back, '!' for none */
  char    path[PATH_MAX];
  char    when;    /* 0, '@' after it is added, '$' before it goes, '*' both */
  char    cmd[512];
};

/* an event: the kernel's KEY=value fields */
struct event {
  char *kv[ENV_MAX];
  int   n;
};

static struct rule *rules;
static int          nrules;
static int          vflag;

static void
usage(void)
{
  eprintf("usage: %s [-s | -d [-f]] [-v]\n", argv0);
}

static int
owner(const char *spec, uid_t *uid, gid_t *gid)
{
  char           u[64], g[64], *end;
  struct passwd *pw;
  struct group  *gr;

  if (sscanf(spec, "%63[^:]:%63s", u, g) != 2)
    return -1;
  *uid = (uid_t)strtoul(u, &end, 10);
  if (*end) {
    if (!(pw = getpwnam(u)))
      return -1;
    *uid = pw->pw_uid;
  }
  *gid = (gid_t)strtoul(g, &end, 10);
  if (*end) {
    if (!(gr = getgrnam(g)))
      return -1;
    *gid = gr->gr_gid;
  }
  return 0;
}

static void
load(const char *path)
{
  FILE *f;
  char  line[1024];
  int   n = 0;

  if (!(f = fopen(path, "r"))) {
    if (errno != ENOENT)
      weprintf("%s:", path);
    return;
  }
  while (fgets(line, sizeof line, f)) {
    struct rule r;
    char        pat[256], own[128], mode[16], *p = line, *rest, re[300];
    int         used = 0;

    n++;
    line[strcspn(line, "\n")] = '\0';
    p += strspn(p, " \t");
    if (*p == '#' || *p == '\0')
      continue;
    memset(&r, 0, sizeof r);
    if (*p == '-') {
      r.cont = 1;
      p++;
    }
    if (sscanf(p, "%255s %127s %15s %n", pat, own, mode, &used) < 3) {
      weprintf("%s:%d: not \"device user:group mode\"\n", path, n);
      continue;
    }
    rest = p + used;
    if (owner(own, &r.uid, &r.gid) < 0) {
      weprintf("%s:%d: no such user or group: %s\n", path, n, own);
      continue;
    }
    r.mode = (mode_t)strtoul(mode, NULL, 8) & 07777;
    if (pat[0] == '@') {
      r.bymaj = 1;
      r.min1  = -1;
      if (sscanf(pat + 1, "%d,%d-%d", &r.maj, &r.min0, &r.min1) < 2) {
        weprintf("%s:%d: not @major,minor[-minor]\n", path, n);
        continue;
      }
      if (r.min1 < 0)
        r.min1 = r.min0;
    } else {
      const char *rx = pat;
      char       *eq;

      if (pat[0] == '$' && (eq = strchr(pat, '='))) {
        *eq = '\0';
        estrlcpy(r.env, pat + 1, sizeof r.env);
        rx = eq + 1;
      }
      /* the whole name, as mdev has it */
      snprintf(re, sizeof re, "^(%s)$", rx);
      if (regcomp(&r.re, re, REG_EXTENDED | REG_NOSUB) != 0) {
        weprintf("%s:%d: bad expression %s\n", path, n, rx);
        continue;
      }
    }
    rest += strspn(rest, " \t");
    if (*rest == '=' || *rest == '>' || *rest == '!') {
      r.place = *rest++;
      if (r.place != '!') {
        int k = (int)strcspn(rest, " \t");

        snprintf(r.path, sizeof r.path, "%.*s", k, rest);
        rest += k;
      }
      rest += strspn(rest, " \t");
    }
    if (*rest == '@' || *rest == '$' || *rest == '*') {
      r.when = *rest++;
      estrlcpy(r.cmd, rest, sizeof r.cmd);
    }
    rules           = ereallocarray(rules, (size_t)nrules + 1, sizeof *rules);
    rules[nrules++] = r;
  }
  fclose(f);
}

static const char *
get(const struct event *e, const char *key)
{
  size_t k = strlen(key);
  int    i;

  for (i = 0; i < e->n; i++)
    if (strncmp(e->kv[i], key, k) == 0 && e->kv[i][k] == '=')
      return e->kv[i] + k + 1;
  return NULL;
}

static int
matches(const struct rule *r, const struct event *e, const char *name)
{
  const char *v;

  if (r->bymaj) {
    const char *ma = get(e, "MAJOR"), *mi = get(e, "MINOR");
    int         m;

    if (!ma || !mi || atoi(ma) != r->maj)
      return 0;
    m = atoi(mi);
    return m >= r->min0 && m <= r->min1;
  }
  v = r->env[0] ? get(e, r->env) : name;
  return v && regexec(&r->re, v, 0, NULL, 0) == 0;
}

/* a directory for path, as mkdir -p */
static void
parents(const char *path)
{
  char  d[PATH_MAX], *s;

  estrlcpy(d, path, sizeof d);
  for (s = d + 1; (s = strchr(s, '/')); s++) {
    *s = '\0';
    mkdir(d, 0755);
    *s = '/';
  }
}

static void
command(const struct rule *r, const struct event *e, const char *name)
{
  pid_t pid;
  int   i;

  if ((pid = fork()) < 0) {
    weprintf("fork:");
    return;
  }
  if (pid == 0) {
    signal(SIGCHLD, SIG_DFL);
    clearenv();
    setenv("PATH", "/bin", 1);
    for (i = 0; i < e->n; i++)
      putenv(e->kv[i]);
    if (name)
      setenv("MDEV", name, 1);
    execl("/bin/sh", "sh", "-c", r->cmd, (char *)NULL);
    _exit(127);
  }
  while (waitpid(pid, NULL, 0) < 0 && errno == EINTR)
    ;
}

/* the node the kernel made, given its owner and mode, and put where the
 * line says */
static void
node(const struct rule *r, const char *name, int adding)
{
  char dev[PATH_MAX], to[PATH_MAX];

  snprintf(dev, sizeof dev, "/dev/%s", name);
  if (!r->place || r->path[0] == '\0') {
    if (r->place == '!' && adding)
      unlink(dev);
  } else {
    /* a path ending in / keeps the name under it */
    if (r->path[strlen(r->path) - 1] == '/')
      snprintf(to, sizeof to, "/dev/%s%s", r->path, strrchr(name, '/') ? strrchr(name, '/') + 1 : name);
    else
      snprintf(to, sizeof to, "/dev/%s", r->path);
    if (adding) {
      parents(to);
      if (rename(dev, to) == 0 && r->place == '>')
        symlink(to + 5, dev);
      estrlcpy(dev, to, sizeof dev);
    } else {
      unlink(to);
      if (r->place == '>')
        unlink(dev);
      return;
    }
  }
  if (!adding || r->place == '!')
    return;
  if (chown(dev, r->uid, r->gid) < 0 || chmod(dev, r->mode) < 0) {
    if (errno != ENOENT)
      weprintf("%s:", dev);
    return;
  }
  if (vflag)
    printf("%s %u:%u %04o\n", dev, (unsigned)r->uid, (unsigned)r->gid, (unsigned)r->mode);
}

static void
handle(const struct event *e)
{
  const char *action = get(e, "ACTION"), *name = get(e, "DEVNAME");
  int         adding, removing, i;

  if (!action)
    return;
  adding   = !strcmp(action, "add") || !strcmp(action, "change");
  removing = !strcmp(action, "remove");
  for (i = 0; i < nrules; i++) {
    const struct rule *r = &rules[i];

    if (!matches(r, e, name))
      continue;
    if (name)
      node(r, name, adding);
    if ((r->when == '@' && !strcmp(action, "add")) || (r->when == '$' && removing) ||
        (r->when == '*' && (!strcmp(action, "add") || removing)))
      command(r, e, name);
    if (!r->cont)
      break;
  }
}

/* the fields of a netlink message: "action@devpath" then KEY=value, each
 * ended by a NUL */
static void
from_buffer(struct event *e, char *buf, size_t n)
{
  char *p;

  e->n = 0;
  for (p = buf; p < buf + n && e->n < ENV_MAX; p += strlen(p) + 1)
    if (strchr(p, '=') && !strchr(p, '@'))
      e->kv[e->n++] = p;
}

/* every device of the sysfs tree asked to say again that it was added, so
 * what was there before mdev listened comes through the same way: drivers,
 * nodes, lines with commands */
static void
replay(const char *dir)
{
  DIR           *d;
  struct dirent *ent;
  char           path[PATH_MAX];
  int            fd;

  snprintf(path, sizeof path, "%s/uevent", dir);
  if ((fd = open(path, O_WRONLY | O_CLOEXEC)) >= 0) {
    if (write(fd, "add", 3) < 0) {
      /* a device that cannot be replayed is left as it is */
    }
    close(fd);
  }
  if (!(d = opendir(dir)))
    return;
  while ((ent = readdir(d))) {
    struct stat st;

    if (ent->d_name[0] == '.')
      continue;
    snprintf(path, sizeof path, "%s/%s", dir, ent->d_name);
    /* the tree's links lead back into it */
    if (lstat(path, &st) == 0 && S_ISDIR(st.st_mode))
      replay(path);
  }
  closedir(d);
}

/* -s: the nodes there now, from /sys/dev, each as if it had just been added */
static void
scan(const char *dir)
{
  DIR           *d;
  struct dirent *ent;

  if (!(d = opendir(dir)))
    return;
  while ((ent = readdir(d))) {
    char         path[PATH_MAX], buf[EVENT_MAX], link[PATH_MAX], sub[80];
    struct event e;
    ssize_t      n, k;
    int          fd;
    char        *p;

    if (ent->d_name[0] == '.')
      continue;
    snprintf(path, sizeof path, "%s/%s/uevent", dir, ent->d_name);
    if ((fd = open(path, O_RDONLY | O_CLOEXEC)) < 0)
      continue;
    n = read(fd, buf, sizeof buf - 64);
    close(fd);
    if (n <= 0)
      continue;
    buf[n] = '\0';
    for (p = buf; (p = strchr(p, '\n')); p++)
      *p = '\0';
    /* what the kernel would have said besides: the action and the subsystem */
    memcpy(buf + n + 1, "ACTION=add", 11);
    from_buffer(&e, buf, (size_t)n + 12);
    snprintf(path, sizeof path, "%s/%s/subsystem", dir, ent->d_name);
    if ((k = readlink(path, link, sizeof link - 1)) > 0 && e.n < ENV_MAX) {
      link[k] = '\0';
      snprintf(sub, sizeof sub, "SUBSYSTEM=%s", strrchr(link, '/') ? strrchr(link, '/') + 1 : link);
      e.kv[e.n++] = sub;
    }
    handle(&e);
  }
  closedir(d);
}

static int
listen_kernel(void)
{
  struct sockaddr_nl sa;
  int                fd, size = RCVBUF;

  if ((fd = socket(AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC, NETLINK_KOBJECT_UEVENT)) < 0)
    eprintf("socket:");
  memset(&sa, 0, sizeof sa);
  sa.nl_family = AF_NETLINK;
  sa.nl_groups = 1;
  if (bind(fd, (struct sockaddr *)&sa, sizeof sa) < 0)
    eprintf("bind:");
  if (setsockopt(fd, SOL_SOCKET, SO_RCVBUFFORCE, &size, sizeof size) < 0)
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof size);
  return fd;
}

// ?man mdev: gives the kernel's devices their owner, mode and place, and runs what mdev.conf says for them
// ?man arguments: [-s | -d [-f]] [-v]
// ?man mdev reads /etc/mdev.conf, BusyBox mdev's table, a line each:
// ?man "[-][$VAR=]regex user:group mode [=path|>path|!] [@cmd|$cmd|*cmd]", or
// ?man "@major,minor[-minor] ..." for a device by its numbers. the regex is
// ?man matched against the device's name under /dev, or with $VAR= against the
// ?man event's VAR (SUBSYSTEM, MODALIAS, DEVPATH...). the first line that
// ?man matches is taken, and one starting with - is taken and the matching goes
// ?man on. =path moves the node there, >path moves it and leaves a link, !
// ?man takes it away. @cmd runs once the device is added, $cmd before it goes,
// ?man *cmd both, through sh with the event's fields and MDEV, the device's
// ?man name, in the environment. the kernel's devtmpfs makes and removes the
// ?man nodes, mdev gives them what the table says. run with no option, it is a
// ?man hotplug helper and takes the one event in its environment
int
main(int argc, char *argv[])
{
  int  sflag = 0, dflag = 0;
  char buf[EVENT_MAX + 1];

  ARGBEGIN
  {
    // ?man -s: set every node there is now, and stop
    case 's':
      sflag = 1;
      break;
    // ?man -d: listen to the kernel's events and stay, having had every device
    // ?man there already say again that it was added
    case 'd':
      dflag = 1;
      break;
    // ?man -f: in the foreground, which mdev always is: for BusyBox's command lines
    case 'f':
      break;
    // ?man -v: say each node it sets
    case 'v':
      vflag = 1;
      break;
    default:
      usage();
  }
  ARGEND;
  if (argc || (sflag && dflag))
    usage();

  load(CONF);
  signal(SIGCHLD, SIG_DFL);

  if (sflag) {
    scan(ARUU_LINUX_PATH_SYS "/dev/char");
    scan(ARUU_LINUX_PATH_SYS "/dev/block");
    return 0;
  }

  if (!dflag) {
    extern char **environ;
    struct event  e;
    char        **p;

    e.n = 0;
    for (p = environ; *p && e.n < ENV_MAX; p++)
      e.kv[e.n++] = *p;
    handle(&e);
    return 0;
  }

  {
    int fd = listen_kernel();

    /* listening first: nothing added between the replay and the loop is lost */
    replay(ARUU_LINUX_PATH_SYS "/devices");
    for (;;) {
      struct sockaddr_nl from;
      struct iovec       iov = {buf, EVENT_MAX};
      struct msghdr      msg;
      struct event       e;
      ssize_t            n;

      memset(&msg, 0, sizeof msg);
      msg.msg_name    = &from;
      msg.msg_namelen = sizeof from;
      msg.msg_iov     = &iov;
      msg.msg_iovlen  = 1;
      if ((n = recvmsg(fd, &msg, 0)) < 0) {
        /* events were lost in a burst: the nodes are set over again */
        if (errno == ENOBUFS) {
          weprintf("events were lost, setting the nodes again\n");
          scan(ARUU_LINUX_PATH_SYS "/dev/char");
          scan(ARUU_LINUX_PATH_SYS "/dev/block");
          continue;
        }
        if (errno == EINTR)
          continue;
        eprintf("recv:");
      }
      /* the kernel's own, port 0: udev's rebroadcasts and anyone else's are not believed */
      if (from.nl_pid != 0 || n == 0 || !memchr(buf, '@', (size_t)n < 64 ? (size_t)n : 64))
        continue;
      buf[n] = '\0';
      from_buffer(&e, buf, (size_t)n);
      handle(&e);
    }
  }
}
