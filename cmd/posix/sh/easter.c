/* see LICENSE file for copyright and license details */
#include "bltin.h"
#include "fs.h"
#include "util.h"

#include <sys/stat.h>
#include <sys/types.h>

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

enum {
  COMMENT_C,       /* line // and block comments */
  COMMENT_HASH,    /* line # only */
  COMMENT_PERCENT, /* line % only */
  COMMENT_SEMI,    /* line ; only */
  COMMENT_DASH,    /* line, only */
  COMMENT_HASKELL, /* line, and block {- -} */
  COMMENT_PAREN,   /* block (* *) only, no line comment */
};

enum {
  LANG_C,
  LANG_CPP,
  LANG_SH,
  LANG_AWK,
  LANG_MAKE,
  LANG_M4,
  LANG_LUA,
  LANG_PERL,
  LANG_PYTHON,
  LANG_RUBY,
  LANG_PHP,
  LANG_JS,
  LANG_TS,
  LANG_JAVA,
  LANG_KOTLIN,
  LANG_SCALA,
  LANG_CS,
  LANG_GO,
  LANG_SWIFT,
  LANG_ZIG,
  LANG_DART,
  LANG_ELIXIR,
  LANG_ERLANG,
  LANG_HASKELL,
  LANG_OCAML,
  LANG_CLOJURE,
  LANG_RUST,
  LANG_ADA,
  LANG_R,
  LANG_COUNT
};

struct CommentSpec {
  const char *line;
  const char *bopen;
  const char *bclose;
};

static const struct CommentSpec comment_specs[] = {
    [COMMENT_C]       = {"// ", "/*", "*/"},
    [COMMENT_HASH]    = {"#", NULL, NULL},
    [COMMENT_PERCENT] = {"%", NULL, NULL},
    [COMMENT_SEMI]    = {";", NULL, NULL},
    [COMMENT_DASH]    = {"--", NULL, NULL},
    [COMMENT_HASKELL] = {"--", "{-", "-}"},
    [COMMENT_PAREN]   = {NULL, "(*", "*)"},
};

/* weight is this languages contribution per percent of the codebase */
struct LangDef {
  const char *name;
  int         weight;
  int         cstyle;
};

static const struct LangDef lang_defs[LANG_COUNT] = {
    [LANG_C]       = {"C", 1, COMMENT_C},
    [LANG_CPP]     = {"C++", 300, COMMENT_C},
    [LANG_SH]      = {"Shell", 6, COMMENT_HASH},
    [LANG_AWK]     = {"Awk", 6, COMMENT_HASH},
    [LANG_MAKE]    = {"Make", 10, COMMENT_HASH},
    [LANG_M4]      = {"M4", 25, COMMENT_HASH},
    [LANG_LUA]     = {"Lua", 25, COMMENT_DASH},
    [LANG_PERL]    = {"Perl", 500, COMMENT_HASH},
    [LANG_PYTHON]  = {"Python", 80, COMMENT_HASH},
    [LANG_RUBY]    = {"Ruby", 90, COMMENT_HASH},
    [LANG_PHP]     = {"PHP", 110, COMMENT_C},
    [LANG_JS]      = {"JS", 100, COMMENT_C},
    [LANG_TS]      = {"TS", 140, COMMENT_C},
    [LANG_JAVA]    = {"Java", 150, COMMENT_C},
    [LANG_KOTLIN]  = {"Kotlin", 160, COMMENT_C},
    [LANG_SCALA]   = {"Scala", 180, COMMENT_C},
    [LANG_CS]      = {"C#", 140, COMMENT_C},
    [LANG_GO]      = {"Go", 3, COMMENT_C},
    [LANG_SWIFT]   = {"Swift", 70, COMMENT_C},
    [LANG_ZIG]     = {"Zig", 65, COMMENT_C},
    [LANG_DART]    = {"Dart", 55, COMMENT_C},
    [LANG_ELIXIR]  = {"Elixir", 50, COMMENT_HASH},
    [LANG_ERLANG]  = {"Erlang", 55, COMMENT_PERCENT},
    [LANG_HASKELL] = {"Haskell", 200, COMMENT_HASKELL},
    [LANG_OCAML]   = {"OCaml", 45, COMMENT_PAREN},
    [LANG_CLOJURE] = {"Clojure", 85, COMMENT_SEMI},
    [LANG_RUST]    = {"Rust", 3000, COMMENT_C},
    [LANG_ADA]     = {"Ada", 15, COMMENT_DASH},
    [LANG_R]       = {"R", 40, COMMENT_HASH},
};

