/* see LICENSE file for copyright and license details */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "mgint.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* elf details file(1) reads from the headers rather than with magic tests */

#define NBUFSIZE   2024
#define NOTES_MAX  256
#define PHNUM_MAX  2048
#define SHNUM_MAX  32768
#define SHSIZE_MAX (128 * 1024 * 1024)

#define ET_REL  1
#define ET_EXEC 2
#define ET_DYN  3

#define PT_DYNAMIC 2
#define PT_INTERP  3
#define PT_NOTE    4

#define SHT_SYMTAB 2
#define SHT_NOTE   7

#define DT_NEEDED  1
#define DT_FLAGS_1 0x6ffffffb
#define DF_1_PIE   0x08000000

#define NT_GNU_VERSION  1
#define NT_GNU_BUILD_ID 3
#define NT_GO_BUILD_ID  4

#define GNU_OS_LINUX    0
#define GNU_OS_HURD     1
#define GNU_OS_SOLARIS  2
#define GNU_OS_KFREEBSD 3
#define GNU_OS_KNETBSD  4

enum ElfFlag { EF_DID_OS_NOTE = 1 << 0, EF_DID_BUILD_ID = 1 << 1 };

struct Elf {
  struct MagicRun *r;
  struct MagicOut *o;
  int              is64;
  int              be;
  long long        fsize; /* -1 when unknown */
  int              flags;
  unsigned         notes; /* notes still allowed */
};

static uint32_t
get32(const struct Elf *e, const unsigned char *p)
{
  if (e->be)
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
  return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
}

static uint16_t
get16(const struct Elf *e, const unsigned char *p)
{
  return e->be ? (uint16_t)(p[0] << 8 | p[1]) : (uint16_t)(p[1] << 8 | p[0]);
}

static uint64_t
get64(const struct Elf *e, const unsigned char *p)
{
  if (e->be)
    return (uint64_t)get32(e, p) << 32 | get32(e, p + 4);
  return (uint64_t)get32(e, p + 4) << 32 | get32(e, p);
}

/* an address sized field: 4 bytes in elf32, 8 in elf64 */
static uint64_t
getaddr(const struct Elf *e, const unsigned char *p32, const unsigned char *p64)
{
  return e->is64 ? get64(e, p64) : get32(e, p32);
}

static void
eprint(struct Elf *e, const char *fmt, long long v)
{
  if (!e->r->mime)
    mg_printf(e->o, fmt, v);
}

/* a buffer may hold only the head of the file, so what lies past it is unknown, not missing */
static void
eunread(struct Elf *e, const char *fmt, long long v)
{
  if (e->r->fd >= 0)
    eprint(e, fmt, v);
}

static void
eputs(struct Elf *e, const char *s)
{
  if (!e->r->mime)
    mg_puts(e->o, s);
}

static int
nameis(const unsigned char *n, uint32_t namesz, const char *v)
{
  return namesz == strlen(v) + 1 && memcmp(n, v, namesz) == 0;
}

static int
os_note(
    struct Elf          *e,
    const unsigned char *nb,
    uint32_t             type,
    uint32_t             namesz,
    uint32_t             descsz,
    size_t               noff,
    size_t               doff
)
{
  static const char   *gnu_os[] = {"Linux", "Hurd", "Solaris", "kFreeBSD", "kNetBSD"};
  const unsigned char *name = nb + noff, *d = nb + doff;
  char                 buf[160];
  uint32_t             os, v;

  if (nameis(name, namesz, "SuSE") && type == NT_GNU_VERSION && descsz == 2) {
    e->flags |= EF_DID_OS_NOTE;
    snprintf(buf, sizeof(buf), ", for SuSE %d.%d", d[0], d[1]);
    eputs(e, buf);
    return 1;
  }
  if (nameis(name, namesz, "GNU") && type == NT_GNU_VERSION && descsz == 16) {
    e->flags |= EF_DID_OS_NOTE;
    os = get32(e, d);
    snprintf(
        buf,
        sizeof(buf),
        ", for GNU/%s %d.%d.%d",
        os <= GNU_OS_KNETBSD ? gnu_os[os] : "<unknown>",
        (int)get32(e, d + 4),
        (int)get32(e, d + 8),
        (int)get32(e, d + 12)
    );
    eputs(e, buf);
    return 1;
  }
  if (nameis(name, namesz, "OpenBSD") && type == 1 && descsz == 4) {
    e->flags |= EF_DID_OS_NOTE;
    eputs(e, ", for OpenBSD");
    return 1;
  }
  if (nameis(name, namesz, "DragonFly") && type == 1 && descsz == 4) {
    e->flags |= EF_DID_OS_NOTE;
    v = get32(e, d);
    snprintf(
        buf,
        sizeof(buf),
        ", for DragonFly %d.%d.%d",
        (int)(v / 100000),
        (int)(v / 10000 % 10),
        (int)(v % 10000)
    );
    eputs(e, buf);
    return 1;
  }
  if (nameis(name, namesz, "Android") && type == 1 && descsz >= 4) {
    e->flags |= EF_DID_OS_NOTE;
    snprintf(buf, sizeof(buf), ", for Android %d", (int)get32(e, d));
    eputs(e, buf);
    if (descsz >= 4 + 64 + 64) {
      snprintf(
          buf,
          sizeof(buf),
          ", built by NDK %.64s (%.64s)",
          (const char *)d + 4,
          (const char *)d + 4 + 64
      );
      eputs(e, buf);
    }
  }
  return 0;
}

