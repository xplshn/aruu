#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "util.h"
#include "paths.h"

enum filetype {
  NONE,   /* detect based on file extension */
  ASM,    /* assembly source */
  ASMPP,  /* assembly source requiring preprocessing */
  C,      /* c source */
  CHDR,   /* c header */
  CPPOUT, /* preprocessed c source */
  OBJ,    /* object file */
  QBE,    /* QBE IL */
};

enum stage {
  PREPROCESS,
  COMPILE,
  CODEGEN,
  ASSEMBLE,
  LINK,
};

#include "config.h"

struct stageinfo {
  const char  *name;
  struct array cmd;
  size_t       cmdbase;
  pid_t        pid;
};

struct input {
  char         *name;
  unsigned      stages;
  enum filetype filetype;
  bool          lib;
};

static struct {
  bool nostdlib;
  bool verbose;
} flags;

static struct stageinfo stages[] = {
    [PREPROCESS] = {.name = "preprocess"},
    [COMPILE]    = {.name = "compile"},
    [CODEGEN]    = {.name = "codegen"},
    [ASSEMBLE]   = {.name = "assemble"},
    [LINK]       = {.name = "link"},
};

static const char *const ignoreflags[] = {
    "fno-builtin", "ffreestanding", "pedantic", "pedantic-errors", "pipe"
};

static void
usage(const char *fmt, ...)
{
  va_list ap;

  if (fmt) {
    fprintf(stderr, "%s: ", argv0);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
  }
  fprintf(
      stderr,
      "usage: %s [-c|-S|-E] [-D name[=value]] [-U name] [-I dir] "
      "[-isystem dir] [-iquote dir] [-idirafter dir] [-std=standard] "
       "[-nostdinc] [-L dir] [-l library] [-static] [-pthread] [-P] [-s] "

      "[-g] [-o output] input...\n",
      argv0
  );
  exit(2);
}

static enum filetype
detectfiletype(const char *name)
{
  const char *dot;

  dot = strrchr(name, '.');
  if (dot) {
    ++dot;
    if (strcmp(dot, "c") == 0)
      return C;
    if (strcmp(dot, "h") == 0)
      return CHDR;
    if (strcmp(dot, "i") == 0)
      return CPPOUT;
    if (strcmp(dot, "qbe") == 0)
      return QBE;
    if (strcmp(dot, "s") == 0)
      return ASM;
    if (strcmp(dot, "S") == 0)
      return ASMPP;
  }

  return OBJ;
}

static char *
changeext(const char *name, const char *ext)
{
  const char *slash, *dot;
  char       *result;
  size_t      baselen;

  slash = strrchr(name, '/');
  if (slash)
    name = slash + 1;
  dot     = strrchr(name, '.');
  baselen = dot ? (size_t)(dot - name) : strlen(name);
  result  = xmalloc(baselen + strlen(ext) + 2);
  memcpy(result, name, baselen);
  result[baselen] = '.';
  strcpy(result + baselen + 1, ext);

  return result;
}

static int
spawn(pid_t *pid, struct array *args, posix_spawn_file_actions_t *actions)
{
  extern char **environ;
  char        **arg;

  if (flags.verbose) {
    fprintf(stderr, "%s: spawning", argv0);
    for (arg = args->val; *arg; ++arg)
      fprintf(stderr, " %s", *arg);
    fputc('\n', stderr);
  }
  return posix_spawnp(pid, *(char **)args->val, actions, NULL, args->val, environ);
}

