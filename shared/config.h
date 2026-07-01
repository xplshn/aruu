/* See LICENSE file for copyright and license details. */
#include "paths.h"

#define ENV_PATH ARUU_PATH_BIN
#define PW_CIPHER "$6$"
#undef UTMP_PATH
#define UTMP_PATH ARUU_PATH_UTMP
#undef BTMP_PATH
#define BTMP_PATH ARUU_PATH_BTMP
#undef WTMP_PATH
#define WTMP_PATH ARUU_PATH_WTMP

#ifndef FEATURE_FIND_DELETE
#define FEATURE_FIND_DELETE     1
#endif
#ifndef FEATURE_FIND_QUIT
#define FEATURE_FIND_QUIT       1
#endif
#ifndef FEATURE_FIND_EMPTY
#define FEATURE_FIND_EMPTY      1
#endif
#ifndef FEATURE_FIND_INUM
#define FEATURE_FIND_INUM       1
#endif
#ifndef FEATURE_FIND_SAMEFILE
#define FEATURE_FIND_SAMEFILE   1
#endif
#ifndef FEATURE_FIND_MAXDEPTH
#define FEATURE_FIND_MAXDEPTH   1
#endif
#ifndef FEATURE_FIND_MINDEPTH
#define FEATURE_FIND_MINDEPTH   1
#endif
#ifndef FEATURE_FIND_MMIN
#define FEATURE_FIND_MMIN       1
#endif
#ifndef FEATURE_FIND_AMIN
#define FEATURE_FIND_AMIN       1
#endif
#ifndef FEATURE_FIND_CMIN
#define FEATURE_FIND_CMIN       1
#endif
#ifndef FEATURE_FIND_INAME
#define FEATURE_FIND_INAME      1
#endif
#ifndef FEATURE_FIND_IPATH
#define FEATURE_FIND_IPATH      1
#endif
#ifndef FEATURE_FIND_REGEX
#define FEATURE_FIND_REGEX      1
#endif
#ifndef FEATURE_FIND_PRINT0
#define FEATURE_FIND_PRINT0     1
#endif
#ifndef FEATURE_SED_INPLACE
#define FEATURE_SED_INPLACE     1
#endif
#ifndef FEATURE_GREP_CONTEXT
#define FEATURE_GREP_CONTEXT    1
#endif
#ifndef FEATURE_GREP_MAX_COUNT
#define FEATURE_GREP_MAX_COUNT  1
#endif
#ifndef FEATURE_TAR_CREATE
#define FEATURE_TAR_CREATE      1
#endif
#ifndef FEATURE_TAR_EXCLUDE
#define FEATURE_TAR_EXCLUDE     1
#endif
#ifndef FEATURE_STAT_FILESYSTEM
#define FEATURE_STAT_FILESYSTEM 1
#endif
#ifndef FEATURE_STAT_FORMAT
#define FEATURE_STAT_FORMAT     1
#endif
#ifndef FEATURE_SORT_BIG
#define FEATURE_SORT_BIG        1
#endif
#ifndef FEATURE_SORT_STABLE
#define FEATURE_SORT_STABLE     1
#endif
#ifndef FEATURE_OD_ENDIAN
#define FEATURE_OD_ENDIAN       1
#endif
#ifndef FEATURE_SED_PRESERVE_NEWLINE
#define FEATURE_SED_PRESERVE_NEWLINE 1
#endif
#ifndef FEATURE_LS_COLOR
#define FEATURE_LS_COLOR             1
#endif
#ifndef FEATURE_DIFF3_MERGE
#define FEATURE_DIFF3_MERGE          1
#endif
#ifndef FEATURE_DIFF3_EDSCRIPT
#define FEATURE_DIFF3_EDSCRIPT       1
#endif
#ifndef FEATURE_DIFF_CONTEXT
#define FEATURE_DIFF_CONTEXT         1
#endif
#ifndef FEATURE_DIFF_UNIFIED
#define FEATURE_DIFF_UNIFIED         1
#endif
#ifndef FEATURE_DIFF_ED
#define FEATURE_DIFF_ED              1
#endif
#ifndef FEATURE_DIFF_BRIEF
#define FEATURE_DIFF_BRIEF           1
#endif
#ifndef FEATURE_DIFF_SIDEBYSIDE
#define FEATURE_DIFF_SIDEBYSIDE      1
#endif
#ifndef FEATURE_PATCH_BACKUP
#define FEATURE_PATCH_BACKUP         1
#endif
#ifndef FEATURE_PATCH_FUZZ
#define FEATURE_PATCH_FUZZ           1
#endif
#ifndef FEATURE_PATCH_STRIP_CR
#define FEATURE_PATCH_STRIP_CR       1
#endif
#ifndef FEATURE_INPROC_EXEC
#define FEATURE_INPROC_EXEC          0
#endif
