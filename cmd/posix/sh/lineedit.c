/* -
 * spdx-license-identifier: bsd-3-clause
 *
 * copyright (c) 1993
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
 * may be used to promote products derived from this software
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

#if !FEATURE_SH_HISTEDIT
#ifndef NO_HISTORY
#define NO_HISTORY
#endif
#endif

#include "../../../shared/paths.h"
#include "alias.h"
#include "builtins.h"
#include "error.h"
#include "eval.h"
#include "exec.h"
#include "main.h"
#include "memalloc.h"
#include "mystring.h"
#include "options.h"
#include "output.h"
#include "parser.h"
#include "shell.h"
#include "util.h"
#include "var.h"

#ifndef NO_HISTORY
#include "lineedit.h"

#include <sys/param.h>
#include <sys/stat.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAXHISTLOOPS 4
#define DEFEDITOR    "ed"
#define VTABSIZE     39

extern struct var *vartab[VTABSIZE];

int        sh_history_enabled = 0;
int        displayhist        = 0;
static int savehist           = 0;

static char *fc_replace(const char *, char *, char *);
static int   not_fcnumber(const char *);
static int   str_to_event(const char *, int);

static char *
escape_filename(const char *filename)
{
  size_t      len;
  size_t      i;
  size_t      j;
  char       *escaped;
  const char *special;

  len     = 0;
  special = " \t\n\"'\\$&|;<>()*?[]!{}";
  for (i = 0; filename[i] != '\0'; i++) {
    if (strchr(special, filename[i]) != NULL)
      len += 2;
    else
      len += 1;
  }

  escaped = malloc(len + 1);
  j       = 0;
  for (i = 0; filename[i] != '\0'; i++) {
    if (strchr(special, filename[i]) != NULL) {
      escaped[j++] = '\\';
      escaped[j++] = filename[i];
    } else {
      escaped[j++] = filename[i];
    }
  }
  escaped[j] = '\0';
  return escaped;
}

static char *
unescape_filename(const char *filename)
{
  size_t len;
  char  *unescaped;
  size_t i;
  size_t j;

  len       = strlen(filename);
  unescaped = malloc(len + 1);
  i         = 0;
  j         = 0;
  while (i < len) {
    if (filename[i] == '\\' && i + 1 < len) {
      unescaped[j++] = filename[i + 1];
      i += 2;
    } else {
      unescaped[j++] = filename[i];
      i++;
    }
  }
  unescaped[j] = '\0';
  return unescaped;
}

static void
complete_tildes(const char *word, struct redlineCompletions *lc)
{
  struct passwd *pw;
  char           completed[512];
  const char    *user_prefix;
  size_t         prefix_len;

  user_prefix = word + 1;
  prefix_len  = strlen(user_prefix);

  setpwent();
  while ((pw = getpwent()) != NULL) {
    if (strncmp(pw->pw_name, user_prefix, prefix_len) == 0) {
      snprintf(completed, sizeof(completed), "~%s/", pw->pw_name);
      redlineAddCompletion(lc, completed);
    }
  }
  endpwent();
}

static void
complete_variables(const char *word, struct redlineCompletions *lc)
{
  struct var **vpp;
  struct var  *vp;
  char         name[256];
  char         completed[512];
  const char  *var_prefix;
  size_t       prefix_len;
  char        *eq;
  size_t       name_len;

  var_prefix = word + 1;
  prefix_len = strlen(var_prefix);

  for (vpp = vartab; vpp < vartab + VTABSIZE; vpp++) {
    for (vp = *vpp; vp; vp = vp->next) {
      if (!(vp->flags & VUNSET)) {
        eq = strchr(vp->text, '=');
        if (eq) {
          name_len = eq - vp->text;
          if (name_len < sizeof(name)) {
            memcpy(name, vp->text, name_len);
            name[name_len] = '\0';
            if (strncmp(name, var_prefix, prefix_len) == 0) {
              snprintf(completed, sizeof(completed), "$%s ", name);
              redlineAddCompletion(lc, completed);
            }
          }
        }
      }
    }
  }
}

static const char *
get_histfile(void)
{
  const char *histfile;

  if (!strcmp(histsizeval(), "0"))
    return (NULL);
  histfile = expandstr("${HISTFILE-${HOME-}/.sh_history}");

  if (histfile[0] == '\0')
    return (NULL);
  return (histfile);
}

void
histsave(void)
{
  const char *histfile;

  if (!savehist || (histfile = get_histfile()) == NULL)
    return;
  INTOFF;
  redlineHistorySave(histfile);
  INTON;
}

void
histload(void)
{
  const char *histfile;

  if ((histfile = get_histfile()) == NULL)
    return;
  errno = 0;
  if (redlineHistoryLoad(histfile) != -1 || errno == ENOENT)
    savehist = 1;
}

static void
find_completions_recurse(
    const char                *fs_dir,
    const char                *user_prefix,
    char                     **comps,
    int                        comp_idx,
    int                        comp_count,
    int                        is_cmd,
    struct redlineCompletions *lc
)
{
  DIR           *dir;
  struct dirent *de;
  struct stat    st;
  char           next_fs[4096];
  char           next_user[4096];
  size_t         len;

  if (comp_idx == comp_count) {
    /* reached the end of components, check if the path exists */
    if (stat(fs_dir, &st) == 0) {
      if (is_cmd && !S_ISDIR(st.st_mode) && access(fs_dir, X_OK) != 0) {
        return;
      }
      snprintf(next_user, sizeof(next_user), "%s", user_prefix);
      if (S_ISDIR(st.st_mode)) {
        /* if it is a directory and doesnt end with
 * slash, add slash */
        len = strlen(next_user);
        if (len > 0 && next_user[len - 1] != '/') {
          strlcat(next_user, "/", sizeof(next_user));
        }
      } else {
        /* file, add space */
        strlcat(next_user, " ", sizeof(next_user));
      }
      redlineAddCompletion(lc, next_user);
    }
    return;
  }

  if (strcmp(comps[comp_idx], ".") == 0 || strcmp(comps[comp_idx], "..") == 0) {
    /* construct filesystem path */
    if (strcmp(fs_dir, "/") == 0) {
      snprintf(next_fs, sizeof(next_fs), "/%s", comps[comp_idx]);
    } else if (strcmp(fs_dir, ".") == 0) {
      snprintf(next_fs, sizeof(next_fs), "./%s", comps[comp_idx]);
    } else {
      snprintf(next_fs, sizeof(next_fs), "%s/%s", fs_dir, comps[comp_idx]);
    }

    /* construct user visible path */
    if (strcmp(user_prefix, "/") == 0) {
      snprintf(next_user, sizeof(next_user), "/%s", comps[comp_idx]);
    } else if (strcmp(user_prefix, "~/") == 0) {
      snprintf(next_user, sizeof(next_user), "~/%s", comps[comp_idx]);
    } else if (user_prefix[0] == '\0') {
      snprintf(next_user, sizeof(next_user), "%s", comps[comp_idx]);
    } else {
      len = strlen(user_prefix);
      if (user_prefix[len - 1] == '/') {
        snprintf(next_user, sizeof(next_user), "%s%s", user_prefix, comps[comp_idx]);
      } else {
        snprintf(next_user, sizeof(next_user), "%s/%s", user_prefix, comps[comp_idx]);
      }
    }

    find_completions_recurse(next_fs, next_user, comps, comp_idx + 1, comp_count, is_cmd, lc);
    return;
  }

  dir = opendir(fs_dir);
  if (!dir)
    return;

  while ((de = readdir(dir)) != NULL) {
    if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
      continue;

    if (strncmp(de->d_name, comps[comp_idx], strlen(comps[comp_idx])) == 0) {
      char *escaped_name = escape_filename(de->d_name);
      /* construct filesystem path */
      if (strcmp(fs_dir, "/") == 0) {
        snprintf(next_fs, sizeof(next_fs), "/%s", de->d_name);
      } else if (strcmp(fs_dir, ".") == 0) {
        snprintf(next_fs, sizeof(next_fs), "./%s", de->d_name);
      } else {
        snprintf(next_fs, sizeof(next_fs), "%s/%s", fs_dir, de->d_name);
      }

      /* middle components must be directories */
      if (comp_idx < comp_count - 1) {
        if (stat(next_fs, &st) != 0 || !S_ISDIR(st.st_mode)) {
          free(escaped_name);
          continue;
        }
      }

      /* construct user-visible path */
      if (strcmp(user_prefix, "/") == 0) {
        snprintf(next_user, sizeof(next_user), "/%s", escaped_name);
      } else if (strcmp(user_prefix, "~/") == 0) {
        snprintf(next_user, sizeof(next_user), "~/%s", escaped_name);
      } else if (user_prefix[0] == '\0') {
        snprintf(next_user, sizeof(next_user), "%s", escaped_name);
      } else {
        len = strlen(user_prefix);
        if (user_prefix[len - 1] == '/') {
          snprintf(next_user, sizeof(next_user), "%s%s", user_prefix, escaped_name);
        } else {
          snprintf(next_user, sizeof(next_user), "%s/%s", user_prefix, escaped_name);
        }
      }

      find_completions_recurse(next_fs, next_user, comps, comp_idx + 1, comp_count, is_cmd, lc);
      free(escaped_name);
    }
  }
  closedir(dir);
}

