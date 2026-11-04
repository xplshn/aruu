#include "ninqu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ninja treats '$', ':' and ' ' as special in a build statement path
 * (outputs, inputs, deps), so escape them with a leading '$'. this is
 * ninja file syntax, separate from the shell quoting below */
static char *
ninja_path_escape(const char *s)
{
  size_t i, j = 0;
  size_t len = strlen(s);
  char  *out = emalloc(len * 2 + 1);

  for (i = 0; i < len; i++) {
    if (s[i] == '$' || s[i] == ':' || s[i] == ' ')
      out[j++] = '$';
    out[j++] = s[i];
  }
  out[j] = '\0';
  return out;
}

/* argv is fully expanded at parse time, so the ninja command is a
 * plain literal, never $in/$out. write one word as a single shell
 * quoted token, and double each '$' so ninja preprocessing hands the
 * shell exactly one */
static void
fprint_shell_word_plain(FILE *f, const char *s)
{
  fputc('\'', f);
  for (; *s; s++) {
    if (*s == '\'')
      fputs("'\\''", f);
    else if (*s == '$')
      fputs("$$", f);
    else
      fputc(*s, f);
  }
  fputc('\'', f);
}

/* ninja ends a "command = ..." value at the physical line end and $\n
 * strips the newline, and \n inside posix single quotes stays two
 * characters. so emit a command substitution running the word through
 * `printf '%b'`, which expands escapes at run time. the argument is
 * single-quoted with backslashes doubled, and the `$` of the opening
 * `$(` is doubled so ninja leaves it for the shell */
static void
fprint_shell_word(FILE *f, const char *s)
{
  const char *p;
  int         has_nl = 0;

  for (p = s; *p; p++) {
    if (*p == '\n') {
      has_nl = 1;
      break;
    }
  }

  if (!has_nl) {
    fprint_shell_word_plain(f, s);
    return;
  }

  /* wrapped in double quotes so the shell treats the substitutions
 * result as one word instead of splitting it on whitespace, which
 * is exactly what an unquoted $(...) would do to a multi-line
 * result */
  fputs("\"$$(printf '%b' '", f);
  for (; *s; s++) {
    if (*s == '\'')
      fputs("'\\''", f);
    else if (*s == '$')
      fputs("$$", f);
    else if (*s == '\\')
      fputs("\\\\", f);
    else if (*s == '\n')
      fputs("\\n", f);
    else
      fputc(*s, f);
  }
  fputs("')\"", f);
}

/* an instance with no (out) has no on-disk path (a phony or pure
 * grouping rule), but ninja needs a target name for its build
 * statement, so one is made from the rule name and instance index.
 * it never exists as a file, so ninja always re-runs it, matching the
 * "always stale" behaviour inst_stale() gives phony instances. the
 * name has no '/', so ninja never creates a parent directory for it */
static const char *
inst_target(struct Inst *inst, int idx, char *buf, size_t bufsz)
{
  if (inst->out[0])
    return inst->out;
  snprintf(buf, bufsz, "__ninqu_phony__%s_%d", rules[inst->rule_idx].name, idx);
  return buf;
}

/* does this instance look like "cc ... -c ... -o foo.o"? if so it is a
 * real compile step, and ninja can track header dependencies through
 * a gcc-style depfile instead of only the coarse order-only edges
 * dep_inst gives. the check is on -c and a .o output, not the
 * compiler name, so any CC the manifest resolved works as long as it
 * understands -MMD -MF */
static int
is_cc_compile(struct Inst *inst)
{
  static const char *const compilers[] = {"cc", "gcc", "clang", "g++", "c++", NULL};
  const char              *prog, *slash;
  size_t                   len;
  int                      k, has_c;

  if (inst->phony || !inst->out[0] || inst->cmd.is_pipe || inst->cmd.argv.n < 2)
    return 0;

  len = strlen(inst->out);
  if (len < 2 || strcmp(inst->out + len - 2, ".o") != 0)
    return 0;

  prog  = inst->cmd.argv.v[0];
  slash = strrchr(prog, '/');
  if (slash)
    prog = slash + 1;

  for (k = 0; compilers[k]; k++)
    if (strcmp(prog, compilers[k]) == 0)
      break;
  if (!compilers[k])
    return 0;

  for (k = 1, has_c = 0; k < inst->cmd.argv.n; k++)
    if (strcmp(inst->cmd.argv.v[k], "-c") == 0)
      has_c = 1;

  return has_c;
}

/* the command a rule runs: an optional workdir cd, the argv or
 * piped stages shell-quoted word by word, and an optional stdout
 * redirect into (out). mirrors spawn_inst()/spawn_pipe() exactly,
 * just written out as text instead of forked and execd. a compile
 * step (is_cc_compile()) gets "-MMD -MF <out>.d" spliced in right
 * after the compiler name, paired with the depfile/deps lines
 * emit_ninja() writes into that instances rule block */
