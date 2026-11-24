/* see license file for copyright and license details */
#include <stdbool.h>
#include "util.h"
#include "cc.h"

/* a system with no file of its own is taken for some unix */
#if !(defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)   \
      || defined(__APPLE__))                                                                     \
    || defined(ARUU_FORCE_OTHER)
const char ospredef[] = "#define __unix__ 1\n";
#endif
