#if !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE
#endif
#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <sys/types.h>

#include "arg.h"
#include "util.h"

/* grow a struct-array when full, n/cap doubles each time */
#define GROW(arr, n, cap, init)                                                                    \
  do {                                                                                             \
    if ((n) >= (cap)) {                                                                            \
      (cap) = (cap) ? (cap) * 2 : (init);                                                          \
      (arr) = erealloc((arr), (size_t)(cap) * sizeof *(arr));                                      \
    }                                                                                              \
  } while (0)

struct StrList {
  char **v;
  int    n;
  int    cap;
};

void        sl_push(struct StrList *sl, const char *s);
int         sl_has(struct StrList *sl, const char *s);
void        sl_split(struct StrList *sl, const char *s);
char       *sl_join(struct StrList *sl);
char      **sl_argv(struct StrList *sl);
const char *base_of(const char *path);
void        child_execvp(char **av) __attribute__((noreturn));

struct Kv {
  char *key;
  char *val;
};

struct KvStore {
  struct Kv *v;
  int        n, cap;
};

struct Map {
  char           name[128];
  struct KvStore entries;
};

char       *kv_get(const char *key);
const char *kv_get_or(const char *key, const char *def);
void        kv_set(const char *key, const char *val);
void        kv_set_manifest(const char *key, const char *val);
void        kv_set_cli(const char *key, const char *val);
char       *kv_expand(const char *s);
char       *kv_get_raw(struct KvStore *s, const char *key);
void        kv_set_raw(struct KvStore *s, const char *key, const char *val);
struct Map *map_find(const char *name);
struct Map *map_new(const char *name);

extern struct KvStore  kv;
extern struct KvStore *local_overlay;
extern struct StrList  kv_locked;
extern struct Map     *maps;
extern int             nmaps, mapscap;

enum SKind { S_ATOM, S_LIST };

struct SNode {
  enum SKind     kind;
  char          *atom;
  struct SNode **kids;
  int            nkids;
  int            line;
};

struct SLex {
  const char *src;
  size_t      pos;
  int         line;
};

struct SNode *snode_atom(const char *s, int line);
struct SNode *snode_list(int line);
void          snode_push(struct SNode *list, struct SNode *kid);
void          snode_free(struct SNode *n);
struct SNode *snode_clone(struct SNode *n);
struct SNode *snode_parse(struct SLex *lx);
int           s_is_list(struct SNode *n);
const char   *s_head(struct SNode *n);
char         *subst_tmpl(const char *s, struct StrList *params, char **vals);
void          snode_subst(struct SNode *n, struct StrList *params, char **vals);
void          lex_swallow_dollar(struct SLex *lx, char *buf, size_t *blen, size_t bsz);
void          lex_string(struct SLex *lx, char *buf, size_t bsz);
const char   *find_dollar_close(const char *inner);

/* gate: (not x) | (and x...) | (or x...) | bare var */
enum GKind { G_TRUE, G_VAR, G_NOT, G_AND, G_OR };

struct Gate {
  enum GKind    kind;
  char         *var;
  struct Gate **kids;
  int           nkids;
};

struct Gate *gate_new(enum GKind kind);
void         gate_free(struct Gate *g);
struct Gate *gate_parse(struct SNode *expr);
int          gate_has_dyn(struct Gate *g);
int          gate_eval(struct Gate *g);
struct Gate *gate_materialize(struct Gate *g);

struct Cmd {
  struct StrList   argv;
  int              is_pipe;
  struct StrList **stages;
  int              nstages;
};

struct Rule {
  char           name[128];
  struct StrList globs;
  struct StrList skip;
  char           require_marker[1024];
  char           require_file[1024];
  struct StrList in;
  struct StrList extra_deps;
  char           out[1024];
  struct Cmd     cmd;
  struct Gate   *gate;
  char           workdir[1024];
  int            phony;
  int            redirect;
  char           produces[128];
  char           description[256];
  char           group_name[128];
  char           member_of[128];
  char           member_alias[128];
  struct StrList features;

