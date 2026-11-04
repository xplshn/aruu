/* see license file for copyright and license details */
#include "../tls.h"
#include "../paths.h"
#include "../util.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if FEATURE_USE_LIBTLS
#include <tls.h>

struct TlsSocket {
  int         fd;
  int         is_tls;
  struct tls *ctx;
};

#elif FEATURE_USE_BEARSSL
#include <bearssl.h>

struct x509_noverify_ctx {
  const br_x509_class    *vtable;
  br_x509_minimal_context minimal;
};

struct TlsTrustStore;

struct TlsSocket {
  int                      fd;
  int                      is_tls;
  br_ssl_client_context    sc;
  struct x509_noverify_ctx x509_noverify;
  struct TlsTrustStore    *tas;
  unsigned char            iobuf[BR_SSL_BUFSIZE_BIDI];
  br_sslio_context         ioc;
};

struct dn_accum {
  unsigned char *data;
  size_t         len;
  size_t         cap;
};

static void
append_dn_callback(void *dn_ctx, const void *src, size_t len)
{
  struct dn_accum *accum = dn_ctx;
  if (accum->len + len > accum->cap) {
    accum->cap  = accum->len + len + 256;
    accum->data = erealloc(accum->data, accum->cap);
  }
  memcpy(accum->data + accum->len, src, len);
  accum->len += len;
}

static void
x509_noverify_start_chain(const br_x509_class **ctx, const char *server_name)
{
  struct x509_noverify_ctx *t = (struct x509_noverify_ctx *)ctx;
  br_x509_minimal_vtable.start_chain((const br_x509_class **)&t->minimal, server_name);
}

static void
x509_noverify_start_cert(const br_x509_class **ctx, uint32_t length)
{
  struct x509_noverify_ctx *t = (struct x509_noverify_ctx *)ctx;
  br_x509_minimal_vtable.start_cert((const br_x509_class **)&t->minimal, length);
}

static void
x509_noverify_append(const br_x509_class **ctx, const unsigned char *buf, size_t len)
{
  struct x509_noverify_ctx *t = (struct x509_noverify_ctx *)ctx;
  br_x509_minimal_vtable.append((const br_x509_class **)&t->minimal, buf, len);
}

static void
x509_noverify_end_cert(const br_x509_class **ctx)
{
  struct x509_noverify_ctx *t = (struct x509_noverify_ctx *)ctx;
  br_x509_minimal_vtable.end_cert((const br_x509_class **)&t->minimal);
}

static unsigned
x509_noverify_end_chain(const br_x509_class **ctx)
{
  struct x509_noverify_ctx *t = (struct x509_noverify_ctx *)ctx;
  (void)br_x509_minimal_vtable.end_chain((const br_x509_class **)&t->minimal);
  /* always succeed and accept certificate when check_cert is 0 */
  return 0;
}

static const br_x509_pkey *
x509_noverify_get_pkey(const br_x509_class *const *ctx, unsigned *usages)
{
  const struct x509_noverify_ctx *t = (const struct x509_noverify_ctx *)ctx;
  return br_x509_minimal_vtable.get_pkey((const br_x509_class *const *)&t->minimal, usages);
}

static const br_x509_class x509_noverify_vtable = {
    sizeof(struct x509_noverify_ctx),
    x509_noverify_start_chain,
    x509_noverify_start_cert,
    x509_noverify_append,
    x509_noverify_end_cert,
    x509_noverify_end_chain,
    x509_noverify_get_pkey
};

static int
sock_read(void *ctx, unsigned char *buf, size_t len)
{
  int fd = *(int *)ctx;
  for (;;) {
    ssize_t rlen = read(fd, buf, len);
    if (rlen <= 0) {
      if (rlen < 0 && errno == EINTR)
        continue;
      return -1;
    }
    return (int)rlen;
  }
}

static int
sock_write(void *ctx, const unsigned char *buf, size_t len)
{
  int fd = *(int *)ctx;
  for (;;) {
    ssize_t wlen = write(fd, buf, len);
    if (wlen <= 0) {
      if (wlen < 0 && errno == EINTR)
        continue;
      return -1;
    }
    return (int)wlen;
  }
}

struct TlsTrustStore {
  br_x509_trust_anchor *v;
  size_t                num;
  size_t                cap;
};

struct PemDecoder {
  unsigned char *data;
  size_t         len;
  size_t         cap;
  unsigned int   value;
  size_t         quantum;
  size_t         padding;
};

