/* see LICENSE file for copyright and license details */
#include "../timer.h"

/* deadlines on the monotonic clock posix has, which may stop while the
 * machine sleeps */
#if !defined(__linux__) || defined(ARUU_FORCE_OTHER)
#include <errno.h>
#include <poll.h>
#include <string.h>

static struct timespec deadline[TIMER_MAX]; /* 0 0 for disarmed */
static int             ntimers;

static int
armed(int i)
{
  return deadline[i].tv_sec || deadline[i].tv_nsec;
}

static long long
nsec(const struct timespec *ts)
{
  return ts->tv_sec * 1000000000LL + ts->tv_nsec;
}

int
timer_init(int n)
{
  if (n > TIMER_MAX) {
    errno = EINVAL;
    return -1;
  }
  ntimers = n;
  memset(deadline, 0, sizeof(deadline));
  return 0;
}

int
timer_set(int i, const struct timespec *ts)
{
  struct timespec now;
  long long       t;

  if (!ts->tv_sec && !ts->tv_nsec) {
    memset(&deadline[i], 0, sizeof(deadline[i]));
    return 0;
  }
  if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
    return -1;
  t                    = nsec(&now) + nsec(ts);
  deadline[i].tv_sec  = t / 1000000000LL;
  deadline[i].tv_nsec = t % 1000000000LL;
  return 0;
}

int
timer_left(int i, struct timespec *ts)
{
  struct timespec now;
  long long       t = 0;

  if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
    return -1;
  if (armed(i) && (t = nsec(&deadline[i]) - nsec(&now)) < 1)
    t = 1;
  ts->tv_sec  = t / 1000000000LL;
  ts->tv_nsec = t % 1000000000LL;
  return 0;
}

int
timer_wait(int fd)
{
  struct pollfd   pfd = {.fd = fd, .events = POLLIN};
  struct timespec now;
  long long       next, left;
  int             i, ms, fired;

  for (;;) {
    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
      return -1;
    fired = 0;
    next  = -1;
    for (i = 0; i < ntimers; i++) {
      if (!armed(i))
        continue;
      left = nsec(&deadline[i]) - nsec(&now);
      if (left <= 0) {
        fired |= 1 << i;
        memset(&deadline[i], 0, sizeof(deadline[i]));
      } else if (next < 0 || left < next) {
        next = left;
      }
    }
    if (fired)
      return fired;
    ms = next < 0 ? -1 : (int)((next + 999999) / 1000000);
    if (poll(&pfd, 1, ms) == -1)
      return -1;
    if (pfd.revents)
      return 0;
  }
}
#endif
