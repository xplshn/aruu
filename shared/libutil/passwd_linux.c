/* see LICENSE file for copyright and license details */
#if FEATURE_PASSWD_EXT
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#endif
#include "../passwd.h"
#include "../paths.h"
#include "../text.h"
#include "../util.h"

#if defined(__linux__) && !defined(ARUU_FORCE_OTHER)
#include <errno.h>
#include <shadow.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

char *
pw_shadow_hash(const char *name)
{
  struct spwd *spw;

  errno = 0;
  spw   = getspnam(name);
  return spw ? spw->sp_pwdp : NULL;
}

static int
update_shadow(const char *name, const char *newhash)
{
  struct spwd *spw, cur;
  FILE        *fp, *tfp;
  int          wrote = 0;
  char         path[256];

  snprintf(path, sizeof(path), ARUU_PATH_ETC "/tcb/%s/shadow", name);
  fp = fopen(path, "r+");
  if (!fp) {
    memcpy(path, ARUU_PATH_SHADOW, sizeof(ARUU_PATH_SHADOW));
    fp = fopen(path, "r+");
  }
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

  /* fgetspent(fp) reads from the stream we already opened above;
 * getspent() reads an implicit global shadow stream that musl
 * never actually backs with this (or any) file and always
 * returns null for, silently turning every update into a
 * "no matching entry" failure */
  while ((spw = fgetspent(fp))) {
    cur = *spw;
    if (strcmp(cur.sp_namp, name) == 0) {
      cur.sp_pwdp = (char *)newhash;
      wrote       = 1;
    }
    errno = 0;
    if (putspent(&cur, tfp) == -1) {
      weprintf("putspent:");
      fclose(tfp);
      fclose(fp);
      return -1;
    }
  }
  if (!wrote) {
    weprintf("shadow: no matching entry\n");
    fclose(tfp);
    fclose(fp);
    return -1;
  }
  fflush(tfp);
  rewind(tfp);
  rewind(fp);
  fconcat(tfp, "tmpfile", fp, "shadow");
  ftruncate(fileno(fp), ftell(tfp));
  fclose(tfp);
  {
    int fd = fileno(fp);
    if (fd >= 0)
      fsync(fd);
  }
  fclose(fp);
  return 0;
}

int
pw_shadow_update(const char *name, const char *newhash)
{
  errno = 0;
  if (!getspnam(name))
    return 1;
  return update_shadow(name, newhash);
}
#endif