static int
spawnphase(struct stageinfo *phase, int *fd, char *input, char *output, bool last)
{
  int                        ret, pipefd[2];
  posix_spawn_file_actions_t actions;

  phase->cmd.len = phase->cmdbase;
  if (last && output) {
    arrayaddptr(&phase->cmd, "-o");
    arrayaddptr(&phase->cmd, output);
  }
  arrayaddptr(&phase->cmd, input && *fd == -1 ? input : "-");
  arrayaddptr(&phase->cmd, NULL);

  ret = posix_spawn_file_actions_init(&actions);
  if (ret)
    goto err0;
  if (*fd != -1)
    ret = posix_spawn_file_actions_adddup2(&actions, *fd, 0);
  if (!last) {
    if (pipe(pipefd) < 0) {
      ret = errno;
      goto err1;
    }
    if (fcntl(pipefd[0], F_SETFD, FD_CLOEXEC) < 0) {
      ret = errno;
      goto err2;
    }
    if (fcntl(pipefd[1], F_SETFD, FD_CLOEXEC) < 0) {
      ret = errno;
      goto err2;
    }
    ret = posix_spawn_file_actions_adddup2(&actions, pipefd[1], 1);
    if (ret)
      goto err2;
  }

  ret = spawn(&phase->pid, &phase->cmd, &actions);
  if (ret)
    goto err2;
  if (!last) {
    *fd = pipefd[0];
    close(pipefd[1]);
  }
  posix_spawn_file_actions_destroy(&actions);

  return 0;

err2:
  if (!last) {
    close(pipefd[0]);
    close(pipefd[1]);
  }
err1:
  posix_spawn_file_actions_destroy(&actions);
err0:
  return ret;
}

static bool
succeeded(const char *phase, pid_t pid, int status)
{
  if (WIFEXITED(status)) {
    if (WEXITSTATUS(status) == 0)
      return true;
    warn("%s: process %ju exited with status %d", phase, (uintmax_t)pid, WEXITSTATUS(status));
  } else if (WIFSIGNALED(status)) {
    warn("%s: process signaled: %s", phase, strsignal(WTERMSIG(status)));
  } else {
    warn("%s: process failed", phase);
  }
  return false;
}

static void
buildobj(struct input *input, char *output)
{
  const char *phase;
  size_t      i, npids;
  pid_t       pid;
  int         status, ret, fd;
  bool        success = true;

  if (input->filetype == OBJ)
    return;
  if (input->stages & 1 << LINK) {
    input->stages &= ~(1 << LINK);
    output = xmalloc(strlen(ARUU_PATH_TMP) + sizeof("/cproc-XXXXXX"));
    sprintf(output, "%s/cproc-XXXXXX", ARUU_PATH_TMP);
    fd = mkstemp(output);
    if (fd < 0)
      fatal("mkstemp:");
    close(fd);
  } else if (output) {
    if (strcmp(output, "-") == 0)
      output = NULL;
  } else if (input->stages & 1 << ASSEMBLE) {
    output = changeext(input->name, "o");
  } else if (input->stages & 1 << CODEGEN) {
    output = changeext(input->name, "s");
  } else if (input->stages & 1 << COMPILE) {
    output = changeext(input->name, "qbe");
  }
  if (strcmp(input->name, "-") == 0)
    input->name = NULL;

  npids = 0;
  for (i = PREPROCESS, fd = -1; input->stages; ++i) {
    if (!(input->stages & 1 << i))
      continue;
    input->stages &= ~(1 << i);
    ret = spawnphase(&stages[i], &fd, input->name, output, !input->stages);
    if (ret) {
      warn("%s: spawn \"%s\": %s", stages[i].name, *(char **)stages[i].cmd.val, strerror(ret));
      goto kill;
    }
    ++npids;
  }
  input->name = output;

  while (npids > 0) {
    pid = wait(&status);
    if (pid < 0)
      fatal("waitpid:");
    for (i = 0; i < countof(stages); ++i) {
      if (pid == stages[i].pid) {
        --npids;
        stages[i].pid = 0;
        phase         = stages[i].name;
        break;
      }
    }
    if (i == countof(stages))
      continue;
    if (!succeeded(phase, pid, status)) {
    kill:
      if (success && npids > 0) {
        for (i = 0; i < countof(stages); ++i) {
          if (stages[i].pid)
            kill(stages[i].pid, SIGTERM);
        }
      }
      success = false;
    }
  }
  if (!success) {
    if (output)
      unlink(output);
    exit(1);
  }
}

