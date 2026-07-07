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

struct TlsSocket {
  int                      fd;
  int                      is_tls;
  br_ssl_client_context    sc;
  struct x509_noverify_ctx x509_noverify;
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
  return -1;
}

static size_t
b64_decode(const char *src, size_t src_len, unsigned char *dst)
{
  size_t i, j = 0;
  int    val = 0, valb = -8;
  for (i = 0; i < src_len; i++) {
    int c = b64_decode_char(src[i]);
    if (c >= 0) {
      val = (val << 6) | c;
      valb += 6;
      if (valb >= 0) {
        dst[j++] = (val >> valb) & 0xFF;
        valb -= 8;
      }
    }
  }
  return j;
}

static int
decode_cert_der(const unsigned char *der, size_t der_len, br_x509_trust_anchor *ta)
{
  br_x509_decoder_context dc;
  struct dn_accum         accum = {NULL, 0, 0};
  const br_x509_pkey     *pk;

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
    free(accum.data);
    return 0;
  }
  return 1;
}

static br_x509_trust_anchor *tas     = NULL;
static size_t                tas_num = 0;

static void
load_ca_certs(void)
{
  static const char *ca_paths[] = {
      ARUU_PATH_ETC "/ssl/certs/ca-certificates.crt",
      ARUU_PATH_ETC "/ssl/cert.pem",
      ARUU_PATH_ETC "/pki/tls/certs/ca-bundle.crt",
  };
  FILE  *fp = NULL;
  size_t i;
  char   line[512];
  struct {
    char  *data;
    size_t len;
    size_t cap;
  } pem       = {NULL, 0, 0};
  int in_cert = 0;

  for (i = 0; i < LEN(ca_paths); i++) {
    if ((fp = fopen(ca_paths[i], "r")))
      break;
  }
  if (!fp) {
    weprintf(
        "no CA certificates found, TLS verification will "
        "fail\n"
    );
    return;
  }

  while (fgets(line, sizeof(line), fp)) {
    if (strncmp(line, "-----BEGIN CERTIFICATE-----", 27) == 0) {
      in_cert = 1;
      pem.len = 0;
    } else if (strncmp(line, "-----END CERTIFICATE-----", 25) == 0) {
      if (in_cert) {
        unsigned char       *der     = emalloc(pem.len);
        size_t               der_len = b64_decode(pem.data, pem.len, der);
        br_x509_trust_anchor ta;
        if (decode_cert_der(der, der_len, &ta)) {
          tas            = ereallocarray(tas, tas_num + 1, sizeof(*tas));
          tas[tas_num++] = ta;
        }
        free(der);
        in_cert = 0;
      }
    } else if (in_cert) {
      size_t llen = strlen(line);
      while (llen > 0 && (line[llen - 1] == '\r' || line[llen - 1] == '\n'))
        llen--;
      if (pem.len + llen > pem.cap) {
        pem.cap  = pem.len + llen + 1024;
        pem.data = erealloc(pem.data, pem.cap);
      }
      memcpy(pem.data + pem.len, line, llen);
      pem.len += llen;
    }
  }
  free(pem.data);
  fclose(fp);
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

  if (!is_tls)
    return s;
#if !FEATURE_USE_LIBTLS && !FEATURE_USE_BEARSSL && !FEATURE_USE_OPENSSL
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
    signal(SIGPIPE, SIG_IGN);

    if (check_cert) {
      static int ca_loaded = 0;
      if (!ca_loaded) {
        load_ca_certs();
        ca_loaded = 1;
      }
      br_ssl_client_init_full(&s->sc, &s->x509_noverify.minimal, tas, tas_num);
    } else {
      br_ssl_client_init_full(&s->sc, &s->x509_noverify.minimal, NULL, 0);
      s->x509_noverify.vtable = &x509_noverify_vtable;
      br_ssl_engine_set_x509(&s->sc.eng, &s->x509_noverify.vtable);
    }

    br_ssl_engine_set_buffer(&s->sc.eng, s->iobuf, sizeof(s->iobuf), 1);
    br_ssl_client_reset(&s->sc, host, 0);
    br_sslio_init(&s->ioc, &s->sc.eng, sock_read, &s->fd, sock_write, &s->fd);

    if (br_sslio_flush(&s->ioc) < 0) {
      weprintf("TLS handshake failed with %s\n", host);
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
    int r = br_sslio_write_all(&s->ioc, buf, len);
    if (r < 0)
      return -1;
    if (br_sslio_flush(&s->ioc) < 0)
      return -1;
    return len;
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
    br_sslio_close(&s->ioc);
#endif
  }
  if (close_fd)
    close(s->fd);
  free(s);
}
