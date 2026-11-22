/* see LICENSE file for copyright and license details */

/* xelf: records inside an executable, as ELF notes
 *
 * a record is one note owned by "xELF": type 1 a manifest (text), 2 the
 * rules the program serves to a plumber (text), 3 an icon (image(6)
 * r8g8b8a8) and 4 an icon in svg, the last two after a line "size <px>"
 * (0 for scalable). type 0 is a record that was taken out. readers walk
 * the program headers and ignore any type or note they do not know, so
 * a strip that keeps the segments keeps the records
 *
 * adding records to a program that has none (or has them) works as
 * patchelf does: the notes go in a segment of their own at the end of
 * the file, with a copy of the program header table that has room for
 * it, so no section moves and nothing needs relocating. what was there
 * before is made type 0, and its segment becomes unused */

#include "util.h"

#include <elf.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define OWNER      "xELF"
#define OWNER_SZ   5
#define NHDR       12
#define REC_TEXT   (512 * 1024)
#define REC_BINARY (1024 * 1024)
#define TOTAL_MAX  (5 * 1024 * 1024)
#define PAGE_MIN   4096
#define IMAGE_HDR  60
#define IMAGE_CHAN "r8g8b8a8"

enum { T_DEAD, T_MANIFEST, T_PLUMB, T_ICON, T_SVG };

struct elf {
  unsigned char *b;
  size_t         n;
  int            wide, big;
};

struct ph {
  uint32_t type, flags;
  uint64_t offset, vaddr, paddr, filesz, memsz, align;
};

struct rec {
  uint32_t       type;
  unsigned char *data;
  size_t         len;
};

static uint64_t
get(const struct elf *e, size_t off, int size)
{
  uint64_t v = 0;
  int      i;

  if (off + size > e->n)
    eprintf("%s: truncated\n", argv0);
  for (i = 0; i < size; i++)
    v |= (uint64_t)e->b[off + (e->big ? size - 1 - i : i)] << (8 * i);
  return v;
}

static void
put(const struct elf *e, unsigned char *at, uint64_t v, int size)
{
  int i;

  for (i = 0; i < size; i++)
    at[e->big ? size - 1 - i : i] = (unsigned char)(v >> (8 * i));
}

static unsigned char *
slurp(const char *path, size_t *len)
{
  FILE          *fp = fopen(path, "rb");
  unsigned char *b;
  long           n;

  if (!fp)
    eprintf("open %s:", path);
  fseek(fp, 0, SEEK_END);
  n = ftell(fp);
  rewind(fp);
  b = emalloc(n ? n : 1);
  if (fread(b, 1, n, fp) != (size_t)n)
    eprintf("read %s:", path);
  fclose(fp);
  *len = n;
  return b;
}

static void
load(struct elf *e, const char *path)
{
  e->b = slurp(path, &e->n);
  if (e->n < 52 || memcmp(e->b, ELFMAG, SELFMAG))
    eprintf("%s: not an elf file\n", path);
  if (e->b[EI_CLASS] != ELFCLASS32 && e->b[EI_CLASS] != ELFCLASS64)
    eprintf("%s: unknown elf class\n", path);
  e->wide = e->b[EI_CLASS] == ELFCLASS64;
  e->big  = e->b[EI_DATA] == ELFDATA2MSB;
  if (get(e, 16, 2) != ET_EXEC && get(e, 16, 2) != ET_DYN)
    eprintf("%s: not an executable or shared object\n", path);
}

/* the header's fields, by where each class keeps them */
static uint64_t
phoff(const struct elf *e)
{
  return e->wide ? get(e, 32, 8) : get(e, 28, 4);
}

static int
phentsize(const struct elf *e)
{
  return e->wide ? 56 : 32;
}

static int
phnum(const struct elf *e)
{
  return (int)get(e, e->wide ? 56 : 44, 2);
}

/* section headers, where a program has them: strip and its kin keep what
 * a section names, so the notes get one too */
#define SEC_NAME ".note.xelf"

struct sh {
  uint32_t name, type;
  uint64_t flags, addr, offset, size, align;
};

static uint64_t
shoff(const struct elf *e)
{
  return e->wide ? get(e, 40, 8) : get(e, 32, 4);
}

static int
shentsize(const struct elf *e)
{
  return e->wide ? 64 : 40;
}

