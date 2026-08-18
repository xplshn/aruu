/* see LICENSE file for copyright and license details */
#ifndef SIG_H_
#define SIG_H_

#include <signal.h>
#include <sys/types.h>

#ifndef SIG2STR_MAX
#define SIG2STR_MAX 32
#endif

#ifndef NSIG
#ifdef _NSIG
#define NSIG _NSIG
#else
#define NSIG 64
#endif
#endif

extern const char *const aruu_sys_signame[NSIG];
extern const int         aruu_sys_nsig;

#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)  \
    || defined(__APPLE__)
/* system already provides sys_signame and sys_nsig */
#else
#define sys_signame aruu_sys_signame
#define sys_nsig    aruu_sys_nsig
#endif

#define sig2str aruu_sig2str
#define str2sig aruu_str2sig

int aruu_sig2str(int signo, char *str);
int aruu_str2sig(const char *str, int *signop);

#endif /* !SIG_H_ */