/* complete matching files in the filesystem */
static void
complete_files(const char *word, int is_cmd, struct redlineCompletions *lc)
{
  char       *path_to_split;
  const char *fs_dir;
  const char *user_prefix;
  const char *home;
  char       *path_copy;
  char       *p;
  char       *comps[128];
  int         comp_count;
  char       *unescaped_word;

  path_to_split  = NULL;
  fs_dir         = ".";
  user_prefix    = "";
  home           = getenv("HOME");
  comp_count     = 0;
  unescaped_word = unescape_filename(word);

  if (unescaped_word[0] == '~') {
    if (unescaped_word[1] == '/' || unescaped_word[1] == '\0') {
      fs_dir        = home ? home : "/";
      user_prefix   = "~/";
      path_to_split = (unescaped_word[1] == '\0') ? "" : (char *)(unescaped_word + 2);
    } else {
      /* ~username is not supported for abbreviation, fallback
 * to home */
      fs_dir        = home ? home : "/";
      user_prefix   = "~/";
      path_to_split = (char *)(unescaped_word + 1);
    }
  } else if (unescaped_word[0] == '/') {
    fs_dir        = "/";
    user_prefix   = "/";
    path_to_split = (char *)(unescaped_word + 1);
  } else {
    fs_dir        = ".";
    user_prefix   = "";
    path_to_split = (char *)unescaped_word;
  }

  path_copy = estrdup(path_to_split);
  p         = path_copy;
  if (*p != '\0') {
    comps[comp_count++] = p;
    while (*p != '\0') {
      if (*p == '/') {
        *p = '\0';
        p++;
        while (*p == '/')
          p++;
        if (*p == '\0') {
          comps[comp_count++] = p;
          break;
        }
        comps[comp_count++] = p;
      } else {
        p++;
      }
    }
  } else {
    /* empty path_to_split (e.g. exactly ~ or exactly / or empty
 * word) */
    comps[comp_count++] = p;
  }

  find_completions_recurse(fs_dir, user_prefix, comps, 0, comp_count, is_cmd, lc);
  free(path_copy);
  free(unescaped_word);
}

