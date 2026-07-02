/* See LICENSE file for copyright and license details. */
#include "../wexec.h"
#include "../config.h"
#include "../paths.h"
#include "../util.h"

#include <errno.h>
#include <setjmp.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_BUILTINS 256
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

static char *(*wexec_env_lookup)(const char *name) = NULL;

void
wexec_register_env_lookup(char *(*fn)(const char *name))
{
  wexec_env_lookup = fn;
}

static const char *
wexec_getenv(const char *name)
{
  if (wexec_env_lookup)
    return wexec_env_lookup(name);
  return getenv(name);
}

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

int
wexec_is_nofork(const char *name)
{
#include "nofork_list.h"
  int i;
  for (i = 0; nofork_list[i]; i++) {
    if (strcmp(nofork_list[i], name) == 0)
      return 1;
  }
  return 0;
}

#if FEATURE_NOEXEC
static int noexec_enabled = 1;

#if FEATURE_NOEXEC_OVERRIDE
static void
noexec_read_env(void)
{
  const char *e = wexec_getenv("ARUU_NOEXEC");
  if (e && strcmp(e, "0") == 0)
    noexec_enabled = 0;
  else
    noexec_enabled = 1;
}
#endif

void
wexec_set_noexec(int enabled)
{
  noexec_enabled = enabled;
}

int
wexec_get_noexec(void)
{
#if FEATURE_NOEXEC_OVERRIDE
  noexec_read_env();
#endif
  return noexec_enabled;
}
#endif

#if FEATURE_NOFORK
static int nofork_enabled = 1;

#if FEATURE_NOFORK_OVERRIDE
static void
nofork_read_env(void)
{
  const char *e = wexec_getenv("ARUU_NOFORK");
  if (e && strcmp(e, "0") == 0)
    nofork_enabled = 0;
  else
    nofork_enabled = 1;
}
#endif

void
wexec_set_nofork(int enabled)
{
  nofork_enabled = enabled;
}

int
wexec_get_nofork(void)
{
#if FEATURE_NOFORK_OVERRIDE
  nofork_read_env();
#endif
  return nofork_enabled;
}
#endif

