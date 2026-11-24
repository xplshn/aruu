/* see license file for copyright and license details */
#include <stdbool.h>
#include "util.h"
#include "cc.h"

#if defined(__NetBSD__) && !defined(ARUU_FORCE_OTHER)
const char ospredef[] = "#define __NetBSD__ 1\n"
                        "#define __unix__ 1\n";
#endif