struct ExtMap {
  const char *ext;
  int         lang;
};

static const struct ExtMap ext_map[] = {
    {".c", LANG_C},         {".cc", LANG_CPP},     {".cpp", LANG_CPP},     {".cxx", LANG_CPP},
    {".hpp", LANG_CPP},     {".sh", LANG_SH},      {".awk", LANG_AWK},     {".mk", LANG_MAKE},
    {".m4", LANG_M4},       {".lua", LANG_LUA},    {".pl", LANG_PERL},     {".pm", LANG_PERL},
    {".py", LANG_PYTHON},   {".pyw", LANG_PYTHON}, {".rb", LANG_RUBY},     {".php", LANG_PHP},
    {".js", LANG_JS},       {".mjs", LANG_JS},     {".cjs", LANG_JS},      {".ts", LANG_TS},
    {".tsx", LANG_TS},      {".java", LANG_JAVA},  {".kt", LANG_KOTLIN},   {".kts", LANG_KOTLIN},
    {".scala", LANG_SCALA}, {".cs", LANG_CS},      {".go", LANG_GO},       {".swift", LANG_SWIFT},
    {".zig", LANG_ZIG},     {".dart", LANG_DART},  {".ex", LANG_ELIXIR},   {".exs", LANG_ELIXIR},
    {".erl", LANG_ERLANG},  {".hrl", LANG_ERLANG}, {".hs", LANG_HASKELL},  {".lhs", LANG_HASKELL},
    {".ml", LANG_OCAML},    {".mli", LANG_OCAML},  {".clj", LANG_CLOJURE}, {".cljs", LANG_CLOJURE},
    {".rs", LANG_RUST},     {".adb", LANG_ADA},    {".ads", LANG_ADA},     {".r", LANG_R},
    {".R", LANG_R},
};

struct NameMap {
  const char *name;
  int         lang;
};

static const struct NameMap name_map[] = {
    {"Makefile", LANG_MAKE},
    {"GNUmakefile", LANG_MAKE},
    {".profile", LANG_SH},
    {"profile", LANG_SH},
    {".bashrc", LANG_SH},
    {".bash_profile", LANG_SH},
    {".shrc", LANG_SH},
    {".kshrc", LANG_SH},
    {".zshrc", LANG_SH},
    {".cshrc", LANG_SH},
    {".login", LANG_SH},
};

/* not scanned for lines, but still checked against markers below */
static const char *const skip_dirs[] = {
    ".git",
    ".hg",
    ".svn",
    "node_modules",
    "vendor",
    "__pycache__",
    ".venv",
    "venv",
    "target",
    "build",
    "dist",
    ".cache",
    ".next",
    ".idea",
    ".vscode",
    NULL,
};

struct Marker {
  const char *name;
  int         is_dir;
  double      penalty;
  const char *remark;
};

static const struct Marker markers[] = {
    {"node_modules", 1, 150.0, "skipping bullshit / malware"},
    {"Cargo.toml", 0, 80.0, "I challenge you to bootstrapping this shitty lang"},
    {"package.json", 0, 60.0, "js. Self-explanatory"},
    {"go.mod", 0, 20.0, "Not C. But still good. Minor penalty"},
    {"Dockerfile", 0, 20.0, "containerization crutch"},
    {"Gemfile", 0, 55.0, "shitty language baseline"},
    {"CMakeLists.txt", 0, 55.0, "shitty build system"},
    {"tsconfig.json", 0, 60.0, "a language with a configuration file. Degenerate."},
};

struct ComboPenalty {
  int         lang_a;
  int         lang_b;
  double      penalty;
  const char *remark;
};

static const struct ComboPenalty combos[] = {
    {LANG_RUST, LANG_CPP, 4000.0, "MAXIMUM BOGUSNESS, rust and c++ sharing a tree"},
    {LANG_RUST, LANG_C, 400.0, "comical bogusness, rust apologizing to the c it links against"},
    {LANG_JS, LANG_TS, 150.0, "half the codebase does not trust the other half"},
    {LANG_C, LANG_CPP, 120.0, "abi boundary between two compilers of the same language"},
};

struct LangStat {
  long files;
  long blank;
  long comment;
  long code;
};