static void
emit_command(FILE *f, struct Inst *inst)
{
  int is_cc = is_cc_compile(inst);
  int i;

  if (inst->workdir[0]) {
    fputs("cd ", f);
    fprint_shell_word(f, inst->workdir);
    fputs(" && ", f);
  }

  if (inst->cmd.is_pipe) {
    for (i = 0; i < inst->cmd.nstages; i++) {
      int k;
      if (i > 0)
        fputs(" | ", f);
      for (k = 0; k < inst->cmd.stages[i]->n; k++) {
        if (k > 0)
          fputc(' ', f);
        fprint_shell_word(f, inst->cmd.stages[i]->v[k]);
      }
    }
  } else {
    for (i = 0; i < inst->cmd.argv.n; i++) {
      if (i > 0)
        fputc(' ', f);
      fprint_shell_word(f, inst->cmd.argv.v[i]);
      if (i == 0 && is_cc) {
        fputs(" -MMD -MF ", f);
        fprint_shell_word(f, inst->out);
        fputs(".d", f);
      }
    }
  }

  if (inst->redirect) {
    fputs(" > ", f);
    fprint_shell_word(f, inst->out);
  }
}

/* write every instance in insts[] (wanted targets plus their
 * transitive deps) out as a build.ninja: one rule and one build
 * statement per instance, since ninqu already resolved each to a
 * literal command line. dep_inst edges become order-only ("||"),
 * because inst_stale() only consults (in)/(stale_extra), so this
 * keeps ninja's mtime check in agreement with ninqu's
 *
 * ninja only knows build outputs, not ninqu's rule/group/meta names,
 * so `ninja LIBUTIL` has nothing to resolve. wanted holds the words
 * ninqu was invoked with, and one phony alias per word makes each
 * buildable. with several targets in one invocation every alias
 * points at the whole combined closure, which is slightly
 * conservative but fine for the makefile's one-target use */
void
emit_ninja(const char *path, struct StrList *wanted)
{
  FILE *f = fopen(path, "w");
  int   i, k;

  if (!f)
    eprintf("emit_ninja: open %s:", path);

  fputs("# autogenerated by `ninqu -G ninja`, do not edit\n", f);
  fputs("ninja_required_version = 1.3\n\n", f);

  for (i = 0; i < ninsts; i++) {
    struct Inst *inst = &insts[i];
    fprintf(f, "rule r%d\n", i);
    fprintf(
        f,
        "  description = %s %s\n",
        rules[inst->rule_idx].name,
        inst->out[0] ? inst->out : "(phony)"
    );
    if (is_cc_compile(inst)) {
      char *esc = ninja_path_escape(inst->out);
      fprintf(f, "  depfile = %s.d\n", esc);
      fputs("  deps = gcc\n", f);
      free(esc);
    }
    fputs("  command = ", f);
    emit_command(f, inst);
    fputs("\n\n", f);
  }

  for (i = 0; i < ninsts; i++) {
    struct Inst *inst = &insts[i];
    char         buf[1024];
    const char  *out = inst_target(inst, i, buf, sizeof buf);
    char        *esc = ninja_path_escape(out);

    fprintf(f, "build %s: r%d", esc, i);
    free(esc);

    for (k = 0; k < inst->in.n; k++) {
      esc = ninja_path_escape(inst->in.v[k]);
      fprintf(f, " %s", esc);
      free(esc);
    }
    for (k = 0; k < inst->stale_extra.n; k++) {
      esc = ninja_path_escape(inst->stale_extra.v[k]);
      fprintf(f, " %s", esc);
      free(esc);
    }

    if (inst->n_dep > 0) {
      int printed_bar = 0;
      for (k = 0; k < inst->n_dep; k++) {
        struct Inst *dep = &insts[inst->dep_inst[k]];
        char         depbuf[1024];
        const char  *depout = inst_target(dep, inst->dep_inst[k], depbuf, sizeof depbuf);
        /* a rule ref that is also a real (dep ...) input already
 * forces both order and staleness, listing it again as
 * order-only is pure noise */
        if (sl_has(&inst->in, depout) || sl_has(&inst->stale_extra, depout))
          continue;
        if (!printed_bar) {
          fputs(" ||", f);
          printed_bar = 1;
        }
        esc = ninja_path_escape(depout);
        fprintf(f, " %s", esc);
        free(esc);
      }
    }
    fputc('\n', f);
  }

  fputs("\ndefault", f);
  for (i = 0; i < ninsts; i++) {
    struct Inst *inst = &insts[i];
    char         buf[1024];
    const char  *out = inst_target(inst, i, buf, sizeof buf);
    char        *esc = ninja_path_escape(out);
    fprintf(f, " %s", esc);
    free(esc);
  }
  fputc('\n', f);

  for (i = 0; i < wanted->n; i++) {
    const char *name = wanted->v[i];
    int         dup  = 0;

    for (k = 0; k < ninsts && !dup; k++) {
      char        buf[1024];
      const char *out = inst_target(&insts[k], k, buf, sizeof buf);
      if (strcmp(out, name) == 0)
        dup = 1;
    }
    if (dup)
      continue;

    {
      char *esc = ninja_path_escape(name);
      fprintf(f, "build %s: phony", esc);
      free(esc);
    }
    for (k = 0; k < ninsts; k++) {
      char        buf[1024];
      const char *out = inst_target(&insts[k], k, buf, sizeof buf);
      char       *esc = ninja_path_escape(out);
      fprintf(f, " %s", esc);
      free(esc);
    }
    fputc('\n', f);
  }

  if (fshut(f, path))
    eprintf("emit_ninja: %s:", path);
  printf("  GEN        %s\n", path);
}
