#include "ninqu.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void
usage(void)
{
  eprintf(
      "usage: %s [-jN] [-f manifest] [-C dir] [-Dvar=val]... [-S] [-F] [-G ninja] "
      "[target|meta.target ...]\n"
      "       %s query (features|groups|metas|rules|graph) [arg]\n",
      argv0,
      argv0
  );
}

int
main(int argc, char *argv[])
{
  const char    *manifest      = "ninqu.rules";
  int            features_mode = 0;
  int            query_mode    = 0;
  const char    *query_sub     = NULL;
  const char    *query_arg     = NULL;
  const char    *generator     = NULL;
  int            i;
  struct StrList wanted = {0};

  /* detect query before ARGBEGIN so it is not parsed as a target */
  if (argc >= 2 && strcmp(argv[1], "query") == 0) {
    query_mode = 1;
    if (argc < 3)
      usage();
    query_sub = argv[2];
    query_arg = (argc >= 4) ? argv[3] : NULL;
    for (i = 3; i < argc; i++) {
      if (argv[i][0] == '-' && argv[i][1] == 'f')
        manifest = argv[i][2] ? argv[i] + 2 : argv[++i];
      else if (argv[i][0] == '-' && argv[i][1] == 'C') {
        if (chdir(argv[i][2] ? argv[i] + 2 : argv[++i]) != 0)
          eprintf("chdir:");
      } else if (argv[i][0] == '-' && argv[i][1] == 'D') {
        char *spec = argv[i][2] ? argv[i] + 2 : argv[++i];
        char *eq   = strchr(spec, '=');
        if (!eq)
          usage();
        *eq = '\0';
        kv_set_cli(spec, eq + 1);
      }
    }
  } else {
    ARGBEGIN
    {
      case 'j':
        jobs_n = (int)estrtonum(EARGF(usage()), 1, INT_MAX);
        break;
      case 'f':
        manifest = EARGF(usage());
        break;
      case 'C':
        if (chdir(EARGF(usage())) != 0)
          eprintf("chdir:");
        break;
      case 'D': {
        char *spec = EARGF(usage());
        char *eq   = strchr(spec, '=');
        if (!eq)
          usage();
        *eq = '\0';
        kv_set_cli(spec, eq + 1);
        break;
      }
      case 'S':
        summary_mode = 1;
        break;
      case 'F':
        features_mode = 1;
        break;
      case 'G':
        generator = EARGF(usage());
        break;
      default:
        usage();
    }
    ARGEND

    for (i = 0; i < argc; i++)
      sl_push(&wanted, argv[i]);
  }

  load_file(manifest);
  resolve_all_bareword_deps();

  if (query_mode) {
    do_query(query_sub, query_arg);
    return 0;
  }
  if (features_mode) {
    do_features();
    return 0;
  }

  if (wanted.n == 0)
    build_default_group();
  else
    for (i = 0; i < wanted.n; i++)
      resolve_wanted(wanted.v[i]);

  resolve_deps();

  if (generator) {
    const struct Backend *be = backend_by_name(generator);
    if (!be)
      eprintf("ninqu: unknown generator '%s'\n", generator);
    be->emit(be->file, &wanted);
    return failed_any ? 1 : 0;
  }

  /* backend=internal forces executor. otherwise generate file and exec backend binary */
  if (strcmp(kv_get_or("BACKEND", "auto"), "internal") != 0) {
    char                 *bin;
    const struct Backend *be = backend_resolve(&bin);
    if (be) {
      be->emit(be->file, &wanted);
      if (failed_any)
        return 1;
      backend_exec(be, bin, &wanted);
    }
  }

  schedule_and_run();

  return failed_any ? 1 : 0;
}