static int
shnum(const struct elf *e)
{
  return shoff(e) ? (int)get(e, e->wide ? 60 : 48, 2) : 0;
}

static int
shstrndx(const struct elf *e)
{
  return (int)get(e, e->wide ? 62 : 50, 2);
}

static struct sh
sh_read(const struct elf *e, const unsigned char *t)
{
  struct elf v = *e;
  struct sh  s;

  v.b = (unsigned char *)t;
  v.n = shentsize(e);
  s.name = (uint32_t)get(&v, 0, 4);
  s.type = (uint32_t)get(&v, 4, 4);
  if (e->wide) {
    s.flags  = get(&v, 8, 8);
    s.addr   = get(&v, 16, 8);
    s.offset = get(&v, 24, 8);
    s.size   = get(&v, 32, 8);
    s.align  = get(&v, 48, 8);
  } else {
    s.flags  = get(&v, 8, 4);
    s.addr   = get(&v, 12, 4);
    s.offset = get(&v, 16, 4);
    s.size   = get(&v, 20, 4);
    s.align  = get(&v, 32, 4);
  }
  return s;
}

static void
sh_write(const struct elf *e, unsigned char *at, const struct sh *s)
{
  put(e, at, s->name, 4);
  put(e, at + 4, s->type, 4);
  if (e->wide) {
    put(e, at + 8, s->flags, 8);
    put(e, at + 16, s->addr, 8);
    put(e, at + 24, s->offset, 8);
    put(e, at + 32, s->size, 8);
    put(e, at + 48, s->align, 8);
  } else {
    put(e, at + 8, s->flags, 4);
    put(e, at + 12, s->addr, 4);
    put(e, at + 16, s->offset, 4);
    put(e, at + 20, s->size, 4);
    put(e, at + 32, s->align, 4);
  }
}

/* the section table after the segment: a copy with the notes' section
 * added and the table of names grown to hold its name. an earlier
 * section of the notes is emptied, so repeated adds do not pile them up.
 * the bytes go to *tail, from *at on (which they move past) */
static void
sections_tail(struct elf *e, uint64_t note_off, uint64_t note_addr, size_t note_len, unsigned char **tail, size_t *tail_len, size_t *at)
{
  int            n = shnum(e), si = shstrndx(e), i, ent = shentsize(e);
  struct sh      names, add_sh;
  unsigned char *table, *strs;
  size_t         strs_at, table_at, name_off;

  if (!n || shoff(e) + (uint64_t)n * ent > e->n || si >= n)
    return;
  names = sh_read(e, e->b + shoff(e) + (size_t)si * ent);
  if (names.offset + names.size > e->n)
    return;
  strs_at   = (*at + 7) & ~(size_t)7;
  name_off  = names.size;
  strs      = ecalloc(1, names.size + sizeof SEC_NAME);
  memcpy(strs, e->b + names.offset, names.size);
  memcpy(strs + name_off, SEC_NAME, sizeof SEC_NAME);
  table_at  = (strs_at + names.size + sizeof SEC_NAME + 7) & ~(size_t)7;
  table     = ecalloc(n + 1, ent);
  memcpy(table, e->b + shoff(e), (size_t)n * ent);
  for (i = 0; i < n; i++) {
    struct sh x = sh_read(e, table + (size_t)i * ent);

    if (x.name < names.size && !strcmp((char *)strs + x.name, SEC_NAME))
      memset(table + (size_t)i * ent, 0, ent);
  }
  names.offset = strs_at;
  names.size += sizeof SEC_NAME;
  sh_write(e, table + (size_t)si * ent, &names);
  memset(&add_sh, 0, sizeof add_sh);
  add_sh.name   = (uint32_t)name_off;
  add_sh.type   = SHT_NOTE;
  add_sh.flags  = SHF_ALLOC;
  add_sh.addr   = note_addr;
  add_sh.offset = note_off;
  add_sh.size   = note_len;
  add_sh.align  = 4;
  sh_write(e, table + (size_t)n * ent, &add_sh);

  *tail_len = table_at + (size_t)(n + 1) * ent - *at;
  *tail     = ecalloc(1, *tail_len);
  memcpy(*tail + (strs_at - *at), strs, names.size);
  memcpy(*tail + (table_at - *at), table, (size_t)(n + 1) * ent);
  if (e->wide) {
    put(e, e->b + 40, table_at, 8);
    put(e, e->b + 60, n + 1, 2);
  } else {
    put(e, e->b + 32, table_at, 4);
    put(e, e->b + 48, n + 1, 2);
  }
  free(strs);
  free(table);
}