char *
wwhich(const char *name)
{
  const char *path, *p, *end;
  char       *buf;
  size_t      dlen, nlen;
  struct stat st;

#if FEATURE_NOEXEC
  /* when builtin dispatch is active, registered builtins have no filesystem path */
  if (wexec_get_noexec() && wexec_is_builtin(name))
    return NULL;
#endif

  /* slash present: treat as a literal path */
  if (strchr(name, '/') != NULL) {
    if (stat(name, &st) == 0 && S_ISREG(st.st_mode) && (st.st_mode & 0111))
      return estrdup(name);
    return NULL;
  }

  path = wexec_getenv("PATH");
  if (path == NULL)
    path = "/usr/local/bin:/usr/bin:/bin";

  nlen = strlen(name);
  p    = path;
  for (;;) {
    end  = strchr(p, ':');
    dlen = end ? (size_t)(end - p) : strlen(p);
    /* skip empty components (current dir) for safety */
    if (dlen > 0) {
      buf = emalloc(dlen + 1 + nlen + 1);
      memcpy(buf, p, dlen);
      buf[dlen] = '/';
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

#if FEATURE_NOEXEC || FEATURE_NOFORK
static sigjmp_buf            inproc_jmp;
static volatile sig_atomic_t inproc_active      = 0;
static volatile int          inproc_exit_status = 0;

static void
inproc_sig_handler(int signo)
{
  if (inproc_active) {
    siglongjmp(inproc_jmp, signo);
  }
}

int
wexec_call_builtin(const char *name, char *const *argv)
{
  struct sigaction sa, osa_int, osa_quit, osa_pipe;
  int              i, argc, ret, sig;
  char           **p;

  sig = sigsetjmp(inproc_jmp, 1);
  if (sig != 0) {
    inproc_active = 0;
    sigaction(SIGINT, &osa_int, NULL);
    sigaction(SIGQUIT, &osa_quit, NULL);
    sigaction(SIGPIPE, &osa_pipe, NULL);
    if (sig == 1)
      return inproc_exit_status;
    kill(getpid(), sig);
    return 128 + sig;
  }

  sa.sa_handler = inproc_sig_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGINT, &sa, &osa_int);
  sigaction(SIGQUIT, &sa, &osa_quit);

  sa.sa_handler = SIG_IGN;
  sigaction(SIGPIPE, &sa, &osa_pipe);

  inproc_active = 1;

  ret  = -1;
  argc = 0;
  for (p = (char **)argv; *p; p++)
    argc++;
  for (i = 0; i < nbuiltins; i++) {
    if (strcmp(builtins[i].name, name) == 0) {
      fflush(stdout);
      fflush(stderr);
      ret = builtins[i].fn(argc, (char **)argv);
      fflush(stdout);
      fflush(stderr);
      break;
    }
  }

  inproc_active = 0;

  sigaction(SIGINT, &osa_int, NULL);
  sigaction(SIGQUIT, &osa_quit, NULL);
  sigaction(SIGPIPE, &osa_pipe, NULL);

  return ret;
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
    const char *base = strrchr(path, '/');
    base             = base ? base + 1 : path;
#if FEATURE_NOEXEC
    if (wexec_get_noexec() && wexec_is_builtin(base)) {
      struct sigaction sa;
      int              i, ret;
      for (i = 1; i < NSIG; i++) {
        if (sigaction(i, NULL, &sa) == 0) {
          if (sa.sa_handler != SIG_IGN && sa.sa_handler != SIG_DFL) {
            sa.sa_handler = SIG_DFL;
            sa.sa_flags   = 0;
            sigemptyset(&sa.sa_mask);
            sigaction(i, &sa, NULL);
          }
        }
      }
      ret = wexec_call_builtin(base, argv);
      _exit(ret);
    }
#endif
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
#if FEATURE_NOFORK
  if (wexec_get_nofork() && wexec_is_builtin(name) && wexec_is_nofork(name)) {
    fflush(stdout);
    fflush(stderr);
    return wexec_call_builtin(name, argv);
  }
#endif
  return fork_exec(name, argv, 1);
}

int
wexecv(const char *path, char *const *argv)
{
  const char *base = strrchr(path, '/');
  base             = base ? base + 1 : path;
#if FEATURE_NOFORK
  if (wexec_get_nofork() && wexec_is_builtin(base) && wexec_is_nofork(base)) {
    fflush(stdout);
    fflush(stderr);
    return wexec_call_builtin(base, argv);
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
#if FEATURE_NOEXEC
  struct sigaction sa;
  int              i;
#endif

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
#if FEATURE_NOEXEC
      if (wexec_get_noexec() && wexec_is_builtin(name)) {
        int    argc;
        char **p;

        /* reset caught signal handlers to default for inproc command */
        for (i = 1; i < NSIG; i++) {
          if (sigaction(i, NULL, &sa) == 0) {
            if (sa.sa_handler != SIG_IGN && sa.sa_handler != SIG_DFL) {
              sa.sa_handler = SIG_DFL;
              sa.sa_flags   = 0;
              sigemptyset(&sa.sa_mask);
              sigaction(i, &sa, NULL);
            }
          }
        }

        argc = 0;
        for (p = (char **)argv; *p; p++)
          argc++;
        wexec_call_builtin(name, argv);
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
#if FEATURE_NOEXEC
      if (wexec_get_noexec() && wexec_is_builtin("sh")) {
        /* reset caught signal handlers to default for inproc command */
        for (i = 1; i < NSIG; i++) {
          if (sigaction(i, NULL, &sa) == 0) {
            if (sa.sa_handler != SIG_IGN && sa.sa_handler != SIG_DFL) {
              sa.sa_handler = SIG_DFL;
              sa.sa_flags   = 0;
              sigemptyset(&sa.sa_mask);
              sigaction(i, &sa, NULL);
            }
          }
        }

        wexec_call_builtin("sh", sh_argv);
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
#if FEATURE_NOEXEC
  if (wexec_get_noexec() && wexec_is_builtin("sh")) {
    pid_t pid;
    int   st;

    pid = fork();
    if (pid < 0) {
      weprintf("fork:");
      return -1;
    }
    if (pid == 0) {
      char            *sh_argv[4];
      struct sigaction sa;
      int              i;
      int              ret;

      /* reset caught signal handlers to default for inproc command */
      for (i = 1; i < NSIG; i++) {
        if (sigaction(i, NULL, &sa) == 0) {
          if (sa.sa_handler != SIG_IGN && sa.sa_handler != SIG_DFL) {
            sa.sa_handler = SIG_DFL;
            sa.sa_flags   = 0;
            sigemptyset(&sa.sa_mask);
            sigaction(i, &sa, NULL);
          }
        }
      }

      sh_argv[0] = "sh";
      sh_argv[1] = "-c";
      sh_argv[2] = (char *)cmd;
      sh_argv[3] = NULL;
      ret        = wexec_call_builtin("sh", sh_argv);
      _exit(ret);
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
#if FEATURE_NOEXEC
  if (wexec_get_noexec() && wexec_is_builtin(name)) {
    struct sigaction sa;
    int              i;
    int              ret;

    /* reset caught signal handlers to default for inproc command */
    for (i = 1; i < NSIG; i++) {
      if (sigaction(i, NULL, &sa) == 0) {
        if (sa.sa_handler != SIG_IGN && sa.sa_handler != SIG_DFL) {
          sa.sa_handler = SIG_DFL;
          sa.sa_flags   = 0;
          sigemptyset(&sa.sa_mask);
          sigaction(i, &sa, NULL);
        }
      }
    }

    fflush(stdout);
    fflush(stderr);
    ret = wexec_call_builtin(name, argv);
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
#if FEATURE_NOEXEC
  {
    const char *base = strrchr(path, '/');
    base             = base ? base + 1 : path;
    if (wexec_get_noexec() && wexec_is_builtin(base)) {
      struct sigaction sa;
      int              i;
      int              ret;

      /* reset caught signal handlers to default for inproc command */
      for (i = 1; i < NSIG; i++) {
        if (sigaction(i, NULL, &sa) == 0) {
          if (sa.sa_handler != SIG_IGN && sa.sa_handler != SIG_DFL) {
            sa.sa_handler = SIG_DFL;
            sa.sa_flags   = 0;
            sigemptyset(&sa.sa_mask);
            sigaction(i, &sa, NULL);
          }
        }
      }

      fflush(stdout);
      fflush(stderr);
      ret = wexec_call_builtin(base, argv);
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

void(exit)(int);

void
wexec_exit(int code)
{
#if FEATURE_NOEXEC || FEATURE_NOFORK
  if (inproc_active) {
    inproc_exit_status = code;
    siglongjmp(inproc_jmp, 1);
  }
#endif
  (exit)(code);
}
