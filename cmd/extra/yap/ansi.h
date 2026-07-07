#ifndef ANSI_H
#define ANSI_H

enum ansi_kind {
  ANSI_NONE = 0,
  ANSI_SGR,
  ANSI_DISCARD,
};

int ansi_escape(const char *s, const char **next, enum ansi_kind *kind);

#endif