static struct ph
ph_read(const struct elf *e, int i)
{
  size_t    at = phoff(e) + (size_t)i * phentsize(e);
  struct ph p;

  p.type = (uint32_t)get(e, at, 4);
  if (e->wide) {
    p.flags  = (uint32_t)get(e, at + 4, 4);
    p.offset = get(e, at + 8, 8);
    p.vaddr  = get(e, at + 16, 8);
    p.paddr  = get(e, at + 24, 8);
    p.filesz = get(e, at + 32, 8);
    p.memsz  = get(e, at + 40, 8);
    p.align  = get(e, at + 48, 8);
  } else {
    p.offset = get(e, at + 4, 4);
    p.vaddr  = get(e, at + 8, 4);
    p.paddr  = get(e, at + 12, 4);
    p.filesz = get(e, at + 16, 4);
    p.memsz  = get(e, at + 20, 4);
    p.flags  = (uint32_t)get(e, at + 24, 4);
    p.align  = get(e, at + 28, 4);
  }
  return p;
}

static void
ph_write(const struct elf *e, unsigned char *at, const struct ph *p)
{
  put(e, at, p->type, 4);
  if (e->wide) {
    put(e, at + 4, p->flags, 4);
    put(e, at + 8, p->offset, 8);
    put(e, at + 16, p->vaddr, 8);
    put(e, at + 24, p->paddr, 8);
    put(e, at + 32, p->filesz, 8);
    put(e, at + 40, p->memsz, 8);
    put(e, at + 48, p->align, 8);
  } else {
    put(e, at + 4, p->offset, 4);
    put(e, at + 8, p->vaddr, 4);
    put(e, at + 12, p->paddr, 4);
    put(e, at + 16, p->filesz, 4);
    put(e, at + 20, p->memsz, 4);
    put(e, at + 24, p->flags, 4);
    put(e, at + 28, p->align, 4);
  }
}

/* one note of a segment: where it starts, its type, whether xELF owns
 * it, and where its desc is. returns the offset of the next, 0 at the end
 * or on a note that does not fit, which is the end of what can be read */
static size_t
note_at(const struct elf *e, size_t at, size_t end, uint32_t *type, int *ours, size_t *desc, size_t *len)
{
  uint64_t namesz, descsz;
  size_t   next;

  if (at + NHDR > end)
    return 0;
  namesz = get(e, at, 4);
  descsz = get(e, at + 4, 4);
  *type  = (uint32_t)get(e, at + 8, 4);
  next   = at + NHDR + ((namesz + 3) & ~3ull);
  *desc  = next;
  *len   = descsz;
  next += (descsz + 3) & ~3ull;
  if (next > end || *desc + descsz > end)
    return 0;
  *ours = namesz == OWNER_SZ && !memcmp(e->b + at + NHDR, OWNER, OWNER_SZ);
  return next;
}

/* calls f on each note of every PT_NOTE segment */
typedef void (*note_fn)(struct elf *, size_t at, uint32_t type, size_t desc, size_t len, void *arg);

static void
each_note(struct elf *e, note_fn f, void *arg)
{
  int i;

  for (i = 0; i < phnum(e); i++) {
    struct ph p = ph_read(e, i);
    size_t    at, end, next, desc, len;
    uint32_t  type;
    int       ours;

    if (p.type != PT_NOTE)
      continue;
    end = p.offset + p.filesz;
    if (end > e->n)
      eprintf("%s: a note segment past the end of the file\n", argv0);
    for (at = p.offset; at < end; at = next) {
      next = note_at(e, at, end, &type, &ours, &desc, &len);
      if (!next)
        break;
      if (ours)
        f(e, at, type, desc, len, arg);
    }
  }
}

/* taking a record out: its type zero and its bytes too */
static void
kill_note(struct elf *e, size_t at, uint32_t type, size_t desc, size_t len, void *arg)
{
  (void)type;
  (void)arg;
  put(e, e->b + at + 8, T_DEAD, 4);
  memset(e->b + desc, 0, len);
}

