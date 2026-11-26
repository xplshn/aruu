/* see LICENSE file for copyright and license details */
// ?man signify: make keys, sign and verify files
// ?man arguments: -G [-n] [-c comment] -p pubkey -s seckey | -S -s seckey -m message [-x sigfile] | -V [-q] -p pubkey -m message [-x sigfile]
// ?man signify signs a file with an ed25519 key and checks such a signature,
// ?man in the files openbsd's signify reads and writes. a secret key is kept
// ?man without a passphrase. the signature of message is message.sig
// ?man unless another file is named
#include "arg.h"
#include "signify.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
usage(void)
{
  fprintf(stderr,
          "usage: %s -G [-n] [-c comment] -p pubkey -s seckey\n"
          "       %s -S -s seckey -m message [-x sigfile]\n"
          "       %s -V [-q] -p pubkey -m message [-x sigfile]\n",
          argv0, argv0, argv0);
  exit(1);
}

static uint8_t *
slurp(const char *path, size_t *n)
{
  FILE *fp = fopen(path, "rb");
  uint8_t *buf = NULL;
  size_t cap = 0, r;

  if (!fp)
    eprintf("open %s:", path);
  *n = 0;
  do {
    if (*n == cap)
      buf = erealloc(buf, cap = cap ? cap * 2 : 65536);
    r = fread(buf + *n, 1, cap - *n, fp);
    *n += r;
  } while (r > 0);
  if (ferror(fp))
    eprintf("read %s:", path);
  fclose(fp);
  return buf;
}

int
main(int argc, char *argv[])
{
  const char *pub = NULL, *sec = NULL, *msg = NULL, *sig = NULL, *comment = "signify";
  char sigbuf[4096];
  int mode = 0, quiet = 0;
  uint8_t *m;
  size_t n;

  ARGBEGIN
  {
    // ?man -G: make a key pair
    case 'G':
    // ?man -S: sign message
    case 'S':
    // ?man -V: verify message
    case 'V':
      if (mode)
        usage();
      mode = ARGC();
      break;
    // ?man -c:str: -c comment: the comment of a new key pair
    case 'c':
      comment = EARGF(usage());
      break;
    // ?man -m:str: -m message: the file signed or checked
    case 'm':
      msg = EARGF(usage());
      break;
    // ?man -n: a key without a passphrase, which is the only kind made here
    case 'n':
      break;
    // ?man -p:str: -p pubkey: the public key
    case 'p':
      pub = EARGF(usage());
      break;
    // ?man -q: say nothing when the signature matches
    case 'q':
      quiet = 1;
      break;
    // ?man -s:str: -s seckey: the secret key
    case 's':
      sec = EARGF(usage());
      break;
    // ?man -x:str: -x sigfile: the signature, message.sig when not given
    case 'x':
      sig = EARGF(usage());
      break;
    default:
      usage();
  }
  ARGEND

  if (argc)
    usage();
  if (mode != 'G' && msg && !sig) {
    snprintf(sigbuf, sizeof(sigbuf), "%s.sig", msg);
    sig = sigbuf;
  }
  switch (mode) {
  case 'G':
    if (!pub || !sec)
      usage();
    return signify_generate(pub, sec, comment) != 0;
  case 'S': {
    struct signify_sec k;
    const char *base;
    char pubname[1024];
    size_t l;

    if (!sec || !msg)
      usage();
    if (signify_read_sec(sec, &k))
      return 1;
    // the comment names the public key the way signify does: the secret
    // key's name with .pub for .sec
    base = strrchr(sec, '/');
    base = base ? base + 1 : sec;
    l = strlen(base);
    if (l > 4 && !strcmp(base + l - 4, ".sec"))
      snprintf(pubname, sizeof(pubname), "%.*s.pub", (int)(l - 4), base);
    else
      snprintf(pubname, sizeof(pubname), "%s", base);
    m = slurp(msg, &n);
    return signify_sign(&k, m, n, pubname, sig) != 0;
  }
  case 'V': {
    struct signify_pub k;

    if (!pub || !msg)
      usage();
    if (signify_read_pub(pub, &k))
      return 1;
    m = slurp(msg, &n);
    if (signify_verify(&k, m, n, sig))
      return 1;
    if (!quiet)
      puts("Signature Verified");
    return 0;
  }
  }
  usage();
}