static int
b64_decode_char(char c)
{
  if (c >= 'A' && c <= 'Z')
    return c - 'A';
  if (c >= 'a' && c <= 'z')
    return c - 'a' + 26;
  if (c >= '0' && c <= '9')
    return c - '0' + 52;
  if (c == '+')
    return 62;
  if (c == '/')
    return 63;
  if (c == '=')
    return -2;
  return -1;
}

static int
b64_decoder_flush(struct PemDecoder *dec)
{
  size_t i, n;

  if (dec->quantum != 4)
    return -1;
  n = 3 - dec->padding;
  for (i = 0; i < n; i++) {
    if (dec->len == dec->cap) {
      size_t cap = dec->cap ? dec->cap * 2 : 1024;
      if (cap < dec->cap)
        return -1;
      dec->data = erealloc(dec->data, cap);
      dec->cap  = cap;
    }
    dec->data[dec->len++] = (dec->value >> (16 - i * 8)) & 0xFF;
  }
  dec->value   = 0;
  dec->quantum = 0;
  dec->padding = 0;
  return 0;
}

static int
b64_decoder_push(struct PemDecoder *dec, char c)
{
  int value = b64_decode_char(c);

  if (value == -2) {
    if ((!dec->padding && dec->quantum < 2) || (dec->padding && dec->quantum != 3))
      return -1;
    dec->value <<= 6;
    dec->padding++;
    dec->quantum++;
  } else {
    if (value < 0 || dec->padding)
      return -1;
    dec->value = (dec->value << 6) | (unsigned int)value;
    dec->quantum++;
  }
  if (dec->quantum == 4)
    return b64_decoder_flush(dec);
  return 0;
}

static int
b64_decoder_push_line(struct PemDecoder *dec, const char *line, size_t len)
{
  size_t i;

  for (i = 0; i < len; i++) {
    if (b64_decoder_push(dec, line[i]) < 0)
      return -1;
  }
  return 0;
}

static int
b64_decoder_finish(const struct PemDecoder *dec)
{
  return dec->quantum == 0 ? 0 : -1;
}

static void
trust_anchor_free(br_x509_trust_anchor *ta)
{
  free(ta->dn.data);
  if (ta->pkey.key_type == BR_KEYTYPE_RSA) {
    free(ta->pkey.key.rsa.n);
    free(ta->pkey.key.rsa.e);
  } else if (ta->pkey.key_type == BR_KEYTYPE_EC) {
    free(ta->pkey.key.ec.q);
  }
  memset(ta, 0, sizeof(*ta));
}

static void
trust_store_free(struct TlsTrustStore *tas)
{
  size_t i;

  if (!tas)
    return;
  for (i = 0; i < tas->num; i++)
    trust_anchor_free(&tas->v[i]);
  free(tas->v);
  memset(tas, 0, sizeof(*tas));
}

static int
decode_cert_der(const unsigned char *der, size_t der_len, br_x509_trust_anchor *ta)
{
  br_x509_decoder_context dc;
  struct dn_accum         accum = {NULL, 0, 0};
  const br_x509_pkey     *pk;

  memset(ta, 0, sizeof(*ta));
  br_x509_decoder_init(&dc, append_dn_callback, &accum);
  br_x509_decoder_push(&dc, der, der_len);
  if (br_x509_decoder_last_error(&dc) != 0) {
    free(accum.data);
    return 0;
  }
  pk = br_x509_decoder_get_pkey(&dc);
  if (!pk) {
    free(accum.data);
    return 0;
  }

  ta->dn.data       = accum.data;
  ta->dn.len        = accum.len;
  ta->flags         = br_x509_decoder_isCA(&dc) ? BR_X509_TA_CA : 0;
  ta->pkey.key_type = pk->key_type;

  if (pk->key_type == BR_KEYTYPE_RSA) {
    ta->pkey.key.rsa.nlen = pk->key.rsa.nlen;
    ta->pkey.key.rsa.n    = emalloc(pk->key.rsa.nlen);
    memcpy(ta->pkey.key.rsa.n, pk->key.rsa.n, pk->key.rsa.nlen);
    ta->pkey.key.rsa.elen = pk->key.rsa.elen;
    ta->pkey.key.rsa.e    = emalloc(pk->key.rsa.elen);
    memcpy(ta->pkey.key.rsa.e, pk->key.rsa.e, pk->key.rsa.elen);
  } else if (pk->key_type == BR_KEYTYPE_EC) {
    ta->pkey.key.ec.curve = pk->key.ec.curve;
    ta->pkey.key.ec.qlen  = pk->key.ec.qlen;
    ta->pkey.key.ec.q     = emalloc(pk->key.ec.qlen);
    memcpy(ta->pkey.key.ec.q, pk->key.ec.q, pk->key.ec.qlen);
  } else {
    trust_anchor_free(ta);
    return 0;
  }
  return 1;
}