struct ScanState {
  struct LangStat stats[LANG_COUNT];
  double          bonus;
  int             marker_seen[LEN(markers)];
};

#define BABEL_THRESHOLD 3
#define BABEL_MULT      1.5

static int
is_classic(int lang)
{
  return lang == LANG_C || lang == LANG_SH || lang == LANG_AWK;
}

static int
is_skip_dir(const char *name)
{
  int i;

  for (i = 0; skip_dirs[i]; i++)
    if (!strcmp(name, skip_dirs[i]))
      return 1;
  return 0;
}

/* checks a 1 or 2 char marker at p without reading past bufend */
static int
marker_at(const char *p, const char *bufend, const char *marker)
{
  if (!marker)
    return 0;
  if (!marker[1])
    return *p == marker[0];
  return p + 1 < bufend && p[0] == marker[0] && p[1] == marker[1];
}

static int
detect_lang(const char *name)
{
  const char *ext;
  size_t      i, nlen;

  for (i = 0; i < LEN(name_map); i++)
    if (!strcmp(name, name_map[i].name))
      return name_map[i].lang;

  ext = strrchr(name, '.');
  if (ext) {
    for (i = 0; i < LEN(ext_map); i++)
      if (!strcmp(ext, ext_map[i].ext))
        return ext_map[i].lang;
  }

  /*
 * anchored to the end of the name, so arch.txt (which contains "rc"
 * but does not end in it) is never mistaken for a shell rc file
 */
  nlen = strlen(name);
  if (nlen >= 2 && !strcmp(name + nlen - 2, "rc"))
    return LANG_SH;
  if (nlen >= 7 && !strcmp(name + nlen - 7, "profile"))
    return LANG_SH;

  return -1;
}

static void
check_marker(const char *name, int is_dir, struct ScanState *st)
{
  size_t i;

  for (i = 0; i < LEN(markers); i++) {
    if (markers[i].is_dir != is_dir || strcmp(name, markers[i].name) != 0)
      continue;
    if (st->marker_seen[i])
      return;
    st->marker_seen[i] = 1;
    st->bonus += markers[i].penalty;
    fprintf(stderr, "note: %s: %s\n", markers[i].name, markers[i].remark);
    return;
  }
}

static void
scan_file(int dirfd, const char *name, int lang, struct LangStat *lp)
{
  const struct CommentSpec *cs;
  char                      buf[8192];
  char                     *p, *bufend;
  ssize_t                   nread;
  int                       fd, in_block, leading_ws, checked, line_has_content, line_is_comment;

  fd = openat(dirfd, name, O_RDONLY);
  if (fd < 0)
    return;

  lp->files++;
  cs               = &comment_specs[lang_defs[lang].cstyle];
  in_block         = 0;
  leading_ws       = 1;
  checked          = 0;
  line_has_content = 0;
  line_is_comment  = 0;

  while ((nread = read(fd, buf, sizeof(buf))) > 0) {
    for (p = buf, bufend = buf + nread; p < bufend; p++) {
      if (*p == '\n') {
        if (!line_has_content)
          lp->blank++;
        else if (line_is_comment)
          lp->comment++;
        else
          lp->code++;
        leading_ws       = 1;
        checked          = 0;
        line_has_content = 0;
        line_is_comment  = 0;
        continue;
      }

      if (leading_ws) {
        if (*p == ' ' || *p == '\t' || *p == '\r')
          continue;
        leading_ws       = 0;
        line_has_content = 1;
      }

      /* only the first token of a line decides if the line starts a comment */
      if (!checked) {
        checked = 1;
        if (in_block) {
          line_is_comment = 1;
          if (marker_at(p, bufend, cs->bclose))
            in_block = 0;
        } else if (marker_at(p, bufend, cs->line)) {
          line_is_comment = 1;
        } else if (marker_at(p, bufend, cs->bopen)) {
          line_is_comment = 1;
          in_block        = 1;
        }
      } else if (in_block && marker_at(p, bufend, cs->bclose)) {
        in_block = 0;
      }
    }
  }

  /* files that dont end in a newline still have one line left to count */
  if (line_has_content) {
    if (line_is_comment)
      lp->comment++;
    else
      lp->code++;
  }

  close(fd);
}