/* a note segment with no note of anyone else is of no use once its
 * records are dead: it becomes unused, so the table does not grow */
static void
drop_note_segments(struct elf *e)
{
  int i;

  for (i = 0; i < phnum(e); i++) {
    struct ph p = ph_read(e, i);
    size_t    at, end, next, desc, len;
    uint32_t  type;
    int       ours, others = 0;

    if (p.type != PT_NOTE)
      continue;
    end = p.offset + p.filesz;
    for (at = p.offset; at < end; at = next) {
      next = note_at(e, at, end, &type, &ours, &desc, &len);
      if (!next)
        break;
      if (!ours)
        others = 1;
    }
    if (!others)
      put(e, e->b + phoff(e) + (size_t)i * phentsize(e), PT_NULL, 4);
  }
}

static void
dump_note(struct elf *e, size_t at, uint32_t type, size_t desc, size_t len, void *arg)
{
  (void)at;
  (void)arg;
  printf("type %u\n", type);
  if (type == T_MANIFEST || type == T_PLUMB || type == T_SVG) {
    fwrite(e->b + desc, 1, len, stdout);
    if (!len || e->b[desc + len - 1] != '\n')
      putchar('\n');
  } else if (type == T_ICON) {
    size_t n = 0;

    while (n < len && e->b[desc + n] != '\n')
      n++;
    printf("%.*s (%zu bytes of image)\n", (int)n, e->b + desc, len - n - 1);
  } else {
    printf("%zu bytes\n", len);
  }
}

/* the file as it is, then zeros to at, then extra, then more */
static void
write_out(const struct elf *e, const char *path, const unsigned char *extra, size_t extra_len, size_t at,
          const unsigned char *more, size_t more_len)
{
  char        tmp[4096];
  struct stat st;
  FILE       *fp;
  size_t      pad;

  if (stat(path, &st) < 0)
    eprintf("stat %s:", path);
  snprintf(tmp, sizeof tmp, "%s.xelf", path);
  fp = fopen(tmp, "wb");
  if (!fp)
    eprintf("create %s:", tmp);
  fwrite(e->b, 1, e->n, fp);
  for (pad = e->n; pad < at; pad++)
    fputc(0, fp);
  fwrite(extra, 1, extra_len, fp);
  fwrite(more, 1, more_len, fp);
  if (fclose(fp) || chmod(tmp, st.st_mode & 07777) < 0 || rename(tmp, path) < 0) {
    unlink(tmp);
    eprintf("write %s:", path);
  }
}

/* an icon file as a record: svg as it is, anything else must be image(6)
 * r8g8b8a8, whose width is the size the record says */
static struct rec
icon_rec(const char *path)
{
  struct rec     r;
  size_t         n, len;
  unsigned char *file = slurp(path, &len), *d;
  char           head[32];
  const char    *dot = strrchr(path, '.');

  if (dot && !strcmp(dot, ".svg")) {
    r.type = T_SVG;
    snprintf(head, sizeof head, "size 0\n");
  } else {
    long minx, maxx, size;

    if (len < IMAGE_HDR || memcmp(file, IMAGE_CHAN, strlen(IMAGE_CHAN)))
      eprintf("%s: not an svg or an image(6) of %s\n", path, IMAGE_CHAN);
    /* the header is the channels, then minx miny maxx maxy, each padded
     * to 11 and followed by a space */
    minx = strtol((char *)file + 12, NULL, 10);
    maxx = strtol((char *)file + 36, NULL, 10);
    size = maxx - minx;
    if (size <= 0)
      eprintf("%s: an image with no width\n", path);
    r.type = T_ICON;
    snprintf(head, sizeof head, "size %ld\n", size);
  }
  n = strlen(head);
  d = emalloc(n + len);
  memcpy(d, head, n);
  memcpy(d + n, file, len);
  free(file);
  r.data = d;
  r.len  = n + len;
  return r;
}

static struct rec
text_rec(uint32_t type, const char *path)
{
  struct rec r;

  r.type = type;
  r.data = slurp(path, &r.len);
  if (r.len > REC_TEXT)
    eprintf("%s: a text record is at most %d bytes\n", path, REC_TEXT);
  return r;
}