/* complete matching executable commands and builtins */
static void
complete_commands(const char *word, struct redlineCompletions *lc)
{
  char                *free_path = NULL, *path;
  const char          *dirname;
  struct cmdentry      e;
  const struct alias  *ap = NULL;
  const unsigned char *bp = builtincmd;
  const void          *a  = NULL;
  DIR                 *dir;
  struct dirent       *entry;
  int                  dfd;
  struct stat          statb;
  char                 completed[512];

  while ((ap = iteralias(ap)) != NULL) {
    if (strncmp(ap->name, word, strlen(word)) == 0) {
      snprintf(completed, sizeof(completed), "%s ", ap->name);
      redlineAddCompletion(lc, completed);
    }
  }

  while (bp && *bp != 0) {
    if (strncmp((const char *)(bp + 2), word, strlen(word)) == 0) {
      snprintf(completed, sizeof(completed), "%.*s ", (int)bp[0], bp + 2);
      redlineAddCompletion(lc, completed);
    }
    bp += 2 + bp[0];
  }

  while ((a = itercmd(a, &e)) != NULL) {
    if (e.cmdtype == CMDFUNCTION && strncmp(e.cmdname, word, strlen(word)) == 0) {
      snprintf(completed, sizeof(completed), "%s ", e.cmdname);
      redlineAddCompletion(lc, completed);
    }
  }

  path = pathval();
  if (path) {
    free_path = path = estrdup(path);
    while ((dirname = strsep(&path, ":")) != NULL) {
      dir = opendir(dirname[0] == '\0' ? "." : dirname);
      if (dir == NULL)
        continue;
      dfd = dirfd(dir);
      if (dfd == -1) {
        closedir(dir);
        continue;
      }
      while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, word, strlen(word)) != 0)
          continue;
        if (entry->d_type == DT_UNKNOWN || entry->d_type == DT_LNK) {
          if (fstatat(dfd, entry->d_name, &statb, 0) == -1)
            continue;
          if (!S_ISREG(statb.st_mode))
            continue;
        } else if (entry->d_type != DT_REG) {
          continue;
        }
        snprintf(completed, sizeof(completed), "%s ", entry->d_name);
        redlineAddCompletion(lc, completed);
      }
      closedir(dir);
    }
    free(free_path);
  }
}

