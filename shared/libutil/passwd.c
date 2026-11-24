/* see LICENSE file for copyright and license details */
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700 /* crypt in unistd.h */
#endif
#if FEATURE_PASSWD_EXT
#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* musl declares fgetpwent/putpwent only under it */
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#endif
#include "../passwd.h"
#include "../paths.h"
#include "../text.h"
#include "../util.h"

#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../config.h"

int
pw_init(void)
{
  struct rlimit rlim;

  rlim.rlim_cur = 0;
  rlim.rlim_max = 0;
  if (setrlimit(RLIMIT_CORE, &rlim) < 0)
    eprintf("setrlimit:");
  return 0;
}

int
pw_check(const struct passwd *pw, const char *pass)
{
  char       *cryptpass, *p;
  const char *stored;

  stored = pw->pw_passwd;
  if (stored[0] == '!' || stored[0] == '*') {
    weprintf("denied\n");
    return -1;
  }

  if (stored[0] == '\0') {
    if (pass[0] == '\0')
      return 1;
    weprintf("incorrect password\n");
    return 0;
  }

  if (stored[0] == 'x' && stored[1] == '\0') {
    errno  = 0;
    stored = pw_shadow_hash(pw->pw_name);
    if (!stored) {
      if (errno)
        weprintf("getspnam: %s:", pw->pw_name);
      else
        weprintf("who are you?\n");
      return -1;
    }
    if (stored[0] == '!' || stored[0] == '*') {
      weprintf("denied\n");
      return -1;
    }
    /* same empty-hash path as above, redone because stored was just
     * reassigned from the shadow entry. an empty shadow hash means a
     * passwordless account, while crypt() with an empty salt would match
     * nothing */
    if (stored[0] == '\0') {
      if (pass[0] == '\0')
        return 1;
      weprintf("incorrect password\n");
      return 0;
    }
  }

  cryptpass = crypt(pass, stored);
  if (!cryptpass) {
    weprintf("crypt:");
    return -1;
  }
  p = estrdup(cryptpass);
  if (strcmp(p, stored) != 0) {
    free(p);
    weprintf("incorrect password\n");
    return 0;
  }
  free(p);
  return 1;
}

int
pwdb_lookup(struct pwdb_entry *ent, const char *name)
{
  struct passwd *pw;
  const char    *hash;

  errno = 0;
  pw    = getpwnam(name);
  if (!pw) {
    if (errno)
      weprintf("getpwnam: %s:", name);
    else
      weprintf("who are you?\n");
    return -1;
  }

  ent->name = estrdup(pw->pw_name);
  ent->uid  = pw->pw_uid;
  ent->gid  = pw->pw_gid;
  hash      = pw->pw_passwd;

  if (hash[0] == 'x' && hash[1] == '\0') {
    const char *shadow = pw_shadow_hash(name);
    if (shadow)
      hash = shadow;
  }

  ent->hash = estrdup(hash);
  return 0;
}

static int
fill_random(void *buf, size_t n)
{
  if (getentropy(buf, n) == 0)
    return 0;
  {
    int    fd;
    size_t off;
    fd = open(ARUU_PATH_DEVURANDOM, O_RDONLY);
    if (fd < 0) {
      weprintf("open /dev/urandom:");
      return -1;
    }
    off = 0;
    while (off < n) {
      ssize_t r = read(fd, (char *)buf + off, n - off);
      if (r <= 0) {
        close(fd);
        weprintf("read /dev/urandom:");
        return -1;
      }
      off += (size_t)r;
    }
    close(fd);
  }
  return 0;
}