static int
trust_store_append(struct TlsTrustStore *tas, const br_x509_trust_anchor *ta)
{
  if (tas->num == tas->cap) {
    size_t cap = tas->cap ? tas->cap * 2 : 8;
    if (cap < tas->cap)
      return -1;
    tas->v   = ereallocarray(tas->v, cap, sizeof(*tas->v));
    tas->cap = cap;
  }
  tas->v[tas->num++] = *ta;
  return 0;
}

static int
line_is(const char *line, size_t len, const char *text)
{
  return strlen(text) == len && memcmp(line, text, len) == 0;
}

static int
load_ca_certs_file(const char *path, struct TlsTrustStore *tas)
{
  static const char begin[] = "-----BEGIN CERTIFICATE-----";
  static const char end[]   = "-----END CERTIFICATE-----";
  FILE             *fp;
  char             *line     = NULL;
  size_t            line_cap = 0, line_len, line_no = 0, cert_num = 0;
  ssize_t           nread;
  int               in_cert = 0, failed = 0;
  struct PemDecoder dec = {0};

  fp = fopen(path, "r");
  if (!fp)
    return 0;
  while ((nread = getline(&line, &line_cap, fp)) >= 0) {
    line_no++;
    line_len = (size_t)nread;
    while (line_len > 0 && (line[line_len - 1] == '\r' || line[line_len - 1] == '\n'))
      line_len--;
    if (in_cert && memchr(line, '\0', line_len) != NULL) {
      weprintf("%s:%zu: invalid base64 in CA certificate %zu\n", path, line_no, cert_num);
      failed = 1;
      break;
    }
    if (line_is(line, line_len, begin)) {
      if (in_cert) {
        weprintf("%s:%zu: malformed PEM in CA certificate %zu\n", path, line_no, cert_num);
        failed = 1;
        break;
      }
      cert_num++;
      in_cert = 1;
      continue;
    }
    if (line_is(line, line_len, end)) {
      br_x509_trust_anchor ta;
      if (!in_cert) {
        weprintf("%s:%zu: unexpected CA certificate end\n", path, line_no);
        failed = 1;
        break;
      }
      if (b64_decoder_finish(&dec) < 0 || dec.len == 0) {
        weprintf("%s:%zu: invalid base64 in CA certificate %zu\n", path, line_no, cert_num);
        failed = 1;
        break;
      }
      if (!decode_cert_der(dec.data, dec.len, &ta)) {
        weprintf("%s:%zu: malformed CA certificate %zu\n", path, line_no, cert_num);
        failed = 1;
        break;
      }
      if (trust_store_append(tas, &ta) < 0) {
        trust_anchor_free(&ta);
        weprintf("%s:%zu: cannot store CA certificate %zu\n", path, line_no, cert_num);
        failed = 1;
        break;
      }
      free(dec.data);
      memset(&dec, 0, sizeof(dec));
      in_cert = 0;
      continue;
    }
    if (in_cert && b64_decoder_push_line(&dec, line, line_len) < 0) {
      weprintf("%s:%zu: invalid base64 in CA certificate %zu\n", path, line_no, cert_num);
      failed = 1;
      break;
    }
  }
  if (!failed && ferror(fp)) {
    weprintf("%s:%zu: cannot read CA certificates\n", path, line_no + 1);
    failed = 1;
  }
  if (!failed && in_cert) {
    weprintf("%s:%zu: unterminated CA certificate %zu\n", path, line_no, cert_num);
    failed = 1;
  }
  if (fclose(fp) != 0 && !failed) {
    weprintf("%s: cannot close CA certificates\n", path);
    failed = 1;
  }
  free(line);
  free(dec.data);
  if (failed) {
    trust_store_free(tas);
    return -1;
  }
  return 1;
}

static void
warn_no_ca_certs(void)
{
  weprintf(
      "no CA certificates found, TLS verification will "
      "fail\n"
  );
}