static int
bid_note(
    struct Elf          *e,
    const unsigned char *nb,
    uint32_t             type,
    uint32_t             namesz,
    uint32_t             descsz,
    size_t               noff,
    size_t               doff
)
{
  char     buf[300], hex[4];
  uint32_t i;

  if (nameis(nb + noff, namesz, "GNU") && type == NT_GNU_BUILD_ID && descsz >= 4 && descsz <= 20) {
    e->flags |= EF_DID_BUILD_ID;
    snprintf(
        buf,
        sizeof(buf),
        ", BuildID[%s]=",
        descsz == 8    ? "xxHash"
        : descsz == 16 ? "md5/uuid"
        : descsz == 20 ? "sha1"
                       : "unknown"
    );
    eputs(e, buf);
    for (i = 0; i < descsz; i++) {
      snprintf(hex, sizeof(hex), "%02x", nb[doff + i]);
      eputs(e, hex);
    }
    return 1;
  }
  if (namesz == 4 && memcmp(nb + noff, "Go", 3) == 0 && type == NT_GO_BUILD_ID && descsz < 128) {
    memcpy(buf, nb + doff, descsz);
    buf[descsz] = '\0';
    eputs(e, ", Go BuildID=");
    eputs(e, buf);
    return 1;
  }
  return 0;
}

static size_t
elf_align(size_t a, size_t align)
{
  return (a + align - 1) / align * align;
}

/* one note at offset, returns where the next starts or 0 to stop */
static size_t
donote(struct Elf *e, const unsigned char *nb, size_t offset, size_t size, size_t align)
{
  uint32_t namesz, descsz, type;
  size_t   noff, doff;

  if (!e->notes)
    return 0;
  e->notes--;
  if (12 + offset > size)
    return 12 + offset;
  namesz = get32(e, nb + offset);
  descsz = get32(e, nb + offset + 4);
  type   = get32(e, nb + offset + 8);
  offset += 12;
  if (!namesz && !descsz)
    return offset >= size ? offset : size;
  if (namesz & 0x80000000) {
    eprint(e, ", bad note name size %#llx", namesz);
    return 0;
  }
  if (descsz & 0x80000000) {
    eprint(e, ", bad note description size %#llx", descsz);
    return 0;
  }
  noff = offset;
  doff = elf_align(offset + namesz, align);
  if (offset + namesz > size)
    return doff;
  offset = elf_align(doff + descsz, align);
  if (doff + descsz > size)
    return offset >= size ? offset : size;
  if (!(e->flags & EF_DID_OS_NOTE) && os_note(e, nb, type, namesz, descsz, noff, doff))
    return offset;
  if (!(e->flags & EF_DID_BUILD_ID) && bid_note(e, nb, type, namesz, descsz, noff, doff))
    return offset;
  return offset;
}

static void
notes(struct Elf *e, const unsigned char *nb, size_t size, size_t align)
{
  size_t off = 0;

  while (off < size) {
    if (!(off = donote(e, nb, off, size, align)))
      break;
  }
}

