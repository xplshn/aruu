/* -
 * spdx-license-identifier: bsd-3-clause
 *
 * copyright (c) 1991, 1993
 * the regents of the university of california. all rights reserved
 *
 * this code is derived from software contributed to berkeley by
 * kenneth almquist
 *
 * redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer
 * 2. redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution
 * 3. neither the name of the university nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR a PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE
 */

#ifndef SHELL_H_
#define SHELL_H_

#if !defined(FEATURE_SH_HISTEDIT) || !FEATURE_SH_HISTEDIT
#ifndef NO_HISTORY
#define NO_HISTORY
#endif
#endif

#include "sig.h"

#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef tcsetsid
#if defined(__linux__) || defined(__CYGWIN__)
#define tcsetsid(fd, pid) ioctl((fd), TIOCSCTTY, 0)
#endif
#endif

#ifndef ALIGN
#define ALIGNBYTES (sizeof(void *) - 1)
#define ALIGN(p)   (((uintptr_t)(p) + ALIGNBYTES) & ~ALIGNBYTES)
#endif

#ifndef __dead2
#define __dead2 __attribute__((__noreturn__))
#endif
#ifndef __unused
#define __unused __attribute__((__unused__))
#endif
#ifndef __nonstring
#if defined(__has_attribute)
#if __has_attribute(__nonstring__)
#define __nonstring __attribute__((__nonstring__))
#elif __has_attribute(nonstring)
#define __nonstring __attribute__((nonstring))
#endif
#endif
#endif
#ifndef __nonstring
#define __nonstring
#endif
#ifndef __printf0like
#define __printf0like(n, m) __attribute__((__format__(__printf__, n, m)))
#endif
#ifndef __printflike
#define __printflike(n, m) __attribute__((__format__(__printf__, n, m)))
#endif

#ifndef O_VERIFY
#define O_VERIFY 0
#endif

#ifndef CLOCK_UPTIME
#define CLOCK_UPTIME CLOCK_MONOTONIC
#endif

#ifndef timespecsub
#define timespecsub(a, b, result)                                                                  \
  do {                                                                                             \
    (result)->tv_sec  = (a)->tv_sec - (b)->tv_sec;                                                 \
    (result)->tv_nsec = (a)->tv_nsec - (b)->tv_nsec;                                               \
    if ((result)->tv_nsec < 0) {                                                                   \
      --(result)->tv_sec;                                                                          \
      (result)->tv_nsec += 1000000000L;                                                            \
    }                                                                                              \
  } while (0)
#endif

#ifndef MAXLOGNAME
#ifdef LOGIN_NAME_MAX
#define MAXLOGNAME LOGIN_NAME_MAX
#else
#define MAXLOGNAME 32
#endif
#endif

#ifndef __DECONST
#define __DECONST(type, var) ((type)(uintptr_t)(const void *)(var))
#endif

#ifndef eaccess
#ifdef AT_EACCESS
#define eaccess(path, mode) faccessat(AT_FDCWD, (path), (mode), AT_EACCESS)
#else
#define eaccess(path, mode) access((path), (mode))
#endif
#endif

/*
 * the follow should be set to reflect the type of system you have:
 * JOBS -> 1 if you have berkeley job control, 0 otherwise
 * define DEBUG=1 to compile in debugging (set global "debug" to turn on)
 * define DEBUG=2 to compile in and turn on debugging
 *
 * when debugging is on, debugging info will be written to ./trace and
 * a quit signal will generate a core dump
 */

#define JOBS 1
/* #define DEBUG 1 */

/*
 * type of used arithmetic. susv3 requires us to have at least signed long
 */
typedef intmax_t arith_t;
#define ARITH_FORMAT_STR "%" PRIdMAX
#define ARITH_MIN        INTMAX_MIN
#define ARITH_MAX        INTMAX_MAX

#ifndef _SH_POINTER_TYPEDEF
#define _SH_POINTER_TYPEDEF
typedef void *pointer;
#endif

#if defined(__has_include)
#if __has_include(<sys/cdefs.h>)
#include <sys/cdefs.h>
#endif
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__APPLE__)
#include <sys/cdefs.h>
#endif

extern char nullstr[1]; /* null string */

#ifdef DEBUG
#define TRACE(param) sh_trace param
#else
#define TRACE(param)
#endif

#endif /* !SHELL_H_ */
