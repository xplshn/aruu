/* see LICENSE file for copyright and license details */
#include "util.h"

#include <ctype.h>
#include <elf.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARMAG  "!<arch>\n"
#define SARMAG 8
#define ARFMAG "`\n"

struct ArHdr {
  char ar_name[16];
  char ar_date[12];
  char ar_uid[6];
  char ar_gid[6];
  char ar_mode[8];
  char ar_size[10];
  char ar_fmag[2];
};

struct Member {
  char          *path;
  char          *name;
  unsigned char *data;
  size_t         size;
  size_t         long_name_offset;
  int            use_long_name;
  uint32_t       offset;
};

struct ArchiveSym {
  int   member_index;
  char *name;
};

static const char *
base_name(const char *path)
{
  const char *name;

  name = path + strlen(path);
  while (name > path && name[-1] != '/' && name[-1] != '\\')
    --name;
  return name;
}

static unsigned char *
read_file(const char *path, size_t *size)
{
  FILE          *fp;
  long           len;
  unsigned char *data;

  fp = fopen(path, "rb");
  if (!fp)
    eprintf("fopen %s:", path);
  if (fseek(fp, 0, SEEK_END) != 0)
    eprintf("fseek %s:", path);
  len = ftell(fp);
  if (len < 0)
    eprintf("ftell %s:", path);
  if (fseek(fp, 0, SEEK_SET) != 0)
    eprintf("fseek %s:", path);

  data = emalloc((size_t)len);
  if (len && fread(data, 1, (size_t)len, fp) != (size_t)len)
    eprintf("fread %s:", path);
  fclose(fp);
  *size = (size_t)len;
  return data;
}

static void
write_all(FILE *fp, const void *data, size_t size, const char *path)
{
  if (size && fwrite(data, 1, size, fp) != size)
    eprintf("fwrite %s:", path);
}

static void
put_field(char *dst, size_t dst_size, const char *str)
{
  size_t len;

  len = strlen(str);
  if (len > dst_size)
    eprintf("archive header field is too large\n");
  memset(dst, ' ', dst_size);
  memcpy(dst, str, len);
}

static void
put_decimal_field(char *dst, size_t dst_size, unsigned long long value)
{
  char buf[32];
  int  len;

  len = snprintf(buf, sizeof(buf), "%llu", value);
  if (len < 0 || (size_t)len > dst_size)
    eprintf("archive header number is too large\n");
  memset(dst, ' ', dst_size);
  memcpy(dst, buf, (size_t)len);
}

static void
write_ar_header(FILE *fp, const char *archive, const char *name, size_t size)
{
  struct ArHdr hdr;

  put_field(hdr.ar_name, sizeof(hdr.ar_name), name);
  put_decimal_field(hdr.ar_date, sizeof(hdr.ar_date), 0);
  put_decimal_field(hdr.ar_uid, sizeof(hdr.ar_uid), 0);
  put_decimal_field(hdr.ar_gid, sizeof(hdr.ar_gid), 0);
  put_field(hdr.ar_mode, sizeof(hdr.ar_mode), "100644");
  put_decimal_field(hdr.ar_size, sizeof(hdr.ar_size), (unsigned long long)size);
  memcpy(hdr.ar_fmag, ARFMAG, sizeof(hdr.ar_fmag));
  write_all(fp, &hdr, sizeof(hdr), archive);
}

static void
write_be32(FILE *fp, const char *archive, uint32_t value)
{
  unsigned char buf[4];

  buf[0] = (unsigned char)(value >> 24);
  buf[1] = (unsigned char)(value >> 16);
  buf[2] = (unsigned char)(value >> 8);
  buf[3] = (unsigned char)value;
  write_all(fp, buf, sizeof(buf), archive);
}

static int
range_ok(uint64_t off, uint64_t len, size_t total)
{
  if (off > (uint64_t)SIZE_MAX || len > (uint64_t)SIZE_MAX)
    return 0;
  return (size_t)off <= total && (size_t)len <= total - (size_t)off;
}

static int
is_exported_symbol(const Elf64_Sym *sym)
{
  unsigned bind;
  unsigned type;

  bind = ELF64_ST_BIND(sym->st_info);
  type = ELF64_ST_TYPE(sym->st_info);
  if (sym->st_shndx == SHN_UNDEF)
    return 0;
  if (bind != STB_GLOBAL && bind != STB_WEAK)
    return 0;
  if (type == STT_SECTION || type == STT_FILE)
    return 0;
  return 1;
}