static int
load_ca_certs(struct TlsTrustStore *tas)
{
  static const char *ca_paths[] = {
      ARUU_PATH_ETC "/ssl/certs/ca-certificates.crt",
      ARUU_PATH_ETC "/ssl/cert.pem",
      ARUU_PATH_ETC "/pki/tls/certs/ca-bundle.crt",
  };
  size_t i;
  int    r;

  for (i = 0; i < LEN(ca_paths); i++) {
    r = load_ca_certs_file(ca_paths[i], tas);
    if (r >= 0) {
      if (r == 0)
        warn_no_ca_certs();
      return 1;
    }
  }
  warn_no_ca_certs();
  return 1;
}

static int
sigpipe_ignore(struct sigaction *old)
{
  struct sigaction sa;

  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = SIG_IGN;
  return sigaction(SIGPIPE, &sa, old);
}

static int
sigpipe_restore(const struct sigaction *old)
{
  return sigaction(SIGPIPE, old, NULL);
}

#else
struct TlsSocket {
  int fd;
  int is_tls;
};
#endif

struct TlsSocket *
tlss_connect(int fd, const char *host, int check_cert, int is_tls)
{
  struct TlsSocket *s;

  s         = emalloc(sizeof(*s));
  s->fd     = fd;
  s->is_tls = is_tls;
#if FEATURE_USE_BEARSSL
  s->tas = NULL;
#endif

  if (!is_tls)
    return s;
#if !FEATURE_USE_LIBTLS && !FEATURE_USE_BEARSSL
  (void)host;
  (void)check_cert;
#endif

#if FEATURE_USE_LIBTLS
  {
    struct tls_config *cfg;

    s->ctx = tls_client();
    if (!s->ctx) {
      weprintf("tls_client failed\n");
      free(s);
      return NULL;
    }
    cfg = tls_config_new();
    if (!cfg) {
      weprintf("tls_config_new failed\n");
      tls_free(s->ctx);
      free(s);
      return NULL;
    }
    if (!check_cert) {
      tls_config_insecure_noverifycert(cfg);
      tls_config_insecure_noverifyname(cfg);
    }
    if (tls_configure(s->ctx, cfg) < 0) {
      weprintf("tls_configure: %s\n", tls_error(s->ctx));
      tls_config_free(cfg);
      tls_free(s->ctx);
      free(s);
      return NULL;
    }
    tls_config_free(cfg);

    if (tls_connect_socket(s->ctx, fd, host) < 0) {
      weprintf("tls_connect_socket: %s\n", tls_error(s->ctx));
      tls_free(s->ctx);
      free(s);
      return NULL;
    }
  }
#elif FEATURE_USE_BEARSSL
  {
    struct sigaction old_sigpipe;
    int              r;

    if (check_cert) {
      s->tas = emalloc(sizeof(*s->tas));
      if (!load_ca_certs(s->tas)) {
        weprintf("cannot load CA certificates\n");
        free(s->tas);
        free(s);
        return NULL;
      }
      br_ssl_client_init_full(&s->sc, &s->x509_noverify.minimal, s->tas->v, s->tas->num);
    } else {
      br_ssl_client_init_full(&s->sc, &s->x509_noverify.minimal, NULL, 0);
      s->x509_noverify.vtable = &x509_noverify_vtable;
      br_ssl_engine_set_x509(&s->sc.eng, &s->x509_noverify.vtable);
    }

    br_ssl_engine_set_buffer(&s->sc.eng, s->iobuf, sizeof(s->iobuf), 1);
    br_ssl_client_reset(&s->sc, host, 0);
    br_sslio_init(&s->ioc, &s->sc.eng, sock_read, &s->fd, sock_write, &s->fd);

    if (sigpipe_ignore(&old_sigpipe) < 0) {
      weprintf("cannot suppress SIGPIPE for TLS handshake\n");
      trust_store_free(s->tas);
      free(s->tas);
      free(s);
      return NULL;
    }
    r = br_sslio_flush(&s->ioc);
    if (sigpipe_restore(&old_sigpipe) < 0) {
      weprintf("cannot restore SIGPIPE after TLS handshake\n");
      r = -1;
    }
    if (r < 0) {
      weprintf("TLS handshake failed with %s\n", host);
      trust_store_free(s->tas);
      free(s->tas);
      free(s);
      return NULL;
    }
  }
#else
  weprintf(
      "TLS not supported, compile with FEATURE_USE_LIBTLS or "
      "FEATURE_USE_BEARSSL\n"
  );
  free(s);
  return NULL;
#endif

  return s;
}

