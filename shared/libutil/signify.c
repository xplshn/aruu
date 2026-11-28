/* see LICENSE file for copyright and license details */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "../base64.h"
#include "../sha512.h"
#include "../signify.h"
#include "../util.h"

#define COMMENT "untrusted comment: "

/* the layouts signify writes, byte for byte */
struct pubfile {
  uint8_t alg[2];
  uint8_t keynum[8];
  uint8_t pk[ED25519_PUBLIC];
};

struct secfile {
  uint8_t alg[2];
  uint8_t kdfalg[2];
  uint8_t rounds[4];
  uint8_t salt[16];
  uint8_t checksum[8];
  uint8_t keynum[8];
  uint8_t sk[ED25519_SECRET];
};

struct sigfile {
  uint8_t alg[2];
  uint8_t keynum[8];
  uint8_t sig[ED25519_SIG];
};

static int
randombytes(uint8_t *buf, size_t n)
{
  int fd = open("/dev/urandom", O_RDONLY);
  ssize_t r;
  size_t got = 0;

  if (fd < 0) {
    weprintf("open /dev/urandom:");
    return -1;
  }
  while (got < n && (r = read(fd, buf + got, n - got)) > 0)
    got += (size_t)r;
  close(fd);
  if (got < n) {
    weprintf("read /dev/urandom:");
    return -1;
  }
  return 0;
}

/* comment and blob into path, which must not be there yet */
static int
writefile(const char *path, const char *comment, const void *blob, size_t n, int mode)
{
  char b64[256];
  FILE *fp;
  int fd;

  if ((fd = open(path, O_WRONLY | O_CREAT | O_EXCL, mode)) < 0) {
    weprintf("open %s:", path);
    return -1;
  }
  if (!(fp = fdopen(fd, "w"))) {
    close(fd);
    return -1;
  }
  base64_encode(b64, blob, n);
  fprintf(fp, COMMENT "%s\n%s\n", comment, b64);
  return fshut(fp, path) ? -1 : 0;
}

/* the blob of path's second line, exactly n bytes of it */
static int
readfile(const char *path, void *blob, size_t n)
{
  char line[512];
  FILE *fp;
  int ok;

  if (!(fp = fopen(path, "r"))) {
    weprintf("open %s:", path);
    return -1;
  }
  ok = fgets(line, sizeof(line), fp) && !strncmp(line, COMMENT, strlen(COMMENT)) &&
       fgets(line, sizeof(line), fp) &&
       base64_decode(blob, n, line, strlen(line)) == (long)n;
  fclose(fp);
  if (!ok) {
    weprintf("%s: not a signify file of the right kind\n", path);
    return -1;
  }
  if (memcmp(blob, "Ed", 2)) {
    weprintf("%s: not an ed25519 key or signature\n", path);
    return -1;
  }
  return 0;
}

static void
checksum(uint8_t out[8], const uint8_t sk[ED25519_SECRET])
{
  struct sha512 h;
  uint8_t d[SHA512_DIGEST_LENGTH];

  sha512_init(&h);
  sha512_update(&h, sk, ED25519_SECRET);
  sha512_sum(&h, d);
  memcpy(out, d, 8);
}

int
signify_generate(const char *pubpath, const char *secpath, const char *comment)
{
  struct pubfile pub;
  struct secfile sec;
  uint8_t seed[ED25519_SEED];
  char c[200];
  int rc;

  memset(&sec, 0, sizeof(sec));
  if (randombytes(seed, sizeof(seed)) || randombytes(sec.keynum, sizeof(sec.keynum)) ||
      randombytes(sec.salt, sizeof(sec.salt)))
    return -1;
  ed25519_keypair(pub.pk, sec.sk, seed);
  memcpy(sec.alg, "Ed", 2);
  memcpy(sec.kdfalg, "BK", 2);
  checksum(sec.checksum, sec.sk);
  memcpy(pub.alg, "Ed", 2);
  memcpy(pub.keynum, sec.keynum, 8);

  snprintf(c, sizeof(c), "%s secret key", comment);
  rc = writefile(secpath, c, &sec, sizeof(sec), 0600);
  if (rc == 0) {
    snprintf(c, sizeof(c), "%s public key", comment);
    rc = writefile(pubpath, c, &pub, sizeof(pub), 0644);
  }
  memset(&sec, 0, sizeof(sec));
  memset(seed, 0, sizeof(seed));
  return rc;
}

int
signify_read_pub(const char *path, struct signify_pub *out)
{
  struct pubfile f;

  if (readfile(path, &f, sizeof(f)))
    return -1;
  memcpy(out->keynum, f.keynum, 8);
  memcpy(out->pk, f.pk, ED25519_PUBLIC);
  return 0;
}

int
signify_read_sec(const char *path, struct signify_sec *out)
{
  struct secfile f;
  uint8_t sum[8];

  if (readfile(path, &f, sizeof(f)))
    return -1;
  if (memcmp(f.kdfalg, "BK", 2) || f.rounds[0] | f.rounds[1] | f.rounds[2] | f.rounds[3]) {
    weprintf("%s: a key with a passphrase, make one without (signify -n)\n", path);
    return -1;
  }
  checksum(sum, f.sk);
  if (memcmp(sum, f.checksum, 8)) {
    weprintf("%s: the key does not match its checksum\n", path);
    return -1;
  }
  memcpy(out->keynum, f.keynum, 8);
  memcpy(out->sk, f.sk, ED25519_SECRET);
  memset(&f, 0, sizeof(f));
  return 0;
}

int
signify_sign(const struct signify_sec *sec, const uint8_t *m, size_t n, const char *pubname,
             const char *sigpath)
{
  struct sigfile f;
  char c[300];

  memcpy(f.alg, "Ed", 2);
  memcpy(f.keynum, sec->keynum, 8);
  ed25519_sign(f.sig, m, n, sec->sk);
  snprintf(c, sizeof(c), "verify with %s", pubname);
  unlink(sigpath);
  return writefile(sigpath, c, &f, sizeof(f), 0644);
}

int
signify_verify(const struct signify_pub *pub, const uint8_t *m, size_t n, const char *sigpath)
{
  struct sigfile f;

  if (readfile(sigpath, &f, sizeof(f)))
    return -1;
  if (memcmp(f.keynum, pub->keynum, 8)) {
    weprintf("%s: signed with another key\n", sigpath);
    return -1;
  }
  if (ed25519_verify(f.sig, m, n, pub->pk)) {
    weprintf("%s: the signature does not match\n", sigpath);
    return -1;
  }
  return 0;
}
