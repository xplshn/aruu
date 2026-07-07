#include "ninqu.h"
#include "wexec.h"

#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int jobs_n = 1;
int summary_mode;
int failed_any;

/* child _exit on error so parent never sees a stale pid */
pid_t
spawn_inst(struct Inst *inst, struct StrList *argv)
{
  pid_t pid = fork();
  if (pid < 0)
    eprintf("fork:");
  if (pid != 0)
    return pid;

  if (inst->workdir[0] && chdir(inst->workdir) != 0)
    eprintf("chdir %s:", inst->workdir);
  if (inst->redirect) {
    FILE *f = fopen(inst->out, "w");
    if (!f)
      eprintf("open %s:", inst->out);
    if (dup2(fileno(f), STDOUT_FILENO) < 0)
      _exit(127);
    fclose(f);
  }
  child_execvp(sl_argv(argv));
}

/* parent waits on supervisor so pipeline has one exit status */
pid_t
spawn_pipe(struct Inst *inst)
{
  pid_t pid = fork();
  if (pid < 0)
    eprintf("fork:");
  if (pid != 0)
    return pid;

  if (inst->workdir[0] && chdir(inst->workdir) != 0)
    eprintf("chdir %s:", inst->workdir);

  {
    int k, prev_read = -1;
    for (k = 0; k < inst->cmd.nstages; k++) {
      int   p[2] = {-1, -1};
      pid_t sp;
      if (k < inst->cmd.nstages - 1) {
        if (pipe(p) != 0)
          _exit(127);
      }
      sp = fork();
      if (sp < 0)
        _exit(127);
      if (sp == 0) {
        if (prev_read >= 0) {
          if (dup2(prev_read, STDIN_FILENO) < 0)
            _exit(127);
          close(prev_read);
        }
        if (k < inst->cmd.nstages - 1) {
          close(p[0]);
          if (dup2(p[1], STDOUT_FILENO) < 0)
            _exit(127);
          close(p[1]);
        }
        child_execvp(sl_argv(inst->cmd.stages[k]));
      }
      if (prev_read >= 0)
        close(prev_read);
      if (k < inst->cmd.nstages - 1) {
        close(p[1]);
        prev_read = p[0];
      }
    }
    for (k = 0; k < inst->cmd.nstages; k++)
      wait(NULL);
    _exit(0);
  }
}

/* mkdir -p the directory an instance writes into, so a rule (out) can point at a path that does not
 * exist yet */
void
mk_out_dir(const char *out)
{
  char  tmp[PATH_MAX];
  char *dir;

  if (!out || !out[0])
    return;
  estrlcpy(tmp, out, sizeof tmp);
  dir = dirname(tmp);
  if (strcmp(dir, ".") == 0 || strcmp(dir, "/") == 0)
    return;
  mkdirp(dir, 0777, 0777);
}

/* flush before child runs so announcement appears before its output */
void
announce_inst(struct Inst *inst)
{
  mk_out_dir(inst->out);
  printf("  %-10s %s\n", rules[inst->rule_idx].name, inst->out[0] ? inst->out : "(phony)");
  fflush(stdout);
}

/* exits on failure since a missing producer is unrecoverable */
void
run_inst_sync(int idx)
{
  struct Inst *inst = &insts[idx];
  pid_t        pid;
  int          status;

  if (!inst_stale(inst))
    return;
  announce_inst(inst);
  pid = inst->cmd.is_pipe ? spawn_pipe(inst) : spawn_inst(inst, &inst->cmd.argv);
  if (waitpid(pid, &status, 0) < 0)
    eprintf("waitpid:");
  if (!(WIFEXITED(status) && WEXITSTATUS(status) == 0))
    eprintf("manifest: producer failed (rule %s)\n", rules[inst->rule_idx].name);
}