static void
buildexe(struct input *inputs, size_t ninputs, char *output)
{
  struct stageinfo *s = &stages[LINK];
  size_t            i;
  int               ret, status;
  pid_t             pid;

  arrayaddptr(&s->cmd, "-o");
  arrayaddptr(&s->cmd, output);
  if (!flags.nostdlib && startfiles[0])
    arrayaddbuf(&s->cmd, startfiles, sizeof(startfiles));
  for (i = 0; i < ninputs; ++i) {
    if (inputs[i].lib)
      arrayaddptr(&s->cmd, "-l");
    arrayaddptr(&s->cmd, inputs[i].name);
  }
  if (!flags.nostdlib && endfiles[0])
    arrayaddbuf(&s->cmd, endfiles, sizeof(endfiles));
  arrayaddptr(&s->cmd, NULL);

  ret = spawn(&pid, &s->cmd, NULL);
  if (ret)
    fatal("%s: spawn \"%s\": %s", s->name, *(char **)s->cmd.val, strerror(errno));
  if (waitpid(pid, &status, 0) < 0)
    fatal("waitpid %ju:", (uintmax_t)pid);
  for (i = 0; i < ninputs; ++i) {
    if (inputs[i].filetype != OBJ)
      unlink(inputs[i].name);
  }
  exit(!succeeded(s->name, pid, status));
}

static char *
nextarg(char ***argv)
{
  if ((**argv)[2] != '\0')
    return &(**argv)[2];
  ++*argv;
  if (!**argv)
    usage(NULL);
  return **argv;
}

static char *
resolvepath(const char *arg0, const char *binname, const char *devrelpath)
{
  char        self[PATH_MAX], *dir, *path;
  size_t      n;
  struct stat st;

  n = readlink("/proc/self/exe", self, sizeof(self) - 1);
  if (n == -1) {
    n = strlen(arg0);
    if (n > sizeof(self) - 1)
      n = sizeof(self) - 1;
    memcpy(self, arg0, n);
  }
  self[n] = '\0';

  dir = strrchr(self, '/');
  if (dir) {
    *dir = '\0';
  } else {
    strcpy(self, ".");
  }

  path = xmalloc(strlen(self) + strlen(devrelpath) + 2);
  sprintf(path, "%s/%s", self, devrelpath);
  if (stat(path, &st) == 0)
    return path;

  free(path);

  path = xmalloc(strlen(self) + strlen(binname) + 2);
  sprintf(path, "%s/%s", self, binname);
  return path;
}

static int
hasprefix(const char *str, const char *pfx)
{
  return memcmp(str, pfx, strlen(pfx)) == 0;
}

