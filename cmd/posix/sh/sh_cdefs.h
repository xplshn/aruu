/* -
 * compat defs for non-bsds
 *
 * __unused and __nonstring are intentionally NOT defined here
 * they collide with struct field names in some libc's <bits/stat.h>
 * (`long __unused[3]`) and similar system headers when defined as
 * macros before those headers are included. they remain defined in
 * shell.h, which includes <sys/stat.h> before defining them
 *
 * all definitions are guarded with #ifndef so they do not conflict
 * with each other or with system-provided definitions
 */

#ifndef SH_CDEFS_H
#define SH_CDEFS_H

#if FEATURE_SH_EXT
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#endif

#ifndef __dead2
#define __dead2 __attribute__((__noreturn__))
#endif

#ifndef __printf0like
#define __printf0like(n, m) __attribute__((__format__(__printf__, n, m)))
#endif

#ifndef __printflike
#define __printflike(n, m) __attribute__((__format__(__printf__, n, m)))
#endif

#ifndef _SH_POINTER_TYPEDEF
#define _SH_POINTER_TYPEDEF
typedef void *pointer; /* used in our memalloc.h and bltin.h
 * canonical home is shell.h, but we
 * want memalloc to be self-contained
 * and not pull all of shell.h */
#endif

#endif /* SH_CDEFS_H */
