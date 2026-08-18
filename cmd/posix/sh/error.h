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

#include "sh_cdefs.h"

/*
 * we enclose jmp_buf in a structure so that we can declare pointers to
 * jump locations. the global variable handler contains the location to
 * jump to when an exception occurs, and the global variable exception
 * contains a code identifying the exception. to implement nested
 * exception handlers, the user should save the value of handler on entry
 * to an inner scope, set handler to point to a jmploc structure for the
 * inner scope, and restore handler on exit from the scope
 */

#include <setjmp.h>
#include <signal.h>

struct jmploc {
  jmp_buf loc;
};

extern struct jmploc        *handler;
extern volatile sig_atomic_t exception;

/* exceptions */
#define EXINT   0 /* SIGINT received */
#define EXERROR 1 /* a generic error with exitstatus */
#define EXEXIT  2 /* call exitshell(exitstatus) */

/*
 * these macros allow the user to suspend the handling of interrupt signals
 * over a period of time. this is similar to SIGHOLD to or sigblock, but
 * much more efficient and portable. (but hacking the kernel is so much
 * more fun than worrying about efficiency and portability. :-))
 */

extern volatile sig_atomic_t suppressint;
extern volatile sig_atomic_t intpending;

#define INTOFF suppressint++
#define INTON                                                                                      \
  {                                                                                                \
    if (--suppressint == 0 && intpending)                                                          \
      onint();                                                                                     \
  }
#define is_int_on() suppressint
#define SETINTON(s)                                                                                \
  do {                                                                                             \
    suppressint = (s);                                                                             \
    if (suppressint == 0 && intpending)                                                            \
      onint();                                                                                     \
  } while (0)
#define FORCEINTON                                                                                 \
  {                                                                                                \
    suppressint = 0;                                                                               \
    if (intpending)                                                                                \
      onint();                                                                                     \
  }
#define SET_PENDING_INT   intpending = 1
#define CLEAR_PENDING_INT intpending = 0
#define int_pending()     intpending

void exraise(int) __dead2;
void onint(void) __dead2;
void warning(const char *, ...) __printflike(1, 2);
void error(const char *, ...) __printf0like(1, 2) __dead2;
void errorwithstatus(int, const char *, ...) __printf0like(2, 3) __dead2;

/*
 * BSD setjmp saves the signal mask, which violates ANSI c and takes time,
 * so we use _setjmp instead
 */

#undef setjmp
#define setjmp(jmploc) _setjmp(jmploc)
#undef longjmp
#define longjmp(jmploc, val) _longjmp(jmploc, val)
