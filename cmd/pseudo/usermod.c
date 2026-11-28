/* see LICENSE file for copyright and license details */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include <errno.h>
#include <grp.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "accounts.h"
#include "passwd.h"
#include "util.h"

static void
usage(void)
{
  eprintf("usage: %s [-R root] [-p hash] [-a] [-G group,...] name\n", argv0);
}

/* whether group is one of list, a comma separated list */
static int
listed(const char *list, const char *group)
{
  size_t n = strlen(group);

  for (const char *p = list; p && *p; p = strchr(p, ',') ? strchr(p, ',') + 1 : NULL)
    if (!strncmp(p, group, n) && (p[n] == ',' || p[n] == '\0'))
      return 1;
  return 0;
}

// ?man usermod: change a person
// ?man arguments: [-R root] [-p hash] [-a] [-G group,...] name
// ?man changes a person's password and the groups they are in
int
main(int argc, char *argv[])
{
  char *root = NULL, *hash = NULL, *groups = NULL, *g, *save;
  struct group *gr;
  int append = 0;

  ARGBEGIN
  {
    // ?man -R:str: work in another root
    case 'R': root = EARGF(usage()); break;
    // ?man -p:str: the password as crypt(3) gives it
    case 'p': hash = EARGF(usage()); break;
    // ?man -a: with -G, only put the person in the groups
    case 'a': append = 1; break;
    // ?man -G:str: the groups the person is in, apart by commas, and no other
    case 'G': groups = EARGF(usage()); break;
    default: usage();
  }
  ARGEND;
  if (argc != 1 || (append && !groups))
    usage();
  if (root && (chroot(root) < 0 || chdir("/") < 0))
    eprintf("chroot %s:", root);
  if (hash && pwdb_update(argv[0], hash) < 0)
    eprintf("%s: the password could not be kept\n", argv[0]);
  if (groups && !append) {
    setgrent();
    while ((gr = getgrent()))
      for (char **m = gr->gr_mem; m && *m; m++)
        if (!strcmp(*m, argv[0]) && !listed(groups, gr->gr_name) && acct_member(gr->gr_name, argv[0], 0) < 0)
          weprintf("%s:", gr->gr_name);
    endgrent();
  }
  for (g = groups ? strtok_r(groups, ",", &save) : NULL; g; g = strtok_r(NULL, ",", &save))
    if (acct_member(g, argv[0], 1) < 0)
      weprintf("%s:", g);
  return 0;
}
