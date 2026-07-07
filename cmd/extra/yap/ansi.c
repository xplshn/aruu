#include "ansi.h"
#include "in_all.h"

static int
consume_csi(const char *s, const char **next, enum ansi_kind *kind)
{
  const unsigned char *p = (const unsigned char *)s + 2;

  while (*p >= 0x30 && *p <= 0x3f) {
    p++;
  }
  while (*p >= 0x20 && *p <= 0x2f) {
    p++;
  }
  if (*p >= 0x40 && *p <= 0x7e) {
    *kind = (*p == 'm') ? ANSI_SGR : ANSI_DISCARD;
    *next = (const char *)p + 1;
    return 1;
  }
  return 0;
}

static int
consume_string(const char *s, const char **next)
{
  const unsigned char *p = (const unsigned char *)s + 2;

  while (*p) {
    if (*p == '\a') {
      *next = (const char *)p + 1;
      return 1;
    }
    if (*p == '\x1b' && p[1] == '\\') {
      *next = (const char *)p + 2;
      return 1;
    }
    p++;
  }
  return 0;
}

static int
consume_esc(const char *s, const char **next)
{
  const unsigned char *p = (const unsigned char *)s + 1;

  while (*p >= 0x20 && *p <= 0x2f) {
    p++;
  }
  if (*p >= 0x30 && *p <= 0x7e) {
    *next = (const char *)p + 1;
    return 1;
  }
  return 0;
}

int
ansi_escape(const char *s, const char **next, enum ansi_kind *kind)
{
  *kind = ANSI_NONE;
  *next = s;
  if ((unsigned char)s[0] != 0x1b || s[1] == '\0') {
    return 0;
  }
  if (s[1] == '[') {
    return consume_csi(s, next, kind);
  }
  if (s[1] == ']' || s[1] == 'P' || s[1] == 'X' || s[1] == '^' || s[1] == '_') {
    *kind = ANSI_DISCARD;
    return consume_string(s, next);
  }
  *kind = ANSI_DISCARD;
  return consume_esc(s, next);
}