/* program headers: linking, interpreter, and notes when there are no sections */
static int
dophn(struct Elf *e, long long off, unsigned num, size_t size, unsigned shnum)
{
  unsigned char ph[56], nb[NBUFSIZE], dyn[16];
  char          interp[NBUFSIZE];
  long long     got, poff;
  uint64_t      tag, val;
  size_t        psize = e->is64 ? 56 : 32, dsize = e->is64 ? 16 : 8, align, len, i;
  uint32_t      type;
  int           dynamic = 0, pie = 0;
  unsigned      need = 0;

  if (!num) {
    eputs(e, ", no program header");
    return 0;
  }
  if (size != psize) {
    eputs(e, ", corrupted program header size");
    return 0;
  }
  interp[0] = '\0';
  for (; num; num--, off += (long long)size) {
    if (mg_pread(e->r, ph, psize, off) < (long long)psize) {
      eunread(e, ", can't read elf program headers at %lld", off);
      return 0;
    }
    type  = get32(e, ph);
    poff  = (long long)getaddr(e, ph + 4, ph + 8);
    len   = (size_t)getaddr(e, ph + 16, ph + 32);
    align = 4;
    switch (type) {
      case PT_NOTE:
        /* the section headers say the same */
        if (shnum)
          continue;
        align = (size_t)getaddr(e, ph + 28, ph + 48);
        if ((align & 0x80000000UL) || align < 4) {
          eprint(e, ", invalid note alignment %#llx", (long long)align);
          align = 4;
        }
        break;
      case PT_DYNAMIC:
      case PT_INTERP:
        break;
      default:
        continue;
    }
    if (len > sizeof(nb))
      len = sizeof(nb);
    if ((got = mg_pread(e->r, nb, len, poff)) < 0) {
      eunread(e, ", can't read section at %lld", poff);
      return 0;
    }
    switch (type) {
      case PT_DYNAMIC:
        dynamic = 1;
        /* DF_1_PIE decides between pie executable and shared object */
        e->r->xmode = 0;
        for (i = 0; i + dsize <= (size_t)got; i += dsize) {
          memcpy(dyn, nb + i, dsize);
          tag = getaddr(e, dyn, dyn);
          val = getaddr(e, dyn + 4, dyn + 8);
          if (tag == DT_FLAGS_1) {
            pie         = (val & DF_1_PIE) != 0;
            e->r->xmode = pie;
          } else if (tag == DT_NEEDED) {
            need++;
          }
        }
        break;
      case PT_INTERP:
        need++;
        if (e->r->mime)
          continue;
        if (got && nb[0]) {
          nb[got - 1] = '\0';
          memcpy(interp, nb, (size_t)got);
        } else {
          strcpy(interp, "*empty*");
        }
        break;
      case PT_NOTE:
        if (e->r->mime)
          return 0;
        notes(e, nb, (size_t)got, align);
        break;
      default:
        break;
    }
  }
  if (e->r->mime)
    return 0;
  mg_puts(
      e->o,
      dynamic ? (pie && !need ? ", static-pie linked" : ", dynamically linked")
              : ", statically linked"
  );
  if (interp[0]) {
    mg_printable((char *)nb, sizeof(nb), interp, sizeof(interp));
    mg_puts(e->o, ", interpreter ");
    mg_puts(e->o, (char *)nb);
  }
  return 0;
}

