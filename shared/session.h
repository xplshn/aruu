/* see LICENSE file for copyright and license details */
#ifndef ARUU_SESSION_H
#define ARUU_SESSION_H

#include <stdio.h>
#include <sys/types.h>
#include <time.h>

struct SessionEntry {
  char   user[64];
  char   line[64];
  char   host[257];
  time_t time;
  int    login; /* a user's session, not one of init's own records */
};

typedef int session_fn(const struct SessionEntry *e, void *arg);

/* a backend per system: the record of a login on the controlling tty
 * that who and last read, best effort */
void session_record(const char *user);
/* fn for every record of a utmp or wtmp file, until it returns non zero.
 * -1 with errno set when the file cannot be read */
int  session_each(const char *path, session_fn *fn, void *arg);
/* a getty starting on line forgets every record of it */
void session_clear(const char *line);
/* the last login of uid, from a lastlog file the caller opened */
int  session_lastlog(FILE *fp, uid_t uid, struct SessionEntry *e);

#endif