int
main(int argc, char *argv[])
{
  enum stage    last     = LINK;
  enum filetype filetype = 0;
  char         *arg, *end, *output = NULL, *arch, *qbearch;
  struct array  inputs = {0}, *cmd;
  struct input *input;
  size_t        i;

  argv0 = progname(argv[0], "cc");

  char *cpp_path = resolvepath(argv[0], "cpp", "cpp");
  char *cc1_path = resolvepath(argv[0], "cc1", "cc1");
  char *qbe_path = resolvepath(argv[0], "qbe", "../../../externalRepos/qbe/qbe");
  char *as_path  = resolvepath(argv[0], "as", "../../../cmd/dev/as/as");
  char *ld_path  = resolvepath(argv[0], "ld", "../ld/ld");

  arrayaddptr(&stages[PREPROCESS].cmd, cpp_path);
  if (countof(preprocesscmd) > 1) {
    arrayaddbuf(
        &stages[PREPROCESS].cmd, preprocesscmd + 1, sizeof(preprocesscmd) - sizeof(preprocesscmd[0])
    );
  }

  arrayaddptr(&stages[COMPILE].cmd, cc1_path);

  arrayaddptr(&stages[CODEGEN].cmd, qbe_path);
  if (countof(codegencmd) > 1) {
    arrayaddbuf(&stages[CODEGEN].cmd, codegencmd + 1, sizeof(codegencmd) - sizeof(codegencmd[0]));
  }

  arrayaddptr(&stages[ASSEMBLE].cmd, as_path);
  if (countof(assemblecmd) > 1) {
    arrayaddbuf(
        &stages[ASSEMBLE].cmd, assemblecmd + 1, sizeof(assemblecmd) - sizeof(assemblecmd[0])
    );
  }

  arrayaddptr(&stages[LINK].cmd, ld_path);
  if (countof(linkcmd) > 1) {
    arrayaddbuf(&stages[LINK].cmd, linkcmd + 1, sizeof(linkcmd) - sizeof(linkcmd[0]));
  }

  if (hasprefix(target, "x86_64-apple-")) {
    arch    = "x86_64-sysv";
    qbearch = "amd64_apple";
  } else if (hasprefix(target, "x86_64-") || hasprefix(target, "amd64-")) {
    arch    = "x86_64-sysv";
    qbearch = "amd64_sysv";
  } else if (hasprefix(target, "aarch64-")) {
    arch    = "aarch64";
    qbearch = "arm64";
  } else if (hasprefix(target, "riscv64-")) {
    arch    = "riscv64";
    qbearch = "rv64";
  } else {
    fatal("unsupported target '%s'", target);
  }
  arrayaddptr(&stages[COMPILE].cmd, "-t");
  arrayaddptr(&stages[COMPILE].cmd, arch);
  arrayaddptr(&stages[CODEGEN].cmd, "-t");
  arrayaddptr(&stages[CODEGEN].cmd, qbearch);

  for (;;) {
  ignore:
    ++argv, --argc;
    arg = *argv;
    if (!arg)
      break;
    if (arg[0] != '-' || arg[1] == '\0') {
      input           = arrayadd(&inputs, sizeof(*input));
      input->name     = arg;
      input->lib      = false;
      input->filetype = filetype == NONE && arg[1] ? detectfiletype(arg) : filetype;
      switch (input->filetype) {
        case ASM:
          input->stages = 1 << ASSEMBLE | 1 << LINK;
          break;
        case ASMPP:
          input->stages = 1 << PREPROCESS | 1 << ASSEMBLE | 1 << LINK;
          break;
        case C:
          input->stages = 1 << PREPROCESS | 1 << COMPILE | 1 << CODEGEN | 1 << ASSEMBLE | 1 << LINK;
          break;
        case CHDR:
          input->stages = 1 << PREPROCESS;
          break;
        case CPPOUT:
          input->stages = 1 << COMPILE | 1 << CODEGEN | 1 << ASSEMBLE | 1 << LINK;
          break;
        case QBE:
          input->stages = 1 << CODEGEN | 1 << ASSEMBLE | 1 << LINK;
          break;
        case OBJ:
          input->stages = 1 << LINK;
          break;
        default:
          usage(
              "reading from standard input requires "
              "-x"
          );
      }
      continue;
    }

    for (i = 0; i < countof(ignoreflags); ++i) {
      if (strcmp(arg + 1, ignoreflags[i]) == 0)
        goto ignore;
    }

    if (strcmp(arg, "-nostdlib") == 0) {
      flags.nostdlib = true;
    } else if (strcmp(arg, "-nostdinc") == 0) {
      arrayaddptr(&stages[PREPROCESS].cmd, arg);
    } else if (strcmp(arg, "-static") == 0) {
      arrayaddptr(&stages[LINK].cmd, arg);
    } else if (strcmp(arg, "-emit-qbe") == 0) {
      if (last > COMPILE)
        last = COMPILE;
    } else if (
        strcmp(arg, "-idirafter") == 0 || strcmp(arg, "-isystem") == 0
        || strcmp(arg, "-iquote") == 0
    ) {
      if (!--argc)
        usage(NULL);
      arrayaddptr(&stages[PREPROCESS].cmd, arg);
      arrayaddptr(&stages[PREPROCESS].cmd, *++argv);
    } else if (strncmp(arg, "-std=", 5) == 0) {
      arrayaddptr(&stages[PREPROCESS].cmd, arg);
    } else if (strcmp(arg, "-pthread") == 0) {
      arrayaddptr(&stages[LINK].cmd, "-l");
      arrayaddptr(&stages[LINK].cmd, "pthread");
    } else if (strcmp(arg, "-dumpmachine") == 0) {
      puts(target);
      return 0;
    } else {
      if (arg[2] != '\0' && strchr("cESsv", arg[1]))
        usage(NULL);
      switch (arg[1]) {
        case 'c':
          if (last > ASSEMBLE)
            last = ASSEMBLE;
          break;
        case 'D':
          arrayaddptr(&stages[PREPROCESS].cmd, "-D");
          arrayaddptr(&stages[PREPROCESS].cmd, nextarg(&argv));
          break;
        case 'E':
          if (last > PREPROCESS)
            last = PREPROCESS;
          break;
        case 'g':
          break;
        case 'I':
          arrayaddptr(&stages[PREPROCESS].cmd, "-I");
          arrayaddptr(&stages[PREPROCESS].cmd, nextarg(&argv));
          break;
        case 'L':
          arrayaddptr(&stages[LINK].cmd, "-L");
          arrayaddptr(&stages[LINK].cmd, nextarg(&argv));
          break;
        case 'l':
          input           = arrayadd(&inputs, sizeof(*input));
          input->name     = nextarg(&argv);
          input->lib      = true;
          input->filetype = OBJ;
          input->stages   = 1 << LINK;
          break;
        case 'O':
          break;
        case 'o':
          output = nextarg(&argv);
          break;
        case 'P':
          arrayaddptr(&stages[PREPROCESS].cmd, "-P");
          break;
        case 'S':
          if (last > CODEGEN)
            last = CODEGEN;
          break;
        case 's':
          arrayaddptr(&stages[LINK].cmd, "-s");
          break;
        case 'U':
          arrayaddptr(&stages[PREPROCESS].cmd, "-U");
          arrayaddptr(&stages[PREPROCESS].cmd, nextarg(&argv));
          break;
        case 'v':
          flags.verbose = true;
          break;
        case 'W':
          if (arg[2] && arg[3] == ',') {
            switch (arg[2]) {
              case 'p':
                cmd = &stages[PREPROCESS].cmd;
                break;
              case 'a':
                cmd = &stages[ASSEMBLE].cmd;
                break;
              case 'l':
                cmd = &stages[LINK].cmd;
                break;
              default:
                usage(NULL);
            }
            for (arg += 4; arg; arg = end ? end + 1 : NULL) {
              end = strchr(arg, ',');
              if (end)
                *end = '\0';
              arrayaddptr(cmd, arg);
            }
          }
          break;
        case 'x':
          arg = nextarg(&argv);
          if (strcmp(arg, "none") == 0)
            filetype = NONE;
          else if (strcmp(arg, "c") == 0)
            filetype = C;
          else if (strcmp(arg, "c-header") == 0)
            filetype = CHDR;
          else if (strcmp(arg, "cpp-output") == 0)
            filetype = CPPOUT;
          else if (strcmp(arg, "qbe") == 0)
            filetype = QBE;
          else if (strcmp(arg, "assembler") == 0)
            filetype = ASM;
          else if (strcmp(arg, "assembler-with-cpp") == 0)
            filetype = ASMPP;
          else
            usage("unknown language '%s'", arg);
          break;
        default:
          usage("unknown option '%s'", arg);
      }
    }
  }

  for (i = 0; i < countof(stages); ++i)
    stages[i].cmdbase = stages[i].cmd.len;
  if (inputs.len == 0)
    usage(NULL);
  if (output) {
    if (strcmp(output, "-") == 0) {
      if (last >= ASSEMBLE)
        usage("cannot write object to stdout");
    } else if (last != LINK && inputs.len > sizeof(*input)) {
      usage(
          "cannot specify -o with multiple input files "
          "without linking"
      );
    }
  }
  arrayforeach(&inputs, input)
  {
    if (!(input->stages & 1 << last))
      continue;
    input->stages &= (1 << last + 1) - 1;
    buildobj(input, output);
  }
  if (last == LINK) {
    if (!output)
      output = "a.out";
    buildexe(inputs.val, inputs.len / sizeof(*input), output);
  }
  return 0;
}