static void
append_archive_sym(
    struct ArchiveSym **symbols,
    int                *nb_symbols,
    int                *cap_symbols,
    int                 member_index,
    const char         *name,
    size_t             *name_bytes
)
{
  struct ArchiveSym *sym;

  if (*nb_symbols == *cap_symbols) {
    *cap_symbols = *cap_symbols ? *cap_symbols * 2 : 64;
    *symbols     = erealloc(*symbols, (size_t)*cap_symbols * sizeof(**symbols));
  }
  sym               = &(*symbols)[(*nb_symbols)++];
  sym->member_index = member_index;
  sym->name         = estrdup(name);
  *name_bytes += strlen(name) + 1;
}

static void
collect_symbols(
    const struct Member *member,
    int                  member_index,
    struct ArchiveSym  **symbols,
    int                 *nb_symbols,
    int                 *cap_symbols,
    size_t              *name_bytes
)
{
  const Elf64_Ehdr *ehdr;
  const Elf64_Shdr *sections;
  int               i;

  if (member->size < sizeof(*ehdr) || memcmp(member->data, ELFMAG, SELFMAG) != 0)
    eprintf("archive input is not an ELF object\n");

  ehdr = (const Elf64_Ehdr *)member->data;
  if (ehdr->e_ident[EI_CLASS] != ELFCLASS64)
    eprintf("archive input is not an ELF64 object\n");
  if (ehdr->e_type != ET_REL)
    eprintf("archive input is not a relocatable ELF object\n");
  if (ehdr->e_shentsize != sizeof(Elf64_Shdr))
    eprintf("unsupported ELF section header size\n");
  if (!range_ok(ehdr->e_shoff, (uint64_t)ehdr->e_shnum * ehdr->e_shentsize, member->size))
    eprintf("invalid ELF section table\n");

  sections = (const Elf64_Shdr *)(member->data + ehdr->e_shoff);
  for (i = 0; i < ehdr->e_shnum; ++i) {
    const Elf64_Shdr    *symsec = &sections[i];
    const Elf64_Shdr    *strsec;
    const unsigned char *symtab;
    const char          *strtab;
    size_t               strtab_size, nsym, j;

    if (symsec->sh_type != SHT_SYMTAB)
      continue;
    if (symsec->sh_entsize && symsec->sh_entsize != sizeof(Elf64_Sym))
      eprintf("unsupported ELF symbol size\n");
    if (symsec->sh_link >= ehdr->e_shnum)
      eprintf("invalid ELF symbol string table\n");
    if (!range_ok(symsec->sh_offset, symsec->sh_size, member->size))
      eprintf("invalid ELF symbol table\n");

    strsec = &sections[symsec->sh_link];
    if (!range_ok(strsec->sh_offset, strsec->sh_size, member->size))
      eprintf("invalid ELF string table\n");
    symtab      = member->data + symsec->sh_offset;
    strtab      = (const char *)member->data + strsec->sh_offset;
    strtab_size = (size_t)strsec->sh_size;
    nsym        = (size_t)(symsec->sh_size / sizeof(Elf64_Sym));

    for (j = 1; j < nsym; ++j) {
      const Elf64_Sym *sym = (const Elf64_Sym *)(symtab + j * sizeof(Elf64_Sym));
      const char      *name;

      if (!is_exported_symbol(sym))
        continue;
      if (sym->st_name >= strtab_size)
        eprintf("invalid ELF symbol name\n");
      name = strtab + sym->st_name;
      if (!*name)
        continue;
      append_archive_sym(symbols, nb_symbols, cap_symbols, member_index, name, name_bytes);
    }
  }
}

static void
append_member(struct Member **members, int *nb_members, int *cap_members, const char *path)
{
  struct Member *member;
  const char    *name;
  size_t         name_len;

  name     = base_name(path);
  name_len = strlen(name);
  if (!name_len)
    eprintf("empty archive member name\n");
  if (*nb_members == *cap_members) {
    *cap_members = *cap_members ? *cap_members * 2 : 16;
    *members     = erealloc(*members, (size_t)*cap_members * sizeof(**members));
  }
  member                   = &(*members)[(*nb_members)++];
  member->path             = estrdup(path);
  member->name             = estrdup(name);
  member->data             = read_file(path, &member->size);
  member->long_name_offset = 0;
  member->use_long_name    = name_len > 15;
  member->offset           = 0;
}

static char *
member_header_name(const struct Member *member)
{
  size_t len;
  char  *ar_name;
  char   buf[32];
  int    n;

  len = strlen(member->name);
  if (member->use_long_name) {
    n = snprintf(buf, sizeof(buf), "/%zu", member->long_name_offset);
    if (n < 0 || (size_t)n >= sizeof(buf) || n > 16)
      eprintf("long archive member offset is too large\n");
    return estrdup(buf);
  }
  if (len > 15)
    eprintf("archive member name is too long for ar\n");
  ar_name = emalloc(len + 2);
  memcpy(ar_name, member->name, len);
  ar_name[len]     = '/';
  ar_name[len + 1] = '\0';
  return ar_name;
}