int
pw_gensalt_cipher(char *salt, size_t salt_sz, const char *prefix, size_t rand_len)
{
  static const char b64[] = "./"
                            "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefgh"
                            "ijklmnopqrstuvwxyz";
  unsigned char     raw[32];
  char              body[48];
  size_t            i, n, body_len;
  unsigned          v;
  int               prefix_len;

  if (rand_len > sizeof(raw))
    rand_len = sizeof(raw);
  if (rand_len == 0) {
    salt[0] = '\0';
    return 0;
  }

  prefix_len = 0;
  if (prefix) {
    prefix_len = snprintf(salt, salt_sz, "%s", prefix);
    if (prefix_len < 0 || (size_t)prefix_len >= salt_sz) {
      weprintf("snprintf:");
      return -1;
    }
  }

  if (fill_random(raw, rand_len) < 0)
    return -1;

  body_len = (rand_len * 4 + 2) / 3;
  if (body_len >= sizeof(body))
    body_len = sizeof(body) - 1;
  n = 0;
  for (i = 0; i + 3 <= rand_len; i += 3) {
    v         = (raw[i] << 16) | (raw[i + 1] << 8) | raw[i + 2];
    body[n++] = b64[v & 0x3f];
    v >>= 6;
    body[n++] = b64[v & 0x3f];
    v >>= 6;
    body[n++] = b64[v & 0x3f];
    v >>= 6;
    body[n++] = b64[v & 0x3f];
  }
  if (i < rand_len) {
    v = raw[i] << 16;
    if (i + 1 < rand_len)
      v |= raw[i + 1] << 8;
    body[n++] = b64[v & 0x3f];
    v >>= 6;
    body[n++] = b64[v & 0x3f];
    v >>= 6;
    if (i + 1 < rand_len)
      body[n++] = b64[v & 0x3f];
  }
  body[n] = '\0';

  if ((size_t)prefix_len + n + 1 >= salt_sz) {
    weprintf("salt buffer too small\n");
    return -1;
  }
  memcpy(salt + prefix_len, body, n + 1);
  return 0;
}

int
pw_gensalt(char *salt, size_t salt_sz)
{
  return pw_gensalt_cipher(salt, salt_sz, PW_CIPHER, 16);
}

int
pw_update_file(const char *path, const char *name, const char *newhash)
{
  struct passwd *pw, cur;
  FILE          *fp, *tfp;
  int            wrote = 0;

  fp = fopen(path, "r+");
  if (!fp) {
    weprintf("fopen %s:", path);
    return -1;
  }
  tfp = tmpfile();
  if (!tfp) {
    weprintf("tmpfile:");
    fclose(fp);
    return -1;
  }
  while ((pw = fgetpwent(fp))) {
    cur = *pw;
    if (strcmp(cur.pw_name, name) == 0) {
      cur.pw_passwd = (char *)newhash;
      wrote         = 1;
    }
    errno = 0;
    if (putpwent(&cur, tfp) == -1) {
      weprintf("putpwent:");
      fclose(tfp);
      fclose(fp);
      return -1;
    }
  }
  if (!wrote) {
    weprintf("passwd: no matching entry\n");
    fclose(tfp);
    fclose(fp);
    return -1;
  }
  fflush(tfp);
  rewind(tfp);
  rewind(fp);
  fconcat(tfp, "tmpfile", fp, path);
  ftruncate(fileno(fp), ftell(tfp));
  {
    int fd = fileno(fp);
    if (fd >= 0)
      fsync(fd);
  }
  fclose(tfp);
  fclose(fp);
  return 0;
}

int
pw_update_master(const char *name, const char *newhash)
{
  int   r;
  pid_t pid, wr;
  int   st;

  r = pw_update_file(ARUU_BSD_PATH_MASTER_PASSWD, name, newhash);
  if (r != 0)
    return r;
  pid = fork();
  if (pid < 0) {
    weprintf("fork:");
    return -1;
  }
  if (pid == 0) {
    execl(ARUU_BSD_PATH_PWD_MKDB, "pwd_mkdb", "-p", NULL);
    weprintf("exec %s:", ARUU_BSD_PATH_PWD_MKDB);
    _exit(127);
  }
  do {
    wr = waitpid(pid, &st, 0);
  } while (wr < 0 && errno == EINTR);
  if (wr < 0) {
    weprintf("waitpid:");
    return -1;
  }
  if (!WIFEXITED(st)) {
    weprintf("pwd_mkdb did not exit normally\n");
    return -1;
  }
  if (WEXITSTATUS(st) != 0) {
    weprintf("pwd_mkdb exited with status %d\n", WEXITSTATUS(st));
    return -1;
  }
  return 0;
}

int
pwdb_update(const char *name, const char *newhash)
{
  int r;

  if ((r = pw_shadow_update(name, newhash)) != 1)
    return r;
  return pw_update_file(ARUU_PATH_PASSWD, name, newhash);
}
