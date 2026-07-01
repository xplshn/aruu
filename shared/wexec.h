/* See LICENSE file for copyright and license details. */
#ifndef ARUU_WEXEC_H
#define ARUU_WEXEC_H

#include <stdio.h>
#include <sys/types.h>

typedef int (*wexec_fn)(int argc, char **argv);

int wexec_register(const char *name, wexec_fn fn);
int wexec_is_builtin(const char *name);

int wexecvp(const char *name, char *const *argv);
int wexecv(const char *path, char *const *argv);

FILE *wpopen(const char *name, char *const *argv, const char *mode);
int wpclose(FILE *fp);

int wsystem(const char *cmd);

void wexecvp_self(const char *name, char *const *argv);
void wexecv_self(const char *path, char *const *argv);

int wpopen_track(FILE *fp, pid_t pid);
pid_t wpopen_untrack(FILE *fp);

#endif