  int *inst_idx;
  int  n_inst, cap_inst;
  int  expanded;
};

struct Group {
  char           name[128];
  struct StrList refs;
};

struct Meta {
  char           name[128];
  struct StrList keys;
  struct StrList vals;
};

struct Template {
  char           name[128];
  struct StrList params;
  struct SNode  *body;
};

struct Rule     *rule_find(const char *name);
struct Rule     *rule_find_output(const char *path);
struct Rule     *rule_find_produces(const char *name);
struct Rule     *rule_new(const char *name);
struct Group    *group_find(const char *name);
struct Group    *group_new(const char *name);
struct Meta     *meta_find(const char *name);
struct Meta     *meta_new(const char *name);
struct Template *template_find(const char *name);
struct Template *template_new(const char *name);

extern struct Rule     *rules;
extern int              nrules, rulescap;
extern struct Group    *groups;
extern int              ngroups, groupscap;
extern struct Meta     *metas;
extern int              nmetas, metascap;
extern struct Template *templates;
extern int              ntemplates, templatescap;

struct Inst {
  int            rule_idx;
  struct StrList in;
  struct StrList stale_extra;
  char           out[1024];
  struct Cmd     cmd;
  char           workdir[1024];
  int            phony;
  int            redirect;

  struct StrList dep_rule_names;
  int           *dep_inst;
  int            n_dep, cap_dep;
  int            will_build;
};

int  inst_new(int rule_idx);
int  inst_stale(struct Inst *inst);
void dep_push(struct Inst *inst, int idx);
void resolve_deps(void);

extern struct Inst *insts;
extern int          ninsts, instscap;

long mtime_of(const char *path);
int  file_exists(const char *path);
int  file_contains(const char *path, const char *needle);
void glob_expand(const char *pattern, struct StrList *out);
void glob_all(struct Rule *r, struct StrList *out);
int  is_wildcard_pattern(const char *pattern);
int  rule_is_literal(struct Rule *r);
int  path_claimed_elsewhere(const char *path, struct Rule *self);
void splitext(const char *base, char *stem, size_t stemsz, char *ext, size_t extsz);
void to_ident(const char *s, char *out, size_t outsz);

pid_t spawn_inst(struct Inst *inst, struct StrList *argv);
pid_t spawn_pipe(struct Inst *inst);
void  mk_out_dir(const char *out);
void  announce_inst(struct Inst *inst);
void  run_inst_sync(int idx);
void  run_batch(int *idxs, int n);
void  run_inst_with_deps(int idx);
void  materialize_if_missing(const char *path);
char *capture_argv(struct StrList *argv);

extern int jobs_n;
extern int summary_mode;
extern int failed_any;

void        load_file(const char *path);
void        load_manifest(struct SNode *root);
void        dispatch_top_form(struct SNode *form);
void        parse_exec_clause(struct SNode *clause, struct Cmd *cmd);
void        parse_shell_clause(struct SNode *clause, struct Cmd *cmd);
void        parse_pipe_clause(struct SNode *clause, struct Cmd *cmd);
void        meta_apply(struct Meta *m);
const char *first_atom(struct SNode *node, const char *msg);
const char *sole_atom(struct SNode *node, const char *msg);

extern struct StrList files_read;

void expand_rule(const char *name);

void schedule_and_run(void);
void emit_ninja(const char *path, struct StrList *wanted);

struct Backend {
  const char *name;
  const char *try_key;
  const char *file;
  void (*emit)(const char *path, struct StrList *wanted);
};

const struct Backend *backend_by_name(const char *name);
const struct Backend *backend_resolve(char **bin);
void backend_exec(const struct Backend *be, const char *bin, struct StrList *wanted)
    __attribute__((noreturn));

void do_features(void);
void do_query(const char *sub, const char *arg);

void include_ref(const char *name);
void build_default_group(void);
void resolve_wanted(const char *w);
void resolve_all_bareword_deps(void);