/* all instances in the batch are at the same kahn level */
void
run_batch(int *idxs, int n)
{
  int    next = 0, running = 0;
  pid_t *pids = emalloc((size_t)(n > 0 ? n : 1) * sizeof *pids);
  int   *slot = emalloc((size_t)(n > 0 ? n : 1) * sizeof *slot);

  while (next < n || running > 0) {
    while (running < jobs_n && next < n) {
      struct Inst *inst = &insts[idxs[next]];
      pid_t        pid;

      if (!inst_stale(inst)) {
        next++;
        continue;
      }
      announce_inst(inst);

      pid           = inst->cmd.is_pipe ? spawn_pipe(inst) : spawn_inst(inst, &inst->cmd.argv);
      pids[running] = pid;
      slot[running] = idxs[next];
      running++;
      next++;
    }
    if (running > 0) {
      int   status;
      pid_t done = wait(&status);
      int   i;
      if (done < 0)
        eprintf("wait:");
      for (i = 0; i < running; i++) {
        if (pids[i] == done) {
          if (!(WIFEXITED(status) && WEXITSTATUS(status) == 0)) {
            char *cmd = sl_join(&insts[slot[i]].cmd.argv);
            weprintf("rule %s failed: %s\n", rules[insts[slot[i]].rule_idx].name, cmd);
            free(cmd);
            failed_any = 1;
          }
          pids[i] = pids[running - 1];
          slot[i] = slot[running - 1];
          running--;
          break;
        }
      }
    }
  }
  free(pids);
  free(slot);
}

/* run transitive deps in order so a producer does not archive uncompiled files */
void
run_inst_with_deps(int idx)
{
  struct Inst *inst = &insts[idx];
  int          i;

  for (i = 0; i < inst->dep_rule_names.n; i++) {
    struct Rule *dep = rule_find(inst->dep_rule_names.v[i]);
    int          k;
    if (!dep)
      continue;
    for (k = 0; k < dep->n_inst; k++)
      dep_push(inst, dep->inst_idx[k]);
  }
  for (i = 0; i < inst->n_dep; i++)
    run_inst_with_deps(inst->dep_inst[i]);
  run_inst_sync(idx);
}

/* run producer for path only if path does not exist */
void
materialize_if_missing(const char *path)
{
  struct Rule *r;

  if (file_exists(path))
    return;
  r = rule_find_output(path);
  if (!r)
    r = rule_find_produces(base_of(path));
  if (!r)
    return;
  expand_rule(r->name);
  if (r->n_inst > 0) {
    int k;
    for (k = 0; k < r->n_inst; k++)
      run_inst_with_deps(r->inst_idx[k]);
  }
}

/* used by (set NAME (capture (exec ...))) at parse time */
char *
capture_argv(struct StrList *argv)
{
  int    fds[2];
  pid_t  pid;
  char  *buf;
  size_t cap, len;
  int    status;

  if (argv->n == 0)
    eprintf("capture: empty argv\n");
  if (pipe(fds) != 0)
    eprintf("capture: pipe:");

  pid = fork();
  if (pid < 0)
    eprintf("capture: fork:");
  if (pid == 0) {
    close(fds[0]);
    if (dup2(fds[1], STDOUT_FILENO) < 0)
      _exit(127);
    close(fds[1]);
    child_execvp(sl_argv(argv));
  }
  close(fds[1]);

  cap = 4096;
  len = 0;
  buf = emalloc(cap);
  for (;;) {
    ssize_t n;
    if (len + 4096 > cap) {
      cap *= 2;
      buf = erealloc(buf, cap);
    }
    n = read(fds[0], buf + len, cap - len - 1);
    if (n < 0) {
      if (errno == EINTR)
        continue;
      eprintf("capture: read:");
    }
    if (n == 0)
      break;
    len += (size_t)n;
  }
  buf[len] = '\0';
  close(fds[0]);

  while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
    buf[--len] = '\0';

  if (waitpid(pid, &status, 0) < 0)
    eprintf("capture: waitpid:");
  if (!(WIFEXITED(status) && WEXITSTATUS(status) == 0))
    eprintf("capture: command failed\n");
  return buf;
}
