#include "pattern.h"
#include "in_all.h"
#include <regex.h>

/*
 * interface to POSIX regular expression routines
 */

static regex_t pattern;
static int     pattern_valid;

char *
re_comp(char *s)
{
  if (!*s) {
    return (char *)0;
  }
  if (pattern_valid) {
    regfree(&pattern);
    pattern_valid = 0;
  }
  if (regcomp(&pattern, s, 0) == 0) {
    pattern_valid = 1;
    return (char *)0;
  }
  return "Error in pattern";
}

int
re_exec(char *s)
{
  return pattern_valid && regexec(&pattern, s, 0, (regmatch_t *)0, 0) == 0;
}