/* section headers: symbol table, debug info and notes */
static int
doshn(struct Elf *e, long long off, unsigned num, size_t size, unsigned strtab)
{
  unsigned char sh[64], *nb;
  char          name[50];
  long long     name_off, offs, n, soff;
  size_t        ssize = e->is64 ? 64 : 40, len;
  uint32_t      type;
  int           stripped = 1, debug = 0;

  if (e->r->mime)
    return 0;
  if (!num) {
    eputs(e, ", no section header");
    return 0;
  }
  if (size != ssize) {
    eputs(e, ", corrupted section header size");
    return 0;
  }
  offs = off + (long long)(size * strtab);
  if (mg_pread(e->r, sh, ssize, offs) < (long long)ssize) {
    eunread(e, ", missing section headers at %lld", offs);
    return 0;
  }
  name_off = (long long)getaddr(e, sh + 16, sh + 24);
  if (e->fsize >= 0 && e->fsize < name_off) {
    eprint(e, ", too large section header offset %lld", name_off);
    return 0;
  }
  for (; num; num--) {
    /* file(1) reads the name from the header before this one, the first time strtab */
    offs = name_off + get32(e, sh);
    if ((n = mg_pread(e->r, name, sizeof(name) - 1, offs)) < 0) {
      eunread(e, ", can't read name of elf section at %lld", offs);
      return 0;
    }
    name[n] = '\0';
    if (strcmp(name, ".debug_info") == 0) {
      debug    = 1;
      stripped = 0;
    }
    if (mg_pread(e->r, sh, ssize, off) < (long long)ssize) {
      eunread(e, ", can't read elf section at %lld", off);
      return 0;
    }
    off += (long long)size;
    type = get32(e, sh + 4);
    soff = (long long)getaddr(e, sh + 16, sh + 24);
    len  = (size_t)getaddr(e, sh + 20, sh + 32);
    if (type == SHT_SYMTAB) {
      stripped = 0;
      continue;
    }
    if (e->fsize >= 0 && soff > e->fsize)
      continue;
    if (type != SHT_NOTE)
      continue;
    if ((unsigned long long)len + (unsigned long long)soff > (unsigned long long)e->fsize) {
      if (!e->r->mime) {
        mg_printf(
            e->o,
            ", note offset/size %#llx+%#llx exceeds file size %#llx",
            (unsigned long long)soff,
            (unsigned long long)len,
            (unsigned long long)e->fsize
        );
      }
      return 0;
    }
    if (len > SHSIZE_MAX || !(nb = malloc(len ? len : 1)))
      return -1;
    if (mg_pread(e->r, nb, len, soff) < (long long)len) {
      free(nb);
      eunread(e, ", can't read elf note at %lld", soff);
      return 0;
    }
    notes(e, nb, len, 4);
    free(nb);
  }
  if (debug)
    eputs(e, ", with debug_info");
  eputs(e, stripped ? ", stripped" : ", not stripped");
  return 0;
}

/*
 * the elf details file(1) appends to the magic description: linking,
 * interpreter, notes and stripping. also decides r->xmode for the pie
 * test of the elf magic. 0 when the head is not elf
 */
int
mg_elf(struct MagicRun *r, struct MagicOut *o)
{
  struct Elf    e;
  unsigned char h[64];
  unsigned      type, phnum, shnum;
  long long     phoff, shoff;

  if (r->head.n <= 5)
    return 0;
  if (r->head.p[0] != 0x7f || (r->head.p[1] != 'E' && r->head.p[1] != 'O') || r->head.p[2] != 'L'
      || r->head.p[3] != 'F')
    return 0;
  memset(&e, 0, sizeof(e));
  e.r     = r;
  e.o     = o;
  e.fsize = r->fsize;
  e.notes = NOTES_MAX;
  memset(h, 0, sizeof(h));
  memcpy(h, r->head.p, r->head.n < sizeof(h) ? r->head.n : sizeof(h));
  switch (h[4]) {
    case 1:
      e.is64 = 0;
      break;
    case 2:
      e.is64 = 1;
      break;
    default:
      eprint(&e, ", unknown class %lld", h[4]);
      return 1;
  }
  /* file(1) wants more than the elf header itself */
  if (r->head.n <= (e.is64 ? 64u : 52u))
    return 0;
  e.be  = h[5] != 1;
  type  = get16(&e, h + 16);
  phoff = (long long)getaddr(&e, h + 28, h + 32);
  shoff = (long long)getaddr(&e, h + 32, h + 40);
  phnum = get16(&e, h + (e.is64 ? 56 : 44));
  shnum = get16(&e, h + (e.is64 ? 60 : 48));
  switch (type) {
    case ET_EXEC:
    case ET_DYN:
      if (phnum > PHNUM_MAX) {
        eprint(&e, ", too many program (%lld)", phnum);
        return 1;
      }
      if (shnum > SHNUM_MAX) {
        eprint(&e, ", too many section (%lld)", shnum);
        return 1;
      }
      if (dophn(&e, phoff, phnum, get16(&e, h + (e.is64 ? 54 : 42)), shnum) < 0)
        return -1;
      /* fallthrough */
    case ET_REL:
      if (shnum > SHNUM_MAX) {
        eprint(&e, ", too many section headers (%lld)", shnum);
        return 1;
      }
      if (doshn(
              &e, shoff, shnum, get16(&e, h + (e.is64 ? 58 : 46)), get16(&e, h + (e.is64 ? 62 : 50))
          )
          < 0)
        return -1;
      break;
    default:
      break;
  }
  if (!e.notes)
    eprint(&e, ", too many notes (%lld)", NOTES_MAX);
  return 1;
}