static int
create_archive(const char *archive, int argc, char **argv, int verbose)
{
  struct Member     *members    = NULL;
  struct ArchiveSym *symbols    = NULL;
  int                nb_members = 0, cap_members = 0;
  int                nb_symbols = 0, cap_symbols = 0;
  size_t             symbol_name_bytes = 0;
  size_t             long_name_bytes   = 0;
  size_t             symbol_table_size, offset;
  FILE              *fp;
  int                i;

  for (i = 0; i < argc; ++i) {
    append_member(&members, &nb_members, &cap_members, argv[i]);
    collect_symbols(
        &members[nb_members - 1],
        nb_members - 1,
        &symbols,
        &nb_symbols,
        &cap_symbols,
        &symbol_name_bytes
    );
    if (verbose)
      printf("a - %s\n", argv[i]);
  }

  for (i = 0; i < nb_members; ++i) {
    if (members[i].use_long_name) {
      members[i].long_name_offset = long_name_bytes;
      long_name_bytes += strlen(members[i].name) + 2;
    }
  }

  symbol_table_size = 4 + (size_t)nb_symbols * 4 + symbol_name_bytes;
  offset            = SARMAG + sizeof(struct ArHdr) + symbol_table_size + (symbol_table_size & 1);
  if (long_name_bytes)
    offset += sizeof(struct ArHdr) + long_name_bytes + (long_name_bytes & 1);
  for (i = 0; i < nb_members; ++i) {
    if (offset > UINT32_MAX)
      eprintf(
          "archive is too large for a 32-bit symbol "
          "index\n"
      );
    members[i].offset = (uint32_t)offset;
    offset += sizeof(struct ArHdr) + members[i].size + (members[i].size & 1);
  }

  fp = fopen(archive, "wb");
  if (!fp)
    eprintf("fopen %s:", archive);
  write_all(fp, ARMAG, SARMAG, archive);
  write_ar_header(fp, archive, "/", symbol_table_size);
  write_be32(fp, archive, (uint32_t)nb_symbols);
  for (i = 0; i < nb_symbols; ++i)
    write_be32(fp, archive, members[symbols[i].member_index].offset);
  for (i = 0; i < nb_symbols; ++i)
    write_all(fp, symbols[i].name, strlen(symbols[i].name) + 1, archive);
  if (symbol_table_size & 1)
    write_all(fp, "\n", 1, archive);
  if (long_name_bytes) {
    write_ar_header(fp, archive, "// ", long_name_bytes);
    for (i = 0; i < nb_members; ++i) {
      if (members[i].use_long_name) {
        write_all(fp, members[i].name, strlen(members[i].name), archive);
        write_all(fp, "/\n", 2, archive);
      }
    }
    if (long_name_bytes & 1)
      write_all(fp, "\n", 1, archive);
  }

  for (i = 0; i < nb_members; ++i) {
    char *ar_name = member_header_name(&members[i]);
    write_ar_header(fp, archive, ar_name, members[i].size);
    write_all(fp, members[i].data, members[i].size, archive);
    if (members[i].size & 1)
      write_all(fp, "\n", 1, archive);
    free(ar_name);
  }
  if (fclose(fp) != 0)
    eprintf("fclose %s:", archive);

  for (i = 0; i < nb_symbols; ++i)
    free(symbols[i].name);
  for (i = 0; i < nb_members; ++i) {
    free(members[i].path);
    free(members[i].name);
    free(members[i].data);
  }
  free(symbols);
  free(members);
  return 0;
}

static unsigned long long
parse_decimal_field(const char *field, size_t size)
{
  char   buf[32];
  char  *end;
  size_t len;

  len = size;
  if (len >= sizeof(buf))
    eprintf("archive header field is too large\n");
  memcpy(buf, field, len);
  buf[len] = '\0';
  errno    = 0;
  while (len && buf[len - 1] == ' ')
    buf[--len] = '\0';
  end = buf;
  while (*end == ' ')
    ++end;
  if (!*end)
    return 0;
  return strtoull(end, NULL, 10);
}

static char *
parse_member_name(const struct ArHdr *hdr)
{
  char   name[17];
  size_t len;

  memcpy(name, hdr->ar_name, sizeof(hdr->ar_name));
  name[sizeof(hdr->ar_name)] = '\0';
  len                        = strlen(name);
  while (len && name[len - 1] == ' ')
    name[--len] = '\0';
  if (len > 1 && name[len - 1] == '/' && !(len == 2 && name[0] == '/' && name[1] == '/'))
    name[--len] = '\0';
  return estrndup(name, len);
}

