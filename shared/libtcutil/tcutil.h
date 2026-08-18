/* toolchain utility library: memory, paths, containers, option parsing */

#ifndef ARUU_TCUTIL_H
#define ARUU_TCUTIL_H

#include <stddef.h>    /* size_t */
#include <stdint.h>    /* intptr_t */
#include <stdio.h>     /* FILE */
#include <sys/types.h> /* ssize_t */

/* macros also defined by shared/util.h, undef to avoid redefinition warning */
#undef MIN
#undef MAX
#undef ALIGN

#define MIN(a, b)        ((a) < (b) ? (a) : (b))
#define MAX(a, b)        ((a) > (b) ? (a) : (b))
#define ALIGN(x, align)  (((x) + (align) - 1) & -(align)) /* align must be power of 2 */
#define UNUSED(x)        ((void)(x))
#define IS_POWER_OF_2(x) ((x) > 0 && ((x) & ((x) - 1)) == 0)
#define INT2VOIDP(i)     ((void *)(intptr_t)(i))
#define UINT2VOIDP(i)    ((void *)(uintptr_t)(i))
#define VOIDP2INT(p)     ((intptr_t)(p))
#define VOIDP2UINT(p)    ((uintptr_t)(p))
#define ARRAY_SIZE(a)    (sizeof(a) / sizeof(*(a)))

struct Name;

/* character classification */
int isalnum_(int c);
int isutf8first(int c);
int isutf8follow(int c);
int isoctal(int c);
int xvalue(char c);

/* string and path helpers */
int         starts_with(const char *str, const char *prefix);
int         is_fullpath(const char *filename);
char       *join_paths(const char *paths[]);
char       *get_ext(const char *filename);
char       *change_ext(const char *path, const char *ext);
const char *skip_whitespaces(const char *s);
const char *block_comment_start(const char *p);
const char *block_comment_end(const char *p);

/* file I/O helpers */
void   *read_or_die(FILE *fp, void *buf, long offset, size_t size, const char *msg);
void    put_padding(FILE *fp, long start);
int     is_file(const char *path);
ssize_t getline_chomp(char **lineptr, size_t *n, FILE *stream);
ssize_t getline_cont(char **lineptr, size_t *n, FILE *stream, int *plineno);

/* memory: toolchain-local, same contract as aruu emalloc/ecalloc/erealloc
 * kept separate so the toolchain can build standalone without libutil */
void *malloc_or_die(size_t size);
void *calloc_or_die(size_t size);
void *realloc_or_die(void *ptr, size_t size);

/* diagnostics */
void           show_version(const char *exe, int arch);
_Noreturn void error(const char *fmt, ...);
void           show_error_line(const char *line, const char *p, int len);

/* value range checks for immediate operands */
int     is_im8(int64_t x);
int     is_im16(int64_t x);
int     is_im32(int64_t x);
int64_t wrap_value(int64_t value, int size, int is_unsigned);
int     most_significant_bit(size_t x);

/* name interning (see table.h for struct name) */
const struct Name *alloc_label(void);

/* container: growable pointer vector */
struct Vector {
  void **data;
  int    capacity;
  int    len;
};

struct Vector *new_vector(void);
void           free_vector(struct Vector *vec);
void           vec_init(struct Vector *vec);
void           vec_clear(struct Vector *vec);
void           vec_push(struct Vector *vec, const void *elem);
void          *vec_pop(struct Vector *vec);
void           vec_insert(struct Vector *vec, int pos, const void *elem);
void           vec_remove_at(struct Vector *vec, int index);
int            vec_contains(struct Vector *vec, void *elem);
void           vec_concat(struct Vector *dst, const struct Vector *src);

/* container: growable byte buffer */
struct DataStorage {
  struct Vector *chunk_stack;
  unsigned char *buf;
  size_t         capacity;
  size_t         len;
};

void data_release(struct DataStorage *data);
void data_init(struct DataStorage *data);
void data_reserve(struct DataStorage *data, size_t capacity);
void data_insert(struct DataStorage *data, ssize_t pos, const void *buf, size_t size);
void data_append(struct DataStorage *data, const void *buf, size_t size);
void data_push(struct DataStorage *data, unsigned char c);
void data_align(struct DataStorage *data, int align);
void data_concat(struct DataStorage *dst, struct DataStorage *src);
void data_leb128(struct DataStorage *data, ssize_t pos, int64_t val);
void data_uleb128(struct DataStorage *data, ssize_t pos, uint64_t val);
void data_string(struct DataStorage *data, const void *str, size_t len);
void data_open_chunk(struct DataStorage *data);
void data_close_chunk(struct DataStorage *data, ssize_t num);
void data_varint32(struct DataStorage *data, ssize_t pos, int64_t val);
void data_varuint32(struct DataStorage *data, ssize_t pos, uint64_t val);

/* container: rope-style string builder */
struct StringBuffer {
  struct Vector *elems;
};

void  sb_init(struct StringBuffer *sb);
void  sb_clear(struct StringBuffer *sb);
int   sb_empty(struct StringBuffer *sb);
void  sb_insert(struct StringBuffer *sb, int pos, const char *start, const char *end);
char *sb_join(struct StringBuffer *sb, const char *separator);

static inline void
sb_append(struct StringBuffer *sb, const char *start, const char *end)
{
  sb_insert(sb, sb->elems->len, start, end);
}

static inline void
sb_prepend(struct StringBuffer *sb, const char *start, const char *end)
{
  sb_insert(sb, 0, start, end);
}

static inline char *
sb_to_string(struct StringBuffer *sb)
{
  return sb_join(sb, NULL);
}

void escape_string(const char *str, size_t size, struct StringBuffer *sb);

/* convenience macro for join_paths */
#define JOIN_PATHS(...) join_paths((const char *[]){__VA_ARGS__, NULL})

/* option parser: long-option style for toolchain commands
 * has_arg values: 0 = none, 1 = required, 2 = optional */
#define no_argument       0
#define required_argument 1
#define optional_argument 2

struct option {
  const char *name;
  int         has_arg;
  int         val;
};

extern int   optind;
extern int   opterr;
extern int   optopt;
extern char *optarg;

int optparse(int argc, char *const argv[], const struct option *opts);

#endif /* ARUU_TCUTIL_H */
