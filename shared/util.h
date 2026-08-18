/* see LICENSE file for copyright and license details */
#ifndef UTIL_H
#define UTIL_H

#include <sys/types.h>

#include <regex.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "arg.h"
#include "compat.h"

#define UTF8_POINT(c) (((c) & 0xc0) != 0x80)

#undef MIN
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#undef MAX
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#undef LIMIT
#define LIMIT(x, a, b) (x) = (x)<(a) ? (a) : (x)>(b) ? (b) : (x)

#define LEN(x) (sizeof(x) / sizeof *(x))

extern char *argv0;

void *ecalloc(size_t, size_t);
void *emalloc(size_t);
void *erealloc(void *, size_t);
#undef reallocarray
void *reallocarray(void *, size_t, size_t);
void *ereallocarray(void *, size_t, size_t);
char *estrdup(const char *);
char *estrndup(const char *, size_t);
void *encalloc(int, size_t, size_t);
void *enmalloc(int, size_t);
void *enrealloc(int, void *, size_t);
void *enreallocarray(int, void *, size_t, size_t);
char *enstrdup(int, const char *);
char *enstrndup(int, const char *, size_t);

void enfshut(int, FILE *, const char *);
void efshut(FILE *, const char *);
int  fshut(FILE *, const char *);

void enprintf(int, const char *, ...);
void eprintf(const char *, ...);
void weprintf(const char *, ...);
void xvprintf(const char *, va_list);

int confirm(const char *, ...);

double estrtod(const char *);

#undef strcasestr
#define strcasestr xstrcasestr
char *strcasestr(const char *, const char *);

#undef strlcat
#define strlcat xstrlcat
size_t strlcat(char *, const char *, size_t);
size_t estrlcat(char *, const char *, size_t);
#undef strlcpy
#define strlcpy xstrlcpy
size_t strlcpy(char *, const char *, size_t);
size_t estrlcpy(char *, const char *, size_t);

#undef strsep
#define strsep xstrsep
char *strsep(char **, const char *);

void strnsubst(char **, const char *, const char *, size_t);

/* regex */
int enregcomp(int, regex_t *, const char *, int);
int eregcomp(regex_t *, const char *, int);

/* io */
ssize_t writeall(int, const void *, size_t);
int     concat(int, const char *, int, const char *);

/* misc */
void   enmasse(int, char **, int (*)(const char *, const char *, int));
void   fnck(const char *, const char *, int (*)(const char *, const char *, int), int);
mode_t getumask(void);
int    hexval(int);
char  *humansize(off_t);
mode_t parsemode(const char *, mode_t, mode_t);
off_t  parseoffset(const char *);
void   putword(FILE *, const char *);
#undef strtonum
#define strtonum xstrtonum
long long strtonum(const char *, long long, long long, const char **);
long long enstrtonum(int, const char *, long long, long long);
long long estrtonum(const char *, long long, long long);
size_t    unescape(char *);
int       mkdirp(const char *, mode_t, mode_t);
#undef memmem
#define memmem xmemmem
void *memmem(const void *, size_t, const void *, size_t);

/* ubase functions */
char         *agetcwd(void);
void          apathmax(char **, long *);
long          estrtol(const char *, int);
unsigned long estrtoul(const char *, int);
#undef explicit_bzero
void explicit_bzero(void *, size_t);
void recurse_dir(const char *, void (*)(const char *));
void devtotty(int, int *, int *);
int  ttytostr(int, int, char *, size_t);

#include <netinet/in.h>
#include <sys/socket.h>

#ifndef IFNAMSIZ
#define IFNAMSIZ 16
#endif

struct NetStats {
  unsigned long long rx_bytes;
  unsigned long long rx_packets;
  unsigned long long rx_errs;
  unsigned long long rx_drop;
  unsigned long long tx_bytes;
  unsigned long long tx_packets;
  unsigned long long tx_errs;
  unsigned long long tx_drop;
};

struct NetInterface {
  char          name[IFNAMSIZ];
  unsigned int  flags;
  int           mtu;
  int           metric;
  unsigned char mac[6];
  int           has_mac;

  /* ipv4 addresses */
  struct sockaddr_in ipv4_addr;
  int                has_ipv4;
  struct sockaddr_in ipv4_mask;
  struct sockaddr_in ipv4_brd;

  /* ipv6 addresses */
  struct sockaddr_in6 ipv6_addr;
  int                 has_ipv6;
  unsigned int        ipv6_scope;
  unsigned int        ipv6_prefix;
};

struct MemInfo {
  unsigned long long total;
  unsigned long long free;
  unsigned long long shared;
  unsigned long long buffers;
  unsigned long long cached;
  unsigned long long totalswap;
  unsigned long long freeswap;
};

int net_get_interfaces(struct NetInterface **, int *);
int net_get_stats(const char *, struct NetStats *);
int net_set_flags(const char *, unsigned int, int);
int net_set_mtu(const char *, int);
int net_set_mac(const char *, const unsigned char *);
int net_add_addr(const char *, const char *, int);
int net_del_addr(const char *, const char *, int);
int net_show_routes(void);
int net_add_route(const char *, const char *, const char *, const char *, int);
int net_del_route(const char *, const char *, const char *, const char *, int);
int net_flush_addrs(const char *);
int net_set_txqueuelen(const char *, int);
int net_set_name(const char *, const char *);

#if FEATURE_NOEXEC || FEATURE_NOFORK
#include "wexec.h"
#define exit(code)  wexec_exit(code)
#define _exit(code) wexec_exit(code)
#endif

#endif