static void
scan(int dirfd, const char *name, struct stat *st, void *data, struct recursor *r)
{
  struct ScanState *scanst = data;
  int               lang;

  check_marker(name, S_ISDIR(st->st_mode), scanst);

  if (S_ISDIR(st->st_mode)) {
    if (is_skip_dir(name))
      return;
    recurse(dirfd, name, data, r);
    return;
  }

  if (!S_ISREG(st->st_mode))
    return;

  lang = detect_lang(name);
  if (lang < 0)
    return;

  scan_file(dirfd, name, lang, &scanst->stats[lang]);
}

int
bogometercmd(int argc, char **argv)
{
  struct recursor  r  = {.fn = scan, .follow = 'P', .flags = SILENT};
  struct ScanState st = {0};
  const char      *target;
  double           pct, score;
  long             total_code;
  int              i, modern_langs;

  target     = (argc > 1) ? argv[1] : ".";
  total_code = 0;
  score      = 0.0;

  recurse(AT_FDCWD, target, &st, &r);

  printf("%-10s %10s %10s %10s %10s\n", "Language", "Files", "Blank", "Comment", "Code");
  printf("---------------------------------------------------------\n");

  for (i = 0; i < LANG_COUNT; i++) {
    if (st.stats[i].files == 0)
      continue;
    printf(
        "%-10s %10ld %10ld %10ld %10ld\n",
        lang_defs[i].name,
        st.stats[i].files,
        st.stats[i].blank,
        st.stats[i].comment,
        st.stats[i].code
    );
    total_code += st.stats[i].code;
  }

  if (total_code == 0) {
    printf("\nNo targeted code found. System is pure.\n");
    return 0;
  }

  printf("---------------------------------------------------------\n");
  modern_langs = 0;
  for (i = 0; i < LANG_COUNT; i++) {
    if (st.stats[i].code == 0)
      continue;
    pct = ((double)st.stats[i].code / total_code) * 100.0;
    score += pct * lang_defs[i].weight;
    printf("Distribution: %-6s is %5.2f%% of targeted language lines\n", lang_defs[i].name, pct);
    if (!is_classic(i))
      modern_langs++;
  }

  if (modern_langs >= BABEL_THRESHOLD) {
    score *= BABEL_MULT;
    fprintf(
        stderr,
        "note: %d languages in one tree: babel tower, no two files agree on what tongue they "
        "speak\n",
        modern_langs
    );
  }

  fflush(stdout);
  for (i = 0; i < (int)LEN(combos); i++) {
    if (st.stats[combos[i].lang_a].code > 0 && st.stats[combos[i].lang_b].code > 0) {
      score += combos[i].penalty;
      fprintf(
          stderr,
          "note: %s + %s: %s\n",
          lang_defs[combos[i].lang_a].name,
          lang_defs[combos[i].lang_b].name,
          combos[i].remark
      );
      fflush(stderr);
    }
  }

  if (st.stats[LANG_RUST].code > 0 && st.stats[LANG_CPP].code == 0 && st.stats[LANG_C].code == 0)
    fprintf(stderr, "note: rust: enormous magnitudes of bogus\n");

  score += st.bonus;

  if (modern_langs == 0 && st.stats[LANG_C].code > 0 && st.stats[LANG_SH].code > 0
      && st.stats[LANG_AWK].code > 0)
    printf("note: sh + awk + c only: peak codebase\n");

  printf("---------------------------------------------------------\n");
  printf("TOTAL CODE LINES: %ld\n", total_code);
  printf("AGGREGATE BOGOSITY INDEX: %5.2f uL\n", score);

  if (score > 5000.0)
    fprintf(stderr, "note: bogosity index over 5000: critical\n");
  else if (st.stats[LANG_C].code > 0 && score < 200.0)
    printf("note: bogosity index under 200 with c present: clean architecture\n");

  return 0;
}

int
boguscmd(int argc __unused, char **argv __unused)
{
  printf(
      "BOGOSITY n. The degree to which something is BOGUS. Measured with a\n"
      "bogometer, in units of the microLenat (uL).\n"
      "\n"
      "BOGUS adj. 1. Non-functional. \"Your patches are bogus.\"\n"
      "2. Useless. \"OPCON is a bogus program.\"\n"
      "3. False. \"Your arguments are bogus.\"\n"
      "4. Incorrect. \"That algorithm is bogus.\"\n"
      "5. Silly. \"Stop writing those bogus sagas.\"\n"
  );
  return 0;
}
