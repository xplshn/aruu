/* See LICENSE file for copyright and license details. */
#include "../wexec.h"
#include "../config.h"
#include "../paths.h"
#include "../util.h"

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_BUILTINS 128
#define MAX_POPEN    32

struct builtin {
  const char *name;
  wexec_fn    fn;
};

static struct builtin builtins[MAX_BUILTINS];
static int            nbuiltins;

struct popen_entry {
  FILE *fp;
  pid_t pid;
};

static struct popen_entry popen_tab[MAX_POPEN];
static int                npopen;

int
wpopen_track(FILE *fp, pid_t pid)
{
  if (npopen >= MAX_POPEN)
    return -1;
  popen_tab[npopen].fp  = fp;
  popen_tab[npopen].pid = pid;
  npopen++;
  return 0;
}

pid_t
wpopen_untrack(FILE *fp)
{
  int   i;
  pid_t pid = -1;
  for (i = 0; i < npopen; i++) {
    if (popen_tab[i].fp == fp) {
      pid          = popen_tab[i].pid;
      popen_tab[i] = popen_tab[npopen - 1];
      npopen--;
      break;
    }
  }
  return pid;
}

int
wexec_register(const char *name, wexec_fn fn)
{
  int i;
  for (i = 0; i < nbuiltins; i++) {
    if (strcmp(builtins[i].name, name) == 0)
      return -1;
  }
  if (nbuiltins >= MAX_BUILTINS)
    return -1;
  builtins[nbuiltins].name = name;
  builtins[nbuiltins].fn   = fn;
  nbuiltins++;
  return 0;
}

int
wexec_is_builtin(const char *name)
{
  int i;
  for (i = 0; i < nbuiltins; i++) {
    if (strcmp(builtins[i].name, name) == 0)
      return 1;
  }
  return 0;
}

#if FEATURE_INPROC_EXEC
static int inproc_enabled  = 1;

#if FEATURE_INPROC_EXEC_OVERRIDE
static int inproc_env_read = 0;

/* ARUU_INPROC_EXEC=0 forces fork+exec at runtime, even though the box was
 * built with FEATURE_INPROC_EXEC=1. lets a person reach an external tool
 * that shares a name with a builtin without giving its full path. only
 * compiled in when the box is also built with FEATURE_INPROC_EXEC_OVERRIDE=1;
 * without that flag this env var is never read and inproc dispatch cannot be
 * bypassed except through an explicit wexec_set_inproc() call. read once
 * and cached, an explicit wexec_set_inproc() call overrides it */
static void
inproc_read_env(void)
{
  const char *e;

  if (inproc_env_read)
    return;
  inproc_env_read = 1;
  e = getenv("ARUU_INPROC_EXEC");
  if (e && strcmp(e, "0") == 0)
    inproc_enabled = 0;
}
#endif

void
wexec_set_inproc(int enabled)
{
#if FEATURE_INPROC_EXEC_OVERRIDE
  inproc_env_read = 1;
#endif
  inproc_enabled = enabled;
}

int
wexec_get_inproc(void)
{
#if FEATURE_INPROC_EXEC_OVERRIDE
  inproc_read_env();
#endif
  return inproc_enabled;
}
#endif

char *
wwhich(const char *name)
{
  const char *path, *p, *end;
  char       *buf;
  size_t      dlen, nlen;
  struct stat  st;

#if FEATURE_INPROC_EXEC
  /* when inproc dispatch is active, registered builtins have no filesystem path */
  if (inproc_enabled && wexec_is_builtin(name))
    return NULL;
#endif

  /* slash present: treat as a literal path */
  if (strchr(name, '/') != NULL) {
    if (stat(name, &st) == 0 && S_ISREG(st.st_mode) && (st.st_mode & 0111))
      return estrdup(name);
    return NULL;
  }

  path = getenv("PATH");
  if (path == NULL)
    path = "/usr/local/bin:/usr/bin:/bin";

  nlen = strlen(name);
  p    = path;
  for (;;) {
    end  = strchr(p, ':');
    dlen = end ? (size_t)(end - p) : strlen(p);
    /* skip empty components (current dir) for safety */
    if (dlen > 0) {
      buf           = emalloc(dlen + 1 + nlen + 1);
      memcpy(buf, p, dlen);
      buf[dlen]     = '/';
      memcpy(buf + dlen + 1, name, nlen + 1);
      if (stat(buf, &st) == 0 && S_ISREG(st.st_mode) && (st.st_mode & 0111))
        return buf;
      free(buf);
    }
    if (!end)
      break;
    p = end + 1;
  }
  return NULL;
}

#if FEATURE_INPROC_EXEC
static int
call_builtin(const char *name, char *const *argv)
{
  int    i, argc;
  char **p;

  argc = 0;
  for (p = (char **)argv; *p; p++)
    argc++;
  for (i = 0; i < nbuiltins; i++) {
    if (strcmp(builtins[i].name, name) == 0) {
      fflush(stdout);
      fflush(stderr);
      return builtins[i].fn(argc, (char **)argv);
    }
  }
  return -1;
}
#endif

static int
fork_exec(const char *path, char *const *argv, int use_path)
{
  pid_t pid;
  int   st;

  pid = fork();
  if (pid < 0) {
    weprintf("fork:");
    return -1;
  }
  if (pid == 0) {
    if (use_path)
      execvp(path, argv);
    else
      execv(path, argv);
    _exit(127);
  }
  if (waitpid(pid, &st, 0) < 0) {
    weprintf("waitpid:");
    return -1;
  }
  if (WIFEXITED(st))
    return WEXITSTATUS(st);
  if (WIFSIGNALED(st))
    return 128 + WTERMSIG(st);
  return -1;
}

