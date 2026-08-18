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

/*
 * errors and exceptions
 */

#include "error.h"
#include "eval.h"
#include "main.h"
#include "nodes.h" /* show.h needs nodes.h */
#include "options.h"
#include "output.h"
#include "shell.h"
#include "show.h"
#include "trap.h"
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

/*
 * code to handle exceptions in c
 */

struct jmploc        *handler;
volatile sig_atomic_t exception;
volatile sig_atomic_t suppressint;
volatile sig_atomic_t intpending;

static void verrorwithstatus(int, const char *, va_list) __printf0like(2, 0) __dead2;

/*
 * called to raise an exception. since c doesnt include exceptions, we
 * just do a longjmp to the exception handler. the type of exception is
 * stored in the global variable "exception"
 *
 * interrupts are disabled; they should be re-enabled when the exception is
 * caught
 */

void
exraise(int e)
{
  INTOFF;
  if (handler == NULL)
    abort();
  exception = e;
  longjmp(handler->loc, 1);
}

/*
 * called from trap.c when a SIGINT is received and not suppressed, or when
 * an interrupt is pending and interrupts are re-enabled using INTON
 * (if the user specifies that SIGINT is to be trapped or ignored using the
 * trap builtin, then this routine is not called.) suppressint is nonzero
 * when interrupts are held using the INTOFF macro. if sigints are not
 * suppressed and the shell is not a root shell, then we want to be
 * terminated if we get here, as if we were terminated directly by a SIGINT
 * arrange for this here
 */

void
onint(void)
{
  sigset_t sigs;

  intpending = 0;
  sigemptyset(&sigs);
  sigprocmask(SIG_SETMASK, &sigs, NULL);

  /*
 * this doesnt seem to be needed, since main() emits a newline
 */
#if 0
	if (tcgetpgrp(0) == getpid())
		write(STDERR_FILENO, "\n", 1);
#endif
  if (rootshell && iflag)
    exraise(EXINT);
  else {
    signal(SIGINT, SIG_DFL);
    kill(getpid(), SIGINT);
    _exit(128 + SIGINT);
  }
}

static void
vwarning(const char *msg, va_list ap)
{
  if (commandname)
    outfmt(out2, "%s: ", commandname);
  else if (arg0)
    outfmt(out2, "%s: ", arg0);
  doformat(out2, msg, ap);
  out2fmt_flush("\n");
}

void
warning(const char *msg, ...)
{
  va_list ap;
  va_start(ap, msg);
  vwarning(msg, ap);
  va_end(ap);
}

/*
 * exverror is called to raise the error exception. if the first argument
 * is not NULL then error prints an error message using printf style
 * formatting. it then raises the error exception
 */
static void
verrorwithstatus(int status, const char *msg, va_list ap)
{
  /*
 * an interrupt trumps an error. certain places catch error
 * exceptions or transform them to a plain nonzero exit code
 * in child processes, and if an error exception can be handled,
 * an interrupt can be handled as well
 *
 * exraise() will disable interrupts for the exception handler
 */
  FORCEINTON;

#ifdef DEBUG
  if (msg)
    TRACE(("verrorwithstatus(%d, \"%s\") pid=%d\n", status, msg, getpid()));
  else
    TRACE(("verrorwithstatus(%d, NULL) pid=%d\n", status, getpid()));
#endif
  if (msg)
    vwarning(msg, ap);
  flushall();
  exitstatus = status;
  exraise(EXERROR);
}

void
error(const char *msg, ...)
{
  va_list ap;
  va_start(ap, msg);
  verrorwithstatus(2, msg, ap);
  va_end(ap);
}

void
errorwithstatus(int status, const char *msg, ...)
{
  va_list ap;
  va_start(ap, msg);
  verrorwithstatus(status, msg, ap);
  va_end(ap);
}