/* main completion callback called by redline library */
static void
sh_complete_callback(const char *buf, struct redlineCompletions *lc)
{
  const char               *word;
  int                       start;
  int                       is_cmd;
  int                       p;
  struct redlineCompletions temp_lc;
  char                      line_prefix[4096];
  char                      full_completion[4096];
  size_t                    i;

  start = strlen(buf);
  while (start > 0) {
    char c = buf[start - 1];
    if (c == ' ' || c == '\t' || c == '\n' || c == '"' || c == '\'' || c == '`' || c == '@'
        || c == '$' || c == '>' || c == '<' || c == '=' || c == ';' || c == '|' || c == '&'
        || c == '{' || c == '(') {
      if (start > 1 && buf[start - 2] == '\\') {
        start -= 2;
        continue;
      }
      break;
    } else if (c == '\\') {
      break;
    }
    start--;
  }
  word = buf + start;

  is_cmd = 0;
  if (start == 0) {
    is_cmd = 1;
  } else {
    p = start;
    while (p > 0 && (buf[p - 1] == ' ' || buf[p - 1] == '\t'))
      p--;
    if (p == 0 || strchr(";&|({`\n", buf[p - 1]) != NULL)
      is_cmd = 1;
  }

  if (start >= (int)sizeof(line_prefix))
    return;
  snprintf(line_prefix, sizeof(line_prefix), "%.*s", start, buf);

  temp_lc.len  = 0;
  temp_lc.cvec = NULL;

  if (word[0] == '$') {
    complete_variables(word, &temp_lc);
  } else if (word[0] == '~' && strchr(word, '/') == NULL) {
    complete_tildes(word, &temp_lc);
  } else if (is_cmd && strchr(word, '/') == NULL && word[0] != '~' && word[0] != '.') {
    complete_commands(word, &temp_lc);
  } else {
    complete_files(word, is_cmd, &temp_lc);
  }

  for (i = 0; i < temp_lc.len; i++) {
    snprintf(full_completion, sizeof(full_completion), "%s%s", line_prefix, temp_lc.cvec[i]);
    redlineAddCompletion(lc, full_completion);
  }

  for (i = 0; i < temp_lc.len; i++) {
    free(temp_lc.cvec[i]);
  }
  free(temp_lc.cvec);
}

void
histedit(void)
{
  sh_history_enabled = (iflag && (Eflag || Vflag));
  if (sh_history_enabled) {
    redlineSetCompletionCallback(sh_complete_callback);
    redlineSetMultiLine(1);
  }
}

void
sethistsize(const char *hs)
{
  int histsize;

  if (hs == NULL || !is_number(hs))
    histsize = 128;
  else
    histsize = atoi(hs);
  redlineHistorySetMaxLen(histsize);
}

void
setterm(const char *term __unused)
{
}

