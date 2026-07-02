/* See LICENSE file for copyright and license details. */
#ifndef ARUU_WEXEC_H
#define ARUU_WEXEC_H

#include <stdio.h>
#include <sys/types.h>

typedef int (*wexec_fn)(int argc, char **argv);

int  wexec_register(const char *name, wexec_fn fn);
int  wexec_is_builtin(const char *name);
void wexec_register_env_lookup(char *(*fn)(const char *name));
#if FEATURE_NOEXEC || FEATURE_NOFORK
int  wexec_call_builtin(const char *name, char *const *argv);
void wexec_exit(int code) __attribute__((noreturn));
#endif

/* search path for name, returns emallocd absolute path or null if not found
 * when feature_noexec is compiled in and builtin dispatch is enabled,
 * returns null for registered builtins (they have no filesystem path) */
char *wwhich(const char *name);

int wexec_is_nofork(const char *name);

#if FEATURE_NOEXEC
void wexec_set_noexec(int enabled);
int  wexec_get_noexec(void);
#else
#define wexec_get_noexec() 0
#endif

#if FEATURE_NOFORK
void wexec_set_nofork(int enabled);
int  wexec_get_nofork(void);
#else
#define wexec_get_nofork() 0
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