/* the records as notes, each padded to 4 */
static unsigned char *
notes_of(const struct elf *e, const struct rec *r, int n, size_t *len)
{
  size_t         total = 0, at = 0;
  unsigned char *buf;
  int            i;

  for (i = 0; i < n; i++) {
    if ((r[i].type == T_ICON || r[i].type == T_SVG) && r[i].len > REC_BINARY)
      eprintf("%s: an icon record is at most %d bytes\n", argv0, REC_BINARY);
    total += NHDR + 8 + ((r[i].len + 3) & ~(size_t)3);
  }
  if (total > TOTAL_MAX)
    eprintf("%s: records of %zu bytes, at most %d\n", argv0, total, TOTAL_MAX);
  buf = ecalloc(1, total);
  for (i = 0; i < n; i++) {
    put(e, buf + at, OWNER_SZ, 4);
    put(e, buf + at + 4, r[i].len, 4);
    put(e, buf + at + 8, r[i].type, 4);
    memcpy(buf + at + NHDR, OWNER, OWNER_SZ);
    memcpy(buf + at + NHDR + 8, r[i].data, r[i].len);
    at += NHDR + 8 + ((r[i].len + 3) & ~(size_t)3);
  }
  *len = total;
  return buf;
}

/* the segment the notes go in: a copy of the program header table with
 * two more entries, then the notes. its address is above every segment
 * there is, and congruent to its place in the file by the biggest
 * alignment the file uses, so any page size maps it */
static void
add(struct elf *e, const char *path, const struct rec *recs, int nrecs)
{
  int            n = phnum(e), i, last_load = -1, m;
  uint64_t       top = 0, align = PAGE_MIN, off, vaddr, ptsz;
  struct ph     *old = emalloc((n + 2) * sizeof *old), load_ph, note_ph;
  unsigned char *table, *notes, *seg, *tail = NULL;
  size_t         notes_len, seg_len, tail_len = 0, tail_at;

  each_note(e, kill_note, NULL);
  drop_note_segments(e);
  for (i = 0; i < n; i++) {
    old[i] = ph_read(e, i);
    if (old[i].type != PT_LOAD)
      continue;
    last_load = i;
    if (old[i].vaddr + old[i].memsz > top)
      top = old[i].vaddr + old[i].memsz;
    if (old[i].align > align)
      align = old[i].align;
  }
  if (last_load < 0)
    eprintf("%s: no loadable segment\n", path);
  m     = n + 2;
  ptsz  = (uint64_t)m * phentsize(e);
  notes = notes_of(e, recs, nrecs, &notes_len);

  off   = (e->n + align - 1) & ~(align - 1);
  vaddr = ((top + align - 1) & ~(align - 1)) + (off & (align - 1));
  /* off is a multiple of align, so the low bits added to vaddr are none */
  seg_len = ptsz + notes_len;
  seg     = ecalloc(1, seg_len);
  table   = seg;
  tail_at = off + seg_len;

  memset(&load_ph, 0, sizeof load_ph);
  load_ph.type   = PT_LOAD;
  load_ph.flags  = PF_R;
  load_ph.offset = off;
  load_ph.vaddr  = vaddr;
  load_ph.paddr  = vaddr;
  load_ph.filesz = seg_len;
  load_ph.memsz  = seg_len;
  load_ph.align  = align;
  note_ph        = load_ph;
  note_ph.type   = PT_NOTE;
  note_ph.offset = off + ptsz;
  note_ph.vaddr  = vaddr + ptsz;
  note_ph.paddr  = vaddr + ptsz;
  note_ph.filesz = notes_len;
  note_ph.memsz  = notes_len;
  note_ph.align  = 4;
  note_ph.flags  = PF_R;

  /* the new load follows the last old one, so loads stay in address order,
   * the note segment goes at the end */
  for (i = 0; i < m; i++) {
    struct ph p;

    if (i <= last_load)
      p = old[i];
    else if (i == last_load + 1)
      p = load_ph;
    else if (i == m - 1)
      p = note_ph;
    else
      p = old[i - 1];
    if (p.type == PT_PHDR) {
      p.offset = off;
      p.vaddr  = vaddr;
      p.paddr  = vaddr;
      p.filesz = ptsz;
      p.memsz  = ptsz;
    }
    ph_write(e, table + (size_t)i * phentsize(e), &p);
  }
  memcpy(seg + ptsz, notes, notes_len);
  sections_tail(e, off + ptsz, vaddr + ptsz, notes_len, &tail, &tail_len, &tail_at);

  /* the header now points at the copy */
  if (e->wide) {
    put(e, e->b + 32, off, 8);
    put(e, e->b + 56, m, 2);
  } else {
    put(e, e->b + 28, off, 4);
    put(e, e->b + 44, m, 2);
  }
  write_out(e, path, seg, seg_len, off, tail, tail_len);
  free(tail);
  free(old);
  free(notes);
  free(seg);
}

