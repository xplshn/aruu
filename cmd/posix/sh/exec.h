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

/* values of cmdtype */
#define CMDUNKNOWN  -1 /* no entry in table for command */
#define CMDNORMAL   0  /* command is an executable program */
#define CMDBUILTIN  1  /* command is a shell builtin */
#define CMDFUNCTION 2  /* command is a shell function */
#define CMDWEXEC    3  /* command is a registered builtin applet */

/* values for typecmd_impl's third parameter */
enum {
  TYPECMD_SMALLV, /* command -v */
  TYPECMD_BIGV,   /* command -V */
  TYPECMD_TYPE    /* type */
};

union node;
struct cmdentry {
  int cmdtype;
  union param {
    int             index;
    struct funcdef *func;
  } u;
  int         special;
  const char *cmdname;
};

/* action to find_command() */
#define DO_ERR    0x01 /* prints errors */
#define DO_NOFUNC 0x02 /* dont return shell functions, for command */

void        shellexec(char **, char **, const char *, int) __dead2;
char       *padvance(const char **, const char **, const char *);
void        find_command(const char *, struct cmdentry *, int, const char *);
int         find_builtin(const char *, int *);
void        hashcd(void);
void        changepath(const char *);
void        defun(const char *, union node *);
int         unsetfunc(const char *);
int         isfunc(const char *);
int         typecmd_impl(int, char **, int, const char *);
void        clearcmdentry(void);
const void *itercmd(const void *, struct cmdentry *);