static char *
resolve_long_name(const char *longnames, size_t longnames_size, const char *name)
{
  unsigned long off;
  char         *end;
  const char   *p;
  const char   *q;

  if (!longnames || name[0] != '/' || !isdigit((unsigned char)name[1]))
    return NULL;
  errno = 0;
  off   = strtoul(name + 1, &end, 10);
  if (errno || *end || off >= longnames_size)
    return NULL;
  p = longnames + off;
  q = p;
  while ((size_t)(q - longnames) < longnames_size && *q != '\n')
    ++q;
  if (q > p && q[-1] == '/')
    --q;
  return estrndup(p, (size_t)(q - p));
}

static unsigned char *
read_archive_member(FILE *fp, const char *archive, size_t size)
{
  unsigned char *data;

  data = emalloc(size);
  if (size && fread(data, 1, size, fp) != size)
    eprintf("fread member %s:", archive);
  if (size & 1) {
    if (fgetc(fp) == EOF)
      eprintf("fgetc padding %s:", archive);
  }
  return data;
}

static int
table_or_extract_archive(const char *archive, int table, int extract, int verbose)
{
  FILE  *fp;
  char   magic[SARMAG];
  char  *longnames      = NULL;
  size_t longnames_size = 0;

  fp = fopen(archive, "rb");
  if (!fp)
    eprintf("fopen %s:", archive);
  if (fread(magic, 1, sizeof(magic), fp) != sizeof(magic) || memcmp(magic, ARMAG, SARMAG) != 0)
    eprintf("not an ar archive\n");

  for (;;) {
    struct ArHdr       hdr;
    char              *raw_name;
    char              *resolved_name;
    char              *name;
    unsigned long long parsed_size;
    size_t             size;
    unsigned char     *data;
    size_t             nread;

    nread = fread(&hdr, 1, sizeof(hdr), fp);
    if (nread == 0)
      break;
    if (nread != sizeof(hdr) || memcmp(hdr.ar_fmag, ARFMAG, 2) != 0)
      eprintf("invalid ar archive\n");
    parsed_size = parse_decimal_field(hdr.ar_size, sizeof(hdr.ar_size));
    if (parsed_size > SIZE_MAX)
      eprintf("archive member is too large\n");
    size     = (size_t)parsed_size;
    raw_name = parse_member_name(&hdr);
    data     = read_archive_member(fp, archive, size);

    if (!strcmp(raw_name, "// ")) {
      free(longnames);
      longnames      = (char *)data;
      longnames_size = size;
      free(raw_name);
      continue;
    }
    if (!strcmp(raw_name, "/") || !strcmp(raw_name, "/SYM64/")) {
      free(data);
      free(raw_name);
      continue;
    }

    resolved_name = resolve_long_name(longnames, longnames_size, raw_name);
    name          = resolved_name ? resolved_name : raw_name;
    if (table || verbose)
      printf("%s%s\n", extract ? "x - " : "", name);
    if (extract) {
      FILE *out = fopen(name, "wb");
      if (!out)
        eprintf("fopen %s:", name);
      write_all(out, data, size, name);
      if (fclose(out) != 0)
        eprintf("fclose %s:", name);
    }
    free(resolved_name);
    free(raw_name);
    free(data);
  }
  free(longnames);
  fclose(fp);
  return 0;
}

static void
usage(void)
{
  fprintf(stderr, "usage: %s [crstvx] archive [files]\n", argv0);
  exit(1);
}

int
main(int argc, char **argv)
{
  const char *ops;
  int         i;
  int         extract = 0;
  int         table   = 0;
  int         verbose = 0;
  int         ret     = 0;

  argv0 = argv[0];
  if (argc < 3)
    usage();

  ops = argv[1];
  if (*ops == '-')
    ++ops;
  if (!*ops)
    usage();

  for (i = 0; ops[i]; ++i) {
    switch (ops[i]) {
      case 'c':
      case 'r':
      case 's':
      case 'q':
        break;
      case 't':
        table = 1;
        break;
      case 'x':
        extract = 1;
        break;
      case 'v':
        verbose = 1;
        break;
      case 'h':
      case 'a':
      case 'b':
      case 'd':
      case 'i':
      case 'o':
      case 'p':
      case 'N':
      default:
        usage();
    }
  }

  if (table || extract) {
    ret = table_or_extract_archive(argv[2], table, extract, verbose);
  } else {
    ret = create_archive(argv[2], argc - 3, argv + 3, verbose);
  }

  if (fshut(stdin, "<stdin>") | fshut(stdout, "<stdout>"))
    ret = 1;

  return ret;
}
