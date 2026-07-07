/* see license file for copyright and license details */
#ifndef TLS_H_
#define TLS_H_

#include <sys/types.h>

struct TlsSocket;

/* tls_connect takes an already connected socket fd and upgrades it to tls
   if check_cert is 0 certificate validation is bypassed
   returns a new tls socket or null on error */
struct TlsSocket *tlss_connect(int fd, const char *host, int check_cert, int is_tls);

/* standard read and write wrapping either libtls, bearssl or plain fallback */
ssize_t tlss_read(struct TlsSocket *s, void *buf, size_t len);
ssize_t tlss_write(struct TlsSocket *s, const void *buf, size_t len);

/* closes the tls connection and optionally closes the socket fd */
void tlss_close(struct TlsSocket *s, int close_fd);

#endif /* tls_h_ */
