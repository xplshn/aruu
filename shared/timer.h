/* see LICENSE file for copyright and license details */
#ifndef ARUU_TIMER_H
#define ARUU_TIMER_H

#include <time.h>

/* one shot timers on a clock that keeps counting while the machine
 * sleeps, waited on together with a file descriptor. a backend per kernel */
#define TIMER_MAX 8

int timer_init(int n);
/* a zero ts disarms timer i */
int timer_set(int i, const struct timespec *ts);
int timer_left(int i, struct timespec *ts);
/* 0 once fd is readable, else the mask of the timers that fired, 1 << i.
 * -1 with errno set on error */
int timer_wait(int fd);

#endif
