/* see LICENSE file for copyright and license details */
#include "../timer.h"

#if defined(__linux__) && !defined(ARUU_FORCE_OTHER)
#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <sys/timerfd.h>
#include <unistd.h>

static int timers[TIMER_MAX];
static int ntimers;

int
timer_init(int n)
{
  if (n > TIMER_MAX) {
    errno = EINVAL;
    return -1;
  }
  for (ntimers = 0; ntimers < n; ntimers++) {
    timers[ntimers] = timerfd_create(CLOCK_BOOTTIME, TFD_CLOEXEC);
    if (timers[ntimers] == -1)
      return -1;
  }
  return 0;
}

int
timer_set(int i, const struct timespec *ts)
{
  struct itimerspec its = {0};

  its.it_value = *ts;
  return timerfd_settime(timers[i], 0, &its, NULL);
}

int
timer_left(int i, struct timespec *ts)
{
  struct itimerspec its;

  if (timerfd_gettime(timers[i], &its) < 0)
    return -1;
  *ts = its.it_value;
  return 0;
}

int
timer_wait(int fd)
{
  struct pollfd pfd[TIMER_MAX + 1];
  uint64_t      n;
  int           i, fired = 0;

  pfd[0].fd     = fd;
  pfd[0].events = POLLIN;
  for (i = 0; i < ntimers; i++) {
    pfd[i + 1].fd     = timers[i];
    pfd[i + 1].events = POLLIN;
  }
  if (poll(pfd, ntimers + 1, -1) == -1)
    return -1;
  if (pfd[0].revents)
    return 0;
  for (i = 0; i < ntimers; i++) {
    if (pfd[i + 1].revents) {
      fired |= 1 << i;
      read(timers[i], &n, sizeof(n));
    }
  }
  return fired;
}
#endif