int
wexecvp(const char *name, char *const *argv)
{
#if FEATURE_INPROC_EXEC
  if (inproc_enabled && wexec_is_builtin(name)) {
    fflush(stdout);
    fflush(stderr);
    return call_builtin(name, argv);
  }
#endif
  return fork_exec(name, argv, 1);
}

int
wexecv(const char *path, char *const *argv)
{
#if FEATURE_INPROC_EXEC
  {
    const char *base = strrchr(path, '/');
    base             = base ? base + 1 : path;
    if (inproc_enabled && wexec_is_builtin(base)) {
      fflush(stdout);
      fflush(stderr);
      return call_builtin(base, argv);
    }
  }
#endif
  return fork_exec(path, argv, 0);
}

FILE *
wpopen(const char *name, char *const *argv, const char *mode)
{
  int   pfd[2];
  pid_t pid;
  int   parent_end, child_end;
  char *sh_argv[4];

  if (pipe(pfd) < 0) {
    weprintf("pipe:");
    return NULL;
  }

  if (*mode == 'r') {
    parent_end = pfd[0];
    child_end  = pfd[1];
  } else {
    parent_end = pfd[1];
    child_end  = pfd[0];
  }

  pid = fork();
  if (pid < 0) {
    weprintf("fork:");
    close(pfd[0]);
    close(pfd[1]);
    return NULL;
  }

  if (pid == 0) {
    close(parent_end);
    if (child_end != (*mode == 'r' ? STDOUT_FILENO : STDIN_FILENO)) {
      dup2(child_end, *mode == 'r' ? STDOUT_FILENO : STDIN_FILENO);
      close(child_end);
    }
    if (argv) {
#if FEATURE_INPROC_EXEC
      if (inproc_enabled && wexec_is_builtin(name)) {
        int    argc;
        char **p;
        argc = 0;
        for (p = (char **)argv; *p; p++)
          argc++;
        call_builtin(name, argv);
        fflush(stdout);
        fflush(stderr);
        _exit(0);
      }
#endif
      execvp(name, argv);
      _exit(127);
    } else {
      sh_argv[0] = "sh";
      sh_argv[1] = "-c";
      sh_argv[2] = (char *)name;
      sh_argv[3] = NULL;
#if FEATURE_INPROC_EXEC
      if (inproc_enabled && wexec_is_builtin("sh")) {
        call_builtin("sh", sh_argv);
        fflush(stdout);
        fflush(stderr);
        _exit(0);
      }
#endif
      execvp("sh", sh_argv);
      _exit(127);
    }
  }

  close(child_end);
  {
    FILE *fp = fdopen(parent_end, mode);
    if (!fp) {
      weprintf("fdopen:");
      close(parent_end);
      kill(pid, SIGTERM);
      waitpid(pid, NULL, 0);
      return NULL;
    }
    if (wpopen_track(fp, pid) < 0) {
      fclose(fp);
      kill(pid, SIGTERM);
      waitpid(pid, NULL, 0);
      return NULL;
    }
    return fp;
  }
}

int
wpclose(FILE *fp)
{
  int   st;
  pid_t pid;

  pid = wpopen_untrack(fp);
  if (fclose(fp) == EOF)
    weprintf("fclose:");
  if (pid < 0)
    return -1;
  if (waitpid(pid, &st, 0) < 0) {
    weprintf("waitpid:");
    return -1;
  }
  if (WIFEXITED(st))
    return WEXITSTATUS(st);
  if (WIFSIGNALED(st))
    return 128 + WTERMSIG(st);
  return -1;
}

int
wsystem(const char *cmd)
{
#if FEATURE_INPROC_EXEC
  if (inproc_enabled && wexec_is_builtin("sh")) {
    char *sh_argv[4];
    sh_argv[0] = "sh";
    sh_argv[1] = "-c";
    sh_argv[2] = (char *)cmd;
    sh_argv[3] = NULL;
    return wexecvp("sh", sh_argv);
  }
#endif
  {
    char *sh_argv[4];
    int   ret;
    sh_argv[0] = "sh";
    sh_argv[1] = "-c";
    sh_argv[2] = (char *)cmd;
    sh_argv[3] = NULL;
    ret        = fork_exec("sh", sh_argv, 1);
    return ret;
  }
}

void
wexecvp_self(const char *name, char *const *argv)
{
#if FEATURE_INPROC_EXEC
  if (inproc_enabled && wexec_is_builtin(name)) {
    int ret;
    fflush(stdout);
    fflush(stderr);
    ret = call_builtin(name, argv);
    fflush(stdout);
    fflush(stderr);
    exit(ret);
  }
#endif
  execvp(name, argv);
  weprintf("execvp %s:", name);
  exit((errno == ENOENT) ? 127 : 126);
}

void
wexecv_self(const char *path, char *const *argv)
{
#if FEATURE_INPROC_EXEC
  {
    const char *base = strrchr(path, '/');
    base             = base ? base + 1 : path;
    if (inproc_enabled && wexec_is_builtin(base)) {
      int ret;
      fflush(stdout);
      fflush(stderr);
      ret = call_builtin(base, argv);
      fflush(stdout);
      fflush(stderr);
      exit(ret);
    }
  }
#endif
  execv(path, argv);
  weprintf("execv %s:", path);
  exit((errno == ENOENT) ? 127 : 126);
}
