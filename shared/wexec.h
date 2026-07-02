/* See LICENSE file for copyright and license details. */
#ifndef ARUU_WEXEC_H
#define ARUU_WEXEC_H

#include <stdio.h>
#include <sys/types.h>

typedef int (*wexec_fn)(int argc, char **argv);

int wexec_register(const char *name, wexec_fn fn);
int wexec_is_builtin(const char *name);

/* search PATH for name; returns emalloc'd absolute path or NULL if not found.
 * when FEATURE_INPROC_EXEC is compiled in and inproc dispatch is enabled,
 * returns NULL for registered builtins (they have no filesystem path). */
char *wwhich(const char *name);

#if FEATURE_INPROC_EXEC
/* runtime toggle: disable to force fork+exec even when builtins are registered.
 * when the box is also built with FEATURE_INPROC_EXEC_OVERRIDE=1,
 * wexec_get_inproc() additionally honors ARUU_INPROC_EXEC=0 from the
 * environment, read once on first use, so a builtin can be bypassed without
 * a rebuild. without that second flag, inproc dispatch is hardwired on for
 * the whole process and only an explicit wexec_set_inproc() call can
 * change it. */
void wexec_set_inproc(int enabled);
int  wexec_get_inproc(void);
#endif

int wexecvp(const char *name, char *const *argv);
int wexecv(const char *path, char *const *argv);

FILE *wpopen(const char *name, char *const *argv, const char *mode);
int   wpclose(FILE *fp);

int wsystem(const char *cmd);

void wexecvp_self(const char *name, char *const *argv);
void wexecv_self(const char *path, char *const *argv);

int   wpopen_track(FILE *fp, pid_t pid);
pid_t wpopen_untrack(FILE *fp);

#endif
