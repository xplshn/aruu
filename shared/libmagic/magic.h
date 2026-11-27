/* see LICENSE file for copyright and license details */
#ifndef ARUU_LIBMAGIC_H
#define ARUU_LIBMAGIC_H

#include <stddef.h>

/*
 * libmagic: tell the type of a file by its bytes, as file(1) does
 *
 * reads the databases file(1) reads, both the magic(5) text sources
 * and the compiled magic.mgc that file 5.40 to 5.48 write, and gives
 * for a buffer the description file(1) prints and the mime type that
 * file --mime-type prints
 *
 * the library is plain c99 and posix: it prints nothing, never exits
 * and keeps no state outside the handle magic_open returns. a handle
 * may be shared by threads only if they dont classify at the same
 * time, regexes are compiled into it on first use
 *
 * usage:
 *
 *   struct Magic *m;
 *   char desc[256], mime[64];
 *
 *   if (!(m = magic_open(NULL)))
 *     return -1;
 *   magic_buffer(m, buf, n, desc, sizeof(desc), mime, sizeof(mime));
 *   magic_close(m);
 */

/* how much of a file file(1) 5.4x reads, and so what magic_buffer wants to see */
#define MAGIC_BYTES_MAX (7 * 1024 * 1024)

/* how much of a file magic_charset and the text tests look at */
#define MAGIC_CHARSET_MAX (64 * 1024)

/*
 * databases tried when magic_open gets NULL and MAGIC is unset, as a
 * colon separated list. each entry "x" loads "x.mgc" if that exists,
 * else the text source "x", a file or a directory of files. override
 * at build time with -DMAGIC_PATH_DEFAULT=\"...\"
 */
#ifndef MAGIC_PATH_DEFAULT
#define MAGIC_PATH_DEFAULT                                                                         \
  "/usr/share/misc/magic:/usr/share/file/misc/magic:/usr/local/share/misc/magic:"                  \
  "/share/misc/magic:/share/file/magic"
#endif

struct Magic;

/*
 * load the databases in path, a colon separated list where every
 * entry that exists is loaded and entries that dont are skipped.
 * NULL means MAGIC from the environment, else MAGIC_PATH_DEFAULT.
 * returns NULL with errno set when nothing could be loaded: ENOENT
 * if no entry exists, ENOEXEC if a file is not a magic database or is
 * of an unsupported version, else the error of open, read or malloc
 */
struct Magic *magic_open(const char *path);

/* free a handle and everything loaded into it, NULL is fine */
void magic_close(struct Magic *m);

/*
 * classify the first n bytes of a file. desc gets what file(1) prints
 * after the name, mime gets the type file --mime-type prints. either
 * buffer may be NULL, both are truncated to fit and always terminated.
 * "data" and application/octet-stream when nothing matches, "empty"
 * and inode/x-empty when n is 0. returns 0, or -1 with errno set when
 * memory runs out
 */
int magic_buffer(
    struct Magic *m, const void *buf, size_t n, char *desc, size_t dn, char *mime, size_t mn
);

/*
 * as magic_buffer, but reads the first MAGIC_BYTES_MAX bytes of the
 * open file fd itself and may read more of it later, as file(1) does
 * for elf headers and for tests at offsets from the end of the file.
 * the caller deals with what is not a regular file (directories,
 * devices, symlinks), magic_fd only looks at the bytes. returns 0, or
 * -1 with errno set when fd cant be read or memory runs out
 */
int magic_fd(struct Magic *m, int fd, char *desc, size_t dn, char *mime, size_t mn);

/*
 * works out once what the loaded tests need of a file: how far each
 * reads, and which tests give which mime type. after it the handle may
 * be shared by threads classifying at the same time, as every regex is
 * compiled. returns 0, or -1 with errno set when memory runs out
 */
int magic_prepare(struct Magic *m);
/*
 * as magic_fd, reading no more of the file than the tests need. hint,
 * a mime type the name suggests (NULL for none), is tried first: only
 * the tests that can give it, over only the bytes they look at. when
 * they give it, that is the answer; else every test runs, over the
 * bytes already read and as many more as the tests with a known reach
 * look at. without magic_prepare it reads as magic_fd does
 */
int magic_fd_hint(struct Magic *m, int fd, const char *hint, char *desc, size_t dn, char *mime, size_t mn);
/*
 * the charset file --mime prints for these bytes: "us-ascii",
 * "utf-8", "iso-8859-1" and so on, or "binary". the result is a
 * constant string
 */
const char *magic_charset(const void *buf, size_t n);

#endif