static void
check_load_order(const struct elf *e, const char *path)
{
  int      i, n = phnum(e), seen_load = 0, phdr_ok = 1;
  uint64_t prev = 0, tbl = phoff(e), tblend = phoff(e) + (uint64_t)n * phentsize(e);
  int      in_load = 0;

  for (i = 0; i < n; i++) {
    struct ph p = ph_read(e, i);

    if (p.type == PT_LOAD) {
      uint64_t a = p.align > 1 ? p.align : 1;

      if (seen_load && p.vaddr < prev)
        eprintf("%s: loads are not in address order\n", path);
      prev      = p.vaddr;
      seen_load = 1;
      if (p.align > 1 && (p.vaddr % a) != (p.offset % a))
        eprintf("%s: a load whose address and offset differ modulo its alignment\n", path);
      if (p.offset + p.filesz > e->n)
        eprintf("%s: a load past the end of the file\n", path);
      if (tbl >= p.offset && tblend <= p.offset + p.filesz)
        in_load = 1;
    } else if (p.type == PT_PHDR && seen_load) {
      phdr_ok = 0;
    }
  }
  if (!phdr_ok)
    eprintf("%s: the PT_PHDR entry follows a load\n", path);
  if (!in_load)
    eprintf("%s: the program header table is in no load segment\n", path);
}

static void
check_note(struct elf *e, size_t at, uint32_t type, size_t desc, size_t len, void *arg)
{
  (void)e;
  (void)at;
  (void)type;
  (void)desc;
  (void)len;
  ++*(int *)arg;
}

static void
usage(void)
{
  eprintf("usage: %s add [-p plumb] bin manifest [icon ...] | dump bin | rm bin | check bin\n", argv0);
}

// ?man xelf: records inside an executable
// ?man arguments: add [-p plumb] bin manifest [icon ...] | dump bin | rm bin | check bin
// ?man keeps records in ELF notes owned by xELF: a manifest, the rules the
// ?man program serves to a plumber and icons (image(6) or svg). add
// ?man replaces what the program carries, dump prints it, rm takes it
// ?man out and check says whether the program is laid out as a loader
// ?man needs it
int
main(int argc, char *argv[])
{
  struct elf e;
  const char *plumb = NULL;
  const char *verb;
  int         count = 0;

  argv0 = argc ? argv[0] : "xelf";
  if (argc < 3)
    usage();
  verb = argv[1];
  argv += 2;
  argc -= 2;

  if (!strcmp(verb, "add")) {
    struct rec *recs;
    int         n = 0;

    if (!strcmp(argv[0], "-p")) {
      if (argc < 4)
        usage();
      plumb = argv[1];
      argv += 2;
      argc -= 2;
    }
    if (argc < 2)
      usage();
    load(&e, argv[0]);
    recs      = ecalloc(argc + 1, sizeof *recs);
    recs[n++] = text_rec(T_MANIFEST, argv[1]);
    if (plumb)
      recs[n++] = text_rec(T_PLUMB, plumb);
    for (int i = 2; i < argc; i++)
      recs[n++] = icon_rec(argv[i]);
    add(&e, argv[0], recs, n);
    return 0;
  }
  load(&e, argv[0]);
  if (!strcmp(verb, "dump")) {
    each_note(&e, dump_note, NULL);
  } else if (!strcmp(verb, "rm")) {
    each_note(&e, kill_note, NULL);
    drop_note_segments(&e);
    write_out(&e, argv[0], NULL, 0, e.n, NULL, 0);
  } else if (!strcmp(verb, "check")) {
    check_load_order(&e, argv[0]);
    each_note(&e, check_note, &count);
    printf("%s: ok, %d records\n", argv[0], count);
  } else {
    usage();
  }
  return 0;
}