int
histcmd(int argc, char **argv __unused)
{
  const char    *editor = NULL;
  int            lflg = 0, nflg = 0, rflg = 0, sflg = 0;
  int            i;
  const char    *firststr, *laststr;
  int            first, last;
  char          *pat = NULL, *repl = NULL;
  static int     active = 0;
  struct jmploc  jmploc;
  struct jmploc *savehandler;
  char           editfilestr[PATH_MAX];
  char *volatile editfile;
  FILE *efp = NULL;
  int   dir;

  if (redlineHistoryLen() == 0)
    error("history not active");

  if (argc == 1)
    error("missing history argument");

  while (not_fcnumber(*argptr))
    do {
      switch (nextopt("e:lnrs")) {
        case 'e':
          editor = shoptarg;
          break;
        case 'l':
          lflg = 1;
          break;
        case 'n':
          nflg = 1;
          break;
        case 'r':
          rflg = 1;
          break;
        case 's':
          sflg = 1;
          break;
        case '\0':
          goto operands;
      }
    } while (nextopt_optptr != NULL);
operands:
  savehandler = handler;
  if (lflg == 0 || editor || sflg) {
    lflg     = 0;
    editfile = NULL;
    if (setjmp(jmploc.loc)) {
      active = 0;
      if (editfile)
        unlink(editfile);
      handler = savehandler;
      longjmp(handler->loc, 1);
    }
    handler = &jmploc;
    if (++active > MAXHISTLOOPS) {
      active      = 0;
      displayhist = 0;
      error("called recursively too many times");
    }
    if (sflg == 0) {
      if (editor == NULL && (editor = bltinlookup("FCEDIT", 1)) == NULL
          && (editor = bltinlookup("EDITOR", 1)) == NULL)
        editor = DEFEDITOR;
      if (editor[0] == '-' && editor[1] == '\0') {
        sflg   = 1;
        editor = NULL;
      }
    }
  }

  if (lflg == 0 && *argptr != NULL && ((repl = strchr(*argptr, '=')) != NULL)) {
    pat     = *argptr;
    *repl++ = '\0';
    argptr++;
  }

  if (*argptr == NULL) {
    firststr = lflg ? "-16" : "-1";
    laststr  = "-1";
  } else if (argptr[1] == NULL) {
    firststr = argptr[0];
    laststr  = lflg ? "-1" : argptr[0];
  } else if (argptr[2] == NULL) {
    firststr = argptr[0];
    laststr  = argptr[1];
  } else {
    error("too many arguments");
  }

  first = str_to_event(firststr, 0);
  last  = str_to_event(laststr, 1);

  if (rflg) {
    i     = last;
    last  = first;
    first = i;
  }

  if (editor) {
    int fd;
    INTOFF;
    sprintf(editfilestr, "%s/_shXXXXXX", ARUU_PATH_TMP);
    if ((fd = mkstemp(editfilestr)) < 0)
      error("can't create temporary file %s", editfile);
    editfile = editfilestr;
    if ((efp = fdopen(fd, "w")) == NULL) {
      close(fd);
      error("Out of space");
    }
  }

  dir = (first <= last) ? 1 : -1;
  for (i = first;; i += dir) {
    if (i < 1 || i > redlineHistoryLen())
      continue;
    const char *hstr = redlineHistoryGet(i - 1);
    if (lflg) {
      if (!nflg)
        out1fmt("%5d ", i);
      out1fmt("%s\n", hstr);
    } else {
      const char *s = pat ? fc_replace(hstr, pat, repl) : hstr;
      if (sflg) {
        if (displayhist) {
          out2fmt_flush("%s\n", s);
        }
        evalstring(s, 0);
        if (displayhist) {
          redlineHistoryAdd(s);
        }
      } else {
        fprintf(efp, "%s\n", s);
      }
    }
    if (i == last)
      break;
  }

  if (editor) {
    char *editcmd;

    fclose(efp);
    INTON;
    editcmd = stalloc(strlen(editor) + strlen(editfile) + 2);
    sprintf(editcmd, "%s %s", editor, editfile);
    evalstring(editcmd, 0);
    readcmdfile(editfile, 0);
    unlink(editfile);
  }

  if (lflg == 0 && active > 0)
    --active;
  if (displayhist)
    displayhist = 0;
  handler = savehandler;
  return 0;
}

static char *
fc_replace(const char *s, char *p, char *r)
{
  char *dest;
  int   plen = strlen(p);

  STARTSTACKSTR(dest);
  while (*s) {
    if (*s == *p && strncmp(s, p, plen) == 0) {
      STPUTS(r, dest);
      s += plen;
      *p = '\0';
    } else
      STPUTC(*s++, dest);
  }
  STPUTC('\0', dest);
  dest = grabstackstr(dest);

  return (dest);
}

static int
not_fcnumber(const char *s)
{
  if (s == NULL)
    return (0);
  if (*s == '-')
    s++;
  return (!is_number(s));
}

static int
str_to_event(const char *str, int last_fallback)
{
  int         relative = 0;
  int         i;
  const char *s = str;

  if (s == NULL) {
    return last_fallback ? redlineHistoryLen()
                         : (redlineHistoryLen() > 16 ? redlineHistoryLen() - 15 : 1);
  }

  switch (*s) {
    case '-':
      relative = 1;
      s++;
      break;
    case '+':
      s++;
      break;
  }

  if (is_number(s)) {
    i = atoi(s);
    if (relative) {
      return redlineHistoryLen() - i;
    }
    return i;
  }

  for (i = redlineHistoryLen() - 1; i >= 0; i--) {
    if (strncmp(redlineHistoryGet(i), str, strlen(str)) == 0) {
      return i + 1;
    }
  }
  error("history pattern not found: %s", str);
  return 0;
}

int
bindcmd(int argc __unused, char **argv __unused)
{
  error("not compiled with line editing support");
  return (0);
}

#else

int
histcmd(int argc __unused, char **argv __unused)
{
  error("not compiled with history support");
  return (0);
}

int
bindcmd(int argc __unused, char **argv __unused)
{
  error("not compiled with line editing support");
  return (0);
}
#endif