ssize_t
tlss_read(struct TlsSocket *s, void *buf, size_t len)
{
  if (!s->is_tls) {
    for (;;) {
      ssize_t r = read(s->fd, buf, len);
      if (r < 0 && errno == EINTR)
        continue;
      return r;
    }
  }
#if FEATURE_USE_LIBTLS
  for (;;) {
    ssize_t r = tls_read(s->ctx, buf, len);
    if (r == TLS_WANT_POLLIN || r == TLS_WANT_POLLOUT)
      continue;
    return r;
  }
#elif FEATURE_USE_BEARSSL
  return br_sslio_read(&s->ioc, buf, len);
#else
  return -1;
#endif
}

ssize_t
tlss_write(struct TlsSocket *s, const void *buf, size_t len)
{
  if (!s->is_tls) {
    for (;;) {
      ssize_t r = write(s->fd, buf, len);
      if (r < 0 && errno == EINTR)
        continue;
      return r;
    }
  }
#if FEATURE_USE_LIBTLS
  for (;;) {
    ssize_t r = tls_write(s->ctx, buf, len);
    if (r == TLS_WANT_POLLIN || r == TLS_WANT_POLLOUT)
      continue;
    return r;
  }
#elif FEATURE_USE_BEARSSL
  {
    struct sigaction old_sigpipe;
    int              r;

    if (sigpipe_ignore(&old_sigpipe) < 0) {
      weprintf("cannot suppress SIGPIPE for TLS write\n");
      return -1;
    }
    r = br_sslio_write_all(&s->ioc, buf, len);
    if (r < 0)
      r = -1;
    else if (br_sslio_flush(&s->ioc) < 0)
      r = -1;
    else
      r = 0;
    if (sigpipe_restore(&old_sigpipe) < 0) {
      weprintf("cannot restore SIGPIPE after TLS write\n");
      return -1;
    }
    return r < 0 ? -1 : (ssize_t)len;
  }
#else
  return -1;
#endif
}

void
tlss_close(struct TlsSocket *s, int close_fd)
{
  if (s->is_tls) {
#if FEATURE_USE_LIBTLS
    tls_close(s->ctx);
    tls_free(s->ctx);
#elif FEATURE_USE_BEARSSL
    {
      struct sigaction old_sigpipe;

      if (sigpipe_ignore(&old_sigpipe) < 0) {
        weprintf("cannot suppress SIGPIPE for TLS teardown\n");
      } else {
        br_sslio_close(&s->ioc);
        if (sigpipe_restore(&old_sigpipe) < 0)
          weprintf("cannot restore SIGPIPE after TLS teardown\n");
      }
    }
    trust_store_free(s->tas);
    free(s->tas);
#endif
  }
  if (close_fd)
    close(s->fd);
  free(s);
}

#if FEATURE_USE_BEARSSL && defined(ARUU_TLS_TESTING)
int
tlss_test_b64_decode(
    const char *src, size_t src_len, unsigned char *dst, size_t dst_cap, size_t *dst_len
)
{
  struct PemDecoder dec = {0};
  size_t            i;

  for (i = 0; i < src_len; i++) {
    if (b64_decoder_push(&dec, src[i]) < 0)
      break;
  }
  if (i == src_len && b64_decoder_finish(&dec) == 0 && dec.len <= dst_cap) {
    memcpy(dst, dec.data, dec.len);
    *dst_len = dec.len;
    free(dec.data);
    return 0;
  }
  free(dec.data);
  return -1;
}

int
tlss_test_load_ca_certs(const char *path)
{
  struct TlsTrustStore tas = {0};
  int                  num;

  if (load_ca_certs_file(path, &tas) != 1)
    return -1;
  num = (int)tas.num;
  trust_store_free(&tas);
  return num;
}

int
tlss_test_sigpipe_setup(void)
{
  struct sigaction old_sigpipe;

  if (sigpipe_ignore(&old_sigpipe) < 0)
    return -1;
  return sigpipe_restore(&old_sigpipe);
}

int
tlss_test_sigpipe_teardown(void)
{
  struct sigaction old_sigpipe;

  if (sigpipe_ignore(&old_sigpipe) < 0)
    return -1;
  return sigpipe_restore(&old_sigpipe);
}
#endif
